#include "Server.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <new>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

#ifdef __3DS__
#include <3ds.h>
#else
#include <sys/statvfs.h>
#endif

namespace ThreeDsLink {
    struct TConnection {
        int socket;
        int64_t openedAt;
        bool closing = false;
    };

    int64_t Now();
    std::string Lower(std::string value);
    std::string MimeType(const std::string &path);
    std::string EncodeUrl(const std::string &value);
    bool ParseNumber(const std::string &value, uint64_t &result);
    bool IsHiddenTemporary(const std::string &name);
}

int64_t ThreeDsLink::Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string ThreeDsLink::Lower(std::string value) {
    for (auto &character : value) {
        if (character >= 'A' && character <= 'Z') {
            character += 'a' - 'A';
        }
    }

    return value;
}

std::string ThreeDsLink::JsonString(const std::string &value) {
    std::string result = "\"";
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            result += '\\';
            result += character;
        } else if (byte < 0x20) {
            char escaped[7];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
            result += escaped;
        } else {
            result += character;
        }
    }

    return result + '"';
}

bool ThreeDsLink::DecodeUrl(const std::string &value, std::string &decoded) {
    decoded.clear();
    for (size_t index = 0; index < value.size(); ++index) {
        auto character = value[index];
        if (character == '%') {
            if (index + 2 >= value.size()) {
                return false;
            }

            const auto digits = Lower(value.substr(index + 1, 2));
            if (digits.find_first_not_of("0123456789abcdef") != std::string::npos) {
                return false;
            }

            character = static_cast<char>(std::strtol(digits.c_str(), nullptr, 16));
            index += 2;
        } else if (character == '+') {
            character = ' ';
        }

        if (static_cast<unsigned char>(character) < 0x20 || character == 0x7f) {
            return false;
        }

        decoded += character;
    }

    return true;
}

std::string ThreeDsLink::EncodeUrl(const std::string &value) {
    std::string result;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
            (byte >= '0' && byte <= '9') || character == '-' || character == '_' ||
            character == '.') {
            result += character;
        } else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", byte);
            result += escaped;
        }
    }

    return result;
}

bool ThreeDsLink::ParseNumber(const std::string &value, uint64_t &result) {
    if (value.empty() || value.size() > 20 ||
        value.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }

    errno = 0;
    result = std::strtoull(value.c_str(), nullptr, 10);
    return errno != ERANGE;
}

bool ThreeDsLink::IsHiddenTemporary(const std::string &name) {
    return name.compare(0, 16, ".3dslink-upload-") == 0;
}

std::string ThreeDsLink::MimeType(const std::string &path) {
    const auto dot = path.find_last_of('.');
    const auto extension = dot == std::string::npos ? "" : Lower(path.substr(dot));
    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".js") {
        return "text/javascript; charset=utf-8";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".jpg" || extension == ".jpeg") {
        return "image/jpeg";
    }
    if (extension == ".bmp") {
        return "image/bmp";
    }
    if (extension == ".gif") {
        return "image/gif";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }

    return "application/octet-stream";
}

ThreeDsLink::TStorage ThreeDsLink::GetStorage(const std::string &root) {
    TStorage storage;
#ifdef __3DS__
    (void)root;
    FS_ArchiveResource resource{};
    if (R_SUCCEEDED(FSUSER_GetArchiveResource(&resource, SYSTEM_MEDIATYPE_SD))) {
        storage.freeBytes = static_cast<uint64_t>(resource.freeClusters) * resource.clusterSize;
        storage.totalBytes = static_cast<uint64_t>(resource.totalClusters) * resource.clusterSize;
    }
#else
    struct statvfs resource{};
    if (statvfs(root.c_str(), &resource) == 0) {
        storage.freeBytes = static_cast<uint64_t>(resource.f_bavail) * resource.f_frsize;
        storage.totalBytes = static_cast<uint64_t>(resource.f_blocks) * resource.f_frsize;
    }
#endif
    return storage;
}

ThreeDsLink::CServer::CServer(TConfig config) : config(std::move(config)) {
}

ThreeDsLink::CServer::~CServer() {
    stop();
    if (listener >= 0) {
        close(listener);
    }
}

bool ThreeDsLink::CServer::start(std::string &error) {
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        error = "Could not create a network socket.";
        return false;
    }

    const int enabled = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#ifdef __3DS__
    //Accepted sockets inherit this receive buffer from the listener.
    const int receiveBufferSize = 32 * 1024;
    setsockopt(listener, SOL_SOCKET, SO_RCVBUF, &receiveBufferSize, sizeof(receiveBufferSize));
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    address.sin_addr.s_addr = inet_addr(config.bindAddress.c_str());
    if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(listener, 8) != 0 || fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
        error = "Could not listen on port " + std::to_string(config.port) + ".";
        return false;
    }

    running = true;
    return true;
}

void ThreeDsLink::CServer::stop() {
    running = false;
}

void ThreeDsLink::CServer::setPaused(bool value) {
    paused = value;
}

bool ThreeDsLink::CServer::isPaused() const {
    return paused;
}

bool ThreeDsLink::CServer::isBusy() const {
    return busy;
}

uint32_t ThreeDsLink::CServer::transferredBytes() const {
    return transferred;
}

uint32_t ThreeDsLink::CServer::transferBytes() const {
    return transferSize;
}

uint32_t ThreeDsLink::CServer::completedTransfers() const {
    return completed;
}

uint32_t ThreeDsLink::CServer::pairedDevices() const {
    return paired;
}

bool ThreeDsLink::CServer::waitSocket(int socket, bool writing) {
    const auto deadline = Now() + 15;
    while (running && Now() < deadline) {
        fd_set descriptors;
        FD_ZERO(&descriptors);
        FD_SET(socket, &descriptors);
        timeval timeout{0, 200000};
        const auto result = select(socket + 1, writing ? nullptr : &descriptors,
                                   writing ? &descriptors : nullptr, nullptr, &timeout);
        if (result > 0) {
            return true;
        }
        if (result < 0 && errno != EINTR) {
            return false;
        }
    }

    return false;
}

bool ThreeDsLink::CServer::sendAll(int socket, const char *data, size_t size) {
    while (size > 0 && running) {
        if (!waitSocket(socket, true)) {
            return false;
        }

        const auto sent = send(socket, data, size, 0);
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }

        data += sent;
        size -= static_cast<size_t>(sent);
    }

    return size == 0;
}

bool ThreeDsLink::CServer::sendHeaders(int socket, int status, uint64_t size,
                                       const std::string &type, const std::string &extraHeaders) {
    const std::string reason = status < 400 ? "OK" : "Error";
    const auto headers =
        "HTTP/1.1 " + std::to_string(status) + " " + reason +
        "\r\nConnection: close\r\nContent-Length: " + std::to_string(size) +
        "\r\nContent-Type: " + type +
        "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff"
        "\r\nReferrer-Policy: no-referrer\r\nX-Frame-Options: DENY"
        "\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; "
        "style-src 'self' 'unsafe-inline'; img-src 'self' blob:; connect-src 'self'; "
        "object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'\r\n" +
        extraHeaders + "\r\n";
    return sendAll(socket, headers.data(), headers.size());
}

void ThreeDsLink::CServer::respond(int socket, int status, const std::string &body,
                                   const std::string &type, const std::string &extraHeaders) {
    if (sendHeaders(socket, status, body.size(), type, extraHeaders)) {
        sendAll(socket, body.data(), body.size());
    }
}

void ThreeDsLink::CServer::fail(int socket, int status, const std::string &message) {
    respond(socket, status, "{\"error\":" + JsonString(message) + "}");
}

bool ThreeDsLink::CServer::readRequest(int socket, TRequest &request) {
    std::string raw;
    std::array<char, 4096> buffer{};
    size_t headerEnd;
    const auto deadline = Now() + 15;
    while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
        if (raw.size() >= 16384 || Now() >= deadline || !waitSocket(socket, false)) {
            fail(socket, 431, "Request headers are too large or timed out.");
            return false;
        }

        const auto received = recv(socket, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            return false;
        }

        raw.append(buffer.data(), static_cast<size_t>(received));
    }

    if (headerEnd > 16384 || raw.substr(0, headerEnd).find('\0') != std::string::npos) {
        fail(socket, 400, "Invalid request headers.");
        return false;
    }

    const auto firstLine = raw.find("\r\n");
    const auto firstSpace = raw.find(' ');
    const auto secondSpace = raw.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace >= firstLine ||
        raw.substr(secondSpace + 1, firstLine - secondSpace - 1) != "HTTP/1.1") {
        fail(socket, 400, "Expected an HTTP/1.1 request.");
        return false;
    }

    request.method = raw.substr(0, firstSpace);
    const auto target = raw.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const auto question = target.find('?');
    request.route = target.substr(0, question);
    if (question != std::string::npos) {
        size_t offset = question + 1;
        while (offset < target.size()) {
            const auto end = target.find('&', offset);
            const auto part = target.substr(offset, end - offset);
            const auto equals = part.find('=');
            std::string key;
            std::string value;
            if (!DecodeUrl(part.substr(0, equals), key) ||
                !DecodeUrl(equals == std::string::npos ? "" : part.substr(equals + 1), value)) {
                fail(socket, 400, "Invalid URL encoding.");
                return false;
            }

            request.query[key] = value;
            if (end == std::string::npos) {
                break;
            }

            offset = end + 1;
        }
    }

    size_t offset = firstLine + 2;
    while (offset < headerEnd) {
        const auto end = raw.find("\r\n", offset);
        const auto colon = raw.find(':', offset);
        if (colon >= end) {
            fail(socket, 400, "Invalid header.");
            return false;
        }

        const auto key = Lower(raw.substr(offset, colon - offset));
        auto value = raw.substr(colon + 1, end - colon - 1);
        const auto valueStart = value.find_first_not_of(" \t");
        value = valueStart == std::string::npos ? "" : value.substr(valueStart);
        const auto valueEnd = value.find_last_not_of(" \t");
        if (valueEnd != std::string::npos) {
            value.resize(valueEnd + 1);
        }
        if (request.headers.count(key) != 0) {
            fail(socket, 400, "Duplicate header.");
            return false;
        }

        request.headers[key] = value;
        offset = end + 2;
    }

    if (request.headers.count("host") == 0 || request.headers.count("transfer-encoding") != 0 ||
        (request.headers.count("content-length") != 0 &&
         !ParseNumber(request.headers.at("content-length"), request.contentLength))) {
        fail(socket, 400,
             "A Host and a valid Content-Length are required; chunked requests are unsupported.");
        return false;
    }

    request.body = raw.substr(headerEnd + 4);
    if (request.body.size() > request.contentLength) {
        fail(socket, 400, "Body exceeds Content-Length.");
        return false;
    }

    return true;
}

bool ThreeDsLink::CServer::resolvePath(const std::string &path, std::string &resolved,
                                       bool allowMissing) {
    if (path.empty() || path.front() != '/' || path.size() > 1024 ||
        path.find_first_of("\\:\r\n") != std::string::npos) {
        return false;
    }

    resolved = config.root;
    size_t offset = 1;
    while (offset < path.size()) {
        const auto end = path.find('/', offset);
        const auto part = path.substr(offset, end - offset);
        if (part.empty() || part == "." || part == ".." || part.size() > 255 ||
            part.find_first_of("<>|?*\"") != std::string::npos || part.back() == '.' ||
            part.back() == ' ' || IsHiddenTemporary(part)) {
            return false;
        }

        resolved += "/" + part;
        struct stat info{};
#ifdef __3DS__
        const auto result = stat(resolved.c_str(), &info);
#else
        const auto result = lstat(resolved.c_str(), &info);
        if (result == 0 && S_ISLNK(info.st_mode)) {
            return false;
        }
#endif
        if (result != 0 && !(allowMissing && errno == ENOENT && end == std::string::npos)) {
            return false;
        }
        if (result == 0 && end != std::string::npos && !S_ISDIR(info.st_mode)) {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }

        offset = end + 1;
    }

    return true;
}

bool ThreeDsLink::CServer::authenticated(const TRequest &request) const {
    const auto cookie = request.headers.find("cookie");
    if (cookie == request.headers.end()) {
        return false;
    }

    size_t offset = 0;
    while (offset < cookie->second.size()) {
        const auto end = cookie->second.find(';', offset);
        auto part = cookie->second.substr(offset, end - offset);
        const auto start = part.find_first_not_of(' ');
        if (start != std::string::npos && part.substr(start) == "3dslink=" + config.sessionToken) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }

        offset = end + 1;
    }

    return false;
}

void ThreeDsLink::CServer::listFiles(int socket, const TRequest &request) {
    const auto pathItem = request.query.find("path");
    const auto path = pathItem == request.query.end() ? "/" : pathItem->second;
    std::string resolved;
    if (!resolvePath(path, resolved)) {
        fail(socket, 404, "This folder could not be opened.");
        return;
    }

    auto *directory = opendir(resolved.c_str());
    if (directory == nullptr) {
        fail(socket, 404, "This folder could not be opened.");
        return;
    }

    uint64_t offset = 0;
    const auto offsetItem = request.query.find("offset");
    if (offsetItem != request.query.end() && !ParseNumber(offsetItem->second, offset)) {
        closedir(directory);
        fail(socket, 400, "Invalid page offset.");
        return;
    }

    std::string entries;
    uint64_t total = 0;
    unsigned int count = 0;
    while (const auto *entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || IsHiddenTemporary(name)) {
            continue;
        }

        struct stat info{};
        const auto entryPath = resolved + "/" + name;
#ifdef __3DS__
        const auto result = stat(entryPath.c_str(), &info);
#else
        const auto result = lstat(entryPath.c_str(), &info);
#endif
        if (result != 0 || (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode))) {
            continue;
        }

        if (total++ < offset || count >= 200) {
            continue;
        }

        if (count++ > 0) {
            entries += ',';
        }

        entries += "{\"name\":" + JsonString(name) +
                   ",\"directory\":" + (S_ISDIR(info.st_mode) ? "true" : "false") +
                   ",\"size\":" + std::to_string(static_cast<uint64_t>(info.st_size)) +
                   ",\"modified\":" + std::to_string(static_cast<int64_t>(info.st_mtime)) + "}";
    }

    closedir(directory);
    respond(socket, 200,
            "{\"path\":" + JsonString(path) + ",\"entries\":[" + entries +
                "],\"total\":" + std::to_string(total) + ",\"nextOffset\":" +
                (offset + count < total ? std::to_string(offset + count) : "null") + "}");
}

void ThreeDsLink::CServer::sendFile(int socket, const TRequest &request, const std::string &path,
                                    bool download) {
    auto *file = std::fopen(path.c_str(), "rb");
    struct stat info{};
    if (file == nullptr || fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode)) {
        if (file != nullptr) {
            std::fclose(file);
        }

        fail(socket, 404, "File not found.");
        return;
    }

    const auto size = static_cast<uint64_t>(info.st_size);
    uint64_t begin = 0;
    uint64_t end = size == 0 ? 0 : size - 1;
    int status = 200;
    const auto rangeItem = request.headers.find("range");
    if (rangeItem != request.headers.end()) {
        const auto &range = rangeItem->second;
        const auto dash = range.find('-');
        bool valid = range.compare(0, 6, "bytes=") == 0 && dash != std::string::npos;
        if (valid) {
            const auto first = range.substr(6, dash - 6);
            const auto last = range.substr(dash + 1);
            if (first.empty()) {
                uint64_t suffix = 0;
                valid = ParseNumber(last, suffix) && suffix > 0;
                begin = suffix >= size ? 0 : size - suffix;
            } else {
                valid = ParseNumber(first, begin) && (last.empty() || ParseNumber(last, end));
            }
        }

        end = std::min(end, size == 0 ? 0 : size - 1);
        if (!valid || size == 0 || begin >= size || end < begin ||
            fseeko(file, static_cast<off_t>(begin), SEEK_SET) != 0) {
            std::fclose(file);
            respond(socket, 416, "", "application/octet-stream",
                    "Content-Range: bytes */" + std::to_string(size) + "\r\n");
            return;
        }

        status = 206;
    }

    const auto length = size == 0 ? 0 : end - begin + 1;
    std::string headers = "Accept-Ranges: bytes\r\n";
    if (status == 206) {
        headers += "Content-Range: bytes " + std::to_string(begin) + "-" + std::to_string(end) +
                   "/" + std::to_string(size) + "\r\n";
    }
    if (download) {
        headers += "Content-Disposition: attachment; filename=\"download\"; filename*=UTF-8''" +
                   EncodeUrl(path.substr(path.find_last_of('/') + 1)) + "\r\n";
        busy = true;
        transferred = 0;
        transferSize = static_cast<uint32_t>(std::min<uint64_t>(length, UINT32_MAX));
    }

    auto success = sendHeaders(socket, status, length,
                               download ? "application/octet-stream" : MimeType(path), headers);
    std::array<char, 32768> buffer{};
    auto remaining = length;
    while (success && remaining > 0) {
        const auto bytes =
            std::fread(buffer.data(), 1, std::min<uint64_t>(buffer.size(), remaining), file);
        if (bytes == 0 || !sendAll(socket, buffer.data(), bytes)) {
            success = false;
            break;
        }

        remaining -= bytes;
        if (download) {
            transferred += static_cast<uint32_t>(bytes);
        }
    }

    std::fclose(file);
    if (download && success) {
        ++completed;
    }

    busy = false;
}

void ThreeDsLink::CServer::uploadFile(int socket, const TRequest &request) {
    const auto pathItem = request.query.find("path");
    std::string destination;
    if (pathItem == request.query.end() || pathItem->second.empty() ||
        pathItem->second.back() == '/' || !resolvePath(pathItem->second, destination, true)) {
        fail(socket, 400, "Choose a valid file name and an existing folder.");
        return;
    }
    if (request.headers.count("content-length") == 0) {
        fail(socket, 411, "Content-Length is required.");
        return;
    }
    if (request.contentLength > UINT32_MAX) {
        fail(socket, 413, "The SD card supports files smaller than 4 GiB.");
        return;
    }

    struct stat existing{};
    if (stat(destination.c_str(), &existing) == 0) {
        fail(socket, 409, "A file with this name already exists. Rename it before uploading.");
        return;
    }

    const auto storage = GetStorage(config.root);
    if (storage.totalBytes != 0 && request.contentLength > storage.freeBytes) {
        fail(socket, 507, "There is not enough space on the SD card.");
        return;
    }

    const auto temporary = destination.substr(0, destination.find_last_of('/') + 1) +
                           ".3dslink-upload-" + config.sessionToken.substr(0, 12);
    const auto descriptor = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0) {
        fail(socket, 500, "Could not create the upload file.");
        return;
    }

    auto *file = fdopen(descriptor, "wb");
    if (file == nullptr) {
        close(descriptor);
        std::remove(temporary.c_str());
        fail(socket, 500, "Could not open the upload file.");
        return;
    }

    //Batch SD writes instead of using newlib's default 1 KiB file buffer.
    const auto fileBufferSize = 256 * 1024;
    auto fileBuffer = std::unique_ptr<char[]>(new (std::nothrow) char[fileBufferSize]);
    if (fileBuffer == nullptr ||
        std::setvbuf(file, fileBuffer.get(), _IOFBF, fileBufferSize) != 0) {
        std::fclose(file);
        std::remove(temporary.c_str());
        fail(socket, 500, "Could not allocate the upload buffer.");
        return;
    }

    busy = true;
    transferred = 0;
    transferSize = static_cast<uint32_t>(request.contentLength);
    auto success = true;
    if (request.headers.count("expect") != 0) {
        const std::string interim = "HTTP/1.1 100 Continue\r\n\r\n";
        success = sendAll(socket, interim.data(), interim.size());
    }

    if (success && !request.body.empty()) {
        success =
            std::fwrite(request.body.data(), 1, request.body.size(), file) == request.body.size();
        transferred = static_cast<uint32_t>(request.body.size());
    }

    std::array<char, 32768> buffer{};
    while (success && transferred < request.contentLength) {
        if (!waitSocket(socket, false)) {
            success = false;
            break;
        }

        const auto received =
            recv(socket, buffer.data(),
                 std::min<uint64_t>(buffer.size(), request.contentLength - transferred.load()), 0);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        if (received <= 0) {
            success = false;
            break;
        }

        const auto count = static_cast<size_t>(received);
        success = std::fwrite(buffer.data(), 1, count, file) == count;
        transferred += static_cast<uint32_t>(count);
    }

    success = std::fclose(file) == 0 && success && running;
    if (success) {
#ifdef __3DS__
        //The SD archive refuses to rename a file over an existing destination.
        success = std::rename(temporary.c_str(), destination.c_str()) == 0;
#else
        //Link publishes the complete upload without replacing an existing file.
        success = link(temporary.c_str(), destination.c_str()) == 0;
#endif
    }

    std::remove(temporary.c_str());
    busy = false;
    if (!success) {
        fail(socket, 500, "Upload interrupted or could not be saved. Please retry.");
        return;
    }

    ++completed;
    respond(socket, 201, "{\"ok\":true}");
}

void ThreeDsLink::CServer::handle(int socket, const TRequest &request) {
    const auto origin = request.headers.find("origin");
    const auto fetchSite = request.headers.find("sec-fetch-site");
    if ((origin != request.headers.end() &&
         origin->second != "http://" + request.headers.at("host")) ||
        (fetchSite != request.headers.end() && fetchSite->second == "cross-site")) {
        fail(socket, 403, "Open 3DSLink directly using the address on your console.");
        return;
    }

    if (request.method == "GET") {
        const std::map<std::string, std::string> assets = {{"/", "/index.html"},
                                                           {"/app.js", "/app.js"},
                                                           {"/style.css", "/style.css"},
                                                           {"/icon.svg", "/icon.svg"}};
        const auto asset = assets.find(request.route);
        if (asset != assets.end()) {
            sendFile(socket, request, config.assets + asset->second, false);
            return;
        }
    }

    if (paused) {
        fail(socket, 503, "Sharing is paused. Resume it on your 3DS.");
        return;
    }

    if (request.method == "POST" && request.route == "/api/pair") {
        if (Now() < pairingBlockedUntil) {
            fail(socket, 429, "Too many attempts. Wait 30 seconds and try again.");
            return;
        }
        if (request.contentLength != 4) {
            fail(socket, 400, "Enter the four-digit PIN shown on your 3DS.");
            return;
        }

        auto code = request.body;
        while (code.size() < 4) {
            char buffer[4];
            if (!waitSocket(socket, false)) {
                return;
            }

            const auto received = recv(socket, buffer, 4 - code.size(), 0);
            if (received <= 0) {
                return;
            }

            code.append(buffer, static_cast<size_t>(received));
        }

        if (code != config.pairingCode) {
            if (++failedPairings >= 5) {
                pairingBlockedUntil = Now() + 30;
                failedPairings = 0;
            }

            fail(socket, 401, "That PIN does not match. Check your 3DS screen.");
            return;
        }

        failedPairings = 0;
        ++paired;
        respond(socket, 200, "{\"ok\":true}", "application/json",
                "Set-Cookie: 3dslink=" + config.sessionToken +
                    "; HttpOnly; SameSite=Strict; Path=/\r\n");
        return;
    }

    if (!authenticated(request)) {
        fail(socket, 401, "Enter your console PIN to connect.");
        return;
    }

    if (request.method == "POST" && request.route == "/api/logout") {
        respond(socket, 200, "{\"ok\":true}", "application/json",
                "Set-Cookie: 3dslink=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0\r\n");
    } else if (request.method == "GET" && request.route == "/api/status") {
        const auto storage = GetStorage(config.root);
        respond(socket, 200,
                "{\"name\":\"3DSLink\",\"freeBytes\":" + std::to_string(storage.freeBytes) +
                    ",\"totalBytes\":" + std::to_string(storage.totalBytes) +
                    ",\"completedTransfers\":" + std::to_string(completed.load()) + "}");
    } else if (request.method == "GET" && request.route == "/api/files") {
        listFiles(socket, request);
    } else if (request.method == "GET" &&
               (request.route == "/api/download" || request.route == "/api/preview")) {
        const auto pathItem = request.query.find("path");
        std::string resolved;
        if (pathItem == request.query.end() || !resolvePath(pathItem->second, resolved)) {
            fail(socket, 404, "File not found.");
            return;
        }

        const auto type = MimeType(resolved);
        if (request.route == "/api/preview" && type != "image/png" && type != "image/jpeg" &&
            type != "image/bmp" && type != "image/gif") {
            fail(socket, 415, "Only PNG, JPEG, BMP and GIF images can be previewed.");
            return;
        }

        sendFile(socket, request, resolved, request.route == "/api/download");
    } else if (request.method == "PUT" && request.route == "/api/upload") {
        uploadFile(socket, request);
    } else if (request.method == "POST" && request.route == "/api/folder") {
        const auto pathItem = request.query.find("path");
        std::string resolved;
        if (pathItem == request.query.end() || !resolvePath(pathItem->second, resolved, true)) {
            fail(socket, 400, "Choose a valid folder name.");
            return;
        }
        if (mkdir(resolved.c_str(), 0755) != 0) {
            fail(socket, errno == EEXIST ? 409 : 500,
                 "The folder already exists or could not be created.");
            return;
        }

        respond(socket, 201, "{\"ok\":true}");
    } else {
        fail(socket, 404, "This endpoint does not exist.");
    }
}

void ThreeDsLink::CServer::run() {
    std::vector<TConnection> pending;
    while (running) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        auto maximum = listener;
        for (const auto &client : pending) {
            FD_SET(client.socket, &readable);
            maximum = std::max(maximum, client.socket);
        }

        timeval timeout{0, 200000};
        if (select(maximum + 1, &readable, nullptr, nullptr, &timeout) < 0) {
            continue;
        }

        for (auto client = pending.begin(); client != pending.end();) {
            const auto ready = FD_ISSET(client->socket, &readable);
            auto finished = !running || Now() - client->openedAt >= (client->closing ? 5 : 10);
            if (ready && client->closing) {
                char discarded[4096];
                const auto received = recv(client->socket, discarded, sizeof(discarded), 0);
                finished =
                    finished || received == 0 ||
                    (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR);
            } else if (ready && !finished) {
                TRequest request;
                if (readRequest(client->socket, request)) {
                    handle(client->socket, request);
                }

                //Send FIN after queued bytes, then drain until the peer closes without blocking
                //other clients.
                finished = shutdown(client->socket, SHUT_WR) != 0;
                client->closing = true;
                client->openedAt = Now();
            }
            if (finished) {
                close(client->socket);
                client = pending.erase(client);
            } else {
                ++client;
            }
        }

        if (FD_ISSET(listener, &readable)) {
            const auto client = accept(listener, nullptr, nullptr);
            if (client >= 0) {
                if (pending.size() >= 6 || client >= FD_SETSIZE ||
                    fcntl(client, F_SETFL, O_NONBLOCK) < 0) {
                    close(client);
                } else {
                    pending.push_back({client, Now(), false});
                }
            }
        }
    }

    for (const auto &client : pending) {
        close(client.socket);
    }
}

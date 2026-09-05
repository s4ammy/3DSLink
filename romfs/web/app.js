"use strict";

const state = {
    path: "/",
    entries: [],
    total: 0,
    nextOffset: null,
    view: "list",
    connected: false,
    loading: false,
    uploading: false,
    queue: [],
    dragDepth: 0,
    toastTimer: null,
    navigationId: 0
};

const roundingObserver = new ResizeObserver(entries => {
    for (const entry of entries) {
        const width = entry.borderBoxSize?.[0]?.inlineSize ?? entry.contentRect.width;
        const height = entry.borderBoxSize?.[0]?.blockSize ?? entry.contentRect.height;
        if (width > 0 && height > 0) {
            entry.target.style.setProperty("--corner-radius", `${Math.min(width, height, 200) * 0.1}px`);
        }
    }
});

function Element(id) {
    return document.getElementById(id);
}

function RoundElements(root = document) {
    for (const element of root.querySelectorAll(".rounded")) {
        roundingObserver.observe(element);
    }
}

function Icon(name, className = "icon") {
    const element = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    element.setAttribute("class", className);
    element.setAttribute("aria-hidden", "true");
    const use = document.createElementNS("http://www.w3.org/2000/svg", "use");
    use.setAttribute("href", `#i-${name}`);
    element.append(use);
    return element;
}

function Node(tag, className, text) {
    const element = document.createElement(tag);
    element.className = className;
    if (text !== undefined) {
        element.textContent = text;
    }

    return element;
}

function FormatBytes(bytes) {
    if (bytes === 0) {
        return "0 B";
    }

    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    const unit = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1);
    return `${(bytes / 1024 ** unit).toLocaleString(undefined, { maximumFractionDigits: unit > 0 ? 1 : 0 })} ${units[unit]}`;
}

function FormatDate(seconds) {
    if (seconds <= 0) {
        return "Unknown";
    }

    return new Date(seconds * 1000).toLocaleDateString(undefined, { month: "short", day: "numeric", year: "numeric" });
}

function JoinPath(parent, name) {
    return `${parent === "/" ? "" : parent}/${name}`;
}

function IsImage(name) {
    return /\.(png|jpe?g|bmp|gif)$/i.test(name);
}

function DownloadUrl(path) {
    return `/api/download?path=${encodeURIComponent(path)}`;
}

function PreviewUrl(path) {
    return `/api/preview?path=${encodeURIComponent(path)}`;
}

function Toast(message) {
    clearTimeout(state.toastTimer);
    Element("toast").textContent = message;
    Element("toast").hidden = false;
    state.toastTimer = setTimeout(() => {
        Element("toast").hidden = true;
    }, 4500);
}

function ShowPairing() {
    state.connected = false;
    state.navigationId += 1;
    Element("app").hidden = true;
    Element("pairScreen").hidden = false;
    for (const dialog of document.querySelectorAll("dialog[open]")) {
        dialog.close();
    }

    Element("pin").value = "";
}

async function Api(route, options = {}) {
    let response;
    try {
        response = await fetch(route, { credentials: "same-origin", cache: "no-store", ...options });
    } catch {
        throw new Error("Your 3DS could not be reached. Check Wi-Fi and keep 3DSLink open.");
    }

    const data = await response.json();
    if (!response.ok) {
        if (response.status === 401 && route !== "/api/pair") {
            ShowPairing();
        }

        throw new Error(data.error || `The request failed (${response.status}).`);
    }

    return data;
}

function SetStorage(storage) {
    const used = Math.max(0, storage.totalBytes - storage.freeBytes);
    const percent = storage.totalBytes ? Math.round(100 * used / storage.totalBytes) : 0;
    Element("storageFree").textContent = storage.totalBytes ? FormatBytes(storage.freeBytes) : "Unknown";
    Element("storageUsed").textContent = storage.totalBytes ? `${FormatBytes(used)} used` : "Storage information unavailable";
    Element("storageTotal").textContent = storage.totalBytes ? `${FormatBytes(storage.totalBytes)} total` : "";
    Element("storagePercent").textContent = storage.totalBytes ? `${percent}% used` : "";
    Element("storageBar").style.width = `${percent}%`;
}

async function RefreshStorage() {
    if (!state.uploading && state.connected) {
        try {
            SetStorage(await Api("/api/status"));
        } catch (error) {
            Toast(error.message);
        }
    }
}

async function Connect(storage) {
    state.connected = true;
    Element("pairScreen").hidden = true;
    Element("app").hidden = false;
    Element("deviceAddress").textContent = location.host;
    Element("connectionAddress").textContent = location.host;
    SetStorage(storage || await Api("/api/status"));
    await Navigate("/");
}

function SetBreadcrumbs() {
    const container = Element("breadcrumbs");
    container.replaceChildren(Icon("sd"));
    const root = Node("button", "", "SD card");
    root.addEventListener("click", () => Navigate("/"));
    container.append(root);
    let path = "";
    for (const part of state.path.split("/").filter(Boolean)) {
        path += `/${part}`;
        const destination = path;
        const button = Node("button", "", part);
        button.addEventListener("click", () => Navigate(destination));
        container.append(Icon("chevron", "icon separator"), button);
    }

    container.lastElementChild.setAttribute("aria-current", "page");
}

async function Navigate(path, append = false) {
    if (state.uploading) {
        Toast("Finish or cancel the current upload before browsing another folder.");
        return;
    }

    const navigationId = ++state.navigationId;
    state.loading = true;
    if (!append) {
        state.path = path;
        state.entries = [];
        state.nextOffset = null;
        Element("search").value = "";
        Element("files").replaceChildren();
        SetBreadcrumbs();
    }

    Element("emptyState").hidden = true;
    Element("folderMessage").hidden = true;
    Element("fileCount").textContent = "Opening folder…";
    Element("loadMore").hidden = true;
    const gallery = state.path === "/luma/screenshots";
    Element("pageTitle").textContent = gallery ? "Screenshots" : "Files";
    for (const button of document.querySelectorAll("[data-location]")) {
        const active = button.dataset.location === path;
        button.classList.toggle("active", active);
        if (active) {
            button.setAttribute("aria-current", "page");
        } else {
            button.removeAttribute("aria-current");
        }
    }

    if (gallery && !append) {
        state.view = "grid";
        Element("sort").value = "newest";
    }

    try {
        const page = await Api(`/api/files?path=${encodeURIComponent(path)}&offset=${append ? state.nextOffset : 0}`);
        if (navigationId !== state.navigationId) {
            return;
        }

        state.entries = append ? state.entries.concat(page.entries) : page.entries;
        state.total = page.total;
        state.nextOffset = page.nextOffset;
        RenderFiles();
    } catch (error) {
        if (navigationId !== state.navigationId) {
            return;
        }

        const message = gallery ? `${error.message} Take a screenshot using Luma's Rosalina menu, then refresh.` : error.message;
        Element("folderMessage").textContent = message;
        Element("folderMessage").hidden = false;
        Element("fileCount").textContent = "Folder unavailable";
    } finally {
        if (navigationId === state.navigationId) {
            state.loading = false;
        }
    }
}

function FileRow(file) {
    const path = JoinPath(state.path, file.name);
    const picture = !file.directory && IsImage(file.name);
    const game = /\.(3dsx|cia|nds)$/i.test(file.name);
    const type = file.directory ? "Folder" : picture ? "Image" : game ? "Homebrew file" : "File";
    const row = Node("div", "file-row");
    const open = Node("button", "file-open");
    open.title = file.name;
    open.setAttribute("aria-label", `${file.directory ? "Open folder" : picture ? "Preview" : "Download"} ${file.name}`);
    const icon = Node("span", `file-icon ${file.directory ? "folder" : picture ? "picture" : game ? "game" : ""}`);
    if (picture && state.view === "grid") {
        const image = document.createElement("img");
        image.src = PreviewUrl(path);
        image.alt = "";
        image.loading = "lazy";
        image.addEventListener("error", () => {
            icon.replaceChildren(Icon("image"));
        });
        icon.append(image);
    } else {
        icon.append(Icon(file.directory ? "folder" : picture ? "image" : game ? "game" : "file"));
    }

    const label = Node("span", "file-label");
    label.append(Node("span", "file-name", file.name), Node("span", "file-type", type));
    open.append(icon, label);
    open.addEventListener("click", () => {
        if (file.directory) {
            Navigate(path);
        } else if (picture) {
            ShowPreview(file, path);
        } else {
            Download(path, file.name);
        }
    });
    row.append(open, Node("span", "file-size", file.directory ? "Folder" : FormatBytes(file.size)), Node("span", "file-date", FormatDate(file.modified)));
    if (file.directory) {
        const button = Node("button", "icon-button", "");
        button.setAttribute("aria-label", `Open folder ${file.name}`);
        button.append(Icon("chevron"));
        button.addEventListener("click", () => Navigate(path));
        row.append(button);
    } else {
        const link = Node("a", "icon-button");
        link.href = DownloadUrl(path);
        link.download = file.name;
        link.setAttribute("aria-label", `Download ${file.name}`);
        link.title = `Download ${file.name}`;
        link.append(Icon("download"));
        link.addEventListener("click", event => {
            if (state.uploading) {
                event.preventDefault();
                Toast("Finish or cancel the upload before downloading.");
            }
        });
        row.append(link);
    }

    return row;
}

function RenderFiles() {
    const search = Element("search").value.toLocaleLowerCase();
    const sort = Element("sort").value;
    const entries = state.entries.filter(file => file.name.toLocaleLowerCase().includes(search));
    entries.sort((left, right) => {
        if (left.directory !== right.directory) {
            return left.directory ? -1 : 1;
        }
        if (sort === "newest") {
            return right.modified - left.modified || left.name.localeCompare(right.name);
        }
        if (sort === "size") {
            return right.size - left.size || left.name.localeCompare(right.name);
        }

        return left.name.localeCompare(right.name, undefined, { numeric: true, sensitivity: "base" });
    });

    Element("files").classList.toggle("grid", state.view === "grid");
    Element("fileHeader").hidden = state.view === "grid";
    Element("files").replaceChildren(...entries.map(FileRow));
    for (const view of ["list", "grid"]) {
        Element(`${view}View`).classList.toggle("selected", state.view === view);
        Element(`${view}View`).setAttribute("aria-pressed", String(state.view === view));
    }

    Element("emptyState").hidden = entries.length !== 0;
    Element("emptyTitle").textContent = search ? "No matching files" : "Empty folder";
    Element("emptyText").textContent = search ? "Try another search, or load more files in this folder." : "Drop files here or select Upload files.";
    const paged = state.nextOffset !== null;
    Element("fileCount").textContent = search ? `${entries.length} matching ${entries.length === 1 ? "item" : "items"}${paged ? " in loaded files" : ""}` : `${state.entries.length}${paged ? ` of ${state.total}` : ""} ${state.total === 1 ? "item" : "items"}`;
    Element("loadMore").hidden = !paged;
}

function Download(path, name) {
    if (state.uploading) {
        Toast("Finish or cancel the upload before downloading.");
        return;
    }

    const link = document.createElement("a");
    link.href = DownloadUrl(path);
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
}

function ShowPreview(file, path) {
    if (state.uploading) {
        Toast("Finish or cancel the upload before previewing an image.");
        return;
    }

    Element("previewName").textContent = file.name;
    Element("previewImage").src = PreviewUrl(path);
    Element("previewImage").alt = file.name;
    Element("previewDownload").href = DownloadUrl(path);
    Element("previewDownload").download = file.name;
    Element("previewDetails").textContent = `${FormatBytes(file.size)} · ${FormatDate(file.modified)}`;
    Element("previewDialog").showModal();
}

function QueueFiles(files) {
    if (!state.connected || state.loading) {
        Toast("Open a folder on your 3DS before uploading.");
        return;
    }

    for (const file of files) {
        if (file.size > 4294967295) {
            Toast(`${file.name} is too large. Files must be smaller than 4 GiB.`);
            continue;
        }

        state.queue.push({ file, path: JoinPath(state.path, file.name), status: "queued", progress: 0, message: "Waiting to upload", request: null });
    }

    if (state.queue.length > 0) {
        Element("transferPanel").hidden = false;
        RenderQueue();
        RunQueue();
    }
}

function RenderQueue() {
    Element("transferList").replaceChildren(...state.queue.map(item => {
        const row = Node("div", "transfer-item");
        const title = Node("div", "transfer-title");
        title.append(Node("span", "", item.file.name));
        if (item.status === "uploading" || item.status === "queued") {
            const cancel = Node("button", "text-button", "Cancel");
            cancel.addEventListener("click", () => {
                item.status = "cancelled";
                item.message = "Cancelled";
                item.request?.abort();
                RenderQueue();
            });
            title.append(cancel);
        } else if (item.status === "failed" || item.status === "cancelled") {
            const retry = Node("button", "text-button", "Retry");
            retry.addEventListener("click", () => {
                item.status = "queued";
                item.progress = 0;
                item.message = "Waiting to upload";
                RenderQueue();
                RunQueue();
            });
            title.append(retry);
        } else {
            title.append(Icon("check"));
        }

        row.append(title, Node("div", "transfer-state", item.message));
        if (item.status === "uploading") {
            const progress = document.createElement("progress");
            progress.max = 100;
            progress.value = item.progress;
            progress.setAttribute("aria-label", `Uploading ${item.file.name}`);
            row.append(progress);
        }

        return row;
    }));
    Element("closeTransfers").disabled = state.uploading || state.queue.some(item => item.status === "queued");
}

function Upload(item) {
    return new Promise(resolve => {
        const request = new XMLHttpRequest();
        item.request = request;
        request.open("PUT", `/api/upload?path=${encodeURIComponent(item.path)}`);
        request.setRequestHeader("Content-Type", "application/octet-stream");
        const started = performance.now();
        request.upload.addEventListener("progress", event => {
            if (event.lengthComputable) {
                item.progress = Math.round(100 * event.loaded / event.total);
                const seconds = Math.max((performance.now() - started) / 1000, 0.1);
                item.message = item.progress === 100 ? "Saving to SD card…" : `${item.progress}% · ${FormatBytes(event.loaded / seconds)}/s`;
                RenderQueue();
            }
        });
        request.addEventListener("load", () => {
            if (request.status === 201) {
                item.status = "complete";
                item.message = `${FormatBytes(item.file.size)} · Saved to SD card`;
            } else {
                item.status = "failed";
                try {
                    item.message = JSON.parse(request.responseText).error;
                } catch {
                    item.message = "Could not upload. Check the connection and retry.";
                }

                if (request.status === 401) {
                    ShowPairing();
                }
            }

            resolve();
        });
        request.addEventListener("error", () => {
            item.status = "failed";
            item.message = "Connection interrupted. Keep 3DSLink open and retry.";
            resolve();
        });
        request.addEventListener("abort", () => {
            item.status = "cancelled";
            item.message = "Cancelled";
            resolve();
        });
        request.send(item.file);
    });
}

async function RunQueue() {
    if (state.uploading || !state.connected) {
        return;
    }

    state.uploading = true;
    try {
        let item;
        while (state.connected && (item = state.queue.find(candidate => candidate.status === "queued"))) {
            item.status = "uploading";
            item.message = "Connecting to your 3DS…";
            RenderQueue();
            await Upload(item);
            item.request = null;
        }
    } finally {
        state.uploading = false;
        RenderQueue();
        if (state.connected) {
            await Navigate(state.path);
            await RefreshStorage();
        }
    }
}

async function LockConnection() {
    if (state.uploading) {
        Toast("Finish or cancel your uploads before locking the connection.");
        return;
    }

    try {
        await Api("/api/logout", { method: "POST" });
        ShowPairing();
    } catch (error) {
        Toast(error.message);
    }
}

Element("pairForm").addEventListener("submit", async event => {
    event.preventDefault();
    Element("pairButton").disabled = true;
    Element("pairError").textContent = "";
    try {
        await Api("/api/pair", { method: "POST", body: Element("pin").value });
        Element("pin").value = "";
        await Connect();
        RunQueue();
    } catch (error) {
        Element("pairError").textContent = error.message;
    } finally {
        Element("pairButton").disabled = false;
    }
});

Element("pin").addEventListener("input", event => {
    event.target.value = event.target.value.replace(/\D/g, "").slice(0, 4);
});

for (const button of document.querySelectorAll("[data-location]")) {
    button.addEventListener("click", () => Navigate(button.dataset.location));
}

Element("screenshotShortcut").addEventListener("click", () => Navigate("/luma/screenshots"));
Element("search").addEventListener("input", RenderFiles);
Element("sort").addEventListener("change", RenderFiles);
Element("refresh").addEventListener("click", async () => {
    await Navigate(state.path);
    await RefreshStorage();
});
Element("loadMore").addEventListener("click", () => Navigate(state.path, true));
for (const view of ["list", "grid"]) {
    Element(`${view}View`).addEventListener("click", () => {
        state.view = view;
        RenderFiles();
    });
}

Element("uploadButton").addEventListener("click", () => Element("fileInput").click());
Element("fileInput").addEventListener("change", event => {
    QueueFiles(Array.from(event.target.files));
    event.target.value = "";
});
Element("logout").addEventListener("click", LockConnection);
const mobileLock = Node("button", "text-button mobile-lock", "Lock");
mobileLock.prepend(Icon("lock"));
mobileLock.addEventListener("click", LockConnection);
document.querySelector(".topbar").append(mobileLock);

Element("newFolder").addEventListener("click", () => {
    if (state.uploading || state.loading) {
        Toast("Wait for the current operation to finish.");
        return;
    }

    Element("folderName").value = "";
    Element("folderError").textContent = "";
    Element("folderDialog").showModal();
});
Element("folderForm").addEventListener("submit", async event => {
    event.preventDefault();
    const name = Element("folderName").value.trim();
    if (!name || /[\\/:*?"<>|]/.test(name) || name === "." || name === ".." || name.endsWith(".")) {
        Element("folderError").textContent = "Choose a name without slashes or special characters.";
        return;
    }

    const button = event.submitter;
    button.disabled = true;
    try {
        await Api(`/api/folder?path=${encodeURIComponent(JoinPath(state.path, name))}`, { method: "POST" });
        Element("folderDialog").close();
        await Navigate(state.path);
        Toast("Folder created.");
    } catch (error) {
        Element("folderError").textContent = error.message;
    } finally {
        button.disabled = false;
    }
});

for (const button of document.querySelectorAll("[data-close]")) {
    button.addEventListener("click", () => Element(button.dataset.close).close());
}

Element("previewImage").addEventListener("error", () => {
    Toast("The image could not be loaded. Try downloading it instead.");
});
Element("previewDialog").addEventListener("close", () => Element("previewImage").removeAttribute("src"));
Element("closeTransfers").addEventListener("click", () => {
    if (!state.uploading) {
        Element("transferPanel").hidden = true;
        state.queue = [];
    }
});

window.addEventListener("dragenter", event => {
    if (state.connected && Array.from(event.dataTransfer.types).includes("Files")) {
        event.preventDefault();
        state.dragDepth += 1;
        Element("dropOverlay").hidden = false;
    }
});
window.addEventListener("dragover", event => {
    if (Array.from(event.dataTransfer.types).includes("Files")) {
        event.preventDefault();
    }
});
window.addEventListener("dragleave", event => {
    event.preventDefault();
    state.dragDepth = Math.max(0, state.dragDepth - 1);
    if (state.dragDepth === 0) {
        Element("dropOverlay").hidden = true;
    }
});
window.addEventListener("drop", event => {
    event.preventDefault();
    state.dragDepth = 0;
    Element("dropOverlay").hidden = true;
    const files = Array.from(event.dataTransfer.files);
    const items = Array.from(event.dataTransfer.items);
    if (items.some(item => item.webkitGetAsEntry?.()?.isDirectory)) {
        Toast("Choose individual files to upload. Folder uploads are not supported yet.");
        return;
    }

    QueueFiles(files);
});
window.addEventListener("beforeunload", event => {
    if (state.uploading) {
        event.preventDefault();
        event.returnValue = "";
    }
});

RoundElements();
Api("/api/status").then(Connect).catch(error => {
    ShowPairing();
    if (!error.message.includes("console PIN")) {
        Element("pairError").textContent = error.message;
    }
});

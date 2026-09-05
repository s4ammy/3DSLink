#!/usr/bin/env bash
set -euo pipefail

readonly projectDirectory="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$projectDirectory"
scripts/build.sh

readonly makeromPath="${MAKEROM:-$projectDirectory/.tools/Project_CTR/makerom/bin/makerom}"
readonly bannerToolPath="${BANNERTOOL:-$projectDirectory/.tools/bannertool/build/bannertool}"

if [[ ! -x "$makeromPath" || ! -x "$bannerToolPath" ]]; then
    printf 'Run scripts/setupPackaging.sh, or set MAKEROM and BANNERTOOL to the tools.\n' >&2
    exit 1
fi

"$bannerToolPath" makebanner -i meta/banner.png -a meta/silence.wav -o build/banner.bnr
"$makeromPath" -f cia -o dist/3DSLink.cia -elf 3DSLink.elf -rsf meta/app.rsf \
    -icon 3DSLink.smdh -banner build/banner.bnr -target t -major 0 -minor 1 -micro 0

python3 - <<'PY'
import hashlib
from pathlib import Path
import zipfile

distribution = Path('dist')
with zipfile.ZipFile(distribution / '3DSLink-0.1.0.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    archive.write(distribution / '3DSLink.cia', '3DSLink.cia')
    archive.write(distribution / '3DSLink.3dsx', '3ds/3dslink/3DSLink.3dsx')
    archive.write(distribution / '3DSLink.smdh', '3ds/3dslink/3DSLink.smdh')
    archive.write('README.md', 'README.md')
    archive.write('THIRD_PARTY.md', 'THIRD_PARTY.md')
    for licenseFile in sorted(Path('meta/licenses').glob('*.txt')):
        archive.write(licenseFile, 'licenses/' + licenseFile.name)

files = ['3DSLink.cia', '3DSLink.3dsx', '3DSLink.smdh', '3DSLink-0.1.0.zip']
checksums = [hashlib.sha256((distribution / name).read_bytes()).hexdigest() + '  ' + name for name in files]
(distribution / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
print('Installable CIA and Homebrew Launcher builds are ready in dist/.')
PY

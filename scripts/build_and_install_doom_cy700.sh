#!/bin/bash
# Build DOOM EX-word (CY700) and prepare the DOOMX add-on package.
# Installs to the device over USB when libexword is available.
set -euo pipefail

export DEVKITPRO=/opt/exword/devkitPro
export DEVKITSH4=/opt/exword/devkitPro/devkitSH4
export PATH="$DEVKITSH4/bin:$DEVKITPRO/tools/bin:/usr/local/bin:$PATH"

SRC=/mnt/d/EXword/addons_src/DOOM_cy700
BUILD=/opt/exword/DOOM_cy700

# --- sync sources into the build tree (LF endings) ---
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp -a "$SRC/." "$BUILD/"
find "$BUILD" -type f \( -name '*.c' -o -name '*.h' -o -name 'Makefile' \) -exec sed -i 's/\r$//' {} +
mkdir -p "$BUILD/src/libc/include"
cp /opt/exword/exword-memo-app/src/libc/*.c "$BUILD/src/libc/"
cp /opt/exword/exword-memo-app/src/libc/*.s "$BUILD/src/libc/"
cp /opt/exword/exword-memo-app/src/libc/memmgr.h "$BUILD/src/libc/"
cp /opt/exword/exword-memo-app/src/libc/logger.h "$BUILD/src/libc/"
cp /opt/exword/exword-memo-app/src/libc/include/*.h "$BUILD/src/libc/include/"
find "$BUILD/src/libc" -type f -exec sed -i 's/\r$//' {} +

cd "$BUILD"
make clean >/dev/null 2>&1 || true
make app

# --- build the CRLF add-on package on the Windows side ---
python3 - <<'PY'
from pathlib import Path
import shutil

workdir = Path("/mnt/d/EXword/addons")
pkg = workdir / "DOOMX"
shutil.rmtree(pkg, ignore_errors=True)
pkg.mkdir(parents=True)
shutil.copy2("/opt/exword/DOOM_cy700/build/ja/DOOMX/doom.d01", pkg / "doom.d01")

def write_crlf(path: Path, lines):
    path.write_bytes(("\r\n".join(lines) + "\r\n").encode("ascii"))

write_crlf(pkg / "diction.htm", [
    "<html>",
    "<head>",
    "<title>DOOM EX-word</title>",
    '<meta name="soft" content="OFF">',
    "</head>",
    "<body>",
    "</body>",
    "</html>",
])
for n in range(3, 16):
    write_crlf(pkg / f"infodp{n}.htm", [
        'dicapl="700"',
        'id="DOOMX"',
        'history="OFF"',
        'kjump="OFF"',
        "",
        "#switch;",
    ])
    write_crlf(pkg / f"infogt{n}.htm", [
        'more_srch="OFF"',
        'narrow_srch="OFF"',
        'order_srch="OFF"',
        'center="OFF"',
        'tts_lang="of"',
        'addin="ON,doom.d01"',
        'jp_lang="of"',
        'pic_prev="OFF"',
        'list_idec="OFF"',
    ])
(pkg / "fileinfo.cji").write_bytes(b"")
print("package ready at", pkg)
PY

echo "==== verify CRLF ===="
xxd /mnt/d/EXword/addons/DOOMX/diction.htm | head -3
xxd /mnt/d/EXword/addons/DOOMX/infodp9.htm | head -3
ls -la /mnt/d/EXword/addons/DOOMX/doom.d01

# --- install over USB when the device is connected ---
pkill -9 exword 2>/dev/null || true
sleep 1
if ! lsusb | grep -qi 07cf; then
  echo "NO_USB - package ready at /mnt/d/EXword/addons/DOOMX"
  exit 0
fi

cd /mnt/d/EXword/addons
AUTHKEY=E149E149E87FE87FF31CF31CFA34FA34A70FA70F
cmds=$(mktemp)
cat > "$cmds" <<EOF
connect library ja
dict auth doomexp 0x${AUTHKEY}
dict remove DOOMX
dict install DOOMX
dict remove MEMOA
dict install MEMOA
dict list
disconnect
exit
EOF
timeout 120 exword < "$cmds" | tee /mnt/d/EXword/install_doom_log.txt
rm -f "$cmds"
echo DONE

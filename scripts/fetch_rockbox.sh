#!/bin/sh
# Download the official Rockbox 4.0 build for the iPod Nano 1G and build the
# MBR/GPT disk images the emulator boots. Firmware and images stay in
# fixtures/rockbox/, which is gitignored. Apple firmware is never fetched.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
DEST="$ROOT/fixtures/rockbox"
VERSION="4.0"
ZIP_NAME="rockbox-ipodnano1g-4.0.zip"
SHA256="04f2661ceb9d11f0ee25aae00037dfef83d8f725b11ec8811ec652d425df189f"
# download.rockbox.org answers some clients with an anti-bot page. The second
# URL is a byte-identical capture of that release file; both must match SHA256.
URLS="
https://download.rockbox.org/release/4.0/${ZIP_NAME}
https://web.archive.org/web/20250527134820id_/https://download.rockbox.org/release/4.0/${ZIP_NAME}
"

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required" >&2
    exit 1
fi

mkdir -p "$DEST/firmware" "$DEST/images"
ZIP="$DEST/$ZIP_NAME"
TMP="$DEST/${ZIP_NAME}.partial"

have_zip=0
if [ -f "$ZIP" ]; then
    got=$(sha256sum "$ZIP" | awk '{print $1}')
    if [ "$got" = "$SHA256" ]; then
        have_zip=1
    else
        echo "existing $ZIP checksum mismatch; downloading again" >&2
        rm -f "$ZIP"
    fi
fi

if [ "$have_zip" -ne 1 ]; then
    ok=0
    for url in $URLS; do
        echo "fetch $url"
        if curl -fL --retry 3 --retry-delay 2 -A "Mozilla/5.0" -o "$TMP" "$url"; then
            got=$(sha256sum "$TMP" | awk '{print $1}')
            if [ "$got" = "$SHA256" ]; then
                mv "$TMP" "$ZIP"
                ok=1
                break
            fi
            echo "checksum mismatch from $url ($got)" >&2
            rm -f "$TMP"
        else
            echo "download failed: $url" >&2
            rm -f "$TMP"
        fi
    done
    if [ "$ok" -ne 1 ]; then
        echo "could not download Rockbox ${VERSION} for ipodnano1g" >&2
        exit 1
    fi
fi

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
unzip -q -o "$ZIP" -d "$STAGE"
if [ ! -f "$STAGE/.rockbox/rockbox.ipod" ]; then
    echo "zip is missing .rockbox/rockbox.ipod" >&2
    exit 1
fi
cp "$STAGE/.rockbox/rockbox.ipod" "$DEST/firmware/rockbox.ipod"

python3 "$ROOT/tools/make_rockbox_fat_disk.py" --check \
    "$STAGE" "$DEST/images/ipodhd-rockbox-nano.img"
python3 "$ROOT/tools/make_gpt_rockbox_disk.py" \
    "$DEST/images/ipodhd-rockbox-nano.img" \
    "$DEST/images/ipodhd-rockbox-nano-gpt.img"

cat > "$DEST/SOURCE.txt" <<EOF
version=${VERSION}
target=ipodnano1g
file=${ZIP_NAME}
sha256=${SHA256}
url=https://download.rockbox.org/release/${VERSION}/${ZIP_NAME}
firmware=firmware/rockbox.ipod
mbr=images/ipodhd-rockbox-nano.img
gpt=images/ipodhd-rockbox-nano-gpt.img
EOF

echo "Rockbox ${VERSION} ready in $DEST"
echo "sha256 ${SHA256}"

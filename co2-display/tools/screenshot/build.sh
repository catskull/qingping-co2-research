#!/bin/sh
# Render the display UI on the host and write docs/images/display-<theme>.png (3x scale).
# Needs a C++17 compiler and python3; run `pio run -e tdisplay` once first so the GFX font is downloaded.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/../../../docs/images"
tmp=$(mktemp -d)
mkdir -p "$out"
c++ -std=c++17 -O2 -I"$here" -o "$tmp/screenshot" "$here/screenshot.cpp" "$here/../../src/lcd_ui.cpp"
"$tmp/screenshot" "$tmp/display" >/dev/null
for f in "$tmp"/display-*.rgb; do
    name=$(basename "$f" .rgb | tr 'A-Z' 'a-z')
    mv "$f" "$tmp/$name.rgb"
done
python3 "$here/png.py" 3 "$tmp"/display-*.rgb >/dev/null
mv "$tmp"/display-*.png "$out/"
rm -rf "$tmp"
ls "$out"

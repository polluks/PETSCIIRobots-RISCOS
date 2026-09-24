#!/bin/zsh
# Build pipeline: petrobots.cpp -> C89 (p_c89.c) and verify with gcc + ncc-riscos.
set -u
ROOT=/root/ai/PETSCIIRobots-RISCOS
TOOLS=$ROOT/tools
OUT=/root/ai/PETSCIIRobots-RISCOS/tools/out
mkdir -p "$OUT"
DEFS=(
  -DPLATFORM_NAME='"riscos"'
  -DPLATFORM_SCREEN_WIDTH=320
  -DPLATFORM_SCREEN_HEIGHT=200
  -DPLATFORM_MAP_WINDOW_TILES_WIDTH=11
  -DPLATFORM_MAP_WINDOW_TILES_HEIGHT=7
  -DPLATFORM_MODULE_BASED_AUDIO
  -DPLATFORM_IMAGE_BASED_TILES
  -DPLATFORM_IMAGE_SUPPORT
  -DPLATFORM_SPRITE_SUPPORT
  -DPLATFORM_COLOR_SUPPORT
  -DPLATFORM_CURSOR_SUPPORT
  -DPLATFORM_CURSOR_SHAPE_SUPPORT
  -DPLATFORM_FADE_SUPPORT
  -DPLATFORM_LIVE_MAP_SUPPORT
  -DPLATFORM_PRELOAD_SUPPORT
  -DOPTIMIZED_MAP_RENDERING
  -DPLATFORM_MAP_COUNT=14
  -DUSING_EXTERNAL_RENDERING
)
SRC="$ROOT/petrobots.cpp"
python3 "$TOOLS/conv_petro.py" "$SRC" > "$OUT/p_trans.c"
python3 "$TOOLS/c89_mid.py" "$OUT/p_trans.c" > "$OUT/p_mid.c"
python3 "$TOOLS/c89_wrap.py" "$OUT/p_mid.c" > "$OUT/p_wrap.c"
cp "$OUT/p_wrap.c" "$OUT/p_c89.c"
INCS=(-I$HOME/g/Norcroft/external/clib/include -I"$ROOT/thirdparty" -I"$ROOT")
q=$(python3 -c "import sys; print(' '.join(sys.argv[1:]))" "${DEFS[@]}")
echo "=== gcc ==="
gcc -std=gnu89 -Wall -fsyntax-only ${=q} "${INCS[@]}" "$OUT/p_c89.c" 2>&1 | rg -n "error:" | head -30
echo "gcc rc: $pipestatus[1]"
echo "=== ncc ==="
~/g/Norcroft/bin/ncc-riscos -c "${INCS[@]}" ${=q} -o "$OUT/petro.o" "$OUT/p_c89.c" 2>&1 | rg -in "error|serious" | head -40
echo "ncc done"
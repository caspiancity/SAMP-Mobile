#!/usr/bin/env bash
# collect4.sh -- NOMRE (plaka) sistemi ucun. Layihenin kokunde: bash collect4.sh -> collect4_out.txt (~50-80 KB)
S="app/src/main/cpp/samp"
OUT="collect4_out.txt"
: > "$OUT"

whole() { [ -f "$1" ] || { echo "[YOX] $1"; return; }
  { echo "######## FAYL $1 ########"; awk '{printf "%d: %s\n",NR,$0}' "$1" | head -n "${2:-300}" | cut -c1-200; } >> "$OUT"; echo "[OK]   $1"; }

ctx() { [ -f "$1" ] || return; grep -qE "$2" "$1" || return
  { echo "######## $1 : /$2/ ########"; grep -nE -B2 -A12 "$2" "$1" | head -n "${3:-70}" | cut -c1-200; } >> "$OUT"; echo "[OK]   $1 : /$2/"; }

rng() { [ -f "$1" ] || return; grep -qE "$2" "$1" || return
  { echo "######## $1 : /$2/ ########"
    awk -v s="$2" -v e="$3" '!on && $0~s{on=1} on&&st&&e!=""&&$0~e{exit} on{printf "%d: %s\n",NR,$0;st=1}' "$1" | head -n "${4:-120}" | cut -c1-200
  } >> "$OUT"; echo "[OK]   $1 : /$2/"; }

CODE=$(find "$S" -type f \( -name '*.cpp' -o -name '*.h' \) | grep -v -i -E '/(vendor|opus|libs|raknet)/' | sort)

# 1) Mövcud plaka sistemi
whole "$S/game/AzPlate.cpp" 420
whole "$S/game/AzPlate.h" 80
rng "$S/gui/gui.cpp" '^static void PlatePreview' '^}' 40
for f in $(grep -lE 'AzPlate::' $CODE | grep -v -E 'AzPlate\.(cpp|h)$' | head -6); do ctx "$f" 'AzPlate::' 30; done

# 2) Sekil/tekstur yukleme + RenderWare tekstur yaratma
for f in $(grep -lE 'stbi_load|stb_image|png_read|LoadTextureFromDB' $CODE | head -5); do ctx "$f" 'stbi_load|LoadTextureFromDB|RwTextureCreate|RwRasterCreate' 40; done
for f in $(grep -lE 'RwRasterLock|RwRasterCreate|RwTextureCreate' $CODE | head -5); do ctx "$f" 'RwRasterLock|RwRasterCreate|RwTextureCreate|RwRasterUnlock' 40; done

# 3) 3D cizim (sprite/quad) imkanlari
for f in $(grep -lE 'RwIm3DTransform|RwIm3DRenderIndexedPrimitive|RwIm3DRenderTriangle|CSprite::|RwRenderStateSet' $CODE | head -6); do ctx "$f" 'RwIm3DTransform|RwIm3DRenderIndexedPrimitive|RwIm3DEnd|CSprite::RenderOneXLUSprite|RwRenderStateSet' 40; done

# 4) Masin modeli olcusu + render hook-lari
for f in $(grep -lE 'GetBoundingBox|m_boundBox|CColModel' $CODE | head -4); do ctx "$f" 'GetBoundingBox|m_boundBox|struct CColModel|class CColModel|m_pColModel' 30; done
ctx "$S/game/Entity/CVehicleGTA.cpp" 'CVehicle__Render|PreRender|RenderEffects|_hook|Redirect|InlineHook' 70
whole "$S/game/Entity/CVehicleGTA.h" 60

# 5) Font / resurs yerleri
{ echo "######## RESURS FAYLLARI ########"
  find app/src/main -type f \( -iname '*.ttf' -o -iname '*.otf' -o -iname '*.png' -o -iname '*.json' \) 2>/dev/null | grep -v -i -E '/(res/mipmap|res/drawable)' | head -60
} >> "$OUT"

echo "-----------------------------------------"
echo "Hazirdir: $OUT  ($(wc -c < "$OUT") bayt)"

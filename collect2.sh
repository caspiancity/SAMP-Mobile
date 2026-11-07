#!/usr/bin/env bash
# Repo kokunde isle:  bash collect2.sh   ->  collect2.txt yaranir (bunu yukle)
cd "$(dirname "$0")" 2>/dev/null
S=app/src/main/cpp/samp
OUT=collect2.txt
: > "$OUT"

sect() { printf '\n\n===== %s =====\n' "$1" >> "$OUT"; }
ctx() {   # pattern [B] [A]
  grep -rn -B"${2:-2}" -A"${3:-8}" "$1" $S --include=*.cpp --include=*.h 2>/dev/null >> "$OUT"
}

sect "1) m_renderLights / RenderLightsFlags istifadesi"
ctx "m_nRenderLightsFlags|m_bRightFront|m_bLeftFront|m_bRightRear|m_bLeftRear|RenderLightsFlags" 2 8

sect "2) DoVehicleLights-in TAM govdesi (RenderLightsFlags-in iclerde necə işlədiyini görmək üçün)"
F=$(grep -rl "CVehicle__DoVehicleLights_hook" $S --include=*.cpp | head -1)
echo "fayl: $F" >> "$OUT"
if [ -n "$F" ]; then
  L=$(grep -n "void CVehicle__DoVehicleLights_hook" "$F" | head -1 | cut -d: -f1)
  [ -n "$L" ] && sed -n "${L},$((L+60))p" "$F" >> "$OUT"
fi

sect "3) m_pCustomCarPlate istifadesi (CVehicleGTA.h-dan basqa)"
grep -rn "m_pCustomCarPlate\|CustomCarPlate\|CustomPlate\|VehiclePlate\|NumberPlate\|number_?plate" $S \
  --include=*.cpp --include=*.h 2>/dev/null | grep -v "CVehicleGTA.h:" >> "$OUT"

sect "4) tekstura yaratma alətləri (RwTextureCreate/stb_image ve s.)"
grep -rln "RwTextureCreate\|RwRasterCreate\|stb_image\|stbi_\|RwTextureRead\|LoadTextureFromDB\|CUtil::LoadTexture" $S \
  --include=*.cpp --include=*.h 2>/dev/null >> "$OUT"

sect "5) plak teksturu/material ustunde setTexture (plate elave olunan yer)"
grep -rn "RwFrameForAllObjects\|plateFrame\|\"plate\"\|PLATE" $S --include=*.cpp --include=*.h 2>/dev/null | head -60 >> "$OUT"

sect "6) RwTexture/RwRaster strukturlarinin tanimlari (elimizde nə var)"
grep -rln "struct RwTexture\|class RwTexture\|struct RwRaster\|class RwRaster" $S --include=*.h 2>/dev/null >> "$OUT"
F2=$(grep -rln "struct RwTexture\b" $S --include=*.h 2>/dev/null | head -1)
[ -n "$F2" ] && { echo "--- $F2 ---" >> "$OUT"; sed -n '1,60p' "$F2" >> "$OUT"; }

sect "7) freetype/font-png benzeri hazir pixel->tekstura funksiyasi varmi (umumi axtaris)"
grep -rln "RwTextureCreate\|png_\|lodepng\|stb_image_write" $S --include=*.cpp --include=*.h 2>/dev/null >> "$OUT"

wc -l "$OUT"
cp "$OUT" ~/storage/downloads/ 2>/dev/null && echo "Downloads-a da kopyalandi"
echo "Hazirdir: $OUT -> yukle"

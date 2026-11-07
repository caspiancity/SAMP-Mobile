#!/usr/bin/env bash
# Repo kokunde isle:  bash collect3.sh   ->  collect3.txt yaranir (bunu yukle)
cd "$(dirname "$0")" 2>/dev/null
S=app/src/main/cpp
OUT=collect3.txt
: > "$OUT"

sect() { printf '\n\n===== %s =====\n' "$1" >> "$OUT"; }
ff()   { find $S -iname "$1" -not -path "*/vendor/*" 2>/dev/null | head -1; }   # boyuk-kicik herf fərqsiz
show() {  # yol [max setir]
  sect "$1"
  if [ -n "$1" ] && [ -f "$1" ]; then head -n "${2:-300}" "$1" >> "$OUT"; else echo "(tapilmadi)" >> "$OUT"; fi
}

# ---- A) NOMRE LOVHESI ----
sect "A1) adinda 'plate' olan butun fayllar"
find $S -iname "*plate*" 2>/dev/null >> "$OUT"
for f in $(find $S -iname "*plate*" -not -path "*/vendor/*" \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) 2>/dev/null | head -4); do
  show "$f" 300
done

show "$(ff VehicleModelInfo.cpp)" 230
show "$(ff VehicleModelInfo.h)" 200

sect "A2) scriptrpc.cpp - ScrSetVehicleNumberPlate"
SR=$(ff scriptrpc.cpp)
[ -n "$SR" ] && sed -n '895,960p' "$SR" >> "$OUT"

sect "A3) patches.cpp 195-275 (ENABLE_CUSTOM_PLATE konteksti)"
PA=$(ff patches.cpp)
[ -n "$PA" ] && sed -n '195,275p' "$PA" >> "$OUT"

show "$(ff materialtextgenerator.h)" 200
show "$(ff materialtextgenerator.cpp)" 260

sect "A4) .hpp daxil olmaqla CCustomPlateManager axtarisi"
grep -rn "CCustomPlateManager" $S --include=*.cpp --include=*.h --include=*.hpp 2>/dev/null | head -30 >> "$OUT"

# ---- B) SINYAL ----
sect "B1) m_renderLights / ms_lightsOn istifadesi (duzeldilmis regex)"
grep -rn -E -B2 -A6 "m_renderLights|m_nRenderLightsFlags|ms_lightsOn|m_bRightFront|m_bLeftRear" $S \
  --include=*.cpp --include=*.h 2>/dev/null | head -120 >> "$OUT"

sect "B2) libGTASA.so simvollari (isiq / sinyal / plate)"
SO=$(find . -name "libGTASA.so" 2>/dev/null | head -1)
echo "so: $SO" >> "$OUT"
if [ -n "$SO" ] && command -v nm >/dev/null 2>&1; then
  nm -D --defined-only "$SO" 2>/dev/null | grep -i -E "lightson|SetupLight|RenderLight|ms_light|HeadLight|TailLight|Indicator|Flasher|Blink|CustomCarPlate|PlateText|CustomPlate|DoVehicleLights|DoSunGlare" | head -80 >> "$OUT"
else
  echo "nm yoxdur ve ya so tapilmadi (pkg install binutils)" >> "$OUT"
fi

sect "B3) CVehicle sarğı sinfi (game/vehicle.h)"
show "$(ff vehicle.h)" 160

# ---- C) /mm MENYUSUNUN CERCIVESI ----
sect "C1) VehTuner"
show "$(ff VehTuner.h)" 200
show "$(ff VehTuner.cpp)" 400

GUI=$(ff gui.cpp)
sect "C2) gui.cpp 1-560 (mm kommunal funksiyalari: Tap/ToggleRow/Slider...)"
[ -n "$GUI" ] && sed -n '1,560p' "$GUI" >> "$OUT"

sect "C3) RenderHandlingDialog - TAM"
if [ -n "$GUI" ]; then
  L=$(grep -n "^void RenderHandlingDialog" "$GUI" | head -1 | cut -d: -f1)
  [ -n "$L" ] && awk -v s="$L" 'NR>=s{print} /s_hudColorIdx *= *0/{if(NR>s) exit}' "$GUI" >> "$OUT"
fi

wc -l "$OUT"
cp "$OUT" ~/storage/downloads/ 2>/dev/null && echo "Downloads-a da kopyalandi"
echo "Hazirdir: $OUT -> yukle"

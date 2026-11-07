#!/usr/bin/env bash
# Repo kokunde isle:  bash collect4.sh   ->  collect4.txt yaranir (bunu yukle)
cd "$(dirname "$0")" 2>/dev/null
S=app/src/main/cpp/samp
OUT=collect4.txt
: > "$OUT"

sect() { printf '\n\n===== %s =====\n' "$1" >> "$OUT"; }
ff()   { find $S -iname "$1" -not -path "*/vendor/*" 2>/dev/null | head -1; }
show() {  # yol [max setir]
  sect "$1"
  if [ -n "$1" ] && [ -f "$1" ]; then head -n "${2:-250}" "$1" >> "$OUT"; else echo "(tapilmadi)" >> "$OUT"; fi
}
SO=$(find . -name "libGTASA.so" 2>/dev/null | head -1)
HAVE_NM=0; command -v nm >/dev/null 2>&1 && HAVE_NM=1
symgrep() {  # regex
  if [ -n "$SO" ] && [ "$HAVE_NM" = 1 ]; then
    nm -D --defined-only "$SO" 2>/dev/null | grep -E "$1" | head -150 >> "$OUT"
  else
    echo "nm yoxdur ve ya libGTASA.so tapilmadi (pkg install binutils)" >> "$OUT"
  fi
}

sect "0) git"
git log --oneline -5 >> "$OUT" 2>&1
git status --short >> "$OUT" 2>&1

# ============ A) Ə / ə HERFI (font) ============
sect "A1) fontlar"
find app -type f \( -iname "*.ttf" -o -iname "*.otf" \) -not -path "*/build/*" -exec ls -la {} \; >> "$OUT" 2>/dev/null
sect "A2) font yukleme kodu (grep)"
grep -rn -E "AddFontFromFileTTF|AddFontFromMemory|GetGlyphRanges|GlyphRanges|ImFontGlyphRangesBuilder|glyph_ranges|ImFontConfig" $S \
  --include=*.cpp --include=*.h 2>/dev/null | grep -v "/vendor/imgui/" | head -40 >> "$OUT"
show "$(ff imguiwrapper.cpp)" 400
show "$(ff imguiwrapper.h)" 200
show "$(ff uisettings.h)" 200
show "$(ff uisettings.cpp)" 220
sect "A3) encoding (cp1251 -> utf8 cevirmesi)"
for f in $(find $S -ipath "*encoding*" \( -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) 2>/dev/null | head -4); do show "$f" 220; done
sect "A4) cp2utf / utf2cp istifadesi"
grep -rn -E "cp2utf|utf2cp" $S --include=*.cpp --include=*.h 2>/dev/null | grep -v "/vendor/" | head -40 >> "$OUT"

# ============ B) CHAT (boy + animasiya) ============
show "$(ff listbox.h)" 200
show "$(ff listbox.cpp)" 300
show "$(ff scrollpanel.h)" 120
show "$(ff scrollpanel.cpp)" 220
show "$(ff label.h)" 120
show "$(ff label.cpp)" 160
show "$(ff widget.cpp)" 220

# ============ C) HUD GIZLETME ============
sect "C1) hooks.cpp 100-210 (CHud::Draw ve render hook)"
sed -n '100,210p' $S/game/hooks.cpp >> "$OUT"
sect "C2) render / HUD cagirislari"
grep -rn -E "pUI->render\(\)|->render\(\)|Render2dStuff|DrawHud|_ZN4CHud" $S --include=*.cpp 2>/dev/null | grep -v "/vendor/" | head -40 >> "$OUT"
sect "C3) libGTASA native HUD / toxunus dugmeleri / radar simvollari"
symgrep "_ZN(4CHud|6CRadar|15CTouchInterface|7CWidget|13CWidgetRegion)"
ls $S/game/Widgets >> "$OUT" 2>&1
show "$(ff WidgetGta.cpp)" 200

# ============ D) HANDLING / TUNING ============
show "$(ff tHandlingData.h)" 260
sect "D1) cTransmission / tTransmissionGear tanimi"
for f in $(grep -rl -E "struct cTransmission|struct tTransmissionGear" $S --include=*.h 2>/dev/null | head -2); do show "$f" 140; done
show "$(ff vehicle.cpp)" 420
sect "D2) libGTASA masin yukseltme / nitro / hidravlika simvollari"
symgrep "(AddVehicleUpgrade|RemoveVehicleUpgrade|Nitro|Hydraulic|SetVehicleColour|CustomCarEnvMap|WheelScale)"

# ============ E) KAMERA ============
sect "E1) kamera fayllari"
find $S -iname "*camera*" -not -path "*/vendor/*" >> "$OUT" 2>/dev/null
for f in $(find $S -iname "*camera*" -not -path "*/vendor/*" \( -name "*.h" -o -name "*.cpp" \) 2>/dev/null | head -3); do show "$f" 220; done
sect "E2) CCamera / TheCamera / camPtr istifadesi"
grep -rn -E "CCamera|TheCamera|CCam\b|camPtr|FirstPerson|1stPerson|CamMode|SetCamera" $S --include=*.cpp --include=*.h 2>/dev/null | grep -v "/vendor/" | head -60 >> "$OUT"
sect "E3) libGTASA kamera simvollari"
symgrep "_ZN(7CCamera|4CCam)"

# ============ F) SINYAL / NOMRE ucun RW funksiyalari ============
sect "F1) vehicle.cpp SetPlateText"
grep -n -A28 "CVehicle::SetPlateText" $S/game/vehicle.cpp >> "$OUT" 2>/dev/null
sect "F2) RW: struct RpMaterial / RwTexture / frame adi funksiyalari"
grep -rn -E "struct RpMaterial|struct RwTexture\b|RwFrameForAllChildren|RwFrameGetName|GetFrameNodeName|FindNamedNode|RpClumpGetFrame|RpGeometryGetMaterial" $S/game/RW $S/game/*.h 2>/dev/null | head -40 >> "$OUT"
F=$(grep -rl "struct RpMaterial" $S/game/RW --include=*.h 2>/dev/null | head -1)
if [ -n "$F" ]; then L=$(grep -n "struct RpMaterial" "$F" | head -1 | cut -d: -f1); echo "--- $F ---" >> "$OUT"; sed -n "${L},$((L+30))p" "$F" >> "$OUT"; fi
F=$(grep -rl "struct RwTexture\b" $S/game/RW --include=*.h 2>/dev/null | head -1)
if [ -n "$F" ]; then L=$(grep -n "struct RwTexture\b" "$F" | head -1 | cut -d: -f1); echo "--- $F ---" >> "$OUT"; sed -n "${L},$((L+25))p" "$F" >> "$OUT"; fi

wc -l "$OUT"
cp "$OUT" ~/storage/downloads/ 2>/dev/null && echo "Downloads-a da kopyalandi"
echo "Hazirdir: $OUT -> yukle"

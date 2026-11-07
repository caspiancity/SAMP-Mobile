#!/usr/bin/env bash
# Repo kokunde isle:  bash collect.sh   ->  collect.txt yaranir (bunu mene yukle)
cd "$(dirname "$0")" 2>/dev/null
S=app/src/main/cpp/samp
OUT=collect.txt
: > "$OUT"

sect() { printf '\n\n===== %s =====\n' "$1" >> "$OUT"; }
cat_file() {   # yol [maks setir sayi]
  sect "$1"
  if [ -n "$1" ] && [ -f "$1" ]; then head -n "${2:-400}" "$1" >> "$OUT"; else echo "(tapilmadi)" >> "$OUT"; fi
}
find_file() { find $S -name "$1" -not -path "*/vendor/*" 2>/dev/null | head -1; }

# 0) patch tetbiq olunubmu, son commitler
sect "git"
git status --short >> "$OUT" 2>&1
git log --oneline -3 >> "$OUT" 2>&1
grep -c "MTA stilli" $S/gui/gui.cpp >> "$OUT" 2>&1

# 1) /mm ve diger C++ komandalar (chat.cpp)
sect "chat.cpp 150-275"
sed -n '150,275p' $S/gui/samp_widgets/chat.cpp >> "$OUT"
cat_file $S/gui/samp_widgets/chat.h 200
cat_file $S/gui/gui.h 250
cat_file "$(find_file widget.h)" 200

# 2) /mm menyusunun ozu (RenderHandlingDialog)
sect "RenderHandlingDialog"
F=$(grep -rl "void RenderHandlingDialog" $S --include=*.cpp 2>/dev/null | head -1)
echo "fayl: $F" >> "$OUT"
if [ -n "$F" ]; then
  L=$(grep -n "void RenderHandlingDialog" "$F" | head -1 | cut -d: -f1)
  [ -n "$L" ] && sed -n "${L},$((L+260))p" "$F" >> "$OUT"
fi
sect "g_mmTouch / PushTouch / UI_IsPointInHandlingDlg"
grep -rn "g_mmTouch\|PushTouch\|UI_IsPointInHandlingDlg" $S --include=*.cpp --include=*.h 2>/dev/null | head -40 >> "$OUT"

# 3) sinyal ve far koronalari
cat_file "$(find_file Coronas.cpp)" 500
cat_file "$(find_file Coronas.h)" 150
cat_file "$(find_file VehTuner.h)" 150
cat_file "$(find_file VehTuner.cpp)" 300
cat_file $S/game/VehControls.h 80

# 4) toxunus hook-u (UI gizletme ucun)
sect "hooks.cpp toxunus"
H=$(find_file hooks.cpp)
echo "fayl: $H" >> "$OUT"
[ -n "$H" ] && grep -n -B3 -A25 "UI_HandlingDlgCapturedEvent" "$H" 2>/dev/null | head -120 >> "$OUT"

# 5) GTA-nin oz HUD-u (radar, pul, saglamliq) - gizletmek ucun
sect "native HUD / radar"
grep -rn "CHud\|CRadar\|HideHud\|m_bHideHud\|DrawHud\|Hud::" $S --include=*.cpp --include=*.h 2>/dev/null \
  | grep -v "vendor\|RenderVehicleHud\|kHud\|s_hud" | head -80 >> "$OUT"

# 6) APK-ni telefona cixarmaq ucun workflow
sect "workflow"
for f in .github/workflows/*; do
  echo "--- $f" >> "$OUT"
  head -n 150 "$f" >> "$OUT"
done

wc -l "$OUT"
cp "$OUT" ~/storage/downloads/ 2>/dev/null && echo "Downloads-a da kopyalandi"
echo "Hazirdir: $OUT  -> bunu mene yukle"

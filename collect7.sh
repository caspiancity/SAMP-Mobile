#!/bin/bash
# repo-nun KOK qovlugunda isle:  bash collect7.sh   ->  c7_a.txt  c7_b.txt  (ikisini de mene gonder)
S=app/src/main/cpp/samp
d() { [ -f "$1" ] && { echo -e "\n===== $1 =====" >> "$2"; head -c ${3:-50000} "$1" >> "$2"; }; }
LIB=$(find . -name libGTASA.so 2>/dev/null | head -1)

# ---------- A: far rengi + handling + tuning + nomre ----------
A=c7_a.txt; : > $A
d $S/game/VehTuner.h $A 20000
d $S/game/VehTuner.cpp $A 60000
d $S/game/Coronas.h $A 15000
d $S/game/Coronas.cpp $A 50000
d $S/game/RegisteredCorona.h $A 8000
echo -e "\n===== libGTASA far/kolge simvollari =====" >> $A
[ -n "$LIB" ] && nm -D --defined-only "$LIB" 2>/dev/null | grep -i "HeadLight\|LightShadow\|StoreShadow\|CShadows\|DoLightBeam" | head -40 >> $A
echo -e "\n===== handling.cfg =====" >> $A
find app -iname "handling*" 2>/dev/null | head >> $A
H=$(find app -iname "handling.cfg" 2>/dev/null | head -1); [ -n "$H" ] && head -c 6000 "$H" >> $A
echo -e "\n===== komponent / tuning istifadesi =====" >> $A
grep -rn "AddComponent\|AddVehicleUpgrade\|RemoveComponent" $S --include=*.cpp 2>/dev/null | grep -v "\.bak" | head -30 >> $A
echo -e "\n===== vehdump.txt =====" >> $A
for f in $(find / -name vehdump.txt 2>/dev/null | head -3); do echo "-- $f" >> $A; tail -c 6000 "$f" >> $A; done

# ---------- B: kamera + xerite/ESC + chat ----------
B=c7_b.txt; : > $B
echo -e "\n===== git: kamera ile bagli kecmis deyisikliker =====" >> $B
git log --oneline -i --grep="cam\|kamera" | head -10 >> $B
d $S/gui/samp_widgets/chat.cpp $B 40000
d $S/gui/samp_widgets/chat.h $B 8000
echo -e "\n===== kamera ikonu / widgetler =====" >> $B
grep -rn -i "camera\|hud_cam\|WIDGET_CAM\|WIDGET_LOOK\|LookBehind" $S/game/Widgets $S/game/hooks.cpp 2>/dev/null | head -50 >> $B
d $S/game/Widgets/TouchInterface.cpp $B 30000
d $S/game/Widgets/TouchInterface.h $B 8000
echo -e "\n===== ESC / geri duymesi =====" >> $B
grep -rn "KEYCODE_BACK\|KEYCODE_ESCAPE\|onBackPressed\|dispatchKeyEvent\|onKeyDown" app/src/main/java 2>/dev/null | head -20 >> $B
grep -rn "KeyEvent\|nativeKey\|AND_Key\|OnKey" $S --include=*.cpp 2>/dev/null | grep -v "\.bak" | head -20 >> $B
echo -e "\n===== xerite / radar =====" >> $B
grep -n "Radar\|DrawMap\|CMenuManager" $S/game/hooks.cpp $S/game/patches.cpp 2>/dev/null | head -30 >> $B
[ -n "$LIB" ] && nm -D --defined-only "$LIB" 2>/dev/null | grep -i "CMenuManager\|CRadar" | head -80 >> $B

ls -l c7_a.txt c7_b.txt

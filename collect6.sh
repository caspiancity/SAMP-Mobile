#!/bin/bash
S=app/src/main/cpp/samp
d() { [ -f "$1" ] && { echo -e "\n===== $1 =====" >> $2; head -c ${3:-60000} "$1" >> $2; }; }

# 1) GUI (/mm menyusu, speedometr, handling paneli)
: > c6_gui.txt
d $S/gui/gui.cpp c6_gui.txt 150000
d $S/gui/gui.h c6_gui.txt 15000

# 2) Masin: plate + sinyal
: > c6_veh.txt
d $S/game/AzPlate.cpp c6_veh.txt 30000
d $S/game/AzPlate.h c6_veh.txt 5000
d $S/game/Entity/CVehicleGTA.cpp c6_veh.txt 40000
d $S/game/Models/VehicleModelInfo.cpp c6_veh.txt 25000
d $S/game/Models/VehicleModelInfo.h c6_veh.txt 12000
echo -e "\n===== patches.cpp 195-265 =====" >> c6_veh.txt
sed -n '195,265p' $S/game/patches.cpp >> c6_veh.txt

# 3) Kamera, font, handling funksiyalari, chat emrleri
: > c6_misc.txt
echo -e "\n===== hooks.cpp CCamera/CPad =====" >> c6_misc.txt
sed -n '1050,1075p' $S/game/hooks.cpp >> c6_misc.txt
grep -n "HookCPad\|CPad" $S/game/hooks.cpp | head -30 >> c6_misc.txt
d $S/game/pad.cpp c6_misc.txt 30000
d $S/game/pad.h c6_misc.txt 8000
echo -e "\n===== imguiwrapper ranges =====" >> c6_misc.txt
grep -n "0x0[0-9A-F][0-9A-F][0-9A-F]\|Font" $S/gui/imguiwrapper.cpp >> c6_misc.txt
grep -rn "Font\b\|font_path\|arial" $S/gui/gui.cpp $S/main.cpp $S/settings.cpp 2>/dev/null | head -20 >> c6_misc.txt
echo -e "\n===== vehicle.cpp handling funksiyalari =====" >> c6_misc.txt
N=$(grep -n "CVehicle::EnsureUniqueHandling" $S/game/vehicle.cpp | head -1 | cut -d: -f1)
[ -n "$N" ] && sed -n "${N},\$p" $S/game/vehicle.cpp | head -c 14000 >> c6_misc.txt
echo -e "\n===== chat.cpp komandalar =====" >> c6_misc.txt
sed -n '150,260p' $S/gui/samp_widgets/chat.cpp >> c6_misc.txt

ls -l c6_*.txt

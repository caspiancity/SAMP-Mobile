#!/usr/bin/env bash
# collect3.sh -- SON toplama. Qalan butun isler (klaviatura/emoji, tuning kamerasi, sync, handling bar) ucun.
# Layihenin kokunde: bash collect3.sh  ->  collect3_out.txt (hedef ~80-120 KB)
S="app/src/main/cpp/samp"
OUT="collect3_out.txt"
: > "$OUT"

whole() { [ -f "$1" ] || { echo "[YOX] $1"; return; }
  { echo "######## FAYL $1 ########"; awk '{printf "%d: %s\n",NR,$0}' "$1" | head -n "${2:-250}" | cut -c1-200; } >> "$OUT"; echo "[OK]   $1"; }

rng() { [ -f "$1" ] || { echo "[YOX] $1"; return; }
  grep -qE "$2" "$1" || { echo "[YOX] $1 : /$2/"; return; }
  { echo "######## $1 : /$2/ ########"
    awk -v s="$2" -v e="$3" '!on && $0~s{on=1} on&&st&&e!=""&&$0~e{exit} on{printf "%d: %s\n",NR,$0;st=1}' "$1" | head -n "${4:-200}" | cut -c1-200
  } >> "$OUT"; echo "[OK]   $1 : /$2/"; }

ctx() { [ -f "$1" ] || return; grep -qE "$2" "$1" || return
  { echo "######## $1 : /$2/ ########"; grep -nE -B3 -A16 "$2" "$1" | head -n "${3:-90}" | cut -c1-200; } >> "$OUT"; echo "[OK]   $1 : /$2/"; }

one() { find app/src/main -type f -iname "$1" 2>/dev/null | grep -v -i -E '/(opus|vendor)/' | head -1; }
def() { grep -rlE "$1" $S --include=*.cpp --include=*.h 2>/dev/null | grep -v '/vendor/' | head -1; }

# --- klaviatura / emoji / yazi ---
whole "$S/gui/imguirenderer.cpp" 220
whole "$(one 'label.h')" 80
whole "$(one 'label.cpp')" 120
F=$(def 'namespace UISettings|class UISettings|struct UISettings'); [ -n "$F" ] && whole "$F" 140
whole "$S/gui/samp_widgets/keyboard.cpp" 60
ctx "$S/gui/samp_widgets/chat.cpp" 'keyboardEvent|Encoding::|utf2cp|cp2utf|addMessage\(' 120

# --- tuning kamerasi (qaraj) ---
rng "$S/gui/gui.cpp" 'CamCtl' '^UI::UI' 220
rng "$S/gui/samp_widgets/chat.cpp" 'cmd == "/far"' '' 120
ctx "$S/game/vehicle.cpp" 'AddComponent|RemoveComponent|ApplyEngineState|ApplyLightState|SetDoorState|m_wheelScale' 110
ctx "$S/game/Entity/CVehicleGTA.h" 'm_anUpgrades|m_wheelScale|AddVehicleUpgrade|RemoveVehicleUpgrade|SetupSuspension|m_comp1' 40
ctx "$S/net/netrpc.cpp" 'ScrSetVehicleComponent|RPC_ScrAddVehicleComponent|RPC_ScrRemoveVehicleComponent|ScrCamera|SetPlayerCameraPos|SetCameraBehind' 60

# --- handling (HndExt_*) ---
F=$(def 'bool HndExt_Set'); [ -n "$F" ] && whole "$F" 420
F=$(def 'HndExt_ApplyFileSlot'); [ -n "$F" ] && ctx "$F" 'HndExt_ApplyFileSlot|HndExt_SlotUsed|HndExt_Reset' 60

# --- sync (gizli mesaj + yeni oyuncuya vezyet) ---
ctx "$S/net/netrpc.cpp" 'RPC_ClientMessage|RPC_Chat\b|void ClientMessage|void Chat\(' 110
rng "$S/net/vehiclepool.cpp" 'bool CVehiclePool::New' '^}' 80
ctx "$S/net/netgame.cpp" 'RPC_ServerCommand|PLAYER_STATE_DRIVER|SendVehicleSync' 40

# --- Java: qalan hisse + fayl secici ---
awk 'NR>=320{printf "%d: %s\n",NR,$0}' app/src/main/java/com/samp/mobile/game/SAMP.java | head -n 120 >> "$OUT" 2>/dev/null
ctx "app/src/main/java/com/samp/mobile/game/GTASA.java" 'onActivityResult|startActivityForResult|ACTION_OPEN_DOCUMENT|READ_EXTERNAL' 60

echo "-----------------------------------------"
echo "Hazirdir: $OUT  ($(wc -c < "$OUT") bayt)"

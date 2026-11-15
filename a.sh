#!/usr/bin/env bash
# collect2.sh -- yalniz CATISMAYAN hisseler (hedef ~100-150 KB). Layihenin kokunde: bash collect2.sh
# Netice: collect2_out.txt  (hec ne deyismir)
S="app/src/main/cpp/samp"
OUT="collect2_out.txt"
: > "$OUT"

find_one() { find app/src/main -type f -iname "$1" 2>/dev/null | grep -v -i '/opus/' | head -1; }

# whole <fayl> <cap>
whole() {
  [ -f "$1" ] || { echo "[YOX] $1"; return; }
  { echo "######## FAYL $1 ########"; awk '{printf "%d: %s\n", NR, $0}' "$1" | head -n "${2:-300}" | cut -c1-200; } >> "$OUT"
  echo "[OK]   $1"
}

# rng <fayl> <baslangic regex> <bitis regex ('' = fayl sonu)> <cap>
rng() {
  [ -f "$1" ] || { echo "[YOX] $1"; return; }
  grep -qE "$2" "$1" || { echo "[YOX] $1 : /$2/"; return; }
  { echo "######## $1 : /$2/ ########"
    awk -v s="$2" -v e="$3" '
      !on && $0 ~ s { on=1 }
      on && st && e != "" && $0 ~ e { exit }
      on { printf "%d: %s\n", NR, $0; st=1 }' "$1" | head -n "${4:-300}" | cut -c1-200
  } >> "$OUT"
  echo "[OK]   $1 : /$2/"
}

# ctx <fayl> <regex> <cap>
ctx() {
  [ -f "$1" ] || { echo "[YOX] $1"; return; }
  grep -qE "$2" "$1" || return
  { echo "######## $1 : /$2/ ########"; grep -nE -B3 -A14 "$2" "$1" | head -n "${3:-100}" | cut -c1-200; } >> "$OUT"
  echo "[OK]   $1 : /$2/"
}

GUI="$S/gui/gui.cpp"

# 1. YENI /mm menyusu (namespace mm2 ve RenderHandlingDialog -> fayl sonuna qeder)
rng "$GUI" '^namespace mm2' '' 900

# 2. Speedometr + HUD (Sport zolaq / Neon qovs burada)
rng "$GUI" 'SPEEDO_STYLES_PATCH: spidometr stilleri' '^void UI::drawList' 520

# 3. Sinyal sistemi (VehControls) + far koronalari
rng "$S/game/Entity/CVehicleGTA.cpp" '^namespace VehControls' '' 260
for f in $(grep -rlE 's_HeadlightOverride|GetHeadlightRGB' $S --include=*.cpp --include=*.h | grep -v CVehicleGTA | head -4); do
  ctx "$f" 's_HeadlightOverride|GetHeadlightRGB|ClearHeadlightRGB' 80
done

# 4. Tuning / handling strukturlari
whole "$S/game/vehicle_tuning.h" 120
whole "$(find_one 'VehTuner.h')" 120
whole "$(find_one 'tHandlingData.h')" 200
ctx "$S/game/Entity/CVehicleGTA.h" 'm_aSuspension|SuspensionLines|m_fSuspensionLowerLimit|wheel|Wheel' 60

# 5. Sebeke: gizli mesaj ucun ClientMessage + chat gonderme + kodlasdirma
rng "$S/net/netrpc.cpp" '^void ClientMessage\(' '^}' 60
ctx "$S/net/netgame.cpp" 'void CNetGame::SendChat(Command|Message)' 50
for f in $(grep -rlE 'cp1251|Cp1251|CP1251|Utf8To|ToUtf8|utf8_to' $S --include=*.cpp --include=*.h | head -3); do
  ctx "$f" 'cp1251|Cp1251|CP1251|Utf8To|ToUtf8|utf8_to' 40
done
rng "$S/gui/samp_widgets/chat.cpp" 'ProcessClientCommands' '^}' 140

# 6. Klaviatura (qalan hisse: duymeler, AZE layout, touchPopEvent) + widget bazasi
rng "$S/gui/samp_widgets/keyboard.cpp" 'KeyboardLayout::Row::Key::draw' '' 450
LAY=$(grep -rl 'KeyboardLayoutLowerAZE::KeyboardLayoutLowerAZE' $S | grep -v 'samp_widgets/keyboard.cpp' | head -1)
[ -n "$LAY" ] && whole "$LAY" 300
for n in 'button.h' 'widget.h' 'imguirenderer.h' 'settings.h'; do whole "$(find_one "$n")" 130; done

# 7. Oyunçu adi (nametag) - butov fayl
whole "$S/playertags.cpp" 260

# 8. Java kopruleri (fayl secici ucun) + build
whole "$S/java/jniutil.h" 80
whole "$S/java/jniutil.cpp" 200
whole "app/src/main/java/com/samp/mobile/game/SAMP.java" 320
grep -n 'uses-permission' app/src/main/AndroidManifest.xml >> "$OUT" 2>/dev/null
grep -nE 'GLOB|add_library|\.cpp' "$S/../CMakeLists.txt" 2>/dev/null | head -40 >> "$OUT"

echo "-----------------------------------------"
echo "Hazirdir: $OUT  ($(wc -c < "$OUT") bayt)"

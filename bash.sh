#!/usr/bin/env bash
# collect.sh -- layihə kökündə (app/ qovluğunun olduğu yerdə) işlət:
#   bash collect.sh
# Nəticə: collect_out.txt  (bunu mənə göndər)
# Heç nəyi dəyişmir, yalnız oxuyur.

ROOT="$(pwd)"
OUT="$ROOT/collect_out.txt"
CPP="$ROOT/app/src/main/cpp"
JAVA="$ROOT/app/src/main/java"
RES="$ROOT/app/src/main/res"
ASSETS="$ROOT/app/src/main/assets"

if [ ! -d "$ROOT/app/src/main" ]; then
  echo "XƏTA: layihə kökündə deyilsən (app/src/main tapılmadı)."
  exit 1
fi

: > "$OUT"
MAXW=300   # çox uzun sətirləri (hex və s.) kəsir

say()  { echo "$*" >> "$OUT"; }
sec()  { say ""; say "################################################################"; say "## $*"; say "################################################################"; }
sub()  { say ""; say "---- $* ----"; }

# tam fayl (maks N sətir)
dump_file() {
  local f="$1" max="${2:-600}"
  [ -f "$f" ] || { say "[YOXDUR] $f"; return; }
  sub "FAYL: ${f#$ROOT/}  ($(wc -l < "$f") sətir, ilk $max göstərilir)"
  nl -ba "$f" | head -n "$max" | cut -c1-$MAXW >> "$OUT"
}

# pattern ətrafı
grep_ctx() {
  local f="$1" pat="$2" b="${3:-3}" a="${4:-12}" max="${5:-150}"
  [ -f "$f" ] || return
  local r
  r=$(grep -n -B"$b" -A"$a" -E "$pat" "$f" 2>/dev/null | cut -c1-$MAXW | head -n "$max")
  [ -z "$r" ] && return
  sub "${f#$ROOT/}  ~  /$pat/"
  echo "$r" >> "$OUT"
}

# bütün layihədə pattern (yalnız sətirlər, kontekstsiz)
grep_all() {
  local pat="$1" max="${2:-60}"
  local r
  r=$(grep -rn -E "$pat" "$CPP" --include=*.cpp --include=*.h --include=*.hpp 2>/dev/null \
      | grep -v "MM2_V2\|HNDV2" | cut -c1-$MAXW | head -n "$max")
  [ -z "$r" ] && { say "(heç nə tapılmadı: $pat)"; return; }
  sub "BÜTÜN LAYİHƏ ~ /$pat/"
  echo "$r" >> "$OUT"
}

# { } bloku (regex ilk uyğun gəlməsindən balansa qədər)
extract_block() {
  local f="$1" re="$2" max="${3:-400}"
  [ -f "$f" ] || return
  grep -qE "$re" "$f" || return
  sub "BLOK: ${f#$ROOT/}  ~  /$re/"
  awk -v re="$re" '
    BEGIN{d=0;on=0}
    { if(!on && $0 ~ re) on=1
      if(on){
        print FNR": "substr($0,1,300)
        n=gsub(/\{/,"{"); m=gsub(/\}/,"}"); d+=n-m
        if(d<=0 && (n+m)>0) exit
      } }' "$f" | head -n "$max" >> "$OUT"
}

findf() { find "$ROOT/app/src/main" \( -path '*/build/*' -o -path '*/.git/*' \) -prune -o -type f -iname "$1" -print 2>/dev/null; }

say "collect.sh çıxışı  --  $(date)"
say "layihə: $ROOT"

# ---------------------------------------------------------------- 0. ümumi
sec "0. ÜMUMİ: qovluq strukturu, git, CMake"
sub "cpp/samp qovluq ağacı"
find "$CPP/samp" -maxdepth 3 \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null | sed "s|$ROOT/||" | sort >> "$OUT"
sub "git log (son 8)"
git -C "$ROOT" log --oneline -n 8 2>/dev/null >> "$OUT"
sub "CMakeLists (samp)"
for f in $(find "$ROOT/app" -maxdepth 4 -name CMakeLists.txt -not -path '*/build/*' 2>/dev/null | head -3); do dump_file "$f" 120; done
sub "ImGui versiyası"
grep -rn "IMGUI_VERSION " "$CPP" --include=imgui.h 2>/dev/null | head -2 >> "$OUT"
sub "assets-də şriftlər və fayllar"
find "$ASSETS" -maxdepth 3 \( -iname '*.ttf' -o -iname '*.otf' -o -iname '*.png' \) 2>/dev/null | sed "s|$ROOT/||" | head -60 >> "$OUT"
sub "server / RakNet / SAMP versiyası ipucları"
grep_all "0\.3\.7|0\.3\.DL|NETGAME_VERSION|RakNet|ServerIP|server_ip|open\.mp|omp" 25

# ---------------------------------------------------------------- 1. mm menyu
sec "1. /mm MENYUSU: namespace mm, toxunuş, ID-lər, köməkçilər"
for f in $(grep -rlE "^namespace mm[[:space:]]*(\{|$)" "$CPP" --include=*.cpp --include=*.h 2>/dev/null); do
  extract_block "$f" "^namespace mm[[:space:]]*(\{|$)" 1400
done
grep_all "g_bShowHandlingDlg|g_mmTouch|MMTouch|s_hndRectValid|s_hndPos|ID_TOGGLE|ID_CLOSE|ID_NAV0|ID_SWATCH" 40
GUI="$CPP/samp/gui/gui.cpp"
[ -f "$GUI" ] || GUI=$(findf gui.cpp | head -1)
grep_ctx "$GUI" "MM_SpeedoPage" 2 120 260
grep_ctx "$GUI" "static .*SignalBlock\(" 2 90 180
grep_ctx "$GUI" "static .*SliderRow\(" 2 60 110
grep_ctx "$GUI" "s_hndRectValid|g_mmTouch" 6 14 120
sub "gui.cpp-də menyu açan/toxunuşu keçirən yerlər (/mm, handling dialog)"
grep -n -E "RenderHandlingDialog|g_bShowHandlingDlg|\"/mm\"" "$GUI" 2>/dev/null | cut -c1-$MAXW | head -30 >> "$OUT"
grep_all "RenderHandlingDialog|g_bShowHandlingDlg|\"/mm\"" 30

# ---------------------------------------------------------------- 2. spidometr
sec "2. SPİDOMETRLƏR (köhnələri silmək üçün)"
grep_all "[Ss]peedo|SPEEDO|Neon|neon|SportBar|sport" 60
for f in $(grep -rliE "speedo" "$CPP" --include=*.cpp --include=*.h 2>/dev/null | head -6); do
  grep_ctx "$f" "[Ss]peedo" 2 6 70
done

# ---------------------------------------------------------------- 3. klaviatura
sec "3. KLAVİATURA"
for f in $(findf 'keyboard*' | grep -E '\.(cpp|h|hpp)$'); do dump_file "$f" 900; done
for f in $(findf 'input*' | grep -E 'samp_widgets|gui' | grep -E '\.(cpp|h|hpp)$' | head -4); do dump_file "$f" 500; done
for f in $(findf 'widget*' | grep -E '\.(h|hpp)$' | head -2); do dump_file "$f" 200; done
grep_all "keyboard\(\)|->keyboard|class .*Keyboard|setInputString|keyboardEvent" 40
sub "klaviatura layout/emoji/atlas ilə bağlı"
grep_all "emoji|Emoji|0x1F6|0x1F6|0x2764|layout|LAYOUT|shift|Shift" 40

# ---------------------------------------------------------------- 4. şrift, renderer, Azərbaycan hərfləri
sec "4. ŞRİFT / RENDERER / AZƏRBAYCAN HƏRFLƏRİ (? problemi)"
grep_all "AddFontFromFileTTF|AddFontFromMemoryTTF|AddFontFromMemoryCompressed|GetGlyphRanges|ImFontAtlas|ImFontConfig|GlyphRanges|ranges\.|ImFontGlyphRangesBuilder|TextureID.*font|Fonts->" 60
for f in $(findf 'renderer*' | grep -E '\.(cpp|h|hpp)$'); do dump_file "$f" 700; done
for f in $(findf 'font*' | grep -E '\.(cpp|h|hpp)$' | head -4); do dump_file "$f" 300; done
grep_all "cp1251|CP1251|1254|1252|win1251|utf8|UTF-8|utf-8|iconv|WideChar|Cp1251|ToUtf8|FromUtf8|encod" 60
grep_all "ə|ğ|ı|ş|ç|ö|ü|0x018F|0x0259|0x011F|0x0131|0x015F" 20
sub "drawText / calculateTextSize tərifləri"
grep_all "drawText\(|calculateTextSize\(" 25

# ---------------------------------------------------------------- 5. playertags
sec "5. PLAYERTAGS (nick, can/zireh, nametag)"
for f in $(findf 'playertags*'); do dump_file "$f" 600; done
grep_all "GetBackgroundDrawList|g_nametag|bShowNameTags|NameTag|nametag|szNick|DrawAzFlag" 40
grep_all "GetHealth|GetArmour|fHealth|fArmour|m_fHealth|m_fArmor" 20

# ---------------------------------------------------------------- 6. şəbəkə / RPC / sinxronizasiya
sec "6. ŞƏBƏKƏ: RPC, BitStream, sinxronizasiya (qarşı oyunçuya görünmə üçün)"
for f in $(findf 'netgame*' | grep -E '\.(cpp|h|hpp)$'); do dump_file "$f" 350; done
grep_all "RegisterRPC|UnregisterRPC|RPC_[A-Za-z]+|rakClient|RakClientInterface|pRakClient|->RPC\(|SendRPC|BitStream" 80
sub "RPC qeydiyyatı və göndərmə nümunələri"
for f in $(grep -rlE "RegisterRPC|RegisterAsRemoteProcedureCall" "$CPP" --include=*.cpp 2>/dev/null | head -4); do
  grep_ctx "$f" "RegisterRPC|RegisterAsRemoteProcedureCall" 3 25 120
done
for f in $(grep -rlE "RPC\(&|->RPC\(|SendRPC" "$CPP" --include=*.cpp 2>/dev/null | head -4); do
  grep_ctx "$f" "RPC\(&|->RPC\(|SendRPC" 8 8 80
done
sub "siqnal sinxronizasiyası (mövcud ola bilər)"
grep_all "GetSignal|SetSignal|VehControls|signal|Signal|turn_signal|indicator" 50
grep_all "ClientMessage|RPC_Chat|RPC_ServerCommand|SendCommand|SendChat|onSendCommand" 30
sub "vehicle sync paketləri (in-car, driver sync)"
grep_all "VEHICLE_SYNC|DriverSync|PassengerSync|ID_VEHICLE_SYNC|SendInCarFullSyncData|INCAR_SYNC_DATA" 30

# ---------------------------------------------------------------- 7. Coronas, VehTuner, VehControls
sec "7. CCoronas, VehTuner, VehControls (far rəngi, tuning)"
for f in $(findf 'coronas*' | grep -E '\.(cpp|h|hpp)$'); do dump_file "$f" 400; done
for f in $(findf 'vehtuner*' | grep -E '\.(cpp|h|hpp)$'); do dump_file "$f" 600; done
for f in $(findf 'vehcontrols*' | grep -E '\.(cpp|h|hpp)$'); do dump_file "$f" 400; done
grep_all "Headlight|headlight|SetHeadlightRGB|GetHeadlightRGB|ClearHeadlightRGB" 25

# ---------------------------------------------------------------- 8. maşın, handling
sec "8. MAŞIN / HANDLING (handling.cfg parse, slotlar)"
for f in $(findf 'vehicle.h' | head -3); do dump_file "$f" 400; done
for f in $(findf 'vehiclepool*' | grep -E '\.(h|hpp)$'); do dump_file "$f" 150; done
for f in $(findf 'tHandlingData.h'); do dump_file "$f" 300; done
grep_all "HND_SIZE|HND_ENGINE_ACCEL|HND_MAX_VELOCITY|GetOrCreateOwnHandling|GetCurrentHandling|m_pHandling|handling\.cfg|handling_custom|ReadHandling|LoadHandling|HandlingData" 50
HK=$(findf hooks.cpp | head -1)
grep_ctx "$HK" "handling\.cfg|handling_custom" 15 40 120
for f in $(grep -rlE "handling\.cfg|LoadHandlingCfg|HandlingCfg" "$CPP" --include=*.cpp 2>/dev/null | grep -v "hooks.cpp" | head -3); do
  grep_ctx "$f" "handling\.cfg|HandlingCfg" 5 40 100
done
sub "SAMP qovluğu yolu (g_pszStorage) istifadəsi"
grep_all "g_pszStorage" 12

# ---------------------------------------------------------------- 9. kamera / tuning / qaraj
sec "9. KAMERA və TUNING (qaraj rejimi üçün)"
grep_all "CCamera|TheCamera|SetCamera|CameraTarget|Camera_|SetCamPos|LookAt|cam_|PointCamera|InterpolateCamera|SetFixed" 50
for f in $(findf 'camera*' | grep -E '\.(cpp|h|hpp)$' | head -3); do dump_file "$f" 250; done
grep_all "AddVehicleUpgrade|RemoveVehicleUpgrade|AddComponent|m_aUpgrades|ModSlot|Upgrade|upgrade|Component|component|ScmEvent|RPC_ScmEvent|TuneModel|SetVehicleMod|SetMod" 60
grep_all "wheel|Wheel|suspension|Suspension|lowrider|SetVehicleHeight|SetSuspension" 30
sub "oyunçu / maşın pointer köməkçiləri (FindPlayerPed, FindIDFromGtaPtr və s.)"
grep_all "FindPlayerPed\(\)|FindIDFromGtaPtr|GetVehiclePool\(\)|IsInVehicle\(\)" 15

# ---------------------------------------------------------------- 10. çat və əmrlər
sec "10. ÇAT və ƏMRLƏR (/mm açılışı, silinənlər)"
for f in $(findf 'chat.cpp' | head -1); do
  grep_ctx "$f" "cmd == \"/mm\"|\"/mm\"|addDebugMessage\(|cmd ==" 4 14 140
done
for f in $(findf 'chat.h' | head -1); do dump_file "$f" 120; done

# ---------------------------------------------------------------- 11. UI əsas (pUI)
sec "11. pUI, ImGui giriş (toxunuş) və gui.h"
for f in $(findf 'gui.h' | head -1); do dump_file "$f" 250; done
grep_all "ImGui_ImplRenderWare|ImGui::NewFrame|io\.MousePos|io\.MouseDown|AddMousePos|AddMouseButton|OnTouch|touchEvent|ProcessTouch" 30
grep_all "class UISettings|fontSize\(\)|scaleX|scaleY" 15

# ---------------------------------------------------------------- 12. Java launcher
sec "12. LAUNCHER (handling.cfg seçimi, qovluqlar)"
for f in $(find "$JAVA" -name 'SettingsFragment.java' 2>/dev/null | head -1); do dump_file "$f" 400; done
for f in "$RES/layout/fragment_settings.xml"; do dump_file "$f" 260; done
sub "AndroidManifest icazələr"
grep -n -E "uses-permission|MANAGE_EXTERNAL|READ_EXTERNAL|WRITE_EXTERNAL" "$ROOT/app/src/main/AndroidManifest.xml" 2>/dev/null | head -20 >> "$OUT"
sub "oyunun SAMP qovluğu necə müəyyən olunur (Java)"
grep -rn -E "getExternalFilesDir|\"SAMP\"|/SAMP|Environment\.getExternalStorage" "$JAVA" 2>/dev/null | cut -c1-$MAXW | head -15 >> "$OUT"

# ---------------------------------------------------------------- son
say ""
say "== BİTDİ =="
SZ=$(wc -c < "$OUT")
LN=$(wc -l < "$OUT")
echo ""
echo "Hazırdır: $OUT"
echo "Ölçü: $((SZ/1024)) KB, $LN sətir"
if [ "$SZ" -gt 900000 ]; then
  echo "DİQQƏT: fayl böyükdür (>900 KB). Göndərməzdən əvvəl bölmək lazım ola bilər:"
  echo "  split -b 400k collect_out.txt collect_part_"
fi

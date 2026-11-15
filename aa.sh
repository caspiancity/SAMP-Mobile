#!/usr/bin/env bash
# collect.sh v2 -- YALNIZ lazim olan hisseler. Hedef: ~100-250 KB (1 MB-dan cox deyil)
# Istifade (layihenin kokunde):  bash collect.sh   ->  collect_out.txt
# Hec ne deyismir, yalniz oxuyur. Her bolmenin olcusu ekrana yazilir.

ROOT="$(pwd)"; SRC="app/src/main"; CPP="$SRC/cpp"
OUT="$ROOT/collect_out.txt"; TMP="$ROOT/.collect_tmp"
MAXTOTAL=300000      # umumi sert hedd (bayt)
CUT=200              # bir setrin max uzunlugu
: > "$OUT"; : > "$TMP"

[ -d "$CPP" ] || { echo "XETA: $CPP yoxdur. Skripti layihenin kokunde isle."; exit 1; }

FILES=$(find "$CPP" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) \
  | grep -v -i -E '/(vendor|thirdparty|third_party|raknet|armhook|imgui|stb|rapidjson|json|libs|encoding)/' | sort)

LAST=0
mark() { local s; s=$(wc -c < "$OUT"); printf '%-30s %8d bayt\n' "$1" $((s-LAST)); LAST=$s; }

put() {   # put <max setir>
  local s; s=$(wc -c < "$OUT")
  if [ "$s" -gt "$MAXTOTAL" ]; then echo "  (hedd kecildi, atlandi)"; : > "$TMP"; return; fi
  [ -s "$TMP" ] && head -n "$1" "$TMP" | cut -c1-"$CUT" >> "$OUT"
  : > "$TMP"
}

# whole <fayl> <max setir>
whole() {
  [ -f "$1" ] || { echo "[YOX] $1"; return; }
  echo "######## FAYL $1 ########" >> "$TMP"
  awk '{printf "%d: %s\n", NR, $0}' "$1" >> "$TMP"
  put "$2"
}

# snip <regex> <evvel> <sonra> <max setir> <fayllar...>
snip() {
  local pat="$1" b="$2" a="$3" lim="$4"; shift 4
  local f ln
  for f in "$@"; do
    [ -f "$f" ] || continue
    ln=$(grep -nE "$pat" "$f" | cut -d: -f1)
    [ -z "$ln" ] && continue
    echo "######## PARCA $f ########" >> "$TMP"
    echo "$ln" | awk -v b="$b" -v a="$a" '
      BEGIN{s=-1;e=-1}
      { st=$1-b; if(st<1)st=1; en=$1+a;
        if(s<0){s=st;e=en} else if(st<=e+1){ if(en>e)e=en } else { print s" "e; s=st; e=en } }
      END{ if(s>=0) print s" "e }' |
    while read -r s e; do
      echo "--- $f : $s-$e" >> "$TMP"
      awk -v s="$s" -v e="$e" 'NR>=s && NR<=e{printf "%d: %s\n", NR, $0}' "$f" >> "$TMP"
    done
  done
  put "$lim"
}

# fnblock <fayl> <regex> <max setir>   (bitis: setir basinda "}")
fnblock() {
  [ -f "$1" ] || return
  grep -qE "$2" "$1" || { echo "[YOX] $1 : $2"; return; }
  echo "######## FUNKSIYA $1 ########" >> "$TMP"
  awk -v pat="$2" '!on && $0 ~ pat {on=1} on{printf "%d: %s\n", NR, $0} on && /^}/{on=0}' "$1" >> "$TMP"
  put "$3"
}

first() { echo "$FILES" | grep -i -m1 -E "$1"; }

GUI=$(first '/gui\.cpp$')
TAGS=$(first '/playertags\.cpp$')
TAGSH=$(first '/playertags\.h$')
VPOOL=$(first '/vehiclepool\.h$')
KB=$(echo "$FILES" | grep -i 'keyboard' | head -3)
WRAP=$(echo "$FILES" | grep -i 'imguiwrapper' | head -3)
CHAT=$(echo "$FILES" | grep -i -E '/chat[^/]*\.(cpp|h)$' | head -3)
echo "gui=$GUI | tags=$TAGS | vpool=$VPOOL"
echo "klaviatura: $(echo $KB)"; echo "wrapper: $(echo $WRAP)"; echo "chat: $(echo $CHAT)"
echo "-----------------------------------------"

# 0. kicik struktur
{ echo "######## STRUKTUR ########"
  echo "$FILES" | head -150
  find "$SRC" -type f \( -iname '*.ttf' -o -iname '*.otf' \) 2>/dev/null | head -20
  find "$SRC/java" -type f \( -name '*.java' -o -name '*.kt' \) 2>/dev/null | head -30
} >> "$OUT"; mark "struktur"

# 1. Sinxronizasiya (far/neon/rengler basqalarina gorunsun) - yalniz vehicle/net fayllari
SYNC='HeadLight|Headlight|bLights|LightsState|VehicleParams|ToggleVehicleLights|SendVehicleSync|SetVehicleColor|[Nn]eon'
snip "$SYNC" 2 8 160 $(echo "$FILES" | grep -i -E '/net/.*(vehicle|netgame|rpc)|/game/.*vehicle')
whole "$VPOOL" 90
mark "sync + vehiclepool"

# 2. gui.cpp: indeks + /mm menyu + speedometr + signal
{ echo "######## gui.cpp FUNKSIYA INDEKSI ########"
  grep -nE '^[A-Za-z_][^;=]*\)[[:space:]]*(const)?[[:space:]]*\{?[[:space:]]*$' "$GUI" | cut -c1-160 | head -250
} >> "$OUT"; mark "gui indeks"
fnblock "$GUI" '^void RenderHandlingDialog\(\)' 420
mark "RenderHandlingDialog"
snip 'Speedometer|speedometer|Speedo|Sport|Neon|Spidometr' 1 45 170 "$GUI"
mark "speedometr"
snip 'Sinyal|Signal' 2 18 50 "$GUI"
mark "sinyal"
snip 'namespace mm2|struct MMTouch|struct Rc|static void (Fill|Stroke|Txt|TxtC)|static float (S|TW)\(|inline float (S|TW)\(' 0 5 70 "$GUI"
mark "gui helperler"

# 3. Handling (native struktur + yukleme) - gui.cpp xaric, dar pattern
snip 'tHandlingData|cHandlingDataMgr|handling\.cfg|HandlingData' 3 14 170 $(echo "$FILES" | grep -v -E 'gui/gui\.cpp')
mark "handling"

# 4. Klaviatura / font / emoji / chat
for f in $KB; do whole "$f" 380; done; mark "klaviatura"
snip 'AddFontFromFileTTF|GlyphRanges|MergeMode|FontConfig' 3 12 90 $WRAP $(echo "$FILES" | grep -i -E 'font|imgui')
mark "font yukleme"
snip 'emoji|Emoji|SendChat|AddChatMessage|OnChatInput|DoChatInput' 2 10 90 $CHAT
mark "chat/emoji"
snip 'ShowKeyboard|HideKeyboard|CallVoidMethod|GetMethodID|ToUtf8|cp1251' 2 10 60 $(echo "$FILES" | grep -i -E 'jni|main|keyboard|util')
mark "JNI klaviatura"

# 5. Playertag
[ -n "$TAGS" ] && fnblock "$TAGS" '^void CPlayerTags::Draw\(' 160
[ -n "$TAGS" ] && snip 'DrawAzFlag|g_pNickFont' 1 6 40 "$TAGS"
[ -n "$TAGSH" ] && whole "$TAGSH" 50
mark "playertag"

# 6. Tuning (qaraj kamerasi, suspension) - dar pattern
snip 'AddVehicleUpgrade|RemoveVehicleUpgrade|SuspensionLines|CCamera|TheCamera|SetFOV' 2 8 130 $(echo "$FILES" | grep -E '/game/' | head -12)
mark "tuning/kamera"

# 7. Java: fayl secici
snip 'ACTION_OPEN_DOCUMENT|ACTION_GET_CONTENT|onActivityResult|registerForActivityResult|native ' 2 12 70 $(find "$SRC/java" -type f \( -name '*.java' -o -name '*.kt' \) 2>/dev/null)
mark "java"

rm -f "$TMP"
echo "-----------------------------------------"
echo "Hazirdir: $OUT   (umumi $(wc -c < "$OUT") bayt)"

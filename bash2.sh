#!/usr/bin/env bash
# collect_v2.sh -- layihenin kokunde:  bash collect_v2.sh   ->   txt/all2.txt  (yalniz bunu gonder)
# Evvelki skriptin xetalari: opus/ qovlugu daxil idi (nitro axtarisini zibilledi), gui.cpp-nin lazim hisseleri
# kesilirdi, ui_prims awk-da "\(" yuzunden bos cixirdi. Hamisi duzeldi.

ROOT="${ROOT:-$PWD}"; cd "$ROOT" || exit 1
SRC="${SRC:-app/src/main}"; OUT="txt"; ALL="$OUT/all2.txt"; mkdir -p "$OUT"
[ -d "$SRC" ] || { echo "XETA: $SRC tapilmadi"; exit 1; }

EXCL=( -not -path '*/opus/*' -not -path '*/imgui/*' -not -path '*/build/*' -not -path '*/.git/*' -not -path '*/.cxx/*' \
       -not -path '*/external/*' -not -path '*/third_party/*' -not -path '*/vendor/*' -not -path '*/stb/*' -not -path '*/rapidjson/*' )
mapfile -t SRCLIST < <(find "$SRC" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.java' \) "${EXCL[@]}" | sort)
F() { printf '%s\n' "${SRCLIST[@]}" | grep -E "/$1\$" | head -1; }

mask() { sed -E 's/((password|passwd|token|secret|api[_-]?key)[A-Za-z_]*[[:space:]]*[=:][[:space:]]*)["'\'']?[^"'\'' ;,]+/\1***/Ig'; }
hdr() { echo; echo "################ $1 ################"; }

# regex-in ilk/son tapildigi yerden n setir
region() { local f="$1" re="$2" n="$3" w="${4:-first}" ln
  [ -f "$f" ] || { echo "(fayl yoxdur: $f)"; return; }
  if [ "$w" = last ]; then ln=$(grep -nE "$re" "$f" | tail -1 | cut -d: -f1); else ln=$(grep -nE "$re" "$f" | head -1 | cut -d: -f1); fi
  [ -n "$ln" ] || { echo "(tapilmadi: $re  @ $f)"; return; }
  echo "=== $f [$ln-$((ln+n-1))] ==="
  awk -v a="$ln" -v b="$((ln+n-1))" 'NR>=a&&NR<=b{printf "%d: %s\n",NR,$0}' "$f"; }

# regex etrafinda ctx setir (butun fayllarda); fayl basina max cap setir
kw() { local pat="$1" c="${2:-8}" cap="${3:-120}" f
  for f in $(grep -lE "$pat" "${SRCLIST[@]}" 2>/dev/null); do
    grep -nE "$pat" "$f" | cut -d: -f1 | awk -v c="$c" '{lo=$1-c;if(lo<1)lo=1;hi=$1+c;if(n>0&&lo<=E[n]+1){if(hi>E[n])E[n]=hi}else{n++;S[n]=lo;E[n]=hi}}END{for(i=1;i<=n;i++)print S[i],E[i]}' |
    while read -r a b; do echo "=== $f [$a-$b] ==="; awk -v a="$a" -v b="$b" 'NR>=a&&NR<=b{printf "%d: %s\n",NR,$0}' "$f"; done | head -n "$cap"
  done; }

whole() { [ -f "$1" ] && { echo "=== TAM: $1 ==="; awk -v m="${2:-400}" 'NR<=m{printf "%d: %s\n",NR,$0}' "$1"; }; }

GUI="$(F gui.cpp)"; VEH="$(F vehicle.cpp)"
{
hdr "A. UI primitivleri (Fill/Txt/Tap/Prs/Col/S/Accent ...)"
kw_file() { local f="$1" pat="$2" c="$3"; grep -nE "$pat" "$f" | head -40 | cut -d: -f1 | awk -v c="$c" '{lo=$1-c;if(lo<1)lo=1;hi=$1+c;if(n>0&&lo<=E[n]+1){if(hi>E[n])E[n]=hi}else{n++;S[n]=lo;E[n]=hi}}END{for(i=1;i<=n;i++)print S[i],E[i]}' |
  while read -r a b; do echo "=== $f [$a-$b] ==="; awk -v a="$a" -v b="$b" 'NR>=a&&NR<=b{printf "%d: %s\n",NR,$0}' "$f"; done; }
kw_file "$GUI" '^(static |inline )?[A-Za-z_:<>&* ]+[ *&](Fill|Stroke|Txt|TxtB|TxtC|Tap|Prs|Dot|Accent|Smooth|EaseOutCubic|Col|S|In|Clamp|ToggleRow|HeaderBtn)[(]' 14 | head -c 30000

hdr "B. mm2 namespace (SmallBtn, SwitchRow, EmptyState, kHF, EditCaller, LocalVeh)"
region "$GUI" 'namespace mm2' 330 first | head -c 24000

hdr "C. Garage namespace (qaraj kamerasi + tuning + komponent/ikon)"
region "$GUI" 'GARAGE_V5' 480 first | head -c 30000

hdr "D. YENI /mm menyusu (tablar: Masin/Handling/Far/Tuning/Nomre/Spidometr)"
region "$GUI" '^void RenderHandlingDialog\(\)[^;]*$' 950 last | head -c 60000
} 2>/dev/null | mask > "$ALL"

{
hdr "E. NITRO / HIDRAVLIKA (opus-suz)"
kw 'AddComponent|RemoveComponent|add_car_component|remove_car_component|request_model|load_requested_models|RequestModel|LoadAllRequestedModels' 10 90 | head -c 22000
kw '[Nn]itro|[Hh]ydraul|\b1010\b|\b1087\b|\b1008\b|\b1009\b|m_nNitroBoosts' 6 60 | head -c 18000
region "$VEH" 'void CVehicle::AddComponent' 45 first
region "$(F CStreaming.h)" 'RequestModel|LoadAllRequestedModels|HasModelLoaded|IsModelLoaded' 25 first
} 2>/dev/null | mask >> "$ALL"

{
hdr "F. HANDLING arxa plan (HNDV2/HNDV3) + struktur"
region "$VEH" 'HNDV2' 280 first | head -c 16000
region "$VEH" 'HNDV3' 120 first | head -c 7000
whole "$(F tHandlingData.h)" 160
kw 'cTransmission|CalculateGearForSimpleCar|InitGearRatios' 12 80 | head -c 9000
kw 'm_fMass;|m_fTurnMass|m_fMassRecpr' 3 30 | head -c 4000
} 2>/dev/null | mask >> "$ALL"

{
hdr "G. DETALLARIN SOKULMESI ucun RW/model struktur"
kw 'struct RwObject|struct RwFrame|rwObjectSetFlags|rwObjectTestFlags|rpATOMICRENDER|RwFrameForAllChildren' 10 70 | head -c 12000
whole "$(F ClumpModelInfo.h)" 70
region "$(F CVehicleGTA.cpp)" 'SetupLightFlags_hook' 30 first
region "$(F CVehicleGTA.cpp)" 'InjectHooks' 14 last
} 2>/dev/null | mask >> "$ALL"

echo "Hazirdir: $ALL ($(wc -c < "$ALL") bayt). Yalniz bu faylı göndər."

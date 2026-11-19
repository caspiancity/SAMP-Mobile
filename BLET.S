#!/usr/bin/env bash
# collect_all.sh -- layihenin kokunde isle:   bash collect_all.sh        (hamisi)
#                                              bash collect_all.sh 1      (yalniz 1-ci merhele)
# Netice: TEK FAYL -> txt/all.txt   (hamisini ona yigir, yalniz onu gonder)
#   1 = Nitro / Hidravlika qosulmamasi (+ ikon)
#   2 = Handling (UI + duzgun isleme + reqemi menyuda deyismek)
#   3 = Tuning (butov masin, tekerler, nomre, qaraj)
#   4 = Detallarin sokulmesi (far, bufer, stop, qapi ...)
# Yalniz lazimli funksiyalar/setirler yigilir (tam fayl yox). Parol/token tipli setirler maskalanir.
# Olcunu deyismek:  MAX=100000 bash collect_all.sh   (merhele basina bayt, default 60000)

ROOT="${ROOT:-$PWD}"
cd "$ROOT" || exit 1
SRC="${SRC:-app/src/main}"
OUT="txt"
ALL="$OUT/all.txt"
MAX="${MAX:-60000}"
mkdir -p "$OUT"

if [ ! -d "$SRC" ]; then
  echo "XETA: $SRC tapilmadi. Layihenin kokunde ise sal (ROOT=... ile de verile biler)."
  exit 1
fi

# kenar kitabxanalar atilir (lazim olsa EXCL-i deyis)
EXCL=( -not -path '*/imgui/*' -not -path '*/build/*' -not -path '*/.git/*' -not -path '*/.cxx/*' \
       -not -path '*/external/*' -not -path '*/third_party/*' -not -path '*/thirdparty/*' \
       -not -path '*/vendor/*' -not -path '*/stb/*' -not -path '*/rapidjson/*' -not -path '*/miniaudio*' )

mapfile -t SRCLIST < <(find "$SRC" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.java' -o -name '*.kt' \) "${EXCL[@]}" 2>/dev/null | sort)
GUI="$(printf '%s\n' "${SRCLIST[@]}" | grep -E '/gui\.cpp$' | head -1)"
VEHCPP="$(printf '%s\n' "${SRCLIST[@]}" | grep -E '/CVehicleGTA\.cpp$' | head -1)"
VEHH="$(printf '%s\n' "${SRCLIST[@]}" | grep -E '/CVehicleGTA\.h$' | head -1)"

mask() { sed -E 's/((password|passwd|token|secret|api[_-]?key)[A-Za-z_]*[[:space:]]*[=:][[:space:]]*)["'\'']?[^"'\'' ;,]+/\1***/Ig'; }

# ---------- yardimci: setir araligi (match +- ctx), setir nomreleri ile
extract() {   # fayl regex ctx
  local f="$1" pat="$2" c="$3"
  grep -nE "$pat" "$f" 2>/dev/null | cut -d: -f1 | awk -v c="$c" '
    { lo=$1-c; if (lo<1) lo=1; hi=$1+c
      if (n>0 && lo<=E[n]+1) { if (hi>E[n]) E[n]=hi } else { n++; S[n]=lo; E[n]=hi } }
    END { for (i=1;i<=n;i++) print S[i], E[i] }' |
  while read -r a b; do
    echo "=== $f  [$a-$b] ==="
    awk -v a="$a" -v b="$b" 'NR>=a && NR<=b { printf "%d: %s\n", NR, $0 }' "$f"
  done
}

# ---------- yardimci: regex-e uygun butun funksiya gövdeleri (brace sayma)
funcs() {     # fayl regex [cap]
  local f="$1" re="$2" cap="${3:-400}"
  [ -f "$f" ] || return
  awk -v re="$re" -v cap="$cap" -v f="$f" '
    !on && $0 ~ re && $0 !~ /;[ \t]*$/ && $0 ~ /\(/ && $0 !~ /^[ \t]*(if|for|while|switch|return|else|\/\/)/ {
      on=1; depth=0; opened=0; cnt=0; print "=== " f " [func @" NR "] ===" }
    on {
      cnt++; if (cnt<=cap) printf "%d: %s\n", NR, $0
      t=$0; o=gsub(/\{/,"{",t); c=gsub(/\}/,"}",t)
      if (o>0) opened=1
      depth+=o-c
      if (opened && depth<=0) on=0
    }' "$f"
}

# regex-e uygun funksiyalar butun mənbə fayllarinda
funcs_all() { # regex [cap]
  local re="$1" cap="${2:-400}" f
  for f in $(grep -lE "$re" "${SRCLIST[@]}" 2>/dev/null); do funcs "$f" "$re" "$cap"; done
}

# acar sozler etrafinda parcalar butun fayllarda
kw() {        # regex ctx [faylbasinaMaxSetir]
  local pat="$1" c="${2:-15}" cap="${3:-260}" f
  for f in $(grep -lE "$pat" "${SRCLIST[@]}" 2>/dev/null); do extract "$f" "$pat" "$c" | head -n "$cap"; done
}

whole() {     # fayl [maxSetir]
  local f="$1" cap="${2:-400}"
  [ -f "$f" ] || return
  echo "=== TAM FAYL: $f ($(wc -l <"$f") setir, ilk $cap) ==="
  awk -v cap="$cap" 'NR<=cap { printf "%d: %s\n", NR, $0 }' "$f"
}

hdr() { echo; echo "################ $1 ################"; }

# her bolme bayt limiti ile (cox boyuk olmasin)
SECMAX="${SECMAX:-14000}"
sec() { local t="$1"; shift; hdr "$t"; "$@" 2>/dev/null | head -c "$SECMAX"; echo; }

common() {
  hdr "AGAC (yalniz adlar)"
  printf '%s\n' "${SRCLIST[@]}" | head -n 400
  hdr "GIT"; git log --oneline -n 5 2>/dev/null
  [ -f app/src/main/cpp/CMakeLists.txt ] && whole app/src/main/cpp/CMakeLists.txt 60
}

# 2 ve 3-cu merhele ucun ortaq, bir defe yigilir
ui_prims() {
  [ -n "$GUI" ] && sec "UI primitivleri (gui.cpp)" funcs "$GUI" 'SmallBtn|EmptyState|(^|[ :*&])(Tap|Prs|Fill|Stroke|Txt|TxtB|TxtC)[ ]*\(' 25
}

vehicle_bits() {  # yalniz masin sinfinin lazim hisseleri
  for f in "$VEHH" "$VEHCPP"; do [ -n "$f" ] && extract "$f" "$1" 6 | head -n 120; done
}

stage1() {
  hdr "MERHELE 1: NITRO / HIDRAVLIKA"
  sec "1a. komponent qosan funksiyalar" funcs_all 'AddVehicleUpgrade|AddVehicleComponent|AddVehicleMod|RemoveVehicleUpgrade|ScrAddVehicleComponent|InstallComponent' 120
  sec "1b. nitro/hidravlika" kw 'Nitro|nitro|Hydraul|hydraul|1008|1009|1010|1087' 8 100
  sec "1c. model yukleme (yalniz yuxaridaki fayllarda)" bash -c "$(declare -f extract); for f in \$(grep -lE 'Nitro|nitro|Hydraul|hydraul|AddVehicleUpgrade' ${SRCLIST[*]} 2>/dev/null); do extract \"\$f\" 'RequestModel|LoadAllRequestedModels|IsModelLoaded' 5 | head -n 40; done"
  sec "1d. ikon (yalniz nitro/hidravlika fayllarinda)" bash -c "$(declare -f extract); for f in \$(grep -lE 'Nitro|nitro|Hydraul|hydraul' ${SRCLIST[*]} 2>/dev/null); do extract \"\$f\" '[Ii]con|AddImage' 5 | head -n 50; done"
  sec "1e. masin sinfi (upgrade sahələri)" vehicle_bits 'AddVehicleUpgrade|m_nUpgrade|Upgrade|Nitro|class CVehicleGTA'
}

stage2() {
  hdr "MERHELE 2: HANDLING"
  [ -n "$GUI" ] && sec "2a. handling dialoqu" funcs "$GUI" 'RenderHandlingDialog|DrawHandling|HandlingPage' 350
  sec "2b. handling istinadlari (diger fayllar)" kw 'tHandlingData|CHandling|SetHandling|handling\.cfg|HandlingCfg' 6 80
  [ -n "$GUI" ] && sec "2c. reqem daxil etme" extract "$GUI" 'keyboardEvent|setInputString|keyboard\(\)->show' 6
}

stage3() {
  hdr "MERHELE 3: TUNING / QARAJ / NOMRE"
  [ -n "$GUI" ] && sec "3a. /mm tablari" extract "$GUI" 'kTabs|s_tab ==|noVeh|inputPage' 10
  sec "3b. tuning/reng/suspenziya" kw 'Suspension|SetColor|m_nPrimaryColor|Garage::|namespace Garage' 6 80
  sec "3c. nomre API" bash -c "f=\$(printf '%s\n' ${SRCLIST[*]} | grep 'AzPlate.h' | head -1); [ -n \"\$f\" ] && awk 'NR<=70{printf \"%d: %s\n\",NR,\$0}' \"\$f\""
}

stage4() {
  hdr "MERHELE 4: DETALLARIN SOKULMESI"
  sec "4a. zerer/panel/qapi/far" kw 'SetPanelStatus|SetDoorStatus|SetLightStatus|SetBumperStatus|m_damageManager' 6 70
  sec "4b. frame gorunurluyu" kw 'SetFrameVisibility|GetFrameFromId|CVisibilityPlugins|RwFrameForAllObjects' 6 70
  sec "4c. model info frame adlari" kw 'm_avDummyPos|m_apExtras|CVehicleModelInfo' 5 60
  sec "4d. masin sinfi" vehicle_bits 'class CVehicleGTA|m_pRwClump|m_nModelIndex'
}

# merhele cixisini maskalayir, MAX-e kesir ve tek fayla (all.txt) elave edir
run() {
  local n="$1" tmp="$OUT/.stage$1.tmp"
  "stage$n" 2>/dev/null | mask > "$tmp"
  local sz; sz=$(wc -c < "$tmp")
  if [ "$sz" -gt "$MAX" ]; then
    head -c "$MAX" "$tmp" >> "$ALL"
    echo >> "$ALL"; echo "[QISALDILDI: stage$n $sz bayt idi, $MAX-e kesildi]" >> "$ALL"
    echo "!! stage$n: $sz bayt -> $MAX-e kesildi (MAX=... ile artir ve ya EXCL-i daralt)"
  else
    cat "$tmp" >> "$ALL"
    echo "stage$n: $sz bayt"
  fi
  rm -f "$tmp"
}

: > "$ALL"
{ common; } 2>/dev/null | mask >> "$ALL"

if [ -n "$1" ] && [ "$1" != "all" ]; then
  STAGES=("$1")
else
  STAGES=(1 2 3 4)
fi

for n in "${STAGES[@]}"; do run "$n"; done

# UI primitivleri yalniz bir defe (2 ve 3-cu merhele ucun ortaq)
case " ${STAGES[*]} " in
  *" 2 "*|*" 3 "*) ui_prims 2>/dev/null | mask >> "$ALL" ;;
esac

echo
echo "Hazirdir: $ALL ($(wc -c < "$ALL") bayt). Yalniz bu tek fayli gonder."

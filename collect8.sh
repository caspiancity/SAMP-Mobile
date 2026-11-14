#!/data/data/com.termux/files/usr/bin/bash
# collect.sh - istifadə: bash collect.sh 1   ->  txt/stage1.txt
STAGE="${1:-1}"
OUT="txt/stage${STAGE}.txt"
BIG=40000        # bu ölçüdən böyük fayldan yalnız açar sözlərin ətrafı götürülür
CTX=25           # açar sözdən əvvəl/sonra neçə sətir

mkdir -p txt; : > "$OUT"

# Tam götürülən fayllar (yolun sonu)
case "$STAGE" in
1)
  FULL=(
    "cpp/samp/CFPSFix.cpp" "cpp/samp/CFPSFix.h"
    "cpp/samp/game/vehicle.cpp"
    "cpp/samp/game/Entity/CVehicleGTA.h"
    "assets/xml/part_data.xml"
  )
  # Böyük ola biləcək fayllar: yalnız açar sözlər
  PART=(
    "cpp/samp/net/scriptrpc.cpp"
    "cpp/samp/game/game.cpp"
    "cpp/samp/game/hooks.cpp"
    "cpp/samp/game/patches.cpp"
  )
  KEYS='nitro|Nitro|hydraul|Hydraul|VehicleComponent|AddComponent|RemoveComponent|ModShop|FPS|fps|FrameLimit|SwapInterval|vsync|Timer|SetTargetFPS'
  ;;
*) echo "Mərhələ $STAGE hələ təyin olunmayıb"; exit 1 ;;
esac

add_header() { printf '\n\n===== FILE: %s =====\n' "$1" >> "$OUT"; }

for p in "${FULL[@]}"; do
  f=$(find app -type f -path "*${p}" 2>/dev/null | head -1)
  [ -z "$f" ] && { echo "TAPILMADI: $p"; continue; }
  add_header "$f"; cat "$f" >> "$OUT"
done

for p in "${PART[@]}"; do
  f=$(find app -type f -path "*${p}" 2>/dev/null | head -1)
  [ -z "$f" ] && { echo "TAPILMADI: $p"; continue; }
  add_header "$f"
  if [ "$(wc -c < "$f")" -le "$BIG" ]; then
    cat "$f" >> "$OUT"
  else
    echo "[faylın yalnız lazımi hissələri, sətir nömrələri ilə]" >> "$OUT"
    grep -nE -C"$CTX" "$KEYS" "$f" >> "$OUT"
  fi
done

echo "Hazırdır: $OUT ($(wc -c < "$OUT") bayt)"

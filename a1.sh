#!/data/data/com.termux/files/usr/bin/bash
# Plaka sistemi üçün lazım olan hissələri txt/plate.txt-də toplayır
ROOT="${1:-.}"
OUT="$ROOT/txt/plate.txt"
CTX=30        # tapılan sətrin ətrafında neçə sətir götürülsün
MAXFILES=25   # ən çox neçə fayl
mkdir -p "$ROOT/txt"
: > "$OUT"

PAT='plate|carplate|CCustomCarPlate|plaka|nomre|nömrə|RpMaterial|RwTexture|RpAtomic|SetupClump|GetModelInfo|VehicleModel|handlingMenu|"/mm"|/mm|mmMenu|MainMenu|ImGui::Begin|CameraSet|SetCamera|CamTarget|FreeCam|/cam|TouchEvent|OnTouch|ImGui_ImplAndroid|json|SaveConfig|LoadConfig|GetBoundingBox|GetColModel|CVehicle|vehicle->'

echo "=== FAYL AĞACI (src/res) ===" >> "$OUT"
find "$ROOT" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.c' -o -name '*.java' -o -name '*.mk' -o -name 'CMakeLists.txt' \) \
  -not -path '*/.git/*' -not -path '*/build/*' -not -path '*/txt/*' | sort >> "$OUT"
echo "--- res ---" >> "$OUT"
find "$ROOT" -type d -name res -not -path '*/.git/*' | head -3 | while read d; do ls -R "$d" | head -60 >> "$OUT"; done

echo "" >> "$OUT"
echo "=== AÇAR SÖZLƏRƏ GÖRƏ HİSSƏLƏR ===" >> "$OUT"

# ən çox uyğunluq olan faylları seç
grep -rIEic "$PAT" "$ROOT" --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.c' --include='*.java' \
  --exclude-dir=.git --exclude-dir=build --exclude-dir=txt --exclude-dir=libs 2>/dev/null \
  | awk -F: '$2>0' | sort -t: -k2 -nr | head -$MAXFILES | cut -d: -f1 | while read f; do
  echo "" >> "$OUT"
  echo "##### $f" >> "$OUT"
  grep -nIEi "$PAT" "$f" | cut -d: -f1 | awk -v c=$CTX '
    { s=$1-c; if (s<1) s=1; e=$1+c
      if (n>0 && s<=pe+1) { pe=e } else { if (n>0) print ps" "pe; ps=s; pe=e; n++ } }
    END { if (n>0) print ps" "pe }' | while read a b; do
      echo "--- sətir $a-$b ---" >> "$OUT"
      sed -n "${a},${b}p" "$f" >> "$OUT"
  done
done

SIZE=$(wc -c < "$OUT")
echo "Hazırdır: $OUT ($SIZE bayt)"
[ "$SIZE" -gt 400000 ] && echo "Böyükdür: CTX və ya MAXFILES azalt (məs: CTX=15 MAXFILES=12)"

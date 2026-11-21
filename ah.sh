#!/bin/bash
# İşlət: layihənin kökündən  ->  bash collect5.sh
ROOT="app/src/main/cpp"
OUT="txt/camera_all.txt"
mkdir -p txt
: > "$OUT"

# Açar sözlər (funksiyanın içində bunlardan biri varsa, funksiya tam çıxarılır)
KW_CAM='camera|CCam|TheCamera|CamMode|cam_mode|FirstPerson|FOV|fov|SetFOV|ChangeCamera|SwitchCamera|CameraMode|Cam_|CCamera|TakeControl|RestoreCamera|LookAt|SetCamPos|CamTarget|ZoomLevel|zoom'
KW_TOUCH='Touch|touch|pinch|Pinch|multitouch|MultiTouch|TouchEvent|OnTouch|CTouchInterface|NvEvent|NVEvent|joystick|Joystick|pointer|Pointer|swipe|drag|OnMove|OnDown|OnUp'
KW_MENU='/mm|\"mm\"|MainMenu|DrawMenu|RenderMenu|ImGui::Begin|ImGui::SliderFloat|ImGui::SliderInt|Slider|OpenMenu|ToggleMenu|menu_open|bMenu|CGUI|CHUD|HudButton|hud_button|button'
KW_HOOK='HOOK|Hook|CHook|InstallHook|SetUpHook|PatchMemory|WriteMemory|NOP|0x[0-9A-Fa-f]{5,}|dlsym|libGTASA'

ALL="($KW_CAM|$KW_TOUCH)"

extract() {
  # $1 = fayl, $2 = regex
  awk -v re="$2" -v file="$1" '
  function flush(   i) {
    if (buf_n == 0) return
    if (buf ~ re) {
      print "\n=================================================="
      print "FILE: " file "   (sətir " start_line "-" NR ")"
      print "=================================================="
      if (buf_n > 250) {
        # böyük blok (class/namespace) - yalnız uyğun sətirlər + kontekst
        for (i = 1; i <= buf_n; i++) {
          if (lines[i] ~ re) {
            for (j = i-12; j <= i+25; j++) if (j >= 1 && j <= buf_n && !shown[j]) { print lines[j]; shown[j]=1 }
            print "   ...."
          }
        }
        delete shown
      } else {
        print buf
      }
    }
    buf = ""; buf_n = 0; delete lines
  }
  {
    prev2 = prev1; prev1 = cur; cur = $0
    line = $0
    opens = gsub(/\{/, "{", line); closes = gsub(/\}/, "}", line)
    if (depth == 0 && opens > 0) {
      buf = prev2 "\n" prev1 "\n" $0; buf_n = 3
      lines[1] = prev2; lines[2] = prev1; lines[3] = $0
      start_line = NR - 2
    } else if (depth > 0) {
      buf = buf "\n" $0; buf_n++; lines[buf_n] = $0
    }
    depth += opens - closes
    if (depth < 0) depth = 0
    if (depth == 0 && buf_n > 0) flush()
  }
  ' "$1"
}

{
echo "######## 1) FAYL AĞACI ########"
find "$ROOT" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" \) \
  -not -path "*/imgui/*" -not -path "*/rapidjson/*" -not -path "*/vendor/*" | sort
echo
echo "######## 2) KAMERA + TOUCH BAĞLI FUNKSİYALAR (tam) ########"
} >> "$OUT"

FILES=$(grep -rlE "$ALL" "$ROOT" --include=*.cpp --include=*.h --include=*.hpp --include=*.c \
        --exclude-dir=imgui --exclude-dir=rapidjson --exclude-dir=vendor 2>/dev/null)

for f in $FILES; do
  extract "$f" "$ALL" >> "$OUT"
done

{
echo
echo "######## 3) /mm MENYUSU (gui.cpp-də və ətrafında) ########"
} >> "$OUT"
for f in $(grep -rlE "$KW_MENU" "$ROOT/samp" --include=gui.cpp --include=gui.h --include=*menu*.cpp --include=*menu*.h 2>/dev/null); do
  extract "$f" "($KW_MENU)" >> "$OUT"
done

{
echo
echo "######## 4) HOOK SİSTEMİ (necə hook qoyulur, nümunə) ########"
} >> "$OUT"
for f in $(grep -rlE "$KW_HOOK" "$ROOT" --include=*.h --include=*.hpp 2>/dev/null | head -5); do
  echo -e "\n=== $f (ilk 120 sətir) ===" >> "$OUT"
  head -120 "$f" >> "$OUT"
done

{
echo
echo "######## 5) KAMERA ADDRESS/OFFSET GEÇƏN SƏTİRLƏR ########"
grep -rnE "(Camera|CCam|TheCamera|FOV|fov).*(0x[0-9A-Fa-f]{4,}|\+ *0x)" "$ROOT" \
  --include=*.cpp --include=*.h --include=*.hpp 2>/dev/null | head -80
echo
echo "######## 6) JNI / Java tərəfində kamera və toxunuş ########"
grep -rnE "camera|Camera|onTouch|MotionEvent|ScaleGesture|pinch" app/src/main/java 2>/dev/null | head -80
} >> "$OUT"

echo "Hazırdır: $OUT  ($(wc -l < "$OUT") sətir, $(du -h "$OUT" | cut -f1))"

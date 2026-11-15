#!/data/data/com.termux/files/usr/bin/bash
# bash collect_v2.sh  ->  txt/v2.txt   (yalniz bu faylı at, bir daha heç nə istəməyəcəm)
# Tam yenidən yazılış üçün: crash/hidravlik, FPS, handling.cfg, kamera, UI(blur), emoji/stiker
OUT=txt/v2.txt
SRC=app/src/main/cpp/samp
JAVA=app/src/main/java
EXC='(/vendor/|/opus/|\.bak)'
mkdir -p txt; : > "$OUT"
SEEN=$(mktemp)

section() { printf '\n\n########## %s ##########\n' "$1" >> "$OUT"; }
dump() {   # dump <yol> [maks bayt]
  [ -f "$1" ] || return
  grep -qxF "$1" "$SEEN" && return
  echo "$1" >> "$SEEN"
  printf '\n\n===== FILE: %s =====\n' "$1" >> "$OUT"
  head -c "${2:-30000}" "$1" >> "$OUT"
}
files_with() {   # files_with <regex> -> fayllar (cpp/h/java)
  grep -rlE "$1" app/src/main --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.java' --include='*.xml' 2>/dev/null | grep -vE "$EXC"
}
ctx() {   # ctx <regex> <setir> : uyğun yerlər konteksti ilə
  grep -rnE -B4 -A"${2:-14}" "$1" app/src/main --include='*.cpp' --include='*.h' --include='*.java' 2>/dev/null \
    | grep -vE "$EXC" | head -c 14000 >> "$OUT"
}

section "1. CRASH / HIDRAVLIK-NITRO / OYUN THREAD-I"
dump "$SRC/game/events.cpp" 20000
dump "$SRC/net/vehiclepool.cpp" 26000
dump "$SRC/net/vehiclepool.h" 8000
ctx 'AddComponent|RemoveComponent|ScrAddComponent|ScrComponent|EVENT_TYPE_CARCOMPONENT' 10
ctx 'void CNetGame::Process|void CGame::Process|void CVehiclePool::Process' 25
dump "$SRC/log.cpp" 9000
dump "$SRC/log.h" 4000
for f in $(files_with 'class[[:space:]]+Log\b|namespace[[:space:]]+Log\b'); do dump "$f" 9000; done

section "2. FPS"
dump "$SRC/CFPSFix.cpp" 6000
ctx 'ApplyFPSLimit|ApplyFPSPatch|iFPSCount|SetTargetFPS' 14

section "3. KAMERA (/cam fps, /cam pc, titreme)"
ctx '"/cam"|/cam[[:space:]]+(fps|pc)|cam fps|cam pc|CamMode|g_camMode|FPSCamera|ThirdPersonCam' 40
for f in $(files_with 'class[[:space:]]+(CCamera|CameraManager|CCam)\b|FPS_CAMERA|FirstPerson'); do dump "$f" 16000; done
ctx 'SetCameraBehind|CCamera::Process|0x[0-9A-Fa-f]+.*CCam|CCam::Process_' 20

section "4. HANDLING.CFG / AYARLAR EKRANI"
ctx 'handling\.cfg' 20
for f in $(files_with 'class[[:space:]]+SettingsFragment'); do dump "$f" 24000; done
dump app/src/main/res/layout/fragment_settings.xml 20000
dump "$SRC/settings.h" 6000
dump "$SRC/settings.cpp" 12000
ls -la app/src/main/assets app/src/main/assets/data 2>/dev/null >> "$OUT"
ctx 'g_pszStorage[[:space:]]*=' 6

section "5. RENDER / BLUR / TEXTURE (iOS stili ucun)"
ctx 'eglSwapBuffers|glGetString|GL_RENDERER|GLES|ImGui_ImplOpenGL3|ImGui_Impl|glBindFramebuffer|glCopyTexSubImage2D' 6
for f in $(files_with 'class[[:space:]]+(ImGuiRenderer|ImGuiWrapper)\b'); do dump "$f" 22000; done
ctx 'stbi_load|LoadTextureFromDB|LoadTextureFromFile|CreateTexture|RwTexture.*Read' 8
ls -la app/src/main/assets/Fonts app/src/main/assets/fonts 2>/dev/null >> "$OUT"

section "6. CHAT / MESAJ / EMOJI-STIKER"
for f in $(files_with 'class[[:space:]]+(Label|MessageItem|ChatItem|Image|ImageView)\b'); do dump "$f" 16000; done
dump "$SRC/gui/samp_widgets/chat.cpp" 40000
dump "$SRC/gui/samp_widgets/chat.h" 9000
dump "$SRC/gui/samp_widgets/keyboard.cpp" 60000
dump "$SRC/gui/samp_widgets/keyboard.h" 10000

section "7. /mm MENYUSU (silinecek sehifeler + yeni dizayn)"
dump "$SRC/gui/gui.cpp" 70000
dump "$SRC/gui/gui.h" 12000

rm -f "$SEEN"
SZ=$(wc -c < "$OUT")
echo "Hazirdir: $OUT ($SZ bayt, $(grep -c '^===== FILE' "$OUT") fayl)"
[ "$SZ" -gt 700000 ] && echo "Cox boyukdur, amma yenə də at (men bolem)."
exit 0

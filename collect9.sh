#!/data/data/com.termux/files/usr/bin/bash
# bash collect.sh  ->  txt/stage257.txt  (yalnız bunu at)
OUT=txt/stage257.txt; CAP=60000
mkdir -p txt; : > "$OUT"

add() {
  find app/src/main \( -path '*/vendor/*' -o -path '*/opus/*' -o -path '*/build/*' \) -prune -o \
       -type f -iname "$1" -print 2>/dev/null | while read -r f; do
    printf '\n\n===== FILE: %s =====\n' "$f" >> "$OUT"
    head -c "$CAP" "$f" >> "$OUT"
  done
}

# 2) handling.cfg
for p in '*handling*.cfg' 'tHandlingData.h' 'settings.cpp' 'settings.h' 'SettingsFragment.java' 'fragment_settings.xml'; do add "$p"; done
# 5) chat + klaviatura + emoji
for p in '*chat*.cpp' '*chat*.h' '*keyboard*.cpp' '*keyboard*.h' '*emoji*'; do add "$p"; done
# 7) nametag, loading, launcher
for p in '*nametag*.cpp' '*nametag*.h' '*playertag*.cpp' '*playertag*.h' 'SplashActivity.java' 'LobbyActivity.java' 'activity_splash*.xml' 'activity_lobby*.xml'; do add "$p"; done

echo "Hazırdır: $OUT ($(wc -c < "$OUT") bayt)"
grep -c '^===== FILE' "$OUT"

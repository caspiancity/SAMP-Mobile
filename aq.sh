#!/usr/bin/env python3
# İşlət (layihə kökündən): python3 collect5.py
import os, re

ROOT = "app/src/main/cpp"
SKIP = ("imgui", "rapidjson", "vendor", "stb_", "/json/", "/lib/")
EXT = (".cpp", ".h", ".hpp", ".c")
LIMIT_FUNC = 100                                   # bir funksiyadan max sətir
CAP = {"cam": 80000, "touch": 40000, "menu": 30000}  # bayt limitləri

CAM_HEAD = re.compile(r"camera|cammode|cam_mode|fov|firstperson|first_person|zoom", re.I)
CAM_BODY = re.compile(r"TheCamera|CCamera|CCam\b|CCam::|SetFOV|CamMode|FirstPerson|TakeControl|SetCamPos|SetCamera")
TOUCH_HEAD = re.compile(r"touch|pinch|multitouch|gesture", re.I)
MENU_HEAD = re.compile(r"menu|mmwindow|drawmm", re.I)
MENU_BODY = re.compile(r'"/mm"|\bmm_\w+|MainMenu')

def blocks(L, lo, hi):
    res, depth, st = [], 0, None
    for i in range(lo, hi):
        s = re.sub(r"//.*", "", L[i])
        o, c = s.count("{"), s.count("}")
        if depth == 0 and o > 0 and st is None:
            st = i
        depth += o - c
        if depth <= 0 and st is not None:
            depth = 0
            if i - st > 150:
                res += blocks(L, st + 1, i)      # böyük class/namespace-in içinə bax
            else:
                res.append((st, i))
            st = None
    return res

files = []
for d, _, fs in os.walk(ROOT):
    if any(s in d + "/" for s in SKIP):
        continue
    for f in fs:
        if f.endswith(EXT):
            files.append(os.path.join(d, f))
files.sort()

found = {"cam": [], "touch": [], "menu": []}
for path in files:
    try:
        L = open(path, encoding="utf-8", errors="ignore").read().split("\n")
    except Exception:
        continue
    base = os.path.basename(path).lower()
    for st, en in blocks(L, 0, len(L)):
        head = " ".join(x.strip() for x in L[max(0, st - 2):st + 1])
        body = "\n".join(L[st:en + 1])
        cat, score = None, 0
        if CAM_HEAD.search(head):
            cat, score = "cam", 2
        elif CAM_BODY.search(body):
            cat, score = "cam", 1
        elif TOUCH_HEAD.search(head):
            cat, score = "touch", 2
        elif base.startswith("gui") and (MENU_HEAD.search(head) or MENU_BODY.search(body)):
            cat, score = "menu", 2
        if cat:
            code = L[max(0, st - 2):min(en + 1, st - 2 + LIMIT_FUNC)]
            found[cat].append((score, path, st + 1, en + 1, head[:160], "\n".join(code)))

os.makedirs("txt", exist_ok=True)
idx = open("txt/camera_index.txt", "w", encoding="utf-8")
out = open("txt/camera_all.txt", "w", encoding="utf-8")

titles = {"cam": "KAMERA / FOV / FIRST PERSON", "touch": "TOUCH / PINCH", "menu": "/mm MENYUSU (gui)"}
for cat in ("cam", "touch", "menu"):
    items = sorted(found[cat], key=lambda x: -x[0])
    idx.write(f"\n##### {titles[cat]}  ({len(items)} funksiya)\n")
    out.write(f"\n\n######## {titles[cat]} ########\n")
    used = 0
    for score, path, a, b, head, code in items:
        idx.write(f"{path}:{a}-{b}  |  {head}\n")
        if used + len(code) > CAP[cat]:
            continue
        used += len(code)
        out.write(f"\n// ===== {path}  [{a}-{b}] =====\n{code}\n")

# Java tərəfi (qısa)
out.write("\n\n######## JAVA ########\n")
n = 0
for d, _, fs in os.walk("app/src/main/java"):
    for f in fs:
        if f.endswith(".java") or f.endswith(".kt"):
            p = os.path.join(d, f)
            for i, l in enumerate(open(p, encoding="utf-8", errors="ignore")):
                if re.search(r"ScaleGesture|onTouchEvent|MotionEvent|pinch", l) and n < 40:
                    out.write(f"{p}:{i+1}: {l.strip()}\n"); n += 1
idx.close(); out.close()

for fn in ("txt/camera_index.txt", "txt/camera_all.txt"):
    print(fn, os.path.getsize(fn) // 1024, "KB")

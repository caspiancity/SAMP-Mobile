#!/usr/bin/env python3
import os, re, struct, sys
ROOT = "app/src/main/cpp/samp"
OUT = []
def rd(p): return open(p, encoding="utf-8", errors="replace").read().replace("\r", "")
def sec(t): OUT.append("\n######## %s ########\n" % t)
def body(lines, i, cap=400):
    depth, started, out = 0, False, []
    for j in range(i, min(len(lines), i + cap)):
        out.append(lines[j]); s = re.sub(r"//.*", "", lines[j])
        depth += s.count("{") - s.count("}")
        if "{" in s: started = True
        if started and depth <= 0: break
    return "\n".join(out)
def ctx(path, pat, before=2, after=6, maxn=12):
    if not os.path.exists(path): return
    L = rd(path).split("\n"); n = 0
    for i, l in enumerate(L):
        if re.search(pat, l):
            OUT.append("// %s:%d\n%s\n" % (path, i + 1, "\n".join(L[max(0, i - before):i + after + 1])))
            n += 1
            if n >= maxn: break
def allsrc():
    for d, _, fs in os.walk(ROOT):
        if any(x in d for x in ("imgui", "rapidjson", "vendor")): continue
        for f in fs:
            if f.endswith((".cpp", ".h", ".hpp")): yield os.path.join(d, f)

gui = ROOT + "/gui/gui.cpp"; G = rd(gui).split("\n")

sec("1) Cam.h (tam)");    OUT.append(rd(ROOT + "/game/Cam.h"))
sec("2) Camera.h (tam)"); OUT.append(rd(ROOT + "/game/Camera.h"))

sec("3) KÖHNƏ CamCtl (gui.cpp) - süni rejimi yaradan kod")
for i, l in enumerate(G):
    if re.match(r"^namespace CamCtl", l) or re.match(r"^void CamCtl_(OnTouch|Update|Command)\(", l):
        OUT.append("// gui.cpp:%d\n%s\n" % (i + 1, body(G, i)))
ctx(gui, r"CamCtl_Update\(\);", 3, 3)
for p in allsrc():
    if p != gui: ctx(p, r"CamCtl", 3, 8, 6)

sec("4) Kameranı script ilə məcburən yerləşdirən BÜTÜN yerlər")
skip = ("Camera.cpp", "Camera.h", "scripting.h", "scriptrpc.cpp")
for p in allsrc():
    if p.endswith(skip): continue
    ctx(p, r"CCamera::(SetPosition|LookAtPoint|SetBehindPlayer|Interpolate)|camera_on_actor|camera_on_vehicle|lock_camera|restore_camera|set_camera_position|point_camera", 2, 3, 6)

sec("5) hooks.cpp: CCamera::Process, SetFOV, hook qurulumu")
H = rd(ROOT + "/game/hooks.cpp").split("\n")
for i, l in enumerate(H):
    if re.match(r"^void CCamera__Process_hook\(", l) or re.match(r"^float CDraw__SetFOV_hook\(", l):
        OUT.append("// hooks.cpp:%d\n%s\n" % (i + 1, body(H, i, 120)))
for i, l in enumerate(H):
    if re.search(r"CCamera__Process\b|CDraw__SetFOV\b|CCamera_", l) and "InstallPLT" in l or re.search(r"InlineHook\(.*(CCam|CDraw|Camera)", l):
        OUT.append("// hooks.cpp:%d  %s" % (i + 1, l.strip()))

sec("6) pad.cpp: HookCPad + kamera ile bagli")
P = rd(ROOT + "/game/pad.cpp").split("\n")
for i, l in enumerate(P):
    if re.match(r"^void HookCPad\(", l): OUT.append("// pad.cpp:%d\n%s\n" % (i + 1, body(P, i, 120)))

sec("7) Kamera ikonu / widget")
for p in allsrc():
    ctx(p, r"CAM_TOGGLE|CamToggle|WIDGET_POSITION_CAM", 1, 4, 5)

sec("8) CHook API")
for p in allsrc():
    if os.path.basename(p) in ("CHook.h", "chook.h", "Hook.h"):
        for l in rd(p).split("\n"):
            if re.search(r"InlineHook|Redirect|InstallPLT|CallFunction|WriteMemory|\bNOP\b|\bRET\b|UnFuck", l): OUT.append("// %s: %s" % (p, l.strip()))

sec("9) /mm menyusu: sehifeler")
ctx(gui, r"KOMANDALAR", 4, 4, 4)
ctx(gui, r"ID_NAV0", 3, 5, 6)
for i, l in enumerate(G):
    if re.search(r"s_page\s*(=|==)", l) and len(OUT) < 4000: OUT.append("gui.cpp:%d: %s" % (i + 1, l.strip()))

sec("10) libGTASA.so simvollari")
def elf_syms(path):
    d = open(path, "rb").read()
    if d[:4] != b"\x7fELF": return []
    is64 = d[4] == 2
    if is64:
        shoff = struct.unpack_from("<Q", d, 0x28)[0]; shentsize, shnum, _ = struct.unpack_from("<HHH", d, 0x3A)
    else:
        shoff = struct.unpack_from("<I", d, 0x20)[0]; shentsize, shnum, _ = struct.unpack_from("<HHH", d, 0x2E)
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64: nm, ty, fl, ad, off, sz, lk, inf, al, es = struct.unpack_from("<IIQQQQIIQQ", d, o)
        else:    nm, ty, fl, ad, off, sz, lk, inf, al, es = struct.unpack_from("<IIIIIIIIII", d, o)
        secs.append((ty, off, sz, lk, es))
    res = []
    for ty, off, sz, lk, es in secs:
        if ty in (2, 11):
            stro = secs[lk][1]; step = es or (24 if is64 else 16)
            for k in range(sz // step):
                if is64: sn, inf, ot, sh, val, ssz = struct.unpack_from("<IBBHQQ", d, off + k * step)
                else:    sn, val, ssz, inf, ot, sh = struct.unpack_from("<IIIBBH", d, off + k * step)
                if sh == 0: continue
                e = d.index(b"\0", stro + sn); res.append((d[stro + sn:e].decode("latin1"), val))
    return res
lib = sys.argv[1] if len(sys.argv) > 1 else None
if not lib:
    for d, _, fs in os.walk("."):
        if "libGTASA.so" in fs: lib = os.path.join(d, "libGTASA.so"); break
if lib and os.path.exists(lib):
    seen = set()
    for nm, v in elf_syms(lib):
        if nm in seen: continue
        if re.search(r"_ZN4CCam|_ZN7CCamera|_ZN4CPad.*Cam|_ZN5CDraw|Zoom|FOV|_ZN6CWorld.*LineOfSight|_ZN4CHud.*Cross", nm):
            seen.add(nm); OUT.append("%08x %s" % (v, nm))
else:
    OUT.append("libGTASA.so tapilmadi. Yolunu ver: python3 collect_cam.py /yol/libGTASA.so")

os.makedirs("txt", exist_ok=True)
t = "\n".join(OUT)
open("txt/cam_final.txt", "w", encoding="utf-8").write(t)
print("txt/cam_final.txt  %d KB, %d setir" % (len(t) // 1024, t.count("\n")))

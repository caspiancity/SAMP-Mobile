#!/usr/bin/env python3
# apply_patch7.py - hidravlik / nitro (maşın komponentləri) düzəlişi
# İstifadə: layihənin kök qovluğunda:  python3 apply_patch7.py
# Hər dəyişən fayl üçün .bak7 ehtiyat nüsxəsi yaradılır.
import re
import shutil
import sys

ROOT = "app/src/main/cpp/samp"
ok, fail = [], []


def load(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def save(p, s):
    shutil.copy(p, p + ".bak7")
    with open(p, "w", encoding="utf-8") as f:
        f.write(s)


# 1) events.cpp: server komponent göndərəndə client onu tətbiq etsin
p = f"{ROOT}/game/events.cpp"
try:
    s = load(p)
    if "pVehicle->AddComponent(iComponent)" in s:
        ok.append("events.cpp (artıq patch olunub)")
    else:
        pat = re.compile(
            r"//\s*if\s*\(\s*CStreaming::TryLoadModel\(iComponent\)\s*\)\s*\n"
            r"\s*//\s*ScriptCommand\(&add_car_component[^\n]*\n"
        )
        new = (
            "pVehicle = pNetGame->GetVehiclePool()->GetAt(dwParam1);\n"
            "        if (pVehicle) {\n"
            "            try { pVehicle->AddComponent(iComponent); } catch (...) {}\n"
            "        }\n"
        )
        s2, n = pat.subn(new, s, count=1)
        if n:
            save(p, s2)
            ok.append("events.cpp")
        else:
            fail.append("events.cpp: CARCOMPONENT komment bloku tapılmadı")
except OSError as e:
    fail.append(f"events.cpp: {e}")

# 2) netrpc.cpp: maşın yaranarkən mod slotlarını və paintjob-u tətbiq et
p = f"{ROOT}/net/netrpc.cpp"
try:
    s = load(p)
    if "pVehicle && pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR" in s:
        ok.append("netrpc.cpp (artıq patch olunub)")
    else:
        start = re.compile(
            r"/\*\s*if\s*\(\s*pVehicle->GetVehicleSubtype\(\)\s*==\s*VEHICLE_SUBTYPE_CAR\s*\)"
        )
        m = start.search(s)
        if not m:
            fail.append("netrpc.cpp: /*if (pVehicle->GetVehicleSubtype()... bloku tapılmadı")
        else:
            head = s[: m.start()]
            tail = s[m.end():]
            # rəng bloku komment olaraq qalır (dublikat tətbiqin qarşısı üçün),
            # ona görə də mövcud '*/' onu bağlasın
            color = re.compile(r"(\n[ \t]*)if\s*\(\s*VehicleInfo\.iBodyColor1")
            tail2, n = color.subn(r"\1/*if (VehicleInfo.iBodyColor1", tail, count=1)
            if not n:
                fail.append("netrpc.cpp: iBodyColor1 bloku tapılmadı")
            else:
                s2 = (
                    head
                    + "if (pVehicle && pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)"
                    + tail2
                )
                save(p, s2)
                ok.append("netrpc.cpp")
except OSError as e:
    fail.append(f"netrpc.cpp: {e}")

# 3) vehicle.cpp: AddComponent exception atmasın (crash olmasın)
p = f"{ROOT}/game/vehicle.cpp"
try:
    s = load(p)
    fn = re.search(r"void CVehicle::AddComponent\(int iComponentID\)\s*\{.*?\n\}", s, re.S)
    if not fn:
        fail.append("vehicle.cpp: AddComponent tapılmadı")
    elif "throw" not in fn.group(0):
        ok.append("vehicle.cpp (artıq patch olunub)")
    else:
        body = fn.group(0).replace(
            'throw std::runtime_error("Model not loaded");', "return;"
        )
        save(p, s[: fn.start()] + body + s[fn.end():])
        ok.append("vehicle.cpp")
except OSError as e:
    fail.append(f"vehicle.cpp: {e}")

print("OK   :", ", ".join(ok) or "-")
print("XƏTA :", "; ".join(fail) or "-")
sys.exit(1 if fail else 0)

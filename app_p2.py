#!/usr/bin/env python3
# apply_patch12.py - netrpc.cpp: WorldVehicleAdd-da hazir modlari duzgun tetbiq et
#   * pVehicle elani (New()-dan sonra GetAt) elave olunur
#   * mod/paintjob/reng bloku komment-den cixarilir (qatar mentiqine toxunulmur)
import os, re, shutil
P = os.path.join(os.getcwd(), "app/src/main/cpp/samp/net/netrpc.cpp")
if not os.path.exists(P):
    print("[YOX] netrpc.cpp tapilmadi"); raise SystemExit
s = open(P, encoding="utf-8").read()
if "NETRPC_PATCH12" in s:
    print("[KECDI] artiq tetbiq olunub"); raise SystemExit

f = s.find("void WorldVehicleAdd(")
e = s.find("void WorldVehicleRemove(")
if f < 0 or e < 0 or e < f:
    print("[KECDI] WorldVehicleAdd tapilmadi"); raise SystemExit
body = s[f:e]

# 1) elan: pVehiclePool->New(&VehicleInfo);  sonra
m = re.search(r'pVehiclePool->New\(&VehicleInfo\);([ \t]*\n[ \t]*/\*CVehicle\* pVehicle)', body)
if not m:
    print("[KECDI] New() ankeri tapilmadi (fayla toxunulmadi)"); raise SystemExit
decl = ('pVehiclePool->New(&VehicleInfo);\n'
        '        CVehicle* pVehicle = pVehiclePool->GetAt(VehicleInfo.VehicleID);   // NETRPC_PATCH12\n'
        '        if (!pVehicle) return;\n')
body = body[:m.start()] + decl + body[m.start() + len('pVehiclePool->New(&VehicleInfo);'):].lstrip(' \t').replace('\n', '\n', 0)
# yuxaridaki lstrip yeni setir ve /* ile baslayan hisseni qoruyur:
# (m.group(1) basinda bosluq+newline qalir)

# 2) mod blokunu ac (yalniz hele komment-dedirse)
if '/*if (pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)' in body:
    body = body.replace('/*if (pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)',
                        'if (pVehicle->GetVehicleSubtype() == VEHICLE_SUBTYPE_CAR)', 1)
    body, n = re.subn(r'(pVehicle->SetColor\(VehicleInfo\.iBodyColor1,\s*VehicleInfo\.iBodyColor2\);\s*\n\s*\})\*/', r'\1', body, count=1)
    if n != 1:
        print("[KECDI] blokun sonu tapilmadi (fayla toxunulmadi)"); raise SystemExit
    msg = "elan elave olundu + mod bloku acildi"
else:
    msg = "elan elave olundu (mod bloku artiq aciqdir)"

shutil.copy(P, P + ".bak12")
open(P, "w", encoding="utf-8").write(s[:f] + body + s[e:])
print("[OK] netrpc.cpp: " + msg)

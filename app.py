#!/usr/bin/env python3
# Repo kokunde isle:  python3 apply_patch3.py
#
# 1) /gizle  -> GTA-nin oz HUD-u (radar/pul/saglamliq), toxunus duymelerinin cizgisi ve server textdraw-lari da gizlenir
# 2) CHAT    -> en cox 6 setir; yeni mesaj altdan yumsaq suruserek gelir
# 3) SINYAL  -> lampa parildamasi (fara yanirdi) SONULU, ayri narinci corona ACIQ. /sinyalayar lampa|korona|ters
# 4) /dump   -> masinin materiallarini + dummy-leri <storage>/vehdump.txt faylina yazir (nomre/sinyal ucun)
# 5) FONT    -> Latin Extended-B (U+018F "Ə") aralığı elave olunur (font faylinda glyph varsa gorunecek)
#
# Hamisi yaddasda yoxlanir; bir anchor tapilmasa HEC BIR fayl deyismir.
import os, re, shutil, sys

R = "app/src/main/cpp/samp"
F_HOOKS = R + "/game/hooks.cpp"
F_CHATH = R + "/gui/samp_widgets/chat.h"
F_CHATC = R + "/gui/samp_widgets/chat.cpp"
F_UISET = R + "/gui/uisettings.cpp"
F_IMGW  = R + "/gui/imguiwrapper.cpp"
F_VCH   = R + "/game/VehControls.h"
F_VEH   = R + "/game/Entity/CVehicleGTA.cpp"
F_AZH   = R + "/game/AzPlate.h"
F_AZC   = R + "/game/AzPlate.cpp"


def load(p):
    if not os.path.exists(p):
        die("fayl yoxdur: " + p)
    raw = open(p, "rb").read().decode("utf-8", errors="surrogateescape")
    return raw.replace("\r\n", "\n"), ("\r\n" in raw)


def save(p, t, crlf):
    shutil.copyfile(p, p + ".bak3")
    if crlf:
        t = t.replace("\n", "\r\n")
    open(p, "wb").write(t.encode("utf-8", errors="surrogateescape"))


def die(msg):
    print("XETA:", msg)
    print("Hec bir fayl deyismedi.")
    sys.exit(1)


def rep1(t, old, new, name):
    n = t.count(old)
    if n != 1:
        die("%s: anchor %d dəfə tapildi (1 gozlenirdi): %r" % (name, n, old[:70]))
    return t.replace(old, new)


def repN(t, old, new, count, name):
    n = t.count(old)
    if n != count:
        die("%s: anchor %d dəfə tapildi (%d gozlenirdi): %r" % (name, n, count, old[:70]))
    return t.replace(old, new)


def rex1(t, pattern, new, name, flags=0):
    ms = list(re.finditer(pattern, t, flags))
    if len(ms) != 1:
        die("%s: regex %d dəfə tapildi (1 gozlenirdi): %s" % (name, len(ms), pattern[:70]))
    m = ms[0]
    return t[:m.start()] + (new(m) if callable(new) else new) + t[m.end():]


# =============================================================================
hooks, c_hooks = load(F_HOOKS)
chath, c_chath = load(F_CHATH)
chatc, c_chatc = load(F_CHATC)
uiset, c_uiset = load(F_UISET)
imgw,  c_imgw  = load(F_IMGW)
vch,   c_vch   = load(F_VCH)
veh,   c_veh   = load(F_VEH)
azh,   c_azh   = load(F_AZH)
azc,   c_azc   = load(F_AZC)

# ---- 1) hooks.cpp: HUD gizletme ----
m = re.search(r"void Render2dStuff\(\)\s*\{", hooks)
if not m:
    die("hooks.cpp: 'void Render2dStuff() {' tapilmadi")
start = m.start()
end = hooks.find("\n}\n", start)
if end == -1:
    die("hooks.cpp: Render2dStuff sonu tapilmadi")
body = hooks[start:end]

lines = body.split("\n")
hit_hud = hit_touch = 0
for i, ln in enumerate(lines):
    s = ln.strip()
    if '"_ZN4CHud4DrawEv"' in s and s.startswith("CHook::CallFunction") and not s.startswith("//"):
        lines[i] = ln[:len(ln) - len(ln.lstrip())] + "if (!g_bHideAllUI) " + s
        hit_hud += 1
    elif "CTouchInterface::DrawAll" in s and not s.startswith("//"):
        lines[i] = ln[:len(ln) - len(ln.lstrip())] + "if (!g_bHideAllUI) " + s
        hit_touch += 1
if hit_hud != 1 or hit_touch != 1:
    die("hooks.cpp: CHud::Draw (%d) / CTouchInterface::DrawAll (%d) sətri 1/1 tapilmadi" % (hit_hud, hit_touch))
body = "\n".join(lines)
body = rep1(body, "if(pTextDrawPool) pTextDrawPool->Draw();",
            "if(pTextDrawPool && !g_bHideAllUI) pTextDrawPool->Draw();", "hooks textdraw")
hooks = hooks[:start] + "extern bool g_bHideAllUI;\n" + body + hooks[end:]

# ---- 2) chat.h / chat.cpp / uisettings.cpp ----
chath = rex1(chath, r"([ \t]*)void addMessage\(const std::string& messsage[^\n]*\n",
             lambda m: m.group(1) + "float m_slide = 0.0f;   // yeni mesaj gelende alt-ustu suruşme (piksel)\n" + m.group(0),
             "chat.h member")

chatc = rep1(chatc, "#include <sstream>", "#include <sstream>\n#include <chrono>", "chat.cpp include")
chatc = rex1(chatc, r"void Chat::draw\(ImGuiRenderer\* renderer\)\s*\{\s*ListBox::draw\(renderer\);\s*\}",
r'''void Chat::draw(ImGuiRenderer* renderer)
{
        // yeni mesajlar altdan yavas-yavas yuxari suruslenir
        static std::chrono::steady_clock::time_point s_last = std::chrono::steady_clock::now();
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - s_last).count();
        s_last = now;
        if (dt > 0.1f) dt = 0.1f;

        if (m_slide > 0.0f)
        {
                m_slide -= (m_slide * 7.0f + 10.0f) * dt;      // yumsaq basla, sona dogru yavasla
                if (m_slide < 0.5f) m_slide = 0.0f;
        }

        if (m_slide > 0.0f && !children().empty())
        {
                Widget* panel = children()[0];
                const ImVec2 pos = panel->position();
                panel->setPosition(ImVec2(pos.x, pos.y + m_slide));
                ListBox::draw(renderer);
                panel->setPosition(pos);
        }
        else
        {
                ListBox::draw(renderer);
        }
}''', "Chat::draw")
chatc = repN(chatc, "this->setScrollY(1.0f);",
             "this->setScrollY(1.0f);\n        m_slide = std::min(m_slide + itemSize().y, itemSize().y * 3.0f);", 2, "chat addMessage")

uiset = rep1(uiset, "#include <string>", "#include <string>\n#include <algorithm>", "uisettings include")
uiset = rep1(uiset, "m_chatDispMesssages = pSettings->Get().iChatMaxMessages;",
             "m_chatDispMesssages = std::max<int>(3, std::min<int>((int)pSettings->Get().iChatMaxMessages, 6));   // kicik chat: en cox 6 setir",
             "uisettings chat lines")

# ---- 5) font aralığı ----
imgw = rex1(imgw, r"0x0250,\s*0x02AF,",
            "0x0180, 0x024F, // Latin Extended-B (U+018F = Ə)\n        0x0250, 0x02AF,", "font ranges")

# ---- 3) sinyal ara hell ----
vch = rex1(vch, r"inline bool g_sigCorona\s*=\s*false;[^\n]*",
           "inline bool g_sigCorona    = true;    // ayri narinci corona (defolt ACIQ)\n"
           "\tinline bool g_sigLamp      = false;   // lampa materiali parildasin (fara yanir! defolt SONULU)",
           "VehControls.h flags")
veh = repN(veh, "        bool l, r;\n        ComputeSides(it->second, l, r);",
           "        if (!g_sigLamp) return;\n\n        bool l, r;\n        ComputeSides(it->second, l, r);", 2, "CVehicleGTA lamp gate")

# ---- 4) /dump + /sinyalayar lampa ----
AZH_ADD = "    // /dump: masinin materiallari + dummy-leri <storage>/vehdump.txt faylina (append)\n    bool Dump(CVehicleGTA* veh, std::string& outPath);\n"
azh = rep1(azh, "    bool Get(CVehicleGTA* veh, std::string& out);\n",
           "    bool Get(CVehicleGTA* veh, std::string& out);\n" + AZH_ADD, "AzPlate.h")

azc = rep1(azc, "#include <cctype>", "#include <cctype>\n#include <cstdio>", "AzPlate.cpp include")
azc = rep1(azc, '#include "Entity/CVehicleGTA.h"',
           '#include "Entity/CVehicleGTA.h"\n#include "game/Models/ModelInfo.h"', "AzPlate.cpp ModelInfo include")
azc += r'''

// ----------------------------------------------------------------------------
//  /dump : masinin atomic/material siyahisi + isiq dummy-leri
// ----------------------------------------------------------------------------
extern char* g_pszStorage;

namespace
{
    FILE* g_dumpF    = nullptr;
    int   g_dumpAtom = 0;
    int   g_dumpMat  = 0;

    RpMaterial* DumpMatCB(RpMaterial* mat, void*)
    {
        if (g_dumpF && mat) {
            const char* tn = mat->texture ? mat->texture->name : "(texture yox)";
            const char* mn = mat->texture ? mat->texture->mask : "";
            fprintf(g_dumpF, "  mat#%d tex=\"%s\" mask=\"%s\" rgba=%d,%d,%d,%d\n",
                    g_dumpMat++, tn, mn,
                    (int)mat->color.red, (int)mat->color.green, (int)mat->color.blue, (int)mat->color.alpha);
        }
        return mat;
    }

    RpAtomic* DumpAtomCB(RpAtomic* atomic, void*)
    {
        if (g_dumpF) fprintf(g_dumpF, "atomic#%d\n", g_dumpAtom++);
        auto geo = RpAtomicGetGeometry(atomic);
        if (geo) RpGeometryForAllMaterials(geo, DumpMatCB, nullptr);
        return atomic;
    }
}

namespace AzPlate
{
    bool Dump(CVehicleGTA* veh, std::string& outPath)
    {
        if (!veh || !veh->m_pRwClump || !g_pszStorage) return false;

        outPath = std::string(g_pszStorage) + "/vehdump.txt";
        FILE* f = fopen(outPath.c_str(), "a");
        if (!f) return false;

        g_dumpF = f;
        g_dumpAtom = 0;
        g_dumpMat = 0;

        fprintf(f, "\n===== model %d =====\n", (int)veh->m_nModelIndex);
        auto* mi = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
        if (mi && mi->m_pVehicleStruct) {
            for (int i = 0; i < 4; ++i) {
                const CVector& d = mi->m_pVehicleStruct->m_avDummyPos[i];
                fprintf(f, "dummy[%d] = %.3f %.3f %.3f\n", i, d.x, d.y, d.z);
            }
        }
        RpClumpForAllAtomics(veh->m_pRwClump, DumpAtomCB, nullptr);
        fprintf(f, "(atomic=%d, material=%d)\n", g_dumpAtom, g_dumpMat);

        fclose(f);
        g_dumpF = nullptr;
        return true;
    }
}
'''

CHAT_DUMP = r'''if (cmd == "/dump")
        {
                CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Xeta: Avtomobilde deyilsiniz!");
                        return true;
                }

                CVehicle* pVeh = nullptr;
                if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
                        CVehiclePool* pPool = pNetGame->GetVehiclePool();
                        VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
                        if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
                }
                if (!pVeh || !pVeh->m_pVehicle) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Avtomobil tapilmadi!");
                        return true;
                }

                std::string path;
                if (AzPlate::Dump(pVeh->m_pVehicle, path))
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Yazildi: %s", path.c_str());
                else
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Dump yazila bilmedi.");
                return true;
        }

        if (cmd == "/sinyalayar")'''

chatc = rex1(chatc, r'if \(cmd == "/sinyalayar"\)', CHAT_DUMP, "chat /dump anchor")
chatc = rep1(chatc, '} else if (a == "korona") {',
             '} else if (a == "lampa") {\n'
             '                        VehControls::g_sigLamp = !VehControls::g_sigLamp;\n'
             '                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Sinyal lampa parildamasi: %s", VehControls::g_sigLamp ? "aciq" : "bagli");\n'
             '                } else if (a == "korona") {', "chat lampa")
chatc = rep1(chatc, "Istifade: /sinyalayar [ters|korona]", "Istifade: /sinyalayar [ters|korona|lampa]", "chat usage")

# =============================================================================
save(F_HOOKS, hooks, c_hooks)
save(F_CHATH, chath, c_chath)
save(F_CHATC, chatc, c_chatc)
save(F_UISET, uiset, c_uiset)
save(F_IMGW, imgw, c_imgw)
save(F_VCH, vch, c_vch)
save(F_VEH, veh, c_veh)
save(F_AZH, azh, c_azh)
save(F_AZC, azc, c_azc)

print("OK  hooks.cpp, chat.h, chat.cpp, uisettings.cpp, imguiwrapper.cpp, VehControls.h, CVehicleGTA.cpp, AzPlate.h/.cpp")
print("\nIndi:  git add -A && git commit -m 'hud gizle, chat anim, dump' && git push")

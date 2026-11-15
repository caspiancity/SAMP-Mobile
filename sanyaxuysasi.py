# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v3.py  --  1-ci MERHELE (apply_v2.py-den SONRA islet)
#   1) klaviatura: Azerbaycan herfleri ucun ikiqat cevirme duzelisi (KBFIX_V3)
#   2) CVehiclePool::FindIDFromGtaPtr: tapilmayanda return yox idi -> INVALID_VEHICLE_ID (VPFIX_V3)
#   3) Sinyal blokuna "BETA" nisani (BETA_V3)
#   4) /mm menyusu: basliqdan surusdur, sag-alt kunceden boyut/kicilt; yer+olcu yadda qalir (MMV3_LAYOUT)
#   5) Handling: slot = launcherden yuklenen handling.cfg fayli (SAMP/hslot_1..5.cfg).
#      Menyuda "Tetbiq et" -> maşının modeline uygun setir faylda tapilir, deyerler tetbiq olunur.
#      Chat-a kod yazma ve "Kod"/"Fayl" duymeleri silindi (HNDV3, HNDV3_UI)
#   Geri qaytarmaq: her faylin yaninda *.bakv3
import os, re, shutil

ROOT = os.getcwd()
CPP  = os.path.join(ROOT, "app/src/main/cpp/samp")
JAVA = os.path.join(ROOT, "app/src/main/java/com/samp/mobile/launcher")


def rd(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def rep1(s, old, new):
    if s.count(old) != 1:
        raise ValueError("anker %d defe tapildi: %s" % (s.count(old), old[:60].replace("\n", " ")))
    return s.replace(old, new)


def rx1(s, pat, repl, flags=0):
    new, n = re.subn(pat, lambda m: repl(m) if callable(repl) else repl, s, count=1, flags=flags)
    if n != 1:
        raise ValueError("regex tapilmadi: " + pat[:60])
    return new


def step(name, path, marker, fn):
    if not os.path.exists(path):
        print("[YOX]   %s: %s tapilmadi" % (name, os.path.relpath(path, ROOT)))
        return
    s = rd(path)
    if marker in s:
        print("[KECDI] %s: artiq tetbiq olunub" % name)
        return
    try:
        n = fn(s)
    except ValueError as e:
        print("[KECDI] %s: %s (fayla toxunulmadi)" % (name, e))
        return
    if not os.path.exists(path + ".bakv3"):
        shutil.copy(path, path + ".bakv3")
    with open(path, "w", encoding="utf-8") as f:
        f.write(n)
    print("[OK]    %s" % name)


# ------------------------------------------------------------------ 1. klaviatura
def p_kb_cpp(s):
    old = "m_caption = Encoding::cp2utf(m_input);"
    if s.count(old) < 1:
        raise ValueError("m_caption anker")
    return s.replace(old, "m_caption = m_input;   // KBFIX_V3: m_input artiq UTF-8-dir")


def p_kb_h(s):
    return rep1(s, "m_caption = Encoding::cp2utf(string);", "m_caption = string;   // KBFIX_V3")


# ------------------------------------------------------------------ 2. vehiclepool.h
def p_vpool(s):
    return rx1(s,
               r'(VEHICLEID FindIDFromGtaPtr\(CVehicleGTA\* pGtaVehicle\)\s*\{.*?return i;\s*\}\s*\}\s*)(\})',
               lambda m: m.group(1) + "return INVALID_VEHICLE_ID;   // VPFIX_V3\n\t}",
               re.S)


# ------------------------------------------------------------------ 3. BETA nisani
def p_beta(s):
    old = 'Txt(S(15.0f), blk.x + S(18.0f), blk.y + S(10.0f), Col(124, 132, 152), "Sinyal");'
    new = r'''// BETA_V3
                const Rc bb{ blk.x + S(16.0f), blk.y + S(8.0f), S(44.0f), S(20.0f) };
                Fill(bb, Orange(46), S(10.0f));
                Stroke(bb, Orange(150), S(10.0f), S(1.0f));
                TxtC(S(12.0f), bb, Col(255, 190, 100, 255), "BETA", true);
                Txt(S(15.0f), bb.x + bb.w + S(10.0f), blk.y + S(10.0f), Col(124, 132, 152), "Sinyal");'''
    return rep1(s, old, new)


# ------------------------------------------------------------------ 4. /mm yer + olcu
LAYOUT_HELPERS = r'''
    // MMV3_LAYOUT: panelin olcusu/yeri SAMP/mm_layout.txt faylinda saxlanir
    static void LayoutLoad(float& z, float& cx, float& cy)
    {
        if (!g_pszStorage) return;
        char p[0x100];
        snprintf(p, sizeof(p), "%sSAMP/mm_layout.txt", g_pszStorage);
        FILE* f = fopen(p, "r");
        if (!f) return;
        float a = 1.0f, b = -1.0f, c = -1.0f;
        if (fscanf(f, "%f %f %f", &a, &b, &c) == 3 && a > 0.2f && a < 3.0f) { z = a; cx = b; cy = c; }
        fclose(f);
    }

    static void LayoutSave(float z, float cx, float cy)
    {
        if (!g_pszStorage) return;
        char p[0x100];
        snprintf(p, sizeof(p), "%sSAMP/mm_layout.txt", g_pszStorage);
        FILE* f = fopen(p, "w");
        if (!f) return;
        fprintf(f, "%.4f %.1f %.1f\n", z, cx, cy);
        fclose(f);
    }
'''

LAYOUT_STATICS = r'''    // MMV3_LAYOUT
    static float         s_zoom = 1.0f, s_cx = -1.0f, s_cy = -1.0f;
    static float         s_dragDx = 0.0f, s_dragDy = 0.0f, s_rsOff = 0.0f, s_rsLeft = 0.0f, s_rsTop = 0.0f;
    static bool          s_layLoaded = false;
'''

LAYOUT_BLOCK = r'''
    const float W0 = 880.0f, H0 = 560.0f;      // MMV3_LAYOUT
    const float uBase = std::min(disp.y / 720.0f, std::min(disp.x * 0.96f / W0, disp.y * 0.96f / H0));
    const float uFit  = std::min(disp.x * 0.98f / W0, disp.y * 0.98f / H0);
    if (!s_layLoaded) { s_layLoaded = true; mm2::LayoutLoad(s_zoom, s_cx, s_cy); }
    s_zoom = Clamp(s_zoom, 0.45f, 1.6f);

    // basliqdan surusdur / sag-alt kunceden olcunu deyis (evvelki kadrin sahesine gore)
    if (want && s_t > 0.8f && s_hndRectValid) {
        const MMTouch& mt = g_mmTouch;
        const Rc pp{ s_hndPos.x, s_hndPos.y, s_hndSize.x, s_hndSize.y };
        const Rc hdr{ pp.x, pp.y, pp.w - S(64.0f), S(58.0f) };
        const Rc hdl{ pp.x + pp.w - S(44.0f), pp.y + pp.h - S(44.0f), S(44.0f), S(44.0f) };
        if (mt.pressed && s_act == 0) {
            if (In(hdl, mt.x, mt.y)) {
                s_act = ID_ZOOMP; s_rsOff = (pp.x + pp.w) - mt.x; s_rsLeft = pp.x; s_rsTop = pp.y;
            } else if (In(hdr, mt.x, mt.y)) {
                s_act = ID_DRAG; s_dragDx = mt.x - s_cx; s_dragDy = mt.y - s_cy;
            }
        }
        if (mt.down && s_act == ID_DRAG) {
            s_cx = mt.x - s_dragDx;
            s_cy = mt.y - s_dragDy;
        } else if (mt.down && s_act == ID_ZOOMP) {
            const float wantU = (mt.x + s_rsOff - s_rsLeft) / W0;
            s_zoom = Clamp(wantU / uBase, 0.45f, 1.6f);
            const float nu = std::min(uBase * s_zoom, uFit);
            s_cx = s_rsLeft + W0 * nu * 0.5f;
            s_cy = s_rsTop  + H0 * nu * 0.5f;
        }
        if (mt.released && (s_act == ID_DRAG || s_act == ID_ZOOMP))
            mm2::LayoutSave(s_zoom, s_cx, s_cy);
    }

    const float u0 = std::min(uBase * s_zoom, uFit);
    s_U = u0 * (0.90f + 0.10f * open);
    if (s_cx < 0.0f || s_cy < 0.0f) { s_cx = disp.x * 0.5f; s_cy = disp.y * 0.5f; }
    {
        const float hw = W0 * u0 * 0.5f, hh = H0 * u0 * 0.5f;
        s_cx = Clamp(s_cx, hw, std::max(hw, disp.x - hw));
        s_cy = Clamp(s_cy, hh, std::max(hh, disp.y - hh));
    }

    const Rc P{ s_cx - W0 * s_U * 0.5f, s_cy - H0 * s_U * 0.5f, W0 * s_U, H0 * s_U };
'''

GRIP = r'''    // MMV3_LAYOUT: olcu tutacagi (sag-alt)
    {
        const float gx = P.x + P.w - S(14.0f), gy = P.y + P.h - S(14.0f);
        for (int k = 1; k <= 3; ++k) {
            const float d = S(4.5f) * (float)k;
            s_dl->AddLine(ImVec2(gx - d, gy), ImVec2(gx, gy - d), Col(255, 255, 255, 80), S(1.6f));
        }
    }
'''


def p_layout(s):
    s = rx1(s, r'(    static int\s+s_slot = 1;\n)', lambda m: m.group(1) + LAYOUT_STATICS)
    s = rx1(s,
            r'\n    const float W0 = 880\.0f, H0 = 560\.0f;\n    float u0 = [^\n]*\n    u0 = std::min\(u0,[^\n]*\n'
            r'    s_U = u0 \* \(0\.90f \+ 0\.10f \* open\);\n\s*\n    const Rc P\{[^\n]*\n',
            lambda m: LAYOUT_BLOCK)
    s = rep1(s, "    mm2::Glass(P, S(28.0f));\n", "    mm2::Glass(P, S(28.0f));\n" + GRIP)
    s = rep1(s, "    static EditCaller* s_caller = nullptr;\n", "    static EditCaller* s_caller = nullptr;\n" + LAYOUT_HELPERS)
    return s


# ------------------------------------------------------------------ 5a. vehicle.cpp: handling.cfg slotlari
HNDV3_BLOCK = r'''
// ===== HNDV3: handling.cfg slotlari (SAMP/hslot_1..5.cfg, launcher yazir) =====
#include <chrono>
#include <cctype>
#include <algorithm>

namespace hv3
{
    static std::map<int, std::string> s_ide;       // model id -> handling adi (vehicles.ide)
    static bool s_ideLoaded = false;

    static std::string Trim(const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && isspace((unsigned char)s[a])) ++a;
        while (b > a && isspace((unsigned char)s[b - 1])) --b;
        return s.substr(a, b - a);
    }

    static std::string Up(std::string s)
    {
        for (size_t i = 0; i < s.size(); ++i) s[i] = (char)toupper((unsigned char)s[i]);
        return s;
    }

    static void Split(const char* line, std::vector<std::string>& out)
    {
        out.clear();
        std::string cur;
        for (const char* c = line; *c; ++c) {
            if (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            } else cur += *c;
        }
        if (!cur.empty()) out.push_back(cur);
    }

    static void LoadIde()
    {
        if (s_ideLoaded) return;
        s_ideLoaded = true;
        FILE* f = fopen(HndxPath("vehicles.ide").c_str(), "r");
        if (!f) return;
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] < '0' || line[0] > '9') continue;
            std::vector<std::string> tk;
            std::string cur;
            for (const char* c = line; *c; ++c) {
                if (*c == ',') { tk.push_back(Trim(cur)); cur.clear(); }
                else cur += *c;
            }
            tk.push_back(Trim(cur));
            if (tk.size() < 5) continue;
            s_ide[atoi(tk[0].c_str())] = Up(tk[4]);
        }
        fclose(f);
    }

    // standart baza: oyunun ozunun yukledigi fayl
    static std::string BaseCfgPath()
    {
        const std::string c = HndxPath("handling_custom.cfg");
        FILE* f = fopen(c.c_str(), "rb");
        if (f) { fclose(f); return c; }
        return HndxPath("handling.cfg");
    }

    // faylda ad ile setri tapir; qiymetler: adin sonrakisi (0-dan). Reqem olmayanlar NAN.
    static bool FindLine(const std::string& path, const std::string& name, std::vector<float>& vals)
    {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return false;
        char line[1024];
        std::vector<std::string> tk;
        bool found = false;
        while (fgets(line, sizeof(line), f)) {
            const char* p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (!*p || *p == ';' || *p == '*' || *p == '!' || *p == '$' || *p == '%' || *p == '^' || *p == '\r' || *p == '\n') continue;
            Split(p, tk);
            if (tk.size() < 2 || Up(tk[0]) != name) continue;
            vals.clear();
            for (size_t i = 1; i < tk.size(); ++i) {
                char* e = nullptr;
                const float v = strtof(tk[i].c_str(), &e);
                vals.push_back((e && e != tk[i].c_str()) ? v : NAN);
            }
            found = true;
            break;
        }
        fclose(f);
        return found;
    }

    // Vahid cevirmesini bilmeye ehtiyac olmasin deye NISBET: yeni = indiki * (fayl_slot / fayl_baza)
    static float Scale(float cur, float base, float imp)
    {
        if (!std::isfinite(base) || !std::isfinite(imp)) return cur;
        if (fabsf(base) > 1.0e-6f) return cur * (imp / base);
        return (fabsf(imp) > 1.0e-6f) ? imp : cur;
    }

    static float Ratio(float base, float imp)
    {
        if (!std::isfinite(base) || !std::isfinite(imp) || fabsf(base) < 1.0e-6f) return 1.0f;
        return imp / base;
    }
}

bool HndExt_SlotUsed(int slot)
{
    if (slot < 1 || slot > HNDX_SLOTS) return false;
    static bool used[HNDX_SLOTS + 1] = {};
    static bool inited = false;
    static std::chrono::steady_clock::time_point last;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (!inited || std::chrono::duration<float>(now - last).count() > 0.7f) {
        inited = true;
        last = now;
        for (int i = 1; i <= HNDX_SLOTS; ++i) {
            char nm[32];
            snprintf(nm, sizeof(nm), "hslot_%d.cfg", i);
            FILE* f = fopen(HndxPath(nm).c_str(), "rb");
            used[i] = (f != nullptr);
            if (f) fclose(f);
        }
    }
    return used[slot];
}

bool HndExt_ApplyFileSlot(CVehicle* v, int slot, std::string& msg)
{
    msg.clear();
    if (!v || !v->m_pVehicle) { msg = "Avtomobil tapılmadı"; return false; }
    if (slot < 1 || slot > HNDX_SLOTS) { msg = "Yanlış slot"; return false; }

    char nm[32];
    snprintf(nm, sizeof(nm), "hslot_%d.cfg", slot);
    const std::string slotPath = HndxPath(nm);
    {
        FILE* f = fopen(slotPath.c_str(), "rb");
        if (!f) { msg = "Slot boşdur — launcher: Settings → Fayl seç"; return false; }
        fclose(f);
    }

    hv3::LoadIde();
    const int mid = (int)v->m_pVehicle->m_nModelIndex;
    std::map<int, std::string>::iterator it = hv3::s_ide.find(mid);
    if (it == hv3::s_ide.end()) { msg = "Model vehicles.ide-də tapılmadı"; return false; }
    const std::string name = it->second;

    std::vector<float> base, imp;
    if (!hv3::FindLine(hv3::BaseCfgPath(), name, base)) { msg = name + " baza handling-də tapılmadı"; return false; }
    if (!hv3::FindLine(slotPath, name, imp)) { msg = name + " bu faylda yoxdur"; return false; }
    if (base.size() < 29 || imp.size() < 29) { msg = "Sətir natamamdır (" + name + ")"; return false; }
    if (!(imp[0] > 0.0f)) { msg = "Fayldakı çəki yanlışdır"; return false; }

    HndxSnap(v);
    tHandlingData* h = (tHandlingData*)GetOrCreateOwnHandling(v->m_pVehicle);
    if (!h) { msg = "Handling yaradıla bilmədi"; return false; }

    const std::vector<float>& B = base;
    const std::vector<float>& I = imp;
    using hv3::Scale;

    // fayl sutunlari (adin sonrasi, 0-dan):
    // 0 mass 1 turnmass 2 drag 3-5 com 6 submerged 7 tractionMult 8 tractionLoss 9 tractionBias 10 gears
    // 11 maxVel 12 accel 13 inertia 14 drive 15 engine 16 brake 17 brakeBias 18 abs 19 steering
    // 20 suspForce 21 suspDamp 22 suspHighSpd 23 upper 24 lower 25 bias 26 antidive 27 seatOffset 28 collision
    const float oldMass = h->m_fMass;
    h->m_fMass = Scale(h->m_fMass, B[0], I[0]);
    if (oldMass > 0.0f && h->m_fMass > 0.0f) v->m_pVehicle->m_fMass *= h->m_fMass / oldMass;
    h->m_fTurnMass = Scale(h->m_fTurnMass, B[1], I[1]);
    h->m_fDragMult = Scale(h->m_fDragMult, B[2], I[2]);
    h->m_vecCentreOfMass.x = Scale(h->m_vecCentreOfMass.x, B[3], I[3]);
    h->m_vecCentreOfMass.y = Scale(h->m_vecCentreOfMass.y, B[4], I[4]);
    h->m_vecCentreOfMass.z = Scale(h->m_vecCentreOfMass.z, B[5], I[5]);

    h->m_fTractionMultiplier = Scale(h->m_fTractionMultiplier, B[7], I[7]);
    h->m_fTractionLoss       = Scale(h->m_fTractionLoss,       B[8], I[8]);
    h->m_fTractionBias       = Scale(h->m_fTractionBias,       B[9], I[9]);

    {   // surat: maksimum suret + vites cedveli birlikde miqyaslanir
        const float kv = hv3::Ratio(B[11], I[11]);
        cTransmission& t = h->m_transmissionData;
        t.m_fMaxVelocity     *= kv;
        t.m_fMaxGearVelocity *= kv;
        constexpr size_t kFloatsPerGear = sizeof(tTransmissionGear) / sizeof(float);
        for (int i = 0; i < 6; ++i) {
            float* g = reinterpret_cast<float*>(&t.m_aGears[i]);
            for (size_t j = 0; j < kFloatsPerGear; ++j) g[j] *= kv;
        }
        t.m_fEngineAcceleration = Scale(t.m_fEngineAcceleration, B[12], I[12]);
        t.m_fEngineInertia      = Scale(t.m_fEngineInertia,      B[13], I[13]);
    }

    h->m_fBrakeDeceleration = Scale(h->m_fBrakeDeceleration, B[16], I[16]);
    h->m_fBrakeBias         = Scale(h->m_fBrakeBias,         B[17], I[17]);
    h->m_fSteeringLock      = Scale(h->m_fSteeringLock,      B[19], I[19]);

    h->m_fSuspensionForceLevel                = Scale(h->m_fSuspensionForceLevel,                B[20], I[20]);
    h->m_fSuspensionDampingLevel              = Scale(h->m_fSuspensionDampingLevel,              B[21], I[21]);
    h->m_fSuspensionHighSpdComDamp            = Scale(h->m_fSuspensionHighSpdComDamp,            B[22], I[22]);
    h->m_fSuspensionUpperLimit                = Scale(h->m_fSuspensionUpperLimit,                B[23], I[23]);
    h->m_fSuspensionLowerLimit                = Scale(h->m_fSuspensionLowerLimit,                B[24], I[24]);
    h->m_fSuspensionBiasBetweenFrontAndRear   = Scale(h->m_fSuspensionBiasBetweenFrontAndRear,   B[25], I[25]);
    h->m_fSuspensionAntiDiveMultiplier        = Scale(h->m_fSuspensionAntiDiveMultiplier,        B[26], I[26]);
    h->m_fSeatOffsetDistance                  = Scale(h->m_fSeatOffsetDistance,                  B[27], I[27]);
    h->m_fCollisionDamageMultiplier           = Scale(h->m_fCollisionDamageMultiplier,           B[28], I[28]);

    HndxFixMass((uintptr_t)h);
    msg = name + " tətbiq edildi (slot " + std::to_string(slot) + ")";
    return true;
}
// ===== /HNDV3 =====
'''


def p_vehicle(s):
    if "HNDV2" not in s:
        raise ValueError("HNDV2 yoxdur: evvelce apply_v2.py isletmek lazimdir")
    if "HndExt_SlotUsed(" not in s:
        raise ValueError("HndExt_SlotUsed anker")
    # kohne bayt-slot funksiyasi adini deyisirik (yenisi asagida tarif olunur)
    s = s.replace("HndExt_SlotUsed(", "HndxOldSlotUsed(")
    s = rep1(s, "bool HndxOldSlotUsed(int slot)", "static bool HndxOldSlotUsed(int slot)")
    return s.rstrip("\n") + "\n" + HNDV3_BLOCK


# ------------------------------------------------------------------ 5b. gui.cpp: handling sehifesi
HND_BUTTONS = r'''            // HNDV3_UI: slot = launcherden yuklenen handling.cfg fayli
            static const char* kAct[2] = { "Tətbiq et", "Sıfırla" };
            const float bx = cx0 + S(272.0f);
            const float bw = (cw - S(272.0f) - S(6.0f)) / 2.0f;
            for (int k = 0; k < 2; ++k) {
                const Rc b{ bx + k * (bw + S(6.0f)), y0, bw, S(40.0f) };
                if (mm2::SmallBtn(85 + k, b, kAct[k], dt, inputPage, k == 0) && pVeh) {
                    if (k == 0) {
                        std::string m;
                        HndExt_ApplyFileSlot(pVeh, s_slot, m);
                        mm2::SetMsg(m);
                    } else {
                        mm2::SetMsg(HndExt_Reset(pVeh) ? "Standarta qaytarıldı" : "Dəyişiklik yoxdur");
                    }
                }
            }
'''


def p_hndui(s):
    s = rep1(s, "std::string HndExt_GetCode(CVehicle*);",
             "std::string HndExt_GetCode(CVehicle*);\nbool HndExt_ApplyFileSlot(CVehicle*, int, std::string&);   // HNDV3")
    s = rx1(s, r'[ \t]*// emeller\n.*?(?=[ \t]*if \(mm2::s_msgT > 0\.01f\))', lambda m: HND_BUTTONS, re.S)
    return s


# ------------------------------------------------------------------ 6. launcher (Java)
JAVA_COPY = r'''    // HNDV3: yuklenen fayl bos olan ilk slota (hslot_1..5.cfg) yazilir
    private File hndDir() {
        File dir = new File(requireActivity().getExternalFilesDir(null), "SAMP");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private File hndSlotFile(int i) {
        return new File(hndDir(), "hslot_" + i + ".cfg");
    }

    private void copyHandling(Uri uri) {
        int slot = 0;
        for (int i = 1; i <= 5; i++) {
            if (!hndSlotFile(i).exists()) { slot = i; break; }
        }
        if (slot == 0) {
            Toast.makeText(getActivity(), "Bütün 5 slot doludur. Əvvəlcə «Sıfırla» bas.", Toast.LENGTH_LONG).show();
            return;
        }
        try {
            InputStream in = requireActivity().getContentResolver().openInputStream(uri);
            if (in == null) throw new IOException("fayl açıla bilmədi");
            File tmp = new File(hndDir(), "hslot_tmp.cfg");
            FileOutputStream os = new FileOutputStream(tmp);
            byte[] buf = new byte[8192];
            int n;
            long total = 0;
            while ((n = in.read(buf)) > 0) { os.write(buf, 0, n); total += n; }
            os.close();
            in.close();
            if (total < 50) { tmp.delete(); throw new IOException("fayl çox kiçikdir"); }
            if (!tmp.renameTo(hndSlotFile(slot))) throw new IOException("slota yazıla bilmədi");
            Toast.makeText(getActivity(), "handling.cfg slot " + slot + "-ə əlavə olundu (" + total + " bayt). Oyunda /mm → Handling", Toast.LENGTH_LONG).show();
        } catch (Exception e) {
            Toast.makeText(getActivity(), "Xəta: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
    }
'''

JAVA_RESET = r'''int n = 0;
                for (int i = 1; i <= 5; i++) { if (hndSlotFile(i).delete()) n++; }
                if (hndTarget().delete()) n++;
                Toast.makeText(getActivity(), n > 0 ? "Handling slotları təmizləndi" : "Yüklənmiş fayl yoxdur", Toast.LENGTH_LONG).show();
'''


def p_settings_java(s):
    s = rx1(s, r'    private void copyHandling\(Uri uri\) \{\n.*?\n    \}\n', lambda m: JAVA_COPY, re.S)
    s = rx1(s, r'boolean ok = hndTarget\(\)\.delete\(\);\n\s*Toast\.makeText\(getActivity\(\), ok \? [^\n]*\n',
            lambda m: JAVA_RESET)
    return s


# ------------------------------------------------------------------ main
if __name__ == "__main__":
    gui = os.path.join(CPP, "gui/gui.cpp")
    if not os.path.exists(gui) or "MM2_V2" not in rd(gui):
        print("DAYAN: gui.cpp-de MM2_V2 yoxdur. Evvelce apply_v2.py isletmek lazimdir.")
        raise SystemExit(1)

    step("keyboard.cpp: Azerbaycan herfleri (caption)", os.path.join(CPP, "gui/samp_widgets/keyboard.cpp"), "KBFIX_V3", p_kb_cpp)
    step("keyboard.h: setInputString", os.path.join(CPP, "gui/samp_widgets/keyboard.h"), "KBFIX_V3", p_kb_h)
    step("vehiclepool.h: FindIDFromGtaPtr return", os.path.join(CPP, "net/vehiclepool.h"), "VPFIX_V3", p_vpool)
    step("gui.cpp: Sinyal BETA nisani", gui, "BETA_V3", p_beta)
    step("gui.cpp: /mm surusdur + olcu", gui, "MMV3_LAYOUT", p_layout)
    step("vehicle.cpp: handling.cfg slotlari", os.path.join(CPP, "game/vehicle.cpp"), "HNDV3", p_vehicle)
    step("gui.cpp: Handling sehifesi (Tetbiq et / Sifirla)", gui, "HNDV3_UI", p_hndui)
    step("SettingsFragment.java: fayl -> slot", os.path.join(JAVA, "fragments/SettingsFragment.java"), "HNDV3", p_settings_java)

    print("\nBitdi. Geri qaytarmaq: her faylin yaninda *.bakv3")
    print("Yoxlama: layihe qur, sonra /mm -> basliqdan surusdur, sag-alt kunceden boyut.")
    print("Handling: launcher Settings -> 'Fayl sec' (slot 1-5), oyunda /mm -> Handling -> slot sec -> 'Tetbiq et'.")

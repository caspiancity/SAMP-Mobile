#!/usr/bin/env python3
# apply_patch10.py - /tg tuning menyusu (VehTuner-in ustunde):
#   * yeni sahələr: asma yuxari/asagi hedd, asma/yol tutus/tormoz on-arxa balansi, anti-dive, agirliq merkezi
#   * yeni sehifeler: "Balans" ve "Slotlar" (5 slot + handling kodu)
#   * /tg komandasi menyunu Merhele/"Muherrik" sehifesinde acir
#   * apply_patch8-in /h* chat komandalari sondurulur (VehTuner ile toqquşmasin)
import os, re, shutil

ROOT = os.getcwd()
SRC = os.path.join(ROOT, "app/src/main/cpp")

def find_all(lname):
    out = []
    for d, _, fs in os.walk(SRC):
        for f in fs:
            if f.lower() == lname: out.append(os.path.join(d, f))
    return out

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()

def sub1(pat, repl, s, flags=0):
    new, n = re.subn(pat, lambda m: repl if isinstance(repl, str) else repl(m), s, count=1, flags=flags)
    if n != 1: raise ValueError(pat[:60])
    return new

def save(path, s):
    if not os.path.exists(path + ".bak10"): shutil.copy(path, path + ".bak10")
    with open(path, "w", encoding="utf-8") as f: f.write(s)

def run(name, paths, fn):
    if not paths:
        print("[YOX]   %s: fayl tapilmadi" % name); return
    for p in paths:
        s = rd(p)
        if "TG_PATCH10" in s:
            print("[KECDI] %s: artiq tetbiq olunub (%s)" % (name, os.path.basename(p))); continue
        try:
            n = fn(s)
        except ValueError as e:
            print("[KECDI] %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); continue
        save(p, n)
        print("[OK]    %s (%s)" % (name, os.path.relpath(p, ROOT)))

# ============================================================ VehTuner.h
def p_header(s):
    s = sub1(r'(#include <cstdint>[^\n]*\n)', lambda m: m.group(1) + '#include <string>   // TG_PATCH10\n', s)
    s = sub1(r'([ \t]*float[ \t]+collisionPct[ \t]*=[ \t]*100\.0f;[^\n]*\n)', lambda m: m.group(1) +
'''    // --- Asma / balans (TG_PATCH10) ---
    float   suspUpperPct = 100.0f;  // asma yuxari hedd               (30 .. 300)
    float   suspLowerPct = 100.0f;  // asma asagi hedd                (30 .. 300)
    float   suspBiasPct  = 100.0f;  // asma on/arxa balansi           (50 .. 150)
    float   antiDivePct  = 100.0f;  // anti-dive                      (0 .. 300)
    float   tractBiasPct = 100.0f;  // yol tutus on/arxa balansi      (50 .. 150)
    float   brakeBiasPct = 100.0f;  // tormoz on/arxa balansi         (50 .. 150)
    float   comZOffset   = 0.0f;    // agirliq merkezi hundurluyu, sm (-50 .. 50), 0 = standart
''', s)
    s = sub1(r'([ \t]*void[ \t]+GetPaletteColor\([^\n]*\n)', lambda m: m.group(1) +
'''
    // ---- TG_PATCH10: slotlar ve handling kodu ----
    constexpr int VEHTUNER_SLOTS = 5;
    bool        SaveSlot(int slot, const VehTune& t);          // slot 1..5
    bool        LoadSlot(int slot, VehTune& t);                // bos slotda false
    bool        SlotUsed(int slot);
    std::string ExportCode(const VehTune& t);                  // kodu qaytarir + SAMP/vehtune_code.txt-ye yazir
    bool        ParseCode(const std::string& code, VehTune& t);
    bool        ImportCodeFile(VehTune& t);                    // SAMP/vehtune_code.txt-den oxuyur
''', s)
    return s

# ============================================================ VehTuner.cpp
CPP_TAIL = r'''

// ===== TG_PATCH10: slotlar + handling kodu =====
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
extern char* g_pszStorage;

namespace
{
    constexpr int VT_NF = 20;

    void VtFields(VehTune& t, float** f)
    {
        f[0]  = &t.speedPct;     f[1]  = &t.accelPct;    f[2]  = &t.inertiaPct;   f[3]  = &t.massPct;
        f[4]  = &t.dragPct;      f[5]  = &t.brakePct;    f[6]  = &t.steerPct;     f[7]  = &t.tractionPct;
        f[8]  = &t.tracLossPct;  f[9]  = &t.turnPct;     f[10] = &t.suspForcePct; f[11] = &t.suspDampPct;
        f[12] = &t.collisionPct; f[13] = &t.suspUpperPct; f[14] = &t.suspLowerPct; f[15] = &t.suspBiasPct;
        f[16] = &t.antiDivePct;  f[17] = &t.tractBiasPct; f[18] = &t.brakeBiasPct; f[19] = &t.comZOffset;
    }

    std::string VtPath(const char* file)
    {
        return std::string(g_pszStorage ? g_pszStorage : "") + "SAMP/" + file;
    }

    int VtHex(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string VtEncode(const VehTune& src)
    {
        VehTune t = src;
        float* f[VT_NF];
        VtFields(t, f);
        std::string s = "T1";
        for (int i = 0; i < VT_NF; ++i) {
            uint32_t b;
            memcpy(&b, f[i], 4);
            char h[9];
            snprintf(h, sizeof(h), "%08X", b);
            s += h;
        }
        return s;
    }

    bool VtDecode(const std::string& code, VehTune& out)
    {
        if (code.size() != (size_t)(2 + 8 * VT_NF) || code[0] != 'T' || code[1] != '1') return false;
        VehTune t = out;
        float* f[VT_NF];
        VtFields(t, f);
        for (int i = 0; i < VT_NF; ++i) {
            uint32_t b = 0;
            for (int k = 0; k < 8; ++k) {
                int h = VtHex(code[2 + i * 8 + k]);
                if (h < 0) return false;
                b = (b << 4) | (uint32_t)h;
            }
            float v;
            memcpy(&v, &b, 4);
            if (!std::isfinite(v) || v < -1000.0f || v > 1000.0f) return false;
            *f[i] = v;
        }
        out = t;
        return true;
    }

    std::string g_vtSlots[VehTuner::VEHTUNER_SLOTS + 1];
    bool        g_vtSlotsLoaded = false;

    void VtSlotsLoad()
    {
        if (g_vtSlotsLoaded) return;
        g_vtSlotsLoaded = true;
        FILE* f = fopen(VtPath("vehtune_slots.txt").c_str(), "r");
        if (!f) return;
        char line[400];
        while (fgets(line, sizeof(line), f)) {
            int slot = 0;
            char code[300];
            if (sscanf(line, "%d %299s", &slot, code) != 2) continue;
            if (slot < 1 || slot > VehTuner::VEHTUNER_SLOTS) continue;
            VehTune probe;
            if (VtDecode(code, probe)) g_vtSlots[slot] = code;
        }
        fclose(f);
    }

    void VtSlotsWrite()
    {
        FILE* f = fopen(VtPath("vehtune_slots.txt").c_str(), "w");
        if (!f) return;
        for (int i = 1; i <= VehTuner::VEHTUNER_SLOTS; ++i)
            if (!g_vtSlots[i].empty()) fprintf(f, "%d %s\n", i, g_vtSlots[i].c_str());
        fclose(f);
    }
}

namespace VehTuner
{
    bool SaveSlot(int slot, const VehTune& t)
    {
        if (slot < 1 || slot > VEHTUNER_SLOTS) return false;
        VtSlotsLoad();
        g_vtSlots[slot] = VtEncode(t);
        VtSlotsWrite();
        return true;
    }

    bool LoadSlot(int slot, VehTune& t)
    {
        if (slot < 1 || slot > VEHTUNER_SLOTS) return false;
        VtSlotsLoad();
        if (g_vtSlots[slot].empty()) return false;
        return VtDecode(g_vtSlots[slot], t);
    }

    bool SlotUsed(int slot)
    {
        if (slot < 1 || slot > VEHTUNER_SLOTS) return false;
        VtSlotsLoad();
        return !g_vtSlots[slot].empty();
    }

    std::string ExportCode(const VehTune& t)
    {
        std::string c = VtEncode(t);
        FILE* f = fopen(VtPath("vehtune_code.txt").c_str(), "w");
        if (f) { fprintf(f, "%s\n", c.c_str()); fclose(f); }
        return c;
    }

    bool ParseCode(const std::string& code, VehTune& t) { return VtDecode(code, t); }

    bool ImportCodeFile(VehTune& t)
    {
        FILE* f = fopen(VtPath("vehtune_code.txt").c_str(), "r");
        if (!f) return false;
        char buf[400];
        int ok = fscanf(f, "%399s", buf);
        fclose(f);
        if (ok != 1) return false;
        return VtDecode(buf, t);
    }
}
'''

def p_cpp(s):
    s = sub1(r'Near100\(t\.suspDampPct\)\s*&&\s*Near100\(t\.collisionPct\);',
             'Near100(t.suspDampPct) && Near100(t.collisionPct) &&\n'
             '               Near100(t.suspUpperPct) && Near100(t.suspLowerPct) && Near100(t.suspBiasPct) &&\n'
             '               Near100(t.antiDivePct) && Near100(t.tractBiasPct) && Near100(t.brakeBiasPct) &&\n'
             '               std::fabs(t.comZOffset) < 0.5f;', s)
    s = sub1(r'([ \t]*t\.collisionPct\s*=\s*Clamp\(t\.collisionPct,\s*10\.0f,\s*300\.0f\);[^\n]*\n)', lambda m: m.group(1) +
'''        t.suspUpperPct= Clamp(t.suspUpperPct,30.0f, 300.0f);
        t.suspLowerPct= Clamp(t.suspLowerPct,30.0f, 300.0f);
        t.suspBiasPct = Clamp(t.suspBiasPct, 50.0f, 150.0f);
        t.antiDivePct = Clamp(t.antiDivePct,  0.0f, 300.0f);
        t.tractBiasPct= Clamp(t.tractBiasPct,50.0f, 150.0f);
        t.brakeBiasPct= Clamp(t.brakeBiasPct,50.0f, 150.0f);
        t.comZOffset  = Clamp(t.comZOffset, -50.0f,  50.0f);
''', s)
    s = sub1(r'([ \t]*e\.copy->m_fCollisionDamageMultiplier\s*=\s*e\.orig->m_fCollisionDamageMultiplier\s*\*\s*\(t\.collisionPct\s*\*\s*0\.01f\);[^\n]*\n)', lambda m: m.group(1) +
'''
        // TG_PATCH10: asma sinirlari, balanslar, agirliq merkezi (hamisi ORIGINAL-dan hesablanir)
        e.copy->m_fSuspensionUpperLimit = e.orig->m_fSuspensionUpperLimit * (t.suspUpperPct * 0.01f);
        e.copy->m_fSuspensionLowerLimit = e.orig->m_fSuspensionLowerLimit * (t.suspLowerPct * 0.01f);
        e.copy->m_fSuspensionBiasBetweenFrontAndRear =
            Clamp(e.orig->m_fSuspensionBiasBetweenFrontAndRear * (t.suspBiasPct * 0.01f), 0.05f, 0.95f);
        e.copy->m_fSuspensionAntiDiveMultiplier = e.orig->m_fSuspensionAntiDiveMultiplier * (t.antiDivePct * 0.01f);
        e.copy->m_fTractionBias = Clamp(e.orig->m_fTractionBias * (t.tractBiasPct * 0.01f), 0.05f, 0.95f);
        e.copy->m_fBrakeBias    = Clamp(e.orig->m_fBrakeBias    * (t.brakeBiasPct * 0.01f), 0.05f, 0.95f);
        e.copy->m_vecCentreOfMass.z = e.orig->m_vecCentreOfMass.z + t.comZOffset * 0.01f;
''', s)
    return s.rstrip("\n") + "\n" + CPP_TAIL

# ============================================================ gui.cpp
PAGES = '''static const char* kPages[10] = { "Nəzarət", "Mühərrik", "Sürücülük", "Asma", "Far", "Komandalar" , "Nömrə", "Spidometr", "Balans", "Slotlar" };'''
SUBS = '''static const char* kSubs[10]  = { "Mühərrik, fara, qapılar və sinyal", "Sürətlənmə, sürət və çəki", "Tormoz, manevr və yol tutuş", "Asma, hədlər və toqquşma", "Far rəngi", "Bütün komandaların siyahısı" , "Azərbaycan nömrə lövhəsi (10-AA-123)", "Spidometr stilini və rəngini seç", "Ön/arxa balans və ağırlıq mərkəzi", "Tuning slotları və handling kodu" };'''

SLOT_PAGE = r'''else if (s_page == 9)    // SLOTLAR / KOD (TG_PATCH10)
                {
                        auto Btn = [&](int id, const Rc& r, const char* label) -> bool {
                                const bool hit = Tap(id, r, canInput);
                                const float pr = Prs(id, dt);
                                const Rc rd = Shrink(r, S(1.5f) * pr);
                                Fill(rd, Accent(0.0f, 24 + (int)(36.0f * pr)), S(9.0f));
                                Stroke(rd, Accent(0.0f, 170), S(9.0f), S(1.3f));
                                TxtC(S(15.0f), rd, Col(232, 238, 250), label, true);
                                return hit;
                        };

                        for (int i = 0; i < VehTuner::VEHTUNER_SLOTS; ++i)
                        {
                                const Rc row{ cx0, P.y + S(124.0f + 60.0f * i), cw, S(52.0f) };
                                Fill(row, Col(27, 30, 40, 255), S(10.0f));
                                char lb[32];
                                snprintf(lb, sizeof(lb), "Slot %d", i + 1);
                                Txt(S(17.0f), row.x + S(16.0f), row.y + S(8.0f), Col(214, 220, 232), lb);
                                const bool used = VehTuner::SlotUsed(i + 1);
                                Txt(S(13.0f), row.x + S(16.0f), row.y + S(29.0f),
                                    used ? Col(120, 215, 160) : Col(124, 132, 152), used ? "Dolu" : "Boş");

                                const Rc bSave{ row.x + row.w - S(256.0f), row.y + S(6.0f), S(116.0f), S(40.0f) };
                                const Rc bLoad{ row.x + row.w - S(130.0f), row.y + S(6.0f), S(116.0f), S(40.0f) };

                                if (Btn(80 + i * 2, bSave, "Yadda saxla") && gtaVeh) {
                                        VehTuner::SaveSlot(i + 1, s_tune);
                                        if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[MM]: Slot %d yadda saxlanildi.", i + 1);
                                }
                                if (Btn(81 + i * 2, bLoad, "Yüklə") && gtaVeh) {
                                        VehTune lt = s_tune;
                                        if (VehTuner::LoadSlot(i + 1, lt)) {
                                                s_tune = lt;
                                                changed = true;
                                                for (int k = 0; k < 8; ++k) s_frac[k] = 0.0f;
                                                if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[MM]: Slot %d yuklendi.", i + 1);
                                        } else if (pUI && pUI->chat()) {
                                                pUI->chat()->addDebugMessage("{FF0000}[MM]: Slot %d bosdur.", i + 1);
                                        }
                                }
                        }

                        const Rc bCode{ cx0, P.y + S(432.0f), S(300.0f), S(44.0f) };
                        const Rc bImp { cx0 + S(316.0f), P.y + S(432.0f), S(300.0f), S(44.0f) };
                        if (Btn(90, bCode, "Handling kodunu göstər") && gtaVeh) {
                                const std::string code = VehTuner::ExportCode(s_tune);
                                if (pUI && pUI->chat()) {
                                        pUI->chat()->addDebugMessage("{00FF00}[MM]: Handling kodu (SAMP/vehtune_code.txt-ye de yazildi):");
                                        pUI->chat()->addDebugMessage("{B9C9BF}%s", code.c_str());
                                }
                        }
                        if (Btn(91, bImp, "Koddan yüklə (fayl)") && gtaVeh) {
                                VehTune lt = s_tune;
                                if (VehTuner::ImportCodeFile(lt)) {
                                        s_tune = lt;
                                        changed = true;
                                        for (int k = 0; k < 8; ++k) s_frac[k] = 0.0f;
                                        if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[MM]: Kod tetbiq olundu.");
                                } else if (pUI && pUI->chat()) {
                                        pUI->chat()->addDebugMessage("{FF0000}[MM]: SAMP/vehtune_code.txt tapilmadi ve ya kod yanlisdir.");
                                }
                        }
                        Txt(S(13.0f), cx0, P.y + S(486.0f), Col(124, 132, 152),
                            "Kodu başqa maşına tətbiq etmək: kodu SAMP/vehtune_code.txt faylına yaz, sonra \"Koddan yüklə\".");
                }
                '''

def p_gui(s):
    s = sub1(r'static const char\* kPages\[8\] = \{[^;]*\};', PAGES, s)
    s = sub1(r'static const char\* kSubs\[8\]\s*= \{[^;]*\};', SUBS, s)
    s = sub1(r'74\.0f \+ 56\.0f \* s_page', '74.0f + 44.0f * s_page', s)
    s = sub1(r'(const Rc ind\{ P\.x \+ S\(16\.0f\), P\.y \+ S\(s_navRel\), S\(166\.0f\), )S\(48\.0f\)( \};)', lambda m: m.group(1) + 'S(40.0f)' + m.group(2), s)
    s = sub1(r'Rc\{ ind\.x, ind\.y \+ S\(12\.0f\), S\(3\.0f\), S\(24\.0f\) \}', 'Rc{ ind.x, ind.y + S(10.0f), S(3.0f), S(20.0f) }', s)
    s = sub1(r'for \(int i = 0; i < 8; \+\+i\)(\s*\{\s*const Rc it\{ P\.x \+ S\(16\.0f\), P\.y \+ S\()74\.0f \+ 56\.0f \* i(\), S\(166\.0f\), )S\(48\.0f\)',
             lambda m: 'for (int i = 0; i < 10; ++i)' + m.group(1) + '74.0f + 44.0f * i' + m.group(2) + 'S(40.0f)', s)
    n = s.count('(s_page <= 4 || s_page == 6)')
    if n != 2: raise ValueError("noVeh sertleri (%d)" % n)
    s = s.replace('(s_page <= 4 || s_page == 6)', '(s_page <= 4 || s_page == 6 || s_page >= 8)')
    s = sub1(r'if \(s_page >= 1 && s_page <= 3\)\s*\n', lambda m: 'if ((s_page >= 1 && s_page <= 3) || s_page == 8)\n', s)
    # asma sehifesi: 2 yeni slider + yeni "Balans" sehifesi
    s = sub1(r'\} else \{(\s*\n\s*sp\[n\+\+\] = \{ "Asma gücü")', lambda m: '} else if (s_page == 3) {' + m.group(1), s)
    s = sub1(r'([ \t]*)sp\[n\+\+\] = \{ "Toqquşma zərəri",[^\n]*\n([ \t]*)\}',
             lambda m: m.group(0)[:-len(m.group(2)) - 1] +
             m.group(1) + 'sp[n++] = { "Asma yuxarı hədd (hündürlük)",   &s_tune.suspUpperPct, 30.0f, 300.0f };\n' +
             m.group(1) + 'sp[n++] = { "Asma aşağı hədd (yol aralığı)",  &s_tune.suspLowerPct, 30.0f, 300.0f };\n' +
             m.group(2) + '} else if (s_page == 8) {\n' +
             m.group(1) + 'sp[n++] = { "Asma balansı (ön/arxa)",        &s_tune.suspBiasPct,  50.0f, 150.0f };\n' +
             m.group(1) + 'sp[n++] = { "Anti-dive (burun enməsi)",        &s_tune.antiDivePct,   0.0f, 300.0f };\n' +
             m.group(1) + 'sp[n++] = { "Yol tutuş balansı (ön/arxa)",   &s_tune.tractBiasPct, 50.0f, 150.0f };\n' +
             m.group(1) + 'sp[n++] = { "Tormoz balansı (ön/arxa)",      &s_tune.brakeBiasPct, 50.0f, 150.0f };\n' +
             m.group(1) + 'sp[n++] = { "Ağırlıq mərkəzi hündürlüyü",      &s_tune.comZOffset,  -50.0f,  50.0f };\n' +
             m.group(2) + '}', s)
    s = sub1(r'if \(SliderRow\(i, row, sp\[i\]\.label, sp\[i\]\.v, sp\[i\]\.mn, sp\[i\]\.mx, dt, canInput\)\) changed = true;',
             'if (SliderRow(i, row, sp[i].label, sp[i].v, sp[i].mn, sp[i].mx, dt, canInput,\n'
             '                                      (sp[i].v == &s_tune.comZOffset) ? " sm" : "%", (sp[i].v == &s_tune.comZOffset) ? 1.0f : 5.0f)) changed = true;', s)
    s = sub1(r'(\n[ \t]*)else[ \t]+// HANDLING SEHIFELERI', lambda m: m.group(1) + SLOT_PAGE.rstrip(' ') + m.group(1).lstrip('\n') + 'else                      // HANDLING SEHIFELERI', s)
    # /tg ile acilanda sehife secimi
    s = sub1(r'([ \t]*)(const bool want = g_bShowHandlingDlg;[^\n]*\n)', lambda m: m.group(1) + m.group(2) +
             m.group(1) + 'if (want && g_mmOpenPage >= 0 && g_mmOpenPage < 10) { s_page = g_mmOpenPage; s_pageAnim = 0.0f; }   // TG_PATCH10\n' +
             m.group(1) + 'if (want) g_mmOpenPage = -1;\n', s)
    s = sub1(r'(\nvoid RenderHandlingDialog\(\))', lambda m: '\nint g_mmOpenPage = -1;   // TG_PATCH10: /tg ucun acilis sehifesi\n' + m.group(1), s)
    return s

# ============================================================ chat.cpp
def p_chat(s):
    m1 = re.search(r'^bool g_bShowHandlingDlg\s*=\s*false;[^\n]*\n', s, re.M)
    m2 = re.search(r'^([ \t]*)if \(cmd == "/mm"\) \{', s, re.M)
    if not m1 or not m2: raise ValueError('/mm ankeri')
    cmd = (m2.group(1) + 'if (cmd == "/tg") {   // TG_PATCH10: tuning menyusu\n'
           + m2.group(1) + '        if (g_bShowHandlingDlg) { g_bShowHandlingDlg = false; }\n'
           + m2.group(1) + '        else { g_mmOpenPage = 1; g_bShowHandlingDlg = true; }\n'
           + m2.group(1) + '        return true;\n'
           + m2.group(1) + '}\n\n')
    s = s[:m2.start()] + cmd + s[m2.start():]
    s = s[:m1.end()] + 'extern int g_mmOpenPage;   // TG_PATCH10 (gui.cpp)\n' + s[m1.end():]
    # patch8-in /h* komandalarini sondur (VehTuner ile toqquşmasin)
    s = re.sub(r'if \(cmd == "/hlist" \|\| (.*?)cmd == "/hcfg"\) \{', lambda m: 'if (false && (cmd == "/hlist" || ' + m.group(1) + 'cmd == "/hcfg")) {', s, count=1, flags=re.S) \
        if ('HNDEXT_PATCH8' in s and 'if (false && (cmd == "/hlist"' not in s) else s
    return s

run("VehTuner.h: yeni sahələr + slot/kod elanları", find_all("vehtuner.h"), p_header)
run("VehTuner.cpp: yeni handling sahələri + slot/kod", find_all("vehtuner.cpp"), p_cpp)
run("gui.cpp: yeni səhifələr (Asma+, Balans, Slotlar)", [os.path.join(SRC, "samp/gui/gui.cpp")] if os.path.exists(os.path.join(SRC, "samp/gui/gui.cpp")) else [], p_gui)
run("chat.cpp: /tg komandası", [os.path.join(SRC, "samp/gui/samp_widgets/chat.cpp")] if os.path.exists(os.path.join(SRC, "samp/gui/samp_widgets/chat.cpp")) else [], p_chat)
print("\nBitdi. Geri qaytarmaq ucun her faylin yaninda *.bak10 var.")

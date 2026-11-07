#!/usr/bin/env python3
# apply_patch_all.py -- patch3 (font/nomre/handling/sinyal) + patch4 (/cam kamera). Layihe kokunden: python3 apply_patch_all.py
# apply_patch3.py  --  layihe kokunden ise sal:  python3 apply_patch3.py
# Edir: (1) E/e fallback shrift, (2) sinyal stale-pointer fix, (3) avtomatik AZ nomre lovhesi,
#       (4) handling: +6 slider ve yeni "Balans" sehifesi.
# Her fayl ucun .bak4 ehtiyat nusxesi yaradilir. Anchor tapilmazsa HEMIN fayl dayisilmir.
import re, sys
from pathlib import Path

ROOT = Path("app/src/main/cpp/samp")
MARK = "AZ_PATCH3"


def ws(s):
    return r"\s+".join(re.escape(t) for t in s.split())


def apply(rel, edits):
    p = ROOT / rel
    if not p.exists():
        print("[YOX ]", rel)
        return False
    t = p.read_text(encoding="utf-8")
    if MARK in t:
        print("[ATLA]", rel, "(artiq tetbiq olunub)")
        return True
    for i, (kind, old, new) in enumerate(edits):
        pat = ws(old) if kind == "lit" else old
        t, n = re.subn(pat, lambda m: new, t, flags=re.S)
        if n != 1:
            print("[XETA]", rel, "-> edit #%d tapilmadi/ikiqat (n=%d). Fayl DEYISILMEDI." % (i + 1, n))
            return False
    if MARK not in t:
        t += "\n// " + MARK + "\n"
    (p.parent / (p.name + ".bak4")).write_text(p.read_text(encoding="utf-8"), encoding="utf-8")
    p.write_text(t, encoding="utf-8")
    print("[OK  ]", rel)
    return True


print("--- Hisse 1: font / sinyal / nomre / handling ---")
ok = True

# ---------------------------------------------------------------- 1) SHRIFT (E / e)
FONT_BLOCK = r'''// ---- AZ_FALLBACK_FONT v2 (AZ_PATCH3) ----
	// Ə (U+018F) / ə (U+0259): sistem fontlarinin HAMISINDAN (4-e qeder) birlesdirilir.
	// Glyph olmayan font sadece kecilir, oldugu font ise doldurur.
	{
		static const ImWchar azRanges[] = { 0x018F, 0x018F, 0x0259, 0x0259, 0 };
		std::vector<std::string> cands;
		static const char* kFixed[] = {
			"/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf",
			"/system/fonts/NotoSans-Regular.ttf", "/system/fonts/DroidSans.ttf",
			"/system/fonts/DejaVuSans.ttf", "/system/fonts/Arial.ttf", nullptr
		};
		for (int i = 0; kFixed[i]; i++) cands.push_back(kFixed[i]);

		if (DIR* d = opendir("/system/fonts"))
		{
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n.size() < 5) continue;
				std::string ext = n.substr(n.size() - 4);
				if (ext != ".ttf" && ext != ".otf") continue;
				if (n.find("Roboto") == std::string::npos && n.find("NotoSans-") == std::string::npos &&
					n.find("DejaVu") == std::string::npos) continue;
				if (n.find("Mono") != std::string::npos || n.find("Condensed") != std::string::npos ||
					n.find("Italic") != std::string::npos) continue;
				cands.push_back("/system/fonts/" + n);
			}
			closedir(d);
		}

		std::vector<std::string> done;
		int merged = 0;
		for (const std::string& path : cands)
		{
			if (merged >= 4) break;
			if (std::find(done.begin(), done.end(), path) != done.end()) continue;
			done.push_back(path);
			if (access(path.c_str(), R_OK) != 0) continue;

			ImFontConfig azCfg;
			azCfg.MergeMode = true;
			azCfg.PixelSnapH = true;
			azCfg.GlyphExtraSpacing.x = UISettings::outlineSize();

			if (io.Fonts->AddFontFromFileTTF(path.c_str(), UISettings::fontSize(), &azCfg, azRanges))
			{
				Log::addParameter("AZ fallback font: %s", path.c_str());
				merged++;
			}
		}
	}
'''

ok &= apply("gui/imguiwrapper.cpp", [
    ("lit", "#include <unistd.h>",
     "#include <unistd.h>\n#include <dirent.h>\n#include <vector>\n#include <string>\n#include <algorithm>"),
    ("re", r"// ---- AZ_FALLBACK_FONT.*?(?=\n\s*createFontTexture\(\);)", FONT_BLOCK),
])

# ---------------------------------------------------------------- 2) SINYAL (kohne pointer)
ok &= apply("game/Entity/CVehicleGTA.cpp", [
    ("lit", "static std::unordered_map<CVehicleGTA*, uint8_t> s_signals;",
     "static std::unordered_map<CVehicleGTA*, uint8_t> s_signals;\n"
     "    static std::unordered_map<CVehicleGTA*, uint32_t> s_sigCt;   // AZ_PATCH3: masinin yaranma vaxti\n"
     "    // Eyni adresde BASQA masin yaranibsa kohne sinyali at\n"
     "    static void Prune(CVehicleGTA* veh)\n"
     "    {\n"
     "        auto c = s_sigCt.find(veh);\n"
     "        if (c != s_sigCt.end() && c->second != (uint32_t)veh->m_nCreationTime) {\n"
     "            s_signals.erase(veh);\n"
     "            s_sigCt.erase(c);\n"
     "        }\n"
     "    }"),
    ("lit", "uint8_t GetSignal(CVehicleGTA* veh) { auto it = s_signals.find(veh);",
     "uint8_t GetSignal(CVehicleGTA* veh) { if (veh) Prune(veh); auto it = s_signals.find(veh);"),
    ("lit", "if (mode == 0) s_signals.erase(veh); else s_signals[veh] = mode;",
     "if (mode == 0) { s_signals.erase(veh); s_sigCt.erase(veh); }\n"
     "        else { s_signals[veh] = mode; s_sigCt[veh] = (uint32_t)veh->m_nCreationTime; }"),
    ("lit", "void Forget(CVehicleGTA* veh) { s_signals.erase(veh); }",
     "void Forget(CVehicleGTA* veh) { s_signals.erase(veh); s_sigCt.erase(veh); }"),
    ("lit", "void ApplyRenderLightBits(CVehicleGTA* veh) { if (s_signals.empty() || !veh) return;",
     "void ApplyRenderLightBits(CVehicleGTA* veh) { if (!veh) return; Prune(veh); if (s_signals.empty()) return;"),
    ("lit", "void RenderSignals(CVehicleGTA* veh) { if (s_signals.empty() || !veh) return;",
     "void RenderSignals(CVehicleGTA* veh) { if (!veh) return; Prune(veh); if (s_signals.empty()) return;"),
])

# ---------------------------------------------------------------- 3) NOMRE (avtomatik)
BEFORE_NEW = r'''std::unordered_map<CVehicleGTA*, uint32_t> g_checked;   // AZ_PATCH3: lovhe yoxlanilmis masinlar

    // Masindan asili, hemise eyni olan "ozu yaradilan" AZ nomre: 10-AA-123
    std::string AutoText(CVehicleGTA* v)
    {
        uint32_t h = (uint32_t)v->m_nCreationTime * 2654435761u ^ ((uint32_t)v->m_nModelIndex * 40503u);
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        static const int kReg[] = { 10, 90, 99, 77, 20, 25, 35, 50 };
        char b[16];
        snprintf(b, sizeof(b), "%02d-%c%c-%03d", kReg[h % 8], 'A' + (int)((h >> 4) % 26),
                 'A' + (int)((h >> 9) % 26), (int)((h >> 14) % 1000));
        return b;
    }

    void Before_hook(CVehicleGTA* thiz, void* /*modelInfo*/)
    {
        s_saved.clear();
        if (!thiz) return;

        RwTexture* tex = nullptr;
        bool needAuto = false;
        const uint32_t ct = (uint32_t)thiz->m_nCreationTime;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            auto it = g_entries.find(thiz);
            if (it != g_entries.end() && it->second.creationTime != thiz->m_nCreationTime) {
                g_entries.erase(it);
                it = g_entries.end();
            }
            if (it != g_entries.end()) {
                tex = it->second.tex;
            } else {
                auto c = g_checked.find(thiz);
                needAuto = (c == g_checked.end() || c->second != ct);
            }
        }

        if (needAuto)
        {
            // Bu masin ilk defe render olunur: lovhe materiali varsa ozu nomre yarat
            std::vector<RpMaterial*> mats;
            CollectPlateMaterials(thiz, mats);
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_checked.size() > 1024) g_checked.clear();
            g_checked[thiz] = ct;
            if (!mats.empty() && g_entries.size() < 48) {
                Entry e;
                e.creationTime = thiz->m_nCreationTime;
                e.text  = AutoText(thiz);
                e.dirty = true;
                g_entries[thiz] = e;
            }
            return;
        }
        if (!tex) return;

        std::vector<RpMaterial*> mats;
        CollectPlateMaterials(thiz, mats);
        for (RpMaterial* m : mats) {
            s_saved.emplace_back(m, m->texture);
            m->texture = tex;
        }
    }
'''

ISPLATE_NEW = r'''bool IsPlateName(const char* n)
    {
        if (!n) return false;
        char b[32];
        size_t i = 0;
        for (; n[i] && i < 31; ++i) b[i] = (char)tolower((unsigned char)n[i]);
        b[i] = 0;
        // AZ_PATCH3: daha genis axtaris
        return strstr(b, "nomer") || strstr(b, "nomre") || strstr(b, "plate") ||
               strstr(b, "licen") || strstr(b, "numberp");
    }'''

ok &= apply("game/AzPlate.cpp", [
    ("re", r"bool IsPlateName\(const char\* n\)\s*\{.*?\n    \}", ISPLATE_NEW),
    ("re", r"void Before_hook\(CVehicleGTA\* thiz, void\* /\*modelInfo\*/\).*?(?=\n    void After_hook)", BEFORE_NEW),
    ("lit", "if (mats.empty()) return false;", "const bool hasMat = !mats.empty();"),
    ("lit", "e.dirty = true; // kohne tekstura yenisi hazir olana qeder qalir return true;",
     "e.dirty = true;       // kohne tekstura yenisi hazir olana qeder qalir\n        return hasMat;"),
    ("lit", "for (auto& kv : g_entries) { Entry& e = kv.second; if (!e.dirty) continue;",
     "int budget = 2;   // kadrda en cox 2 tekstura (50 masin birden donmasin)\n"
     "        for (auto& kv : g_entries) {\n            Entry& e = kv.second;\n"
     "            if (!e.dirty) continue;\n            if (budget-- <= 0) break;"),
])

# ---------------------------------------------------------------- 4) HANDLING genisletme
ok &= apply("game/VehTuner.h", [
    ("lit", "// --- Far ---",
     "// --- Elave (AZ_PATCH3) ---\n"
     "    float   suspUpperPct    = 100.0f;  // asma yuxari hedd           (50 .. 200)\n"
     "    float   suspLowerPct    = 100.0f;  // asma asagi hedd            (50 .. 200)\n"
     "    float   brakeBiasPct    = 100.0f;  // tormoz on/arxa paylanmasi  (50 .. 150)\n"
     "    float   tractionBiasPct = 100.0f;  // yapisma on/arxa paylanmasi (50 .. 150)\n"
     "    float   suspBiasPct     = 100.0f;  // asma on/arxa balansi       (50 .. 150)\n"
     "    float   antiDivePct     = 100.0f;  // anti-dive (basi enmeye qarsi) (0 .. 300)\n"
     "    // --- Far ---"),
])

ok &= apply("game/VehTuner.cpp", [
    ("lit", "Near100(t.suspDampPct) && Near100(t.collisionPct);",
     "Near100(t.suspDampPct) && Near100(t.collisionPct) &&\n"
     "               Near100(t.suspUpperPct) && Near100(t.suspLowerPct) && Near100(t.brakeBiasPct) &&\n"
     "               Near100(t.tractionBiasPct) && Near100(t.suspBiasPct) && Near100(t.antiDivePct); // AZ_PATCH3"),
    ("lit", "t.collisionPct= Clamp(t.collisionPct,10.0f, 300.0f);",
     "t.collisionPct= Clamp(t.collisionPct,10.0f, 300.0f);\n"
     "        t.suspUpperPct    = Clamp(t.suspUpperPct,    50.0f, 200.0f);   // AZ_PATCH3\n"
     "        t.suspLowerPct    = Clamp(t.suspLowerPct,    50.0f, 200.0f);\n"
     "        t.brakeBiasPct    = Clamp(t.brakeBiasPct,    50.0f, 150.0f);\n"
     "        t.tractionBiasPct = Clamp(t.tractionBiasPct, 50.0f, 150.0f);\n"
     "        t.suspBiasPct     = Clamp(t.suspBiasPct,     50.0f, 150.0f);\n"
     "        t.antiDivePct     = Clamp(t.antiDivePct,      0.0f, 300.0f);"),
    ("lit",
     "e.copy->m_fCollisionDamageMultiplier = e.orig->m_fCollisionDamageMultiplier * (t.collisionPct * 0.01f);",
     "e.copy->m_fCollisionDamageMultiplier = e.orig->m_fCollisionDamageMultiplier * (t.collisionPct * 0.01f);\n"
     "\n"
     "        // AZ_PATCH3: elave sahəler (hamisi ORIGINAL-dan hesablanir)\n"
     "        e.copy->m_fSuspensionUpperLimit = e.orig->m_fSuspensionUpperLimit * (t.suspUpperPct * 0.01f);\n"
     "        e.copy->m_fSuspensionLowerLimit = e.orig->m_fSuspensionLowerLimit * (t.suspLowerPct * 0.01f);\n"
     "        e.copy->m_fBrakeBias    = Clamp(e.orig->m_fBrakeBias    * (t.brakeBiasPct    * 0.01f), 0.02f, 0.98f);\n"
     "        e.copy->m_fTractionBias = Clamp(e.orig->m_fTractionBias * (t.tractionBiasPct * 0.01f), 0.02f, 0.98f);\n"
     "        e.copy->m_fSuspensionBiasBetweenFrontAndRear =\n"
     "            Clamp(e.orig->m_fSuspensionBiasBetweenFrontAndRear * (t.suspBiasPct * 0.01f), 0.05f, 0.95f);\n"
     "        e.copy->m_fSuspensionAntiDiveMultiplier = e.orig->m_fSuspensionAntiDiveMultiplier * (t.antiDivePct * 0.01f);"),
])

PAGES = ('static const char* kPages[8] = { "Nəzarət", "Mühərrik", "Sürücülük", "Asma", "Far", '
         '"Komandalar", "Nömrə", "Balans" }; // AZ_PATCH3')
SUBS = ('static const char* kSubs[8]  = { "Mühərrik, fara, qapılar və sinyal", "Sürətlənmə, sürət və çəki", '
        '"Tormoz, manevr və yol tutuş", "Asma və toqquşma", "Far rəngi", "Bütün komandaların siyahısı", '
        '"Azərbaycan nömrə lövhəsi (10-AA-123)", "Tormoz/yapışma paylanması, asma balansı" };')

ok &= apply("gui/gui.cpp", [
    ("re", r"static const char\* kPages\[7\]\s*=\s*\{[^}]*\};", PAGES),
    ("re", r"static const char\* kSubs\[7\]\s*=\s*\{[^}]*\};", SUBS),
    ("lit", "for (int i = 0; i < 7; ++i)", "for (int i = 0; i < 8; ++i)"),
    ("lit", "56.0f * s_page", "50.0f * s_page"),
    ("lit", "56.0f * i)", "50.0f * i)"),
    ("lit", 'sp[n++] = { "Asma gücü", &s_tune.suspForcePct, 30.0f, 300.0f };',
     'if (s_page == 7) {\n'
     '                                sp[n++] = { "Tormoz paylanması (ön/arxa)",  &s_tune.brakeBiasPct,    50.0f, 150.0f };\n'
     '                                sp[n++] = { "Yapışma paylanması (ön/arxa)", &s_tune.tractionBiasPct, 50.0f, 150.0f };\n'
     '                                sp[n++] = { "Asma balansı (ön/arxa)",       &s_tune.suspBiasPct,     50.0f, 150.0f };\n'
     '                                sp[n++] = { "Anti-dive (baş enməsi)",       &s_tune.antiDivePct,      0.0f, 300.0f };\n'
     '                        } else {\n'
     '                                sp[n++] = { "Asma gücü", &s_tune.suspForcePct, 30.0f, 300.0f };'),
    ("lit", 'sp[n++] = { "Toqquşma zərəri", &s_tune.collisionPct, 10.0f, 300.0f };',
     'sp[n++] = { "Toqquşma zərəri", &s_tune.collisionPct, 10.0f, 300.0f };\n'
     '                                sp[n++] = { "Asma yuxarı hədd",  &s_tune.suspUpperPct, 50.0f, 200.0f };\n'
     '                                sp[n++] = { "Asma aşağı hədd",   &s_tune.suspLowerPct, 50.0f, 200.0f };\n'
     '                        }'),
    ("lit", "if (s_page >= 1 && s_page <= 3)", "if ((s_page >= 1 && s_page <= 3) || s_page == 7)"),
    ("lit", 'else snprintf(s_plMsg, sizeof(s_plMsg), "Bu maşında nömrə lövhəsi materialı tapılmadı");',
     'else snprintf(s_plMsg, sizeof(s_plMsg), "Bu modeldə lövhə yeri yoxdur (/dump -> vehdump.txt)");'),
])

ok3 = ok

print("\n--- Hisse 2: /cam kamera ---")

ROOT = Path("app/src/main/cpp/samp")
MARK = "AZ_PATCH4"


def ws(s):
    return r"\s+".join(re.escape(t) for t in s.split())


def apply(rel, edits):
    p = ROOT / rel
    if not p.exists():
        print("[YOX ]", rel)
        return False
    t = p.read_text(encoding="utf-8")
    if MARK in t:
        print("[ATLA]", rel, "(artiq tetbiq olunub)")
        return True
    for i, (old, new) in enumerate(edits):
        t, n = re.subn(ws(old), lambda m: new, t)
        if n != 1:
            print("[XETA]", rel, "-> edit #%d tapilmadi/ikiqat (n=%d). Fayl DEYISILMEDI." % (i + 1, n))
            return False
    t += "\n// " + MARK + "\n"
    (p.parent / (p.name + ".bak5")).write_text(p.read_text(encoding="utf-8"), encoding="utf-8")
    p.write_text(t, encoding="utf-8")
    print("[OK  ]", rel)
    return True


CAM_BLOCK = r'''extern UI* pUI;

// ============================================================================
//  AZ_PATCH4: /cam  --  PC-stili kamera (mouse-look-un mobil versiyasi) + birinci shexs
//   /cam            : off -> pc -> fps -> off  (novbeli rejim)
//   /cam pc|fps|off : birbasa secim
//   /cam sens N     : hessaslig (1..20, standart 5)
//  Ekranin SAG terefinde (x>35%, y<72%) barmaqla surusdurende kamera donur.
//  Kamera SA-MP-nin oz script emrleri ile (set_camera_position + point_camera) her kadr yerlesdirilir.
// ============================================================================
namespace CamCtl
{
        static int   s_mode   = 0;        // 0 = oyunun kamerasi, 1 = PC (3-cu shexs), 2 = birinci shexs
        static bool  s_active = false;
        static float s_yaw    = 0.0f;
        static float s_pitch  = -0.25f;
        static float s_sens   = 0.0045f;  // radian / piksel
        static int   s_ptr    = -1;
        static float s_lx = 0.0f, s_ly = 0.0f;
        static float s_dist   = 4.5f;

        static void ClampPitch()
        {
                const float lo = (s_mode == 2) ? -1.4f : -1.3f;
                const float hi = (s_mode == 2) ?  1.4f :  0.12f;   // 3-cu shexsde yuxari baxanda kamera yerin altina dusmesin
                if (s_pitch < lo) s_pitch = lo;
                if (s_pitch > hi) s_pitch = hi;
        }
}

void CamCtl_OnTouch(int type, int num, int x, int y)
{
        using namespace CamCtl;
        if (s_mode == 0) { s_ptr = -1; return; }

        const ImVec2 d = ImGui::GetIO().DisplaySize;
        const bool inZone = (float)x > d.x * 0.35f && (float)y < d.y * 0.72f;

        if (type == 2) {                         // push
                if (s_ptr < 0 && inZone) { s_ptr = num; s_lx = (float)x; s_ly = (float)y; }
        }
        else if (type == 3) {                    // move
                if (s_ptr < 0 && inZone) { s_ptr = num; s_lx = (float)x; s_ly = (float)y; return; }
                if (num == s_ptr) {
                        const float dx = (float)x - s_lx, dy = (float)y - s_ly;
                        s_lx = (float)x; s_ly = (float)y;
                        s_yaw   -= dx * s_sens;   // sağa surusdur -> sağa bax
                        s_pitch -= dy * s_sens;   // asagi surusdur -> asagi bax (mouse kimi)
                        ClampPitch();
                }
        }
        else if (type == 1) {                    // pop
                if (num == s_ptr) s_ptr = -1;
        }
}

void CamCtl_Update()
{
        using namespace CamCtl;

        if (s_mode == 0) {
                if (s_active) { s_active = false; s_ptr = -1; CCamera::SetBehindPlayer(); }
                return;
        }
        if (!pGame || !pNetGame || pGame->IsGamePaused()) return;

        CPlayerPed* ped = pGame->FindPlayerPed();
        if (!ped || !ped->m_pPed) return;

        RwMatrix m = ped->m_pPed->GetMatrix().ToRwMatrix();
        if (!s_active) {
                s_active = true;
                s_yaw = atan2f(m.up.y, m.up.x);               // oyuncunun baxdigi istiqamet
                s_pitch = (s_mode == 2) ? 0.0f : -0.25f;
        }
        ClampPitch();

        const float cp = cosf(s_pitch), sp = sinf(s_pitch);
        const float dx = cosf(s_yaw) * cp, dy = sinf(s_yaw) * cp, dz = sp;
        const float px = m.pos.x, py = m.pos.y, pz = m.pos.z;

        float cx, cy, cz, lx, ly, lz;
        if (s_mode == 2) {                                    // birinci shexs
                cx = px + cosf(s_yaw) * 0.15f;
                cy = py + sinf(s_yaw) * 0.15f;
                cz = pz + 0.68f;
                lx = cx + dx * 20.0f; ly = cy + dy * 20.0f; lz = cz + dz * 20.0f;
        } else {                                              // PC / 3-cu shexs
                const float dist = ped->IsInVehicle() ? 7.0f : s_dist;
                const float tz = pz + 0.8f;
                cx = px - dx * dist; cy = py - dy * dist; cz = tz - dz * dist;
                lx = px; ly = py; lz = tz;
        }

        ScriptCommand(&set_camera_position, cx, cy, cz, 0.0f, 0.0f, 0.0f);
        ScriptCommand(&point_camera, lx, ly, lz, 2);
}

// chat.cpp -> /cam
void CamCtl_Command(const char* a, const char* b, char* rep, int n)
{
        using namespace CamCtl;
        std::string s = a ? a : "";
        for (size_t i = 0; i < s.size(); ++i) s[i] = (char)tolower((unsigned char)s[i]);

        if (s.empty())            s_mode = (s_mode + 1) % 3;
        else if (s == "pc")       s_mode = 1;
        else if (s == "fps" || s == "1") s_mode = 2;
        else if (s == "off")      s_mode = 0;
        else if (s == "sens") {
                const float v = b ? (float)atof(b) : 0.0f;
                if (v >= 0.5f && v <= 20.0f) { s_sens = v * 0.0009f; snprintf(rep, n, "Hessaslig: %.1f", v); }
                else snprintf(rep, n, "Istifade: /cam sens 1..20");
                return;
        }
        else { snprintf(rep, n, "Istifade: /cam [pc|fps|off|sens N]"); return; }

        s_active = false;   // novbeti kadrda yeniden baslat
        ClampPitch();
        const char* names[3] = { "oyunun kamerasi (off)", "PC rejimi (sag terefde surusdur)", "birinci shexs" };
        snprintf(rep, n, "Kamera: %s", names[s_mode]);
}
'''

ok = True

ok &= apply("gui/gui.cpp", [
    ('#include "../game/AzPlate.h"',
     '#include "../game/AzPlate.h"\n#include "../game/Camera.h"\n#include "../game/scripting.h"\n'
     '#include <string>\n#include <cstdlib>\n#include <cstring>'),
    ("extern UI* pUI;", CAM_BLOCK),
    ("AzPlate::ProcessPending();", "AzPlate::ProcessPending();\n        CamCtl_Update();            // AZ_PATCH4: /cam"),
])

ok &= apply("game/hooks.cpp", [
    ("void (*AND_TouchEvent)(int type, int num, int posX, int posY);",
     "void CamCtl_OnTouch(int type, int num, int x, int y);   // AZ_PATCH4 (gui.cpp)\n"
     "void (*AND_TouchEvent)(int type, int num, int posX, int posY);"),
    ("if (pGame->IsGameInputEnabled()) AND_TouchEvent(type, num, posX, posY);",
     "CamCtl_OnTouch(type, num, posX, posY);   // AZ_PATCH4: UI qebul etmeyen toxunuslar\n"
     "\tif (pGame->IsGameInputEnabled())\n\t\tAND_TouchEvent(type, num, posX, posY);"),
])

ok &= apply("gui/samp_widgets/chat.cpp", [
    ("bool g_bShowHandlingDlg = false;",
     "void CamCtl_Command(const char* a, const char* b, char* rep, int n);   // AZ_PATCH4 (gui.cpp)\n"
     "bool g_bShowHandlingDlg = false;"),
    ('if (cmd == "/gizle") {',
     'if (cmd == "/cam") {\n'
     '                std::string ca, cb;\n'
     '                ss >> ca >> cb;\n'
     '                char rep[96];\n'
     '                CamCtl_Command(ca.c_str(), cb.c_str(), rep, sizeof(rep));\n'
     '                pUI->chat()->addDebugMessage("{00FF00}[Client]: %s", rep);\n'
     '                return true;\n'
     '        }\n\n'
     '        if (cmd == "/gizle") {'),
])

print()
all_ok = ok3 and ok
print("HAMISI HAZIRDIR." if all_ok else "BEZI FAYLLAR DEYISILMEDI -- [XETA]/[YOX] setirlerini gonder.")
sys.exit(0 if all_ok else 1)

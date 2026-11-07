#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_patch4.py  --  repo-nun KOK qovluguna qoy:   python3 apply_patch4.py [--dry]

Edir:
  1) imguiwrapper.cpp : Ə / ə ucun sistem fontundan ehtiyat glyph (esas fontda yoxdursa '?' cixmasin)
  2) AzPlate.cpp      : lovhe materialini tapma 4 pillede (oyunun oz m_pPlateMaterial, deqiq ad,
                        ehtiyat ad, en/hundurluk nisbeti). Dublikat material bugu da duzeldilir.
  3) gui.cpp          : 2 yeni spidometr (Sport zolaq, Neon qovs) + /mm -> "Spidometr" sehifesi
                        (secim yadda qalir: <storage>/hudstyle.txt). Nomre tapilmayanda vehdump.txt yazilir.

Hec nese yazilmir, evvel hamisi yaddasda hazirlanir; bir anchor tapilmasa HEC BIR fayl deyismir.
Yedekler: .patchbak4/ qovluguna (git-e girmesin deye .gitignore-a elave olunur).
"""
import os, re, sys, shutil

CPP = "app/src/main/cpp/samp"
DRY = "--dry" in sys.argv


class PatchError(Exception):
    pass


def rd(path):
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def wr(path, text):
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def flex(anchor):
    toks = anchor.split()
    return re.compile(r"\s+".join(re.escape(t) for t in toks))


def sub1(text, anchor, repl, what):
    ms = list(flex(anchor).finditer(text))
    if len(ms) != 1:
        raise PatchError("%s: anchor %d dəfə tapıldı (1 olmalı): %s" % (what, len(ms), anchor[:60]))
    m = ms[0]
    return text[:m.start()] + repl + text[m.end():]


def insert_before(text, anchor, block, what):
    ms = list(flex(anchor).finditer(text))
    if len(ms) != 1:
        raise PatchError("%s: anchor %d dəfə tapıldı (1 olmalı): %s" % (what, len(ms), anchor[:60]))
    m = ms[0]
    return text[:m.start()] + block + text[m.start():]


def replace_function(text, signature, new_code, what):
    ms = list(flex(signature).finditer(text))
    if len(ms) != 1:
        raise PatchError("%s: funksiya %d dəfə tapıldı: %s" % (what, len(ms), signature[:60]))
    start = ms[0].start()
    i = text.index("{", ms[0].end())
    depth = 0
    j = i
    while j < len(text):
        c = text[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    if depth != 0:
        raise PatchError("%s: funksiya sonu tapılmadı" % what)
    # evvelki setrin girintisini saxla
    ls = text.rfind("\n", 0, start) + 1
    indent = text[ls:start]
    if indent.strip():
        indent = ""
    return text[:start] + new_code.lstrip() + text[j + 1:]


# =====================================================================
# 1) imguiwrapper.cpp
# =====================================================================
FONT_BLOCK = r'''// ---- AZ_FALLBACK_FONT: Ə (U+018F) / ə (U+0259) ----
	// Esas fontda bu 2 herf yoxdursa ekranda '?' cixir. Sistem fontundan YALNIZ bu 2 glyph birlesdirilir.
	{
		static const char* kAzFallbacks[] = {
			"/system/fonts/Roboto-Regular.ttf",
			"/system/fonts/RobotoStatic-Regular.ttf",
			"/system/fonts/NotoSans-Regular.ttf",
			"/system/fonts/DroidSans.ttf",
			"/system/fonts/Arial.ttf",
			nullptr
		};
		static const ImWchar azRanges[] = { 0x018F, 0x018F, 0x0259, 0x0259, 0 };

		for (int i = 0; kAzFallbacks[i]; i++)
		{
			if (access(kAzFallbacks[i], R_OK) != 0) continue;

			ImFontConfig azCfg;
			azCfg.MergeMode = true;
			azCfg.PixelSnapH = true;
			azCfg.GlyphExtraSpacing.x = UISettings::outlineSize();

			if (io.Fonts->AddFontFromFileTTF(kAzFallbacks[i], UISettings::fontSize(), &azCfg, azRanges))
			{
				Log::addParameter("AZ fallback font: %s", kAzFallbacks[i]);
				break;
			}
		}
	}

	'''


def patch_imgui(t):
    if "AZ_FALLBACK_FONT" in t:
        return t, False
    t = sub1(t, '#include "uisettings.h"', '#include "uisettings.h"\n#include <unistd.h>', "imguiwrapper include")
    t = insert_before(t, "createFontTexture(); m_renderer = new ImGuiRenderer(", FONT_BLOCK, "imguiwrapper font")
    return t, True


# =====================================================================
# 2) AzPlate.cpp
# =====================================================================
PLATE_HELPERS = r'''// PLATE_MULTI_STRATEGY: 0 = deqiq adlar, 1 = ehtiyat adlar, 2 = en/hundurluk nisbeti (3:1 .. 8.5:1)
    int g_plateLevel = 0;

    bool SafeRead(uintptr_t addr, void* out, size_t n)
    {
        if (addr < 0x10000) return false;
        iovec l{ out, n };
        iovec r{ (void*)addr, n };
        return process_vm_readv(getpid(), &l, 1, &r, 1, 0) == (ssize_t)n;
    }

    void LowerCopy(const char* n, char* b, size_t cap)
    {
        size_t i = 0;
        if (n) for (; n[i] && i < cap - 1; ++i) b[i] = (char)tolower((unsigned char)n[i]);
        b[i] = 0;
    }

    bool IsPlateName(const char* n, int level)
    {
        if (!n || !n[0]) return false;
        char b[40];
        LowerCopy(n, b, sizeof(b));
        static const char* kStrict[] = { "nomer", "nomre", "carplate", "numberplate", nullptr };
        static const char* kLoose[]  = { "plate", "number", "licen", nullptr };
        const char** keys = (level == 0) ? kStrict : kLoose;
        for (int k = 0; keys[k]; ++k)
            if (strstr(b, keys[k])) return true;
        return false;
    }

    bool LooksLikePlateTexture(RwTexture* t)
    {
        if (!t || !t->raster) return false;
        const int w = (int)RwRasterGetWidth(t->raster);
        const int h = (int)RwRasterGetHeight(t->raster);
        if (w < 32 || h < 4) return false;
        const float ar = (float)w / (float)h;
        if (ar < 3.0f || ar > 8.5f) return false;

        char b[40];
        LowerCopy(t->name, b, sizeof(b));
        static const char* kBad[] = { "light", "lamp", "glass", "window", "chrome", "grill", "vent",
                                      "sticker", "decal", "wheel", "tire", nullptr };
        for (int k = 0; kBad[k]; ++k)
            if (strstr(b, kBad[k])) return false;
        return true;
    }
'''

MATCB = r'''RpMaterial* MatCB(RpMaterial* mat, void*)
    {
        if (!mat || !mat->texture || !g_collect) return mat;

        const bool ok = (g_plateLevel < 2) ? IsPlateName(mat->texture->name, g_plateLevel)
                                           : LooksLikePlateTexture(mat->texture);
        if (ok && std::find(g_collect->begin(), g_collect->end(), mat) == g_collect->end())
            g_collect->push_back(mat);
        return mat;
    }'''

COLLECT = r'''void CollectPlateMaterials(CVehicleGTA* veh, std::vector<RpMaterial*>& out)
    {
        out.clear();
        if (!veh || !veh->m_pRwClump) return;

        // 0) oyunun oz lovhe materiali: modelin m_pPlateMaterial gostericisi (yaddasa tehlukesiz baxilir)
        {
            auto* mi = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
            if (mi)
            {
                RpMaterial* pm = mi->m_pPlateMaterial;
                const uintptr_t a = (uintptr_t)pm;
                RwTexture* tx = nullptr;
                uint32_t probe = 0;
                if (pm && (a & 3) == 0 &&
                    SafeRead(a, &tx, sizeof(tx)) && tx &&
                    SafeRead((uintptr_t)tx, &probe, sizeof(probe)))
                {
                    out.push_back(pm);
                    return;
                }
            }
        }

        // 1-3) clump icinde ad / nisbet ile axtaris
        for (g_plateLevel = 0; g_plateLevel <= 2 && out.empty(); ++g_plateLevel)
        {
            g_collect = &out;
            RpClumpForAllAtomics(veh->m_pRwClump, AtomCB, nullptr);
            g_collect = nullptr;
        }
        g_plateLevel = 0;
    }'''


def patch_plate(t):
    if "PLATE_MULTI_STRATEGY" in t:
        return t, False
    t = sub1(t, "#include <cctype>",
             "#include <cctype>\n#include <algorithm>\n#include <unistd.h>\n#include <sys/uio.h>", "AzPlate include")
    t = replace_function(t, "bool IsPlateName(const char* n)", PLATE_HELPERS, "IsPlateName")
    t = replace_function(t, "RpMaterial* MatCB(RpMaterial* mat, void*)", MATCB, "MatCB")
    t = replace_function(t, "void CollectPlateMaterials(CVehicleGTA* veh, std::vector<RpMaterial*>& out)",
                         COLLECT, "CollectPlateMaterials")
    return t, True


# =====================================================================
# 3) gui.cpp
# =====================================================================
FWD_DECL = r'''// SPEEDO_STYLES_PATCH: /mm -> Spidometr sehifesi (asagida, HUD bolmesinde teyin olunur)
static void MM_SpeedoPage(float cx0, float cw, const mm::Rc& P, float dt, bool canInput);

'''

PAGE_BRANCH = r'''else if (s_page == 7)    // SPIDOMETR
                {
                        MM_SpeedoPage(cx0, cw, P, dt, canInput);
                }
                else                      // HANDLING SEHIFELERI'''

PLATE_FAIL = r'''else {
                                    std::string dumpPath;
                                    AzPlate::Dump(gtaVeh, dumpPath);   // diaqnostika: vehdump.txt
                                    snprintf(s_plMsg, sizeof(s_plMsg), "Material tapılmadı (vehdump.txt yazıldı)");
                                }'''

HUD_BLOCK = r'''// ============================================================================
//  SPEEDO_STYLES_PATCH: spidometr stilleri (0 = Klassik, 1 = Sport zolaq, 2 = Neon qovs)
//  Secim ve reng <storage>/hudstyle.txt faylinda saxlanir.
// ============================================================================
extern char* g_pszStorage;

static int s_hudStyle = 0;

static inline ImU32 HudA(ImU32 c, int a)
{
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        return (c & 0x00FFFFFFu) | ((ImU32)a << 24);
}

static void HudCfgLoad()
{
        static bool s_done = false;
        if (s_done) return;
        s_done = true;
        if (!g_pszStorage) return;

        char path[512];
        snprintf(path, sizeof(path), "%s/hudstyle.txt", g_pszStorage);
        FILE* f = fopen(path, "r");
        if (!f) return;
        int a = 0, b = 0;
        if (fscanf(f, "%d %d", &a, &b) == 2) {
                if (a >= 0 && a < 3) s_hudStyle = a;
                if (b >= 0 && b < 5) s_hudColorIdx = b;
        }
        fclose(f);
}

static void HudCfgSave()
{
        if (!g_pszStorage) return;
        char path[512];
        snprintf(path, sizeof(path), "%s/hudstyle.txt", g_pszStorage);
        FILE* f = fopen(path, "w");
        if (!f) return;
        fprintf(f, "%d %d\n", s_hudStyle, s_hudColorIdx);
        fclose(f);
}

static void HudText(ImDrawList* dl, ImFont* font, float size, float x, float y, ImU32 col, const char* s, int align)
{
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        if (align == 1) x -= sz.x * 0.5f;
        else if (align == 2) x -= sz.x;
        dl->AddText(font, size, ImVec2(x, y), col, s);
}

// ox: dir = -1 sola, +1 saga
static void HudArrow(ImDrawList* dl, float cx, float cy, float s, float dir, ImU32 col)
{
        const float tail = cx - dir * s;
        const float neck = cx - dir * s * 0.1f;
        dl->AddTriangleFilled(ImVec2(cx + dir * s, cy), ImVec2(neck, cy - s * 0.85f), ImVec2(neck, cy + s * 0.85f), col);
        dl->AddRectFilled(ImVec2(std::min(tail, neck), cy - s * 0.32f), ImVec2(std::max(tail, neck), cy + s * 0.32f), col);
}

static void HudChip(ImDrawList* dl, ImFont* font, float x, float y, float rad, bool on, const char* t, ImU32 accent)
{
        dl->AddCircleFilled(ImVec2(x, y), rad, on ? accent : IM_COL32(60, 64, 76, 210), 20);
        HudText(dl, font, rad * 1.15f, x, y - rad * 0.58f, on ? IM_COL32(10, 12, 18, 255) : IM_COL32(170, 174, 186, 255), t, 1);
}

// ---- Stil 1: SPORT ZOLAQ ----
static void HudSport(ImDrawList* dl, ImFont* font, const ImVec2& disp, float u, float kmh, const char* gear,
                     bool eng, bool lig, bool unlocked, ImU32 accent, float frac, const char* odo, uint8_t sig)
{
        const float r  = 92.0f * u;
        const float pw = r * 2.8f, ph = r * 1.10f;
        const float px = disp.x * 0.66f - pw * 0.5f;
        const float py = disp.y - 14.0f * u - ph;
        const float rr = 14.0f * u;
        const bool  blink = VehControls::BlinkOn();
        const ImU32 sigOn  = IM_COL32(255, 160, 30, 255);
        const ImU32 sigOff = IM_COL32(64, 68, 80, 210);
        const ImU32 white  = IM_COL32(255, 255, 255, 255);

        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(8, 10, 16, 205), rr);
        dl->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(255, 255, 255, 30), rr, mm::kAllCorners, 1.4f * u);

        // seqmentli zolaq
        const int   n   = 30;
        const float gap = 2.5f * u;
        const float bx0 = px + pw * 0.05f, bw = pw * 0.90f;
        const float by  = py + ph * 0.12f, bh = ph * 0.13f;
        const float sw  = (bw - gap * (n - 1)) / (float)n;
        for (int i = 0; i < n; ++i)
        {
                const float t = (float)i / (float)(n - 1);
                const bool  lit = ((float)i + 0.5f) / (float)n <= frac;
                ImU32 col = IM_COL32(48, 52, 64, 220);
                if (lit) col = (t > 0.78f) ? IM_COL32(235, 60, 80, 255) : accent;
                const float x = bx0 + (float)i * (sw + gap);
                dl->AddRectFilled(ImVec2(x, by), ImVec2(x + sw, by + bh), col, 2.0f * u);
        }

        // suret
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(kmh + 0.5f));
        HudText(dl, font, ph * 0.50f, px + pw * 0.62f, py + ph * 0.34f, white, sp, 2);
        HudText(dl, font, ph * 0.15f, px + pw * 0.645f, py + ph * 0.56f, accent, "KM/H", 0);
        HudText(dl, font, ph * 0.11f, px + pw * 0.645f, py + ph * 0.76f, IM_COL32(190, 194, 208, 220), odo, 0);

        // vites qutusu
        const float gx0 = px + pw * 0.80f, gx1 = px + pw * 0.95f, gy0 = py + ph * 0.40f, gy1 = py + ph * 0.90f;
        dl->AddRectFilled(ImVec2(gx0, gy0), ImVec2(gx1, gy1), IM_COL32(14, 16, 24, 240), 8.0f * u);
        dl->AddRect(ImVec2(gx0, gy0), ImVec2(gx1, gy1), accent, 8.0f * u, mm::kAllCorners, 1.8f * u);
        HudText(dl, font, ph * 0.34f, (gx0 + gx1) * 0.5f, gy0 + ((gy1 - gy0) - ph * 0.34f) * 0.5f, white, gear, 1);

        // sinyal oxlari
        const bool lOn = (sig == VEH_SIGNAL_LEFT  || sig == VEH_SIGNAL_HAZARD) && blink;
        const bool rOn = (sig == VEH_SIGNAL_RIGHT || sig == VEH_SIGNAL_HAZARD) && blink;
        const float ay = py + ph * 0.50f;
        HudArrow(dl, px + pw * 0.08f, ay, ph * 0.12f, -1.0f, lOn ? sigOn : sigOff);
        HudArrow(dl, px + pw * 0.30f, ay, ph * 0.12f, +1.0f, rOn ? sigOn : sigOff);

        // M / F / Q
        const float cy = py + ph * 0.80f, cr = ph * 0.085f;
        HudChip(dl, font, px + pw * 0.08f, cy, cr, eng,      "M", accent);
        HudChip(dl, font, px + pw * 0.19f, cy, cr, lig,      "F", accent);
        HudChip(dl, font, px + pw * 0.30f, cy, cr, unlocked, "Q", accent);
}

// ---- Stil 2: NEON QOVS (yarimdaire) ----
static void HudNeon(ImDrawList* dl, ImFont* font, const ImVec2& disp, float u, float kmh, const char* gear,
                    bool eng, bool lig, bool unlocked, ImU32 accent, float frac, const char* odo, uint8_t sig)
{
        const float PI = 3.14159265f;
        const float r  = 92.0f * u;
        const float R  = r * 1.10f;
        const ImVec2 c(disp.x * 0.66f, disp.y - 14.0f * u - r * 0.34f);
        const float th = 8.0f * u;
        const bool  blink = VehControls::BlinkOn();
        const ImU32 sigOn  = IM_COL32(255, 160, 30, 255);
        const ImU32 sigOff = IM_COL32(64, 68, 80, 210);
        const ImU32 white  = IM_COL32(255, 255, 255, 255);
        const ImU32 bg     = IM_COL32(8, 10, 16, 175);

        // zemin: yarimdaire + alt zolaq
        dl->PathClear();
        dl->PathArcTo(c, R * 1.10f, PI, 2.0f * PI, 40);
        dl->PathLineTo(c);
        dl->PathFillConvex(bg);
        dl->AddRectFilled(ImVec2(c.x - R * 1.10f, c.y), ImVec2(c.x + R * 1.10f, c.y + r * 0.34f + 2.0f * u), bg);

        // yol
        dl->PathArcTo(c, R * 0.92f, PI, 2.0f * PI, 48);
        dl->PathStroke(IM_COL32(60, 66, 82, 200), 0, th);
        // qirmizi zona
        dl->PathArcTo(c, R * 0.92f, PI + PI * 0.8f, 2.0f * PI, 12);
        dl->PathStroke(IM_COL32(235, 60, 80, 150), 0, th * 0.5f);

        if (frac > 0.004f)
        {
                for (int k = 3; k >= 1; --k) {
                        dl->PathArcTo(c, R * 0.92f, PI, PI + PI * frac, 48);
                        dl->PathStroke(HudA(accent, 16 * (4 - k)), 0, th + (float)k * 5.0f * u);
                }
                dl->PathArcTo(c, R * 0.92f, PI, PI + PI * frac, 48);
                dl->PathStroke(accent, 0, th);

                const float a = PI + PI * frac;
                dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R * 0.92f, c.y + sinf(a) * R * 0.92f), th * 0.85f, white, 16);
        }

        // xetler
        for (int i = 0; i <= 9; ++i) {
                const float a = PI + PI * ((float)i / 9.0f);
                dl->AddLine(ImVec2(c.x + cosf(a) * R * 0.74f, c.y + sinf(a) * R * 0.74f),
                            ImVec2(c.x + cosf(a) * R * 0.80f, c.y + sinf(a) * R * 0.80f),
                            IM_COL32(190, 194, 208, 170), 1.4f * u);
        }

        // suret
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(kmh + 0.5f));
        HudText(dl, font, R * 0.52f, c.x, c.y - R * 0.66f, white, sp, 1);
        HudText(dl, font, R * 0.14f, c.x, c.y - R * 0.13f, accent, "KM/H", 1);

        // vites (sol) ve yol sayaci (sag)
        HudText(dl, font, R * 0.26f, c.x - R * 0.55f, c.y - R * 0.32f, white, gear, 1);
        HudText(dl, font, R * 0.12f, c.x + R * 0.55f, c.y - R * 0.20f, IM_COL32(190, 194, 208, 220), odo, 1);

        // alt zolaq: sinyal oxlari + M F Q
        const bool lOn = (sig == VEH_SIGNAL_LEFT  || sig == VEH_SIGNAL_HAZARD) && blink;
        const bool rOn = (sig == VEH_SIGNAL_RIGHT || sig == VEH_SIGNAL_HAZARD) && blink;
        const float by = c.y + r * 0.17f;
        HudArrow(dl, c.x - R * 0.55f, by, r * 0.11f, -1.0f, lOn ? sigOn : sigOff);
        HudArrow(dl, c.x + R * 0.55f, by, r * 0.11f, +1.0f, rOn ? sigOn : sigOff);
        HudChip(dl, font, c.x - R * 0.24f, by, r * 0.085f, eng,      "M", accent);
        HudChip(dl, font, c.x,             by, r * 0.085f, lig,      "F", accent);
        HudChip(dl, font, c.x + R * 0.24f, by, r * 0.085f, unlocked, "Q", accent);
}

// ---- /mm -> Spidometr sehifesi ----
static void HudPreview(int style, const mm::Rc& pv)
{
        using namespace mm;
        const float PI = 3.14159265f;
        const ImU32 white = Col(255, 255, 255, 235);

        if (style == 0)
        {
                const float cx = pv.x + pv.w * 0.5f, cy = pv.y + pv.h * 0.52f, rad = std::min(pv.w, pv.h) * 0.40f;
                const float a0 = PI * 0.75f, sw = PI * 1.5f;
                s_dl->PathArcTo(ImVec2(cx, cy), rad, a0, a0 + sw, 28);
                s_dl->PathStroke(Col(70, 76, 92, 220), 0, S(3.0f));
                s_dl->PathArcTo(ImVec2(cx, cy), rad, a0, a0 + sw * 0.62f, 28);
                s_dl->PathStroke(Accent(0.0f), 0, S(3.0f));
                s_dl->AddLine(ImVec2(cx, cy), ImVec2(cx + cosf(a0 + sw * 0.62f) * rad * 0.8f, cy + sinf(a0 + sw * 0.62f) * rad * 0.8f), white, S(1.6f));
        }
        else if (style == 1)
        {
                const int   n = 14;
                const float gap = S(2.0f), bx = pv.x + pv.w * 0.1f, bw = pv.w * 0.8f, by = pv.y + pv.h * 0.18f, bh = pv.h * 0.12f;
                const float sw = (bw - gap * (n - 1)) / (float)n;
                for (int k = 0; k < n; ++k)
                        Fill(Rc{ bx + (float)k * (sw + gap), by, sw, bh }, k < 9 ? Accent(0.0f) : Col(50, 54, 66, 220), S(1.5f));
                TxtC(pv.h * 0.38f, Rc{ pv.x, pv.y + pv.h * 0.38f, pv.w * 0.72f, pv.h * 0.5f }, white, "128", true);
                Fill(Rc{ pv.x + pv.w * 0.76f, pv.y + pv.h * 0.50f, pv.w * 0.16f, pv.h * 0.32f }, Col(255, 255, 255, 18), S(4.0f));
        }
        else
        {
                const float cx = pv.x + pv.w * 0.5f, cy = pv.y + pv.h * 0.80f, rad = std::min(pv.w * 0.38f, pv.h * 0.62f);
                s_dl->PathArcTo(ImVec2(cx, cy), rad, PI, 2.0f * PI, 28);
                s_dl->PathStroke(Col(70, 76, 92, 220), 0, S(3.5f));
                s_dl->PathArcTo(ImVec2(cx, cy), rad, PI, PI + PI * 0.6f, 28);
                s_dl->PathStroke(Accent(0.0f), 0, S(3.5f));
                TxtC(pv.h * 0.30f, Rc{ pv.x, cy - pv.h * 0.42f, pv.w, pv.h * 0.30f }, white, "128", true);
        }
}

static void MM_SpeedoPage(float cx0, float cw, const mm::Rc& P, float dt, bool canInput)
{
        using namespace mm;
        HudCfgLoad();

        static const char* kNames[3] = { "Klassik dairə", "Sport zolaq", "Neon qövs" };
        static const char* kDescs[3] = { "MTA üslubunda dairəvi ibrə və rəqəmlər",
                                         "Seqmentli işıq zolağı, iri rəqəm və dişli qutusu",
                                         "Yarımdairə neon qövs, sadə və müasir" };

        for (int i = 0; i < 3; ++i)
        {
                const Rc row{ cx0, P.y + S(124.0f + 112.0f * (float)i), cw, S(100.0f) };
                const int id = 93 + i;
                if (Tap(id, row, canInput) && s_hudStyle != i) { s_hudStyle = i; HudCfgSave(); }

                const float p = Prs(id, dt);
                const bool on = (s_hudStyle == i);
                const Rc d = Shrink(row, S(1.5f) * p);
                Fill(d, Col(27, 30, 40, 255), S(10.0f));
                if (on) Stroke(d, Accent(0.0f, 200), S(10.0f), S(1.6f));

                const Rc pv{ d.x + S(14.0f), d.y + S(12.0f), S(170.0f), d.h - S(24.0f) };
                Fill(pv, Col(10, 12, 18, 255), S(8.0f));
                HudPreview(i, pv);

                TxtB(S(19.0f), d.x + S(204.0f), d.y + S(24.0f), Col(238, 241, 248), kNames[i]);
                Txt(S(13.0f), d.x + S(204.0f), d.y + S(54.0f), Col(124, 132, 152), kDescs[i]);
                if (on) Txt(S(13.0f), d.x + S(204.0f), d.y + S(74.0f), Accent(0.0f), "Seçilib");
        }

        Txt(S(16.0f), cx0, P.y + S(470.0f), Col(214, 220, 232), "Rəng");
        for (int i = 0; i < 5; ++i)
        {
                const float cxs = cx0 + S(120.0f) + (float)i * S(54.0f), cys = P.y + S(480.0f);
                const Rc sq{ cxs - S(22.0f), cys - S(22.0f), S(44.0f), S(44.0f) };
                const int sid = ID_SWATCH + i;
                if (Tap(sid, sq, canInput)) { s_hudColorIdx = i; HudCfgSave(); }

                const float p = Prs(sid, dt);
                Dot(cxs, cys, S(18.0f) - S(2.0f) * p, HudA(kHudPalette[i], (int)(255.0f * s_A)));
                if (s_hudColorIdx == i) s_dl->AddCircle(ImVec2(cxs, cys), S(23.0f), Col(255, 255, 255, 230), 28, S(2.2f));
        }
        Txt(S(12.0f), cx0, P.y + S(524.0f), Col(124, 132, 152), "İpucu: sürət göstəricisinə uzun basaraq da rəngi dəyişə bilərsiniz.");
}

'''

DISPATCH = r'''{
                static int s_savedClr = -1;
                if (s_savedClr != s_hudColorIdx) { if (s_savedClr != -1) HudCfgSave(); s_savedClr = s_hudColorIdx; }
        }
        if (s_hudStyle == 1) { HudSport(dl, font, disp, u, kmh, gear, eng, lig, !locked, accent, s_dialShown, odo, VehControls::GetSignal(gtaVeh)); return; }
        if (s_hudStyle == 2) { HudNeon (dl, font, disp, u, kmh, gear, eng, lig, !locked, accent, s_dialShown, odo, VehControls::GetSignal(gtaVeh)); return; }

        const float a0 = 3.14159265f * 0.75f;'''


def patch_gui(t):
    if "SPEEDO_STYLES_PATCH" in t:
        return t, False

    # nav massivleri: 7 -> 8 element
    t, n1 = re.subn(r'(static const char\* kPages\[)7(\]\s*=\s*\{.*?)\s*\};',
                    lambda m: m.group(1) + "8" + m.group(2) + ', "Spidometr" };', t, count=1, flags=re.S)
    t, n2 = re.subn(r'(static const char\* kSubs\[)7(\]\s*=\s*\{.*?)\s*\};',
                    lambda m: m.group(1) + "8" + m.group(2) + ', "Spidometr stilini və rəngini seç" };', t, count=1, flags=re.S)
    if n1 != 1 or n2 != 1:
        raise PatchError("gui.cpp: kPages/kSubs tapılmadı (%d,%d)" % (n1, n2))

    t = sub1(t, "for (int i = 0; i < 7; ++i)", "for (int i = 0; i < 8; ++i)", "gui nav loop")
    t = sub1(t, "else // HANDLING SEHIFELERI", PAGE_BRANCH, "gui page branch")
    t = insert_before(t, "void RenderHandlingDialog()", FWD_DECL, "gui forward decl")
    t = sub1(t, 'else snprintf(s_plMsg, sizeof(s_plMsg), "Bu maşında nömrə lövhəsi materialı tapılmadı");',
             PLATE_FAIL, "gui plate fail")
    t = insert_before(t, "static void DrawGauge(ImDrawList* dl, float cx, float cy, float r, bool on, const char* label, ImU32 accent, float u)",
                      HUD_BLOCK, "gui hud block")
    t = sub1(t, "if (g_bHideAllUI || g_bShowHandlingDlg) return;",
             "HudCfgLoad();\n        if (g_bHideAllUI || g_bShowHandlingDlg) return;", "gui hud load")
    t = sub1(t, "const float a0 = 3.14159265f * 0.75f;", DISPATCH, "gui hud dispatch")
    return t, True


# =====================================================================
def main():
    if not os.path.isdir(CPP):
        print("XETA: repo-nun kok qovlugunda deyilsən (%s tapılmadı)." % CPP)
        sys.exit(1)

    jobs = [
        (CPP + "/gui/imguiwrapper.cpp", patch_imgui),
        (CPP + "/game/AzPlate.cpp", patch_plate),
        (CPP + "/gui/gui.cpp", patch_gui),
    ]

    results = []
    try:
        for path, fn in jobs:
            if not os.path.isfile(path):
                raise PatchError("fayl yoxdur: " + path)
            old = rd(path)
            new, changed = fn(old)
            results.append((path, old, new, changed))
    except PatchError as e:
        print("XETA: %s" % e)
        print("Hec bir fayl deyisdirilmedi. Bu xetani mene gonder.")
        sys.exit(2)

    for path, old, new, changed in results:
        print(("[DEYISDI] " if changed else "[kecildi] ") + path)

    if DRY:
        print("--dry: faylar yazilmadi.")
        return

    bak = ".patchbak4"
    os.makedirs(bak, exist_ok=True)
    for path, old, new, changed in results:
        if not changed:
            continue
        shutil.copyfile(path, os.path.join(bak, path.replace("/", "__")))
        wr(path, new)

    gi = ".gitignore"
    have = rd(gi) if os.path.isfile(gi) else ""
    add = [l for l in (".patchbak4/", "c6_*.txt", "collect5*") if l not in have.split()]
    if add:
        wr(gi, have + ("" if have.endswith("\n") or not have else "\n") + "\n".join(add) + "\n")

    print("\nHazirdir. Yedekler: %s/   Indi build et, sonra:" % bak)
    print("  git add -A && git commit -m 'AZ glyph, plate tapma, 2 yeni spidometr' && git push")


if __name__ == "__main__":
    main()

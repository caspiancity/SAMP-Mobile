# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v4.py -- layihenin kokunde isle:  python3 apply_v4.py
#   1) keyboard.h: setInputString-de '//' sherhi '}'-i udurdu (kompilyasiya xetasi) -> duzeldildi
#   2) gui.cpp: "Sport zolaq" ve "Neon qovs" silindi -> yeni "Axin halqasi" ve "Susse zolaq"
#   3) playertags.cpp: muasir nametag (pill, bayraq, ID ayri, mesafe, gradient can/zireh)
#   4) imguiwrapper.cpp: oz nick fontu (<font qovlugu>/nick.ttf), Az herfleri ile
# Geri qaytarmaq: her faylin yaninda *.bakv4. Hec ne tapilmasa fayla toxunulmur.
import os, re, shutil

ROOT = os.getcwd()
CPP  = os.path.join(ROOT, "app/src/main/cpp/samp")

def rd(p):
    with open(p, "rb") as f:
        t = f.read().decode("utf-8")
    crlf = "\r\n" in t
    return t.replace("\r\n", "\n"), crlf

def wr(p, t, crlf):
    if crlf:
        t = t.replace("\n", "\r\n")
    with open(p, "wb") as f:
        f.write(t.encode("utf-8"))

def step(name, path, marker, fn):
    if not os.path.exists(path):
        print("[YOX]   %s: %s tapilmadi" % (name, os.path.relpath(path, ROOT))); return
    s, crlf = rd(path)
    if marker in s:
        print("[KECDI] %s: artiq tetbiq olunub" % name); return
    try:
        n = fn(s)
    except ValueError as e:
        print("[KECDI] %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); return
    if not os.path.exists(path + ".bakv4"):
        shutil.copy(path, path + ".bakv4")
    wr(path, n, crlf)
    print("[OK]    %s" % name)

# ======================================================================= 1. keyboard.h
def p_kbh(s):
    pat = re.compile(r'(void setInputString\(const std::string& string\) \{ m_input = string; m_caption = string;)[ \t]*//[^\n]*\}')
    if not pat.search(s):
        raise ValueError("setInputString")
    return pat.sub(lambda m: m.group(1) + " /* KBH_V4 */ }", s, count=1)

# ======================================================================= 2. speedometrler
HUD_NEW = r'''// ===== HUD_FLOW_V4: yeni muasir axici speedometrler =====
//  1 = Axin halqasi (gradient qovs + axan zerreler)
//  2 = Susse zolaq (axici maye zolagi)
static ImU32 HudMix(ImU32 a, ImU32 b, float t)
{
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        const int ar = (int)(a & 255u), ag = (int)((a >> 8) & 255u), ab = (int)((a >> 16) & 255u), aa = (int)((a >> 24) & 255u);
        const int br = (int)(b & 255u), bg = (int)((b >> 8) & 255u), bb = (int)((b >> 16) & 255u), ba = (int)((b >> 24) & 255u);
        return IM_COL32(ar + (int)((float)(br - ar) * t), ag + (int)((float)(bg - ag) * t),
                        ab + (int)((float)(bb - ab) * t), aa + (int)((float)(ba - aa) * t));
}

static void HudGradArc(ImDrawList* dl, const ImVec2& c, float rad, float a0, float a1, ImU32 c0, ImU32 c1, float th)
{
        const int seg = 40;
        for (int i = 0; i < seg; ++i) {
                const float t0 = (float)i / (float)seg, t1 = (float)(i + 1) / (float)seg;
                dl->PathArcTo(c, rad, a0 + (a1 - a0) * t0, a0 + (a1 - a0) * t1 + 0.015f, 3);
                dl->PathStroke(HudMix(c0, c1, (t0 + t1) * 0.5f), 0, th);
        }
}

static float HudNum(float& cur, float target)
{
        cur += (target - cur) * (1.0f - expf(-ImGui::GetIO().DeltaTime * 12.0f));
        return cur;
}

// ---- Stil 1: AXIN HALQASI ----
static void HudFlow(ImDrawList* dl, ImFont* font, const ImVec2& disp, float u, float kmh, const char* gear,
                    bool eng, bool lig, bool unlocked, ImU32 accent, float frac, const char* odo, uint8_t sig)
{
        const float PI = 3.14159265f;
        const float R  = 96.0f * u;
        const ImVec2 c(disp.x * 0.66f, disp.y - 14.0f * u - R * 1.08f);
        const float th = 9.0f * u;
        const float a0 = PI * 0.75f, sweep = PI * 1.5f;
        const float tm = (float)ImGui::GetTime();
        const bool  blink = VehControls::BlinkOn();
        const ImU32 white  = IM_COL32(255, 255, 255, 255);
        const ImU32 hot    = HudMix(accent, white, 0.55f);
        const ImU32 sigOn  = IM_COL32(255, 160, 30, 255);
        const ImU32 sigOff = IM_COL32(64, 68, 80, 210);

        static float s_num = 0.0f;
        const float shown = HudNum(s_num, kmh);

        // zemin
        dl->AddCircleFilled(c, R * 1.12f, IM_COL32(8, 10, 16, 150), 64);
        dl->AddCircle(c, R * 1.12f, IM_COL32(255, 255, 255, 22), 64, 1.2f * u);

        // yol
        dl->PathArcTo(c, R, a0, a0 + sweep, 56);
        dl->PathStroke(IM_COL32(255, 255, 255, 26), 0, th);
        for (int e = 0; e < 2; ++e) {
                const float a = a0 + sweep * (float)e;
                dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R, c.y + sinf(a) * R), th * 0.5f, IM_COL32(255, 255, 255, 26), 12);
        }

        // dolu hisse
        if (frac > 0.004f)
        {
                const float a1 = a0 + sweep * frac;
                HudGradArc(dl, c, R, a0, a1, HudA(accent, 10), HudA(accent, 46), th * 2.6f);   // yumsaq isiq
                HudGradArc(dl, c, R, a0, a1, HudA(accent, 90), hot, th);
                dl->AddCircleFilled(ImVec2(c.x + cosf(a0) * R, c.y + sinf(a0) * R), th * 0.5f, HudA(accent, 90), 12);

                // axan zerreler
                for (int k = 0; k < 3; ++k) {
                        const float p = fmodf(tm * 0.55f + (float)k / 3.0f, 1.0f);
                        const float a = a0 + (a1 - a0) * p;
                        dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R, c.y + sinf(a) * R), th * 0.22f,
                                            IM_COL32(255, 255, 255, (int)(200.0f * sinf(p * PI))), 10);
                }
                // bas
                const ImVec2 h(c.x + cosf(a1) * R, c.y + sinf(a1) * R);
                dl->AddCircleFilled(h, th * 1.25f, HudA(hot, 60), 20);
                dl->AddCircleFilled(h, th * 0.62f, white, 20);
        }

        // nuqteler
        for (int i = 0; i <= 9; ++i) {
                const float q = (float)i / 9.0f, a = a0 + sweep * q;
                dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R * 0.80f, c.y + sinf(a) * R * 0.80f),
                                    (i % 3 == 0 ? 1.8f : 1.2f) * u, q <= frac ? HudA(hot, 190) : IM_COL32(255, 255, 255, 50), 8);
        }

        // suret
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(shown + 0.5f));
        const float ns = R * 0.58f;
        HudText(dl, font, ns, c.x + 1.2f * u, c.y - R * 0.50f + 1.5f * u, IM_COL32(0, 0, 0, 110), sp, 1);
        HudText(dl, font, ns, c.x, c.y - R * 0.50f, white, sp, 1);
        HudText(dl, font, R * 0.13f, c.x, c.y + R * 0.10f, hot, "KM/H", 1);

        // M F Q
        HudChip(dl, font, c.x - R * 0.26f, c.y + R * 0.40f, R * 0.075f, eng,      "M", accent);
        HudChip(dl, font, c.x,             c.y + R * 0.40f, R * 0.075f, lig,      "F", accent);
        HudChip(dl, font, c.x + R * 0.26f, c.y + R * 0.40f, R * 0.075f, unlocked, "Q", accent);

        // vites + sinyal
        const float gy = c.y + R * 0.76f, gw = R * 0.46f, gh = R * 0.24f;
        dl->AddRectFilled(ImVec2(c.x - gw * 0.5f, gy - gh * 0.5f), ImVec2(c.x + gw * 0.5f, gy + gh * 0.5f), IM_COL32(14, 16, 24, 230), gh * 0.5f);
        dl->AddRect(ImVec2(c.x - gw * 0.5f, gy - gh * 0.5f), ImVec2(c.x + gw * 0.5f, gy + gh * 0.5f), HudA(accent, 200), gh * 0.5f, mm::kAllCorners, 1.4f * u);
        HudText(dl, font, gh * 0.78f, c.x, gy - gh * 0.39f, white, gear, 1);

        const bool lOn = (sig == VEH_SIGNAL_LEFT  || sig == VEH_SIGNAL_HAZARD) && blink;
        const bool rOn = (sig == VEH_SIGNAL_RIGHT || sig == VEH_SIGNAL_HAZARD) && blink;
        HudArrow(dl, c.x - R * 0.50f, gy, R * 0.10f, -1.0f, lOn ? sigOn : sigOff);
        HudArrow(dl, c.x + R * 0.50f, gy, R * 0.10f, +1.0f, rOn ? sigOn : sigOff);

        HudText(dl, font, R * 0.10f, c.x, c.y + R * 0.95f, IM_COL32(190, 194, 208, 200), odo, 1);
}

// ---- Stil 2: SUSSE ZOLAQ ----
static void HudGlass(ImDrawList* dl, ImFont* font, const ImVec2& disp, float u, float kmh, const char* gear,
                     bool eng, bool lig, bool unlocked, ImU32 accent, float frac, const char* odo, uint8_t sig)
{
        const float PI = 3.14159265f; (void)PI;
        const float pw = 312.0f * u, ph = 116.0f * u;
        const float px = disp.x * 0.66f - pw * 0.5f, py = disp.y - 14.0f * u - ph;
        const float rr = 24.0f * u, pad = 20.0f * u;
        const float tm = (float)ImGui::GetTime();
        const bool  blink = VehControls::BlinkOn();
        const ImU32 white  = IM_COL32(255, 255, 255, 255);
        const ImU32 hot    = HudMix(accent, white, 0.55f);
        const ImU32 sigOn  = IM_COL32(255, 160, 30, 255);
        const ImU32 sigOff = IM_COL32(64, 68, 80, 210);

        static float s_num = 0.0f;
        const float shown = HudNum(s_num, kmh);

        // susse panel
        dl->AddRectFilled(ImVec2(px, py + 5.0f * u), ImVec2(px + pw, py + ph + 5.0f * u), IM_COL32(0, 0, 0, 60), rr);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(14, 16, 24, 196), rr);
        dl->AddRectFilledMultiColor(ImVec2(px + rr * 0.5f, py + 1.5f * u), ImVec2(px + pw - rr * 0.5f, py + ph * 0.42f),
                                    IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(255, 255, 255, 40), rr, mm::kAllCorners, 1.2f * u);

        // suret (sabit en: reqem sayi deyisende KM/H yerinden oynamasin)
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(shown + 0.5f));
        const float ns = ph * 0.50f;
        const float nw = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, "888").x;
        HudText(dl, font, ns, px + pad + nw, py + ph * 0.08f, white, sp, 2);
        HudText(dl, font, ph * 0.13f, px + pad + nw + 8.0f * u, py + ph * 0.40f, hot, "KM/H", 0);

        // vites
        const float gr = ph * 0.20f, gcx = px + pw - pad - gr, gcy = py + ph * 0.34f;
        dl->AddCircleFilled(ImVec2(gcx, gcy), gr, IM_COL32(8, 10, 16, 230), 32);
        dl->AddCircle(ImVec2(gcx, gcy), gr, HudA(accent, 210), 32, 1.6f * u);
        HudText(dl, font, gr * 1.15f, gcx, gcy - gr * 0.58f, white, gear, 1);

        // axici maye zolagi
        const float bx = px + pad, bw = pw - pad * 2.0f, bh = ph * 0.11f, by = py + ph * 0.66f;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 26), bh * 0.5f);
        if (frac > 0.004f)
        {
                float fw = bw * frac;
                if (fw < bh) fw = bh;
                dl->AddRectFilled(ImVec2(bx - 2.0f * u, by - 3.0f * u), ImVec2(bx + fw + 2.0f * u, by + bh + 3.0f * u), HudA(accent, 36), bh);
                dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + fw, by + bh), HudMix(accent, IM_COL32(0, 0, 0, 255), 0.30f), bh * 0.5f);

                dl->PushClipRect(ImVec2(bx, by), ImVec2(bx + fw, by + bh), true);
                const float inner = fw - bh * 0.5f;
                if (inner > 2.0f)
                        dl->AddRectFilledMultiColor(ImVec2(bx, by), ImVec2(bx + inner, by + bh),
                                                    HudA(hot, 0), HudA(hot, 220), HudA(hot, 220), HudA(hot, 0));
                const float bandW = bw * 0.22f;
                const float sx = bx - bandW + (bw + bandW * 2.0f) * fmodf(tm * 0.45f, 1.0f);
                dl->AddRectFilledMultiColor(ImVec2(sx, by), ImVec2(sx + bandW * 0.5f, by + bh),
                                            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 0));
                dl->AddRectFilledMultiColor(ImVec2(sx + bandW * 0.5f, by), ImVec2(sx + bandW, by + bh),
                                            IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 120));
                dl->PopClipRect();

                const ImVec2 h(bx + fw - bh * 0.5f, by + bh * 0.5f);
                dl->AddCircleFilled(h, bh * 1.5f, HudA(hot, 60), 20);
                dl->AddCircleFilled(h, bh * 0.85f, white, 20);
        }

        // alt sira: sinyal oxlari, M F Q, yol sayaci
        const float ry = py + ph * 0.89f;
        const bool lOn = (sig == VEH_SIGNAL_LEFT  || sig == VEH_SIGNAL_HAZARD) && blink;
        const bool rOn = (sig == VEH_SIGNAL_RIGHT || sig == VEH_SIGNAL_HAZARD) && blink;
        HudArrow(dl, px + pad + ph * 0.07f, ry, ph * 0.07f, -1.0f, lOn ? sigOn : sigOff);
        HudArrow(dl, px + pad + ph * 0.25f, ry, ph * 0.07f, +1.0f, rOn ? sigOn : sigOff);
        HudChip(dl, font, px + pw * 0.54f, ry, ph * 0.06f, eng,      "M", accent);
        HudChip(dl, font, px + pw * 0.62f, ry, ph * 0.06f, lig,      "F", accent);
        HudChip(dl, font, px + pw * 0.70f, ry, ph * 0.06f, unlocked, "Q", accent);
        HudText(dl, font, ph * 0.09f, px + pw - pad, ry - ph * 0.045f, IM_COL32(190, 194, 208, 200), odo, 2);
}

'''

HUD_PREVIEW = r'''static void HudPreview(int style, const mm::Rc& pv)
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
                const float cx = pv.x + pv.w * 0.5f, cy = pv.y + pv.h * 0.54f, rad = std::min(pv.w, pv.h) * 0.36f;
                const float a0 = PI * 0.75f, sw = PI * 1.5f;
                s_dl->PathArcTo(ImVec2(cx, cy), rad, a0, a0 + sw, 28);
                s_dl->PathStroke(Col(255, 255, 255, 34), 0, S(4.0f));
                HudGradArc(s_dl, ImVec2(cx, cy), rad, a0, a0 + sw * 0.64f, HudA(Accent(0.0f), 90), Accent(0.0f), S(4.0f));
                TxtC(rad * 0.9f, Rc{ cx - rad, cy - rad * 0.55f, rad * 2.0f, rad * 0.9f }, white, "128", true);
        }
        else
        {
                Fill(Rc{ pv.x + pv.w * 0.08f, pv.y + pv.h * 0.16f, pv.w * 0.84f, pv.h * 0.68f }, Col(255, 255, 255, 16), S(9.0f));
                TxtC(pv.h * 0.30f, Rc{ pv.x + pv.w * 0.08f, pv.y + pv.h * 0.22f, pv.w * 0.5f, pv.h * 0.34f }, white, "128", true);
                const Rc tr{ pv.x + pv.w * 0.14f, pv.y + pv.h * 0.66f, pv.w * 0.72f, pv.h * 0.08f };
                Fill(tr, Col(255, 255, 255, 34), S(3.0f));
                Fill(Rc{ tr.x, tr.y, tr.w * 0.64f, tr.h }, Accent(0.0f), S(3.0f));
        }
}
'''

def p_hud(s):
    i0 = s.find("// ---- Stil 1: SPORT ZOLAQ ----")
    i1 = s.find("// ---- /mm -> Spidometr sehifesi ----")
    if i0 < 0 or i1 < 0 or i0 >= i1:
        raise ValueError("HudSport/HudNeon bloku")
    s = s[:i0] + HUD_NEW + s[i1:]

    pat = re.compile(r'static void HudPreview\(int style, const mm::Rc& pv\)\n\{.*?\n\}\n(?=\nstatic void MM_SpeedoPage\()', re.S)
    s, n = pat.subn(lambda m: HUD_PREVIEW, s, count=1)
    if n != 1:
        raise ValueError("HudPreview")

    pat = re.compile(r'static const char\* kNames\[3\] = \{ "Klassik[^}]*\};')
    s, n = pat.subn(lambda m: 'static const char* kNames[3] = { "Klassik dair\u0259", "Ax\u0131n halqas\u0131", "\u015e\u00fc\u015f\u0259 zolaq" };', s, count=1)
    if n != 1:
        raise ValueError("kNames (speedo)")

    pat = re.compile(r'static const char\* kDescs\[3\] = \{[^}]*\};')
    new = ('static const char* kDescs[3] = { "MTA \u00fcslubunda dair\u0259vi ibr\u0259 v\u0259 r\u0259q\u0259ml\u0259r",\n'
           '                                         "Gradient halqa, axan i\u015f\u0131q z\u0259rr\u0259l\u0259ri v\u0259 iri r\u0259q\u0259m",\n'
           '                                         "\u015e\u00fc\u015f\u0259 panel v\u0259 axar maye zolaq\u0131" };')
    s, n = pat.subn(lambda m: new, s, count=1)
    if n != 1:
        raise ValueError("kDescs")

    a = "HudSport(dl, font, disp, u,"
    b = "HudNeon (dl, font, disp, u,"
    if s.count(a) != 1 or s.count(b) != 1:
        raise ValueError("RenderVehicleHud cagirislari (%d/%d)" % (s.count(a), s.count(b)))
    s = s.replace(a, "HudFlow(dl, font, disp, u,", 1).replace(b, "HudGlass(dl, font, disp, u,", 1)
    return s

# ======================================================================= 3. playertags
TAG_INCLUDES = '''#include "game/World.h"
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <algorithm>'''

TAG_DRAW = r'''// ===== PLAYERTAG_V4: muasir nametag =====
ImFont* g_pNickFont = nullptr;   // imguiwrapper.cpp doldurur (nick.ttf varsa)

static inline float PtClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static ImU32 PtHealthColor(float f, int a)
{
	// 1.0 yasil -> 0.5 sari -> 0.0 qirmizi
	int r, g, b;
	if (f > 0.5f) {
		const float t = (f - 0.5f) * 2.0f;
		r = (int)(255.0f + (88.0f - 255.0f) * t);
		g = (int)(204.0f + (214.0f - 204.0f) * t);
		b = (int)(64.0f + (112.0f - 64.0f) * t);
	} else {
		const float t = f * 2.0f;
		r = (int)(235.0f + (255.0f - 235.0f) * t);
		g = (int)(77.0f + (204.0f - 77.0f) * t);
		b = (int)(77.0f + (64.0f - 77.0f) * t);
	}
	return IM_COL32(r, g, b, a);
}

void CPlayerTags::Draw(ImGuiRenderer* renderer, CVector* vec, const char* szNick, uint32_t dwColor, float fDist, float fHealth, float fArmour, bool bAfk, bool bMicro)
{
	CVector vecTagPos;
	vecTagPos.x = vec->x;
	vecTagPos.y = vec->y;
	vecTagPos.z = vec->z;
	vecTagPos.z += 0.25f + (fDist * 0.0475f);

	CVector vecOut;
	// CSprite::CalcScreenCoors
	((void (*)(CVector*, CVector*, float*, float*, bool, bool))(g_libGTASA + (VER_x32 ? 0x005C57E8 + 1 : 0x6E9DF8)))(&vecTagPos, &vecOut, 0, 0, 0, 0);

	if (vecOut.z < 1.0f) return;

	// mesafeye gore seffaflig ve olcu
	const float maxD = (pNetGame && pNetGame->m_pNetSet) ? pNetGame->m_pNetSet->fNameTagDrawDistance : 70.0f;
	const float fadeFrom = maxD * 0.5f;
	float fade = 1.0f;
	if (fDist > fadeFrom) fade = 1.0f - 0.7f * (fDist - fadeFrom) / ((maxD - fadeFrom) > 1.0f ? (maxD - fadeFrom) : 1.0f);
	fade = PtClamp(fade, 0.3f, 1.0f);
	const float scale = PtClamp(1.1f - fDist / (maxD * 1.5f), 0.7f, 1.0f);

	// "Ad (12)" -> "Ad" + "#12"
	char name[48];
	char idtxt[16] = "";
	snprintf(name, sizeof(name), "%s", szNick);
	{
		char* p = strrchr(name, '(');
		if (p && p > name && p[-1] == ' ') {
			snprintf(idtxt, sizeof(idtxt), "#%d", atoi(p + 1));
			p[-1] = '\0';
		}
	}

	ImFont* font = g_pNickFont ? g_pNickFont : ImGui::GetFont();
	const float fs  = (UISettings::fontSize() / 2) * scale;
	const float fs2 = fs * 0.78f;
	const ImVec2 nsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
	const ImVec2 isz = idtxt[0] ? font->CalcTextSizeA(fs2, FLT_MAX, 0.0f, idtxt) : ImVec2(0.0f, 0.0f);

	const float h     = fs * 1.55f;
	const float pad   = h * 0.38f;
	const float flagH = fs * 0.8f;
	const float flagW = flagH * 1.5f;
	const float gap   = fs * 0.32f;
	float w = pad + flagW + gap + nsz.x + pad;
	if (idtxt[0]) w += gap + isz.x;

	const float x0 = vecOut.x - w * 0.5f;
	const float y0 = vecOut.y;

	ImDrawList* dl = ImGui::GetBackgroundDrawList();

	// pill panel
	dl->AddRectFilled(ImVec2(x0, y0 + 1.5f), ImVec2(x0 + w, y0 + h + 1.5f), IM_COL32(0, 0, 0, (int)(55.0f * fade)), h * 0.5f);
	dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(16, 18, 26, (int)(165.0f * fade)), h * 0.5f);
	dl->AddRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(255, 255, 255, (int)(34.0f * fade)), h * 0.5f);

	// bayraq + ad + id
	float cx = x0 + pad;
	DrawAzFlag(dl, ImVec2(cx, y0 + (h - flagH) * 0.5f), flagW, flagH);
	cx += flagW + gap;

	const ImU32 base = __builtin_bswap32(dwColor | 0x000000FF);
	const ImU32 nameCol = (base & 0x00FFFFFFu) | ((ImU32)((int)(255.0f * fade)) << 24);
	const float ty = y0 + (h - nsz.y) * 0.5f;
	dl->AddText(font, fs, ImVec2(cx + 1.0f, ty + 1.0f), IM_COL32(0, 0, 0, (int)(190.0f * fade)), name);
	dl->AddText(font, fs, ImVec2(cx, ty), nameCol, name);
	cx += nsz.x;
	if (idtxt[0]) {
		cx += gap;
		dl->AddText(font, fs2, ImVec2(cx, y0 + (h - isz.y) * 0.5f), IM_COL32(170, 176, 196, (int)(220.0f * fade)), idtxt);
	}

	// AFK / mikrofon
	if (bAfk || bMicro) {
		const float ic = h * 0.9f;
		if (bAfk && m_pAFKIconTexture)
			dl->AddImage((ImTextureID)m_pAFKIconTexture->raster, ImVec2(x0 + w + gap, y0 + (h - ic) * 0.5f), ImVec2(x0 + w + gap + ic, y0 + (h + ic) * 0.5f));
		if (bMicro && m_pMicroIconTexture)
			dl->AddImage((ImTextureID)m_pMicroIconTexture->raster, ImVec2(x0 - gap - ic, y0 + (h - ic) * 0.5f), ImVec2(x0 - gap, y0 + (h + ic) * 0.5f));
	}

	// can / zireh
	if (fHealth < 0.0f) return;

	float bh = UISettings::nametagBarSize().y * 0.62f * scale;
	if (bh < 3.0f) bh = 3.0f;
	float bw = PtClamp(w * 0.8f, UISettings::nametagBarSize().x, w);
	const float bx = vecOut.x - bw * 0.5f;
	float by = y0 + h + fs * 0.28f;

	auto bar = [&](float y, float frac, ImU32 fill)
	{
		dl->AddRectFilled(ImVec2(bx - 1.0f, y - 1.0f), ImVec2(bx + bw + 1.0f, y + bh + 1.0f), IM_COL32(0, 0, 0, (int)(150.0f * fade)), bh);
		dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw, y + bh), IM_COL32(255, 255, 255, (int)(28.0f * fade)), bh * 0.5f);
		if (frac > 0.0f) {
			float fw = bw * frac;
			if (fw < bh) fw = bh;
			dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + fw, y + bh), fill, bh * 0.5f);
		}
	};

	if (fArmour > 0.0f) {
		bar(by, PtClamp(fArmour / 100.0f, 0.0f, 1.0f), IM_COL32(205, 214, 236, (int)(255.0f * fade)));
		by += bh + 2.5f;
	}
	{
		const float hp = PtClamp(fHealth / 100.0f, 0.0f, 1.0f);
		bar(by, hp, PtHealthColor(hp, (int)(255.0f * fade)));
	}
}
'''

def p_tags(s):
    if '#include "game/World.h"' not in s:
        raise ValueError("World.h include")
    if "static void DrawAzFlag(" not in s:
        raise ValueError("DrawAzFlag yoxdur")
    if s.count("void CPlayerTags::Draw(") != 1:
        raise ValueError("CPlayerTags::Draw sayi")
    s = s.replace('#include "game/World.h"', TAG_INCLUDES, 1)
    pat = re.compile(r'void CPlayerTags::Draw\(.*\Z', re.S)
    new, n = pat.subn(lambda m: TAG_DRAW, s, count=1)
    if n != 1:
        raise ValueError("CPlayerTags::Draw")
    return new

# ======================================================================= 4. nick fontu
NICK_FONT = r'''	// NICKFONT_V4: oyuncu adi ucun oz fontun. Font qovlugunda nick.ttf (ve ya nick.otf) varsa istifade olunur.
	{
		extern ImFont* g_pNickFont;
		g_pNickFont = nullptr;
		std::string nfDir = m_fontPath;
		const size_t nfSlash = nfDir.find_last_of('/');
		nfDir = (nfSlash == std::string::npos) ? std::string("") : nfDir.substr(0, nfSlash + 1);

		static const ImWchar nickRanges[] =
		{
			0x0020, 0x00FF,
			0x0100, 0x017F,   // c g i o s u
			0x018F, 0x018F,   // Ə
			0x0259, 0x0259,   // ə
			0x0400, 0x04FF,
			0
		};
		static const char* nickNames[] = { "nick.ttf", "nick.otf", "Nick.ttf", "Nick.otf", "NICK.TTF", "NICK.OTF" };
		for (const char* nm : nickNames)
		{
			const std::string np = nfDir + nm;
			if (access(np.c_str(), R_OK) != 0) continue;
			ImFontConfig nc;
			nc.PixelSnapH = true;
			g_pNickFont = io.Fonts->AddFontFromFileTTF(np.c_str(), UISettings::fontSize(), &nc, nickRanges);
			if (g_pNickFont) { Log::addParameter("Nick font: %s", np.c_str()); break; }
		}
	}

'''

def p_wrapper(s):
    pat = re.compile(r'\n([ \t]*)createFontTexture\(\);\s*\n\s*m_renderer = new ImGuiRenderer\(ImGui::GetBackgroundDrawList\(\), font\);')
    ms = list(pat.finditer(s))
    if len(ms) != 1:
        raise ValueError("createFontTexture + m_renderer (%d)" % len(ms))
    m = ms[0]
    return s[:m.start()] + "\n" + NICK_FONT + s[m.start() + 1:]

# ======================================================================= isle
if __name__ == "__main__":
    step("keyboard.h: setInputString '}' xetasi", os.path.join(CPP, "gui/samp_widgets/keyboard.h"), "KBH_V4", p_kbh)
    step("gui.cpp: yeni speedometrler", os.path.join(CPP, "gui/gui.cpp"), "HUD_FLOW_V4", p_hud)
    step("playertags.cpp: muasir nametag", os.path.join(CPP, "playertags.cpp"), "PLAYERTAG_V4", p_tags)
    step("imguiwrapper.cpp: nick fontu", os.path.join(CPP, "gui/imguiwrapper.cpp"), "NICKFONT_V4", p_wrapper)
    print("\nBitdi. Geri qaytarmaq: her faylin yaninda *.bakv4")
    print("Oz fontun ucun: font qovlugunda (settings-deki esas font hardadirsa orada) nick.ttf adli fayl yerlesdir.")

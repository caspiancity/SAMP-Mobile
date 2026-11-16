# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v6.py -- layihenin kokunde isle:  python3 apply_v6.py   (apply_v4 ve apply_v5 isleyibse)
#   1) PLAYERTAG: MTA stilinde yigcam ad (outline), kicik bayraq, nazik can/zireh zolagi (pill panel YOX)
#   2) CHAT '?': Az herfleri (ə ğ ı ş ç ö ü) bir baytliq kodla gedir, qebul edende geri cevrilir
#   3) EMOJI: 18 vektor emoji (font atlasina yazilir), klaviaturada emoji paneli, :ad: ile chat-da gedir
#   4) TUNING: /mm -> "Tuning" -> qaraj kamerasi (surusdur = dondur), hundurluk, on/arxa balans, serlik, tekər
# Geri qaytarmaq: her faylin yaninda *.bakv6
import os, re, shutil

ROOT = os.getcwd()
CPP  = os.path.join(ROOT, "app/src/main/cpp/samp")
JAVA = os.path.join(ROOT, "app/src/main/java/com/samp/mobile/game/SAMP.java")

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
    if not os.path.exists(path + ".bakv6"):
        shutil.copy(path, path + ".bakv6")
    wr(path, n, crlf)
    print("[OK]    %s" % name)

def once(s, old, new, what):
    if s.count(old) != 1:
        raise ValueError("%s (%d tapildi)" % (what, s.count(old)))
    return s.replace(old, new, 1)

def sub1(s, pattern, repl, what, flags=0):
    pat = re.compile(pattern, flags)
    n = len(pat.findall(s))
    if n != 1:
        raise ValueError("%s (%d tapildi)" % (what, n))
    return pat.sub(lambda m: repl(m) if callable(repl) else repl, s, count=1)

# =============================================================== 1. PLAYERTAG (MTA)
TAG_INCLUDES = '''#include "game/World.h"
#include <cmath>
#include <cfloat>
#include <cstring>
#include <cstdlib>
#include <algorithm>'''

TAG_DRAW = r'''// ===== PLAYERTAG_V5: MTA stilinde yigcam nametag =====
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
	if (fDist > fadeFrom) fade = 1.0f - 0.6f * (fDist - fadeFrom) / ((maxD - fadeFrom) > 1.0f ? (maxD - fadeFrom) : 1.0f);
	fade = PtClamp(fade, 0.35f, 1.0f);
	const float scale = PtClamp(1.15f - fDist / (maxD * 1.3f), 0.65f, 1.0f);

	// "Ad (12)" -> "Ad" + "(12)"
	char name[48];
	char idtxt[16] = "";
	snprintf(name, sizeof(name), "%s", szNick);
	{
		char* p = strrchr(name, '(');
		if (p && p > name && p[-1] == ' ') {
			snprintf(idtxt, sizeof(idtxt), "(%d)", atoi(p + 1));
			p[-1] = '\0';
		}
	}

	ImFont* font = g_pNickFont ? g_pNickFont : ImGui::GetFont();
	const float fs  = PtClamp(UISettings::fontSize() * 0.30f * scale, 11.0f, 30.0f);
	const float fs2 = fs * 0.80f;
	const ImVec2 nsz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
	const ImVec2 isz = idtxt[0] ? font->CalcTextSizeA(fs2, FLT_MAX, 0.0f, idtxt) : ImVec2(0.0f, 0.0f);

	const float gap   = fs * 0.28f;
	const float flagH = fs * 0.62f;
	const float flagW = flagH * 1.5f;
	float w = flagW + gap + nsz.x;
	if (idtxt[0]) w += gap + isz.x;

	const float x0 = vecOut.x - w * 0.5f;
	const float y0 = vecOut.y;
	const int   A  = (int)(255.0f * fade);

	ImDrawList* dl = ImGui::GetBackgroundDrawList();

	auto outlined = [&](float x, float y, float size, ImU32 col, const char* txt)
	{
		const ImU32 bk = IM_COL32(0, 0, 0, (int)(230.0f * fade));
		dl->AddText(font, size, ImVec2(x - 1.0f, y), bk, txt);
		dl->AddText(font, size, ImVec2(x + 1.0f, y), bk, txt);
		dl->AddText(font, size, ImVec2(x, y - 1.0f), bk, txt);
		dl->AddText(font, size, ImVec2(x, y + 1.0f), bk, txt);
		dl->AddText(font, size, ImVec2(x, y), col, txt);
	};

	// bayraq (kicik) + ad + (id)
	float cx = x0;
	DrawAzFlag(dl, ImVec2(cx, y0 + (nsz.y - flagH) * 0.5f), flagW, flagH);
	cx += flagW + gap;

	const ImU32 base = __builtin_bswap32(dwColor | 0x000000FF);
	const ImU32 nameCol = (base & 0x00FFFFFFu) | ((ImU32)A << 24);
	outlined(cx, y0, fs, nameCol, name);
	cx += nsz.x;
	if (idtxt[0]) {
		cx += gap;
		outlined(cx, y0 + (nsz.y - isz.y) * 0.5f, fs2, IM_COL32(190, 194, 208, A), idtxt);
		cx += isz.x;
	}

	// AFK / mikrofon (kicik)
	if (bAfk || bMicro) {
		const float ic = fs * 1.05f;
		if (bAfk && m_pAFKIconTexture)
			dl->AddImage((ImTextureID)m_pAFKIconTexture->raster, ImVec2(cx + gap, y0), ImVec2(cx + gap + ic, y0 + ic));
		if (bMicro && m_pMicroIconTexture)
			dl->AddImage((ImTextureID)m_pMicroIconTexture->raster, ImVec2(x0 - gap - ic, y0), ImVec2(x0 - gap, y0 + ic));
	}

	// can / zireh (nazik, MTA kimi)
	if (fHealth < 0.0f) return;

	const float bw = PtClamp(w * 0.85f, 38.0f, 96.0f);
	const float bh = PtClamp(fs * 0.30f, 3.0f, 6.0f);
	const float bx = vecOut.x - bw * 0.5f;
	float by = y0 + nsz.y + fs * 0.16f;

	auto bar = [&](float y, float frac, ImU32 fill)
	{
		dl->AddRectFilled(ImVec2(bx - 1.0f, y - 1.0f), ImVec2(bx + bw + 1.0f, y + bh + 1.0f), IM_COL32(0, 0, 0, (int)(210.0f * fade)), 1.5f);
		dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw, y + bh), IM_COL32(50, 50, 56, (int)(200.0f * fade)), 1.0f);
		if (frac > 0.0f) dl->AddRectFilled(ImVec2(bx, y), ImVec2(bx + bw * frac, y + bh), fill, 1.0f);
	};

	{
		const float hp = PtClamp(fHealth / 100.0f, 0.0f, 1.0f);
		bar(by, hp, PtHealthColor(hp, A));
		by += bh + 2.0f;
	}
	if (fArmour > 0.0f)
		bar(by, PtClamp(fArmour / 100.0f, 0.0f, 1.0f), IM_COL32(215, 220, 235, A));
}
'''

def p_tags(s):
    if '#include <cfloat>' not in s:
        s = once(s, '#include "game/World.h"', TAG_INCLUDES, "World.h include")
    if "static void DrawAzFlag(" not in s:
        raise ValueError("DrawAzFlag yoxdur")
    i = s.find("// ===== PLAYERTAG_V4")
    if i < 0:
        i = s.find("void CPlayerTags::Draw(")
    if i < 0:
        raise ValueError("CPlayerTags::Draw")
    return s[:i] + TAG_DRAW

# =============================================================== 2. CHAT Az herfleri + emoji kodlasdirma
CHAT_ENC = r'''// ===== CHATENC_V5: Azerbaycan herfleri + emoji =====
// Telde (serverde) Az herfleri bir baytliq idare kodlari ile, emoji ise :ad: ile gedir.
// Qebul edende hamisi geri cevrilir (yalniz bu client istifade edenler ucun).
static const char* const kAzUtf[14] = {
	"\xC9\x99", "\xC6\x8F", "\xC4\x9F", "\xC4\x9E", "\xC4\xB1", "\xC4\xB0", "\xC5\x9F",
	"\xC5\x9E", "\xC3\xA7", "\xC3\x87", "\xC3\xB6", "\xC3\x96", "\xC3\xBC", "\xC3\x9C" };
static const unsigned char kAzWire[14] = { 1, 2, 3, 4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 19 };
static const char* const kEmoName[18] = {
	"smile", "grin", "joy", "love", "cool", "wink", "tongue", "sad", "cry",
	"angry", "wow", "neutral", "kiss", "heart", "star", "ok", "no", "fire" };

static void ChatAppendEmoji(std::string& out, int idx)
{
	const unsigned cp = 0xE000u + (unsigned)idx;
	out += (char)(0xE0 | (cp >> 12));
	out += (char)(0x80 | ((cp >> 6) & 0x3F));
	out += (char)(0x80 | (cp & 0x3F));
}

static std::string ChatEncode(const std::string& in)
{
	std::string out;
	out.reserve(in.size() + 8);
	const size_t n = in.size();
	for (size_t i = 0; i < n; )
	{
		const unsigned char c = (unsigned char)in[i];
		if (c == 0xEE && i + 2 < n + 0 && (unsigned char)in[i + 1] == 0x80 &&
		    (unsigned char)in[i + 2] >= 0x80 && (unsigned char)in[i + 2] < 0x80 + 18)
		{
			out += ':';
			out += kEmoName[(unsigned char)in[i + 2] - 0x80];
			out += ':';
			i += 3;
			continue;
		}
		if (c >= 0xC3 && c <= 0xC9 && i + 1 < n)
		{
			bool hit = false;
			for (int k = 0; k < 14; ++k)
			{
				if ((unsigned char)kAzUtf[k][0] == c && (unsigned char)kAzUtf[k][1] == (unsigned char)in[i + 1])
				{
					out += (char)kAzWire[k];
					i += 2;
					hit = true;
					break;
				}
			}
			if (hit) continue;
		}
		out += (char)c;
		++i;
	}
	return out;
}

static std::string ChatDecode(const std::string& in)
{
	std::string out;
	out.reserve(in.size() + 8);
	const size_t n = in.size();
	for (size_t i = 0; i < n; )
	{
		const unsigned char c = (unsigned char)in[i];
		if (c < 32)
		{
			bool hit = false;
			for (int k = 0; k < 14; ++k)
				if (kAzWire[k] == c) { out += kAzUtf[k]; hit = true; break; }
			++i;
			if (!hit) out += (char)c;
			continue;
		}
		if (c == ':' && i + 1 < n)
		{
			// :ad:
			size_t j = i + 1;
			while (j < n && j - i <= 9 && in[j] >= 'a' && in[j] <= 'z') ++j;
			if (j < n && in[j] == ':' && j > i + 1)
			{
				const std::string nm = in.substr(i + 1, j - i - 1);
				int idx = -1;
				for (int k = 0; k < 18; ++k) if (nm == kEmoName[k]) { idx = k; break; }
				if (idx >= 0) { ChatAppendEmoji(out, idx); i = j + 1; continue; }
			}
			// :) :( :D :P
			const char d = in[i + 1];
			int idx = -1;
			if (d == ')') idx = 0; else if (d == 'D') idx = 1; else if (d == '(') idx = 7; else if (d == 'P' || d == 'p') idx = 6;
			if (idx >= 0) { ChatAppendEmoji(out, idx); i += 2; continue; }
		}
		else if (c == ';' && i + 1 < n && in[i + 1] == ')') { ChatAppendEmoji(out, 5); i += 2; continue; }
		else if (c == '<' && i + 1 < n && in[i + 1] == '3') { ChatAppendEmoji(out, 13); i += 2; continue; }
		out += (char)c;
		++i;
	}
	return out;
}

'''

def p_chat(s):
    # 1) kodlayici/dekodlayici funksiyalar
    j = s.find("bool VtSync_OnMessage(const std::string& msg);")
    if j < 0:
        j = s.find("void Chat::addMessage(const std::string& message")
    if j < 0:
        raise ValueError("Chat::addMessage")
    s = s[:j] + CHAT_ENC + s[j:]
    # 2) qebul: addMessage + addPlayerMessage
    s = sub1(s, r'void Chat::addMessage\(const std::string& message, const ImColor& color\)\n\{\n',
             "void Chat::addMessage(const std::string& message_in, const ImColor& color)\n{\n        const std::string message = ChatDecode(message_in);\n",
             "Chat::addMessage")
    s = sub1(s, r'void Chat::addPlayerMessage\(const std::string& message, const std::string& nick, const ImColor& nick_color\)\n\{\n',
             "void Chat::addPlayerMessage(const std::string& message_in, const std::string& nick, const ImColor& nick_color)\n{\n        const std::string message = ChatDecode(message_in);\n",
             "Chat::addPlayerMessage")
    # 3) gonderme
    s = sub1(s, r"if \(input\[0\] == '/'\) pNetGame->SendChatCommand\(input\.c_str\(\)\);\s*\n\s*else pNetGame->SendChatMessage\(input\.c_str\(\)\);",
             "{\n                const std::string wire = ChatEncode(input);\n                if (input[0] == '/') pNetGame->SendChatCommand(wire.c_str());\n                else pNetGame->SendChatMessage(wire.c_str());\n        }",
             "SendChat")
    return s

def p_javaenc(s):
    s = once(s, 'toReturn = str.getBytes("windows-1251");', 'toReturn = azEnc(str).getBytes("windows-1251");', "getBytes windows-1251")
    helper = r'''    // AZENC_V5: Az herfleri windows-1251-de olmadigi ucun '?' olurdu -> bir baytliq kodlar (chat.cpp ChatDecode geri cevirir)
    private static String azEnc(String s) {
        if (s == null) return "";
        String[] from = { "\u0259", "\u018F", "\u011F", "\u011E", "\u0131", "\u0130", "\u015F", "\u015E", "\u00E7", "\u00C7", "\u00F6", "\u00D6", "\u00FC", "\u00DC" };
        char[] to = { 1, 2, 3, 4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 19 };
        for (int i = 0; i < from.length; i++) s = s.replace(from[i], String.valueOf(to[i]));
        return s;
    }

'''
    return once(s, "    private native void onInputEnd(byte[] str);", helper + "    private native void onInputEnd(byte[] str);", "onInputEnd")

# =============================================================== 3. EMOJI (atlas + klaviatura)
EMOJI_GEN = r'''// ===== EMOJI_V5: vektor emoji -> font atlasina (PUA U+E000..U+E011) =====
#include <vector>
#include <functional>
#include <cmath>
namespace EmojiGen
{
	typedef std::function<bool(float, float)> Fn;
	struct Layer { Fn in; unsigned char r, g, b, a; };
	typedef std::vector<Layer> Layers;

	static Layer L(Fn f, int r, int g, int b, int a = 255)
	{
		Layer l; l.in = f; l.r = (unsigned char)r; l.g = (unsigned char)g; l.b = (unsigned char)b; l.a = (unsigned char)a; return l;
	}
	static Fn Ell(float cx, float cy, float rx, float ry)
	{
		return [=](float x, float y) { const float dx = (x - cx) / rx, dy = (y - cy) / ry; return dx * dx + dy * dy <= 1.0f; };
	}
	static Fn Arc(float cx, float cy, float R, float th, float a0, float a1)
	{
		return [=](float x, float y) {
			const float dx = x - cx, dy = y - cy;
			const float d = sqrtf(dx * dx + dy * dy);
			if (fabsf(d - R) > th * 0.5f) return false;
			float a = atan2f(dy, dx);
			if (a < 0.0f) a += 6.2831853f;
			return a >= a0 && a <= a1;
		};
	}
	static Fn Cap(float x0, float y0, float x1, float y1, float r)
	{
		return [=](float x, float y) {
			const float vx = x1 - x0, vy = y1 - y0;
			float t = ((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy + 1e-9f);
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			const float px = x0 + vx * t - x, py = y0 + vy * t - y;
			return px * px + py * py <= r * r;
		};
	}
	static Fn RRect(float cx, float cy, float hw, float hh, float rad)
	{
		return [=](float x, float y) {
			const float qx = fabsf(x - cx) - (hw - rad), qy = fabsf(y - cy) - (hh - rad);
			const float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
			return ox * ox + oy * oy <= rad * rad && (qx <= rad && qy <= rad);
		};
	}
	static Fn Heart(float cx, float cy, float s)
	{
		return [=](float x, float y) {
			const float px = (x - cx) / s, py = -(y - cy) / s + 0.1f;
			const float a = px * px + py * py - 1.0f;
			return a * a * a - px * px * py * py * py <= 0.0f;
		};
	}
	static Fn Tri(float ax, float ay, float bx, float by, float cx, float cy)
	{
		return [=](float x, float y) {
			const float d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by);
			const float d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy);
			const float d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay);
			const bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0), pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
			return !(neg && pos);
		};
	}
	static Fn Star(float cx, float cy, float R, float r)
	{
		return [=](float x, float y) {
			float vx[10], vy[10];
			for (int i = 0; i < 10; ++i) {
				const float a = -1.5707963f + (float)i * 0.62831853f;
				const float rr = (i & 1) ? r : R;
				vx[i] = cx + cosf(a) * rr; vy[i] = cy + sinf(a) * rr;
			}
			bool in = false;
			for (int i = 0, j = 9; i < 10; j = i++)
				if (((vy[i] > y) != (vy[j] > y)) && (x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i])) in = !in;
			return in;
		};
	}
	static Fn Or(Fn a, Fn b) { return [=](float x, float y) { return a(x, y) || b(x, y); }; }
	static Fn And(Fn a, Fn b) { return [=](float x, float y) { return a(x, y) && b(x, y); }; }
	static Fn Below(float yy) { return [=](float, float y) { return y > yy; }; }

	static const float PI_ = 3.14159265f;

	static void Face(Layers& ls, int fr = 255, int fg = 204, int fb = 64, int rr = 214, int rg = 140, int rb = 20)
	{
		ls.push_back(L(Ell(0, 0, 0.94f, 0.94f), rr, rg, rb));
		ls.push_back(L(Ell(0, 0, 0.86f, 0.86f), fr, fg, fb));
	}
	static void Eyes(Layers& ls, float y = -0.2f)
	{
		ls.push_back(L(Ell(-0.33f, y, 0.09f, 0.14f), 70, 45, 20));
		ls.push_back(L(Ell(0.33f, y, 0.09f, 0.14f), 70, 45, 20));
	}
	static void Smile(Layers& ls)  { ls.push_back(L(Arc(0, 0.02f, 0.52f, 0.10f, 0.18f * PI_, 0.82f * PI_), 70, 45, 20)); }
	static void HappyEyes(Layers& ls)
	{
		ls.push_back(L(Arc(-0.33f, -0.1f, 0.17f, 0.09f, PI_, 2 * PI_), 70, 45, 20));
		ls.push_back(L(Arc(0.33f, -0.1f, 0.17f, 0.09f, PI_, 2 * PI_), 70, 45, 20));
	}
	static void OpenMouth(Layers& ls, bool teeth)
	{
		Fn d = And(Ell(0, 0.1f, 0.55f, 0.52f), Below(0.12f));
		ls.push_back(L(d, 95, 30, 25));
		if (teeth) ls.push_back(L(And(d, [](float, float y) { return y < 0.27f; }), 255, 255, 255));
	}

	static Layers Build(int idx)
	{
		Layers ls;
		switch (idx)
		{
		case 0:  Face(ls); HappyEyes(ls); Smile(ls);
			ls.push_back(L(Ell(-0.6f, 0.2f, 0.1f, 0.07f), 255, 110, 100, 130));
			ls.push_back(L(Ell(0.6f, 0.2f, 0.1f, 0.07f), 255, 110, 100, 130)); break;
		case 1:  Face(ls); Eyes(ls); OpenMouth(ls, true); break;
		case 2:  Face(ls); HappyEyes(ls); OpenMouth(ls, false);
			ls.push_back(L(And(Ell(0, 0.62f, 0.26f, 0.2f), Below(0.45f)), 255, 105, 130));
			ls.push_back(L(Ell(-0.74f, -0.02f, 0.1f, 0.17f), 90, 180, 255));
			ls.push_back(L(Ell(0.74f, -0.02f, 0.1f, 0.17f), 90, 180, 255)); break;
		case 3:  Face(ls);
			ls.push_back(L(Heart(-0.34f, -0.2f, 0.19f), 230, 35, 60));
			ls.push_back(L(Heart(0.34f, -0.2f, 0.19f), 230, 35, 60)); Smile(ls); break;
		case 4:  Face(ls);
			ls.push_back(L(RRect(-0.3f, -0.14f, 0.28f, 0.17f, 0.07f), 25, 25, 30));
			ls.push_back(L(RRect(0.3f, -0.14f, 0.28f, 0.17f, 0.07f), 25, 25, 30));
			ls.push_back(L(Cap(-0.05f, -0.17f, 0.05f, -0.17f, 0.04f), 25, 25, 30));
			ls.push_back(L(Ell(-0.42f, -0.2f, 0.07f, 0.04f), 255, 255, 255, 90)); Smile(ls); break;
		case 5:  Face(ls);
			ls.push_back(L(Ell(-0.33f, -0.2f, 0.09f, 0.14f), 70, 45, 20));
			ls.push_back(L(Arc(0.33f, -0.18f, 0.15f, 0.09f, PI_, 2 * PI_), 70, 45, 20)); Smile(ls); break;
		case 6:  Face(ls); Eyes(ls);
			ls.push_back(L(Ell(0.0f, 0.52f, 0.17f, 0.22f), 255, 105, 135));
			ls.push_back(L(Cap(-0.4f, 0.32f, 0.4f, 0.32f, 0.05f), 70, 45, 20)); break;
		case 7:  Face(ls); Eyes(ls);
			ls.push_back(L(Arc(0, 0.74f, 0.32f, 0.09f, 1.15f * PI_, 1.85f * PI_), 70, 45, 20)); break;
		case 8:  Face(ls); Eyes(ls);
			ls.push_back(L(Arc(0, 0.74f, 0.32f, 0.09f, 1.15f * PI_, 1.85f * PI_), 70, 45, 20));
			ls.push_back(L(Cap(-0.36f, -0.02f, -0.4f, 0.48f, 0.07f), 90, 180, 255));
			ls.push_back(L(Cap(0.36f, -0.02f, 0.4f, 0.48f, 0.07f), 90, 180, 255)); break;
		case 9:  Face(ls, 235, 75, 50, 170, 35, 20);
			ls.push_back(L(Cap(-0.58f, -0.45f, -0.14f, -0.22f, 0.065f), 60, 15, 10));
			ls.push_back(L(Cap(0.58f, -0.45f, 0.14f, -0.22f, 0.065f), 60, 15, 10));
			ls.push_back(L(Ell(-0.33f, -0.06f, 0.08f, 0.11f), 60, 15, 10));
			ls.push_back(L(Ell(0.33f, -0.06f, 0.08f, 0.11f), 60, 15, 10));
			ls.push_back(L(Arc(0, 0.72f, 0.3f, 0.09f, 1.15f * PI_, 1.85f * PI_), 60, 15, 10)); break;
		case 10: Face(ls); Eyes(ls);
			ls.push_back(L(Ell(0, 0.4f, 0.14f, 0.2f), 95, 30, 25)); break;
		case 11: Face(ls); Eyes(ls);
			ls.push_back(L(Cap(-0.3f, 0.42f, 0.3f, 0.42f, 0.05f), 70, 45, 20)); break;
		case 12: Face(ls);
			ls.push_back(L(Ell(-0.33f, -0.2f, 0.09f, 0.14f), 70, 45, 20));
			ls.push_back(L(Arc(0.33f, -0.18f, 0.15f, 0.09f, PI_, 2 * PI_), 70, 45, 20));
			ls.push_back(L(Ell(0.04f, 0.4f, 0.11f, 0.1f), 230, 50, 70));
			ls.push_back(L(Heart(0.52f, 0.2f, 0.17f), 235, 40, 70)); break;
		case 13: ls.push_back(L(Heart(0, 0, 0.8f), 228, 35, 60));
			ls.push_back(L(Ell(-0.36f, -0.34f, 0.16f, 0.1f), 255, 255, 255, 110)); break;
		case 14: ls.push_back(L(Star(0, 0.05f, 0.98f, 0.42f), 225, 150, 0));
			ls.push_back(L(Star(0, 0.05f, 0.84f, 0.34f), 255, 205, 40)); break;
		case 15: ls.push_back(L(RRect(0, 0, 0.88f, 0.88f, 0.26f), 56, 184, 92));
			ls.push_back(L(Cap(-0.4f, 0.02f, -0.12f, 0.32f, 0.11f), 255, 255, 255));
			ls.push_back(L(Cap(-0.12f, 0.32f, 0.45f, -0.3f, 0.11f), 255, 255, 255)); break;
		case 16: ls.push_back(L(RRect(0, 0, 0.88f, 0.88f, 0.26f), 226, 56, 56));
			ls.push_back(L(Cap(-0.4f, -0.4f, 0.4f, 0.4f, 0.11f), 255, 255, 255));
			ls.push_back(L(Cap(0.4f, -0.4f, -0.4f, 0.4f, 0.11f), 255, 255, 255)); break;
		default: // 17 = atesh
			ls.push_back(L(Or(Ell(0, 0.3f, 0.62f, 0.62f), Tri(0.08f, -0.98f, -0.56f, 0.15f, 0.56f, 0.15f)), 255, 110, 20));
			ls.push_back(L(Or(Ell(0, 0.46f, 0.36f, 0.36f), Tri(0.0f, -0.3f, -0.3f, 0.35f, 0.3f, 0.35f)), 255, 215, 60)); break;
		}
		return ls;
	}

	static void Render(const Layers& ls, int S, unsigned int* out)
	{
		const int SS = 3;
		for (int py = 0; py < S; ++py)
			for (int px = 0; px < S; ++px)
			{
				float R = 0, G = 0, B = 0, A = 0;
				for (int sy = 0; sy < SS; ++sy)
					for (int sx = 0; sx < SS; ++sx)
					{
						const float u = (((float)px + ((float)sx + 0.5f) / SS) / (float)S) * 2.0f - 1.0f;
						const float v = (((float)py + ((float)sy + 0.5f) / SS) / (float)S) * 2.0f - 1.0f;
						float r = 0, g = 0, b = 0, a = 0;
						for (size_t k = 0; k < ls.size(); ++k)
							if (ls[k].in(u, v)) {
								const float la = (float)ls[k].a / 255.0f;
								r = ls[k].r * la + r * (1.0f - la);
								g = ls[k].g * la + g * (1.0f - la);
								b = ls[k].b * la + b * (1.0f - la);
								a = la + a * (1.0f - la);
							}
						R += r; G += g; B += b; A += a;
					}
				const float inv = 1.0f / (float)(SS * SS);
				R *= inv; G *= inv; B *= inv; A *= inv;
				unsigned int rr = 0, gg = 0, bb = 0, aa = (unsigned int)(A * 255.0f + 0.5f);
				if (A > 0.001f) {
					rr = (unsigned int)(R / A + 0.5f); gg = (unsigned int)(G / A + 0.5f); bb = (unsigned int)(B / A + 0.5f);
					if (rr > 255) rr = 255; if (gg > 255) gg = 255; if (bb > 255) bb = 255;
				}
				if (aa > 255) aa = 255;
				out[py * S + px] = rr | (gg << 8) | (bb << 16) | (aa << 24);
			}
	}

	static int s_rect[18];
	static int s_size = 0;
}

void Emoji_Register(ImFontAtlas* atlas, ImFont* font, int S)
{
	EmojiGen::s_size = S;
	for (int i = 0; i < 18; ++i)
		EmojiGen::s_rect[i] = atlas->AddCustomRectFontGlyph(font, (ImWchar)(0xE000 + i), S, S, (float)S + 1.0f, ImVec2(0.0f, (float)S * 0.08f));
}

void Emoji_Fill(ImFontAtlas* atlas)
{
	unsigned char* px32 = nullptr;
	int w = 0, h = 0;
	atlas->GetTexDataAsRGBA32(&px32, &w, &h);   // atlasi qurur + RGBA32 buferi yaradir
	if (!px32 || EmojiGen::s_size <= 0) return;

	const int S = EmojiGen::s_size;
	std::vector<unsigned int> bmp((size_t)S * (size_t)S);
	for (int i = 0; i < 18; ++i)
	{
		const ImFontAtlas::CustomRect* r = atlas->GetCustomRectByIndex(EmojiGen::s_rect[i]);
		if (!r || !r->IsPacked()) continue;
		EmojiGen::Render(EmojiGen::Build(i), S, bmp.data());
		for (int y = 0; y < S; ++y)
			for (int x = 0; x < S; ++x)
			{
				const unsigned int c = bmp[(size_t)y * (size_t)S + (size_t)x];
				const size_t o = (size_t)(r->Y + y) * (size_t)w + (size_t)(r->X + x);
				((unsigned int*)px32)[o] = c;
				if (atlas->TexPixelsAlpha8) atlas->TexPixelsAlpha8[o] = (unsigned char)(c >> 24);
			}
	}
}

'''

EMOJI_CALL = r'''	// EMOJI_V5: vektor emoji glifleri
	{
		extern void Emoji_Register(ImFontAtlas* atlas, ImFont* font, int size);
		extern void Emoji_Fill(ImFontAtlas* atlas);
		int es = (int)UISettings::fontSize();
		if (es < 24) es = 24;
		if (es > 64) es = 64;
		Emoji_Register(io.Fonts, font, es);
		Emoji_Fill(io.Fonts);
	}

'''

def p_wrapper(s):
    pat = re.compile(r'\n([ \t]*)createFontTexture\(\);\s*\n\s*m_renderer = new ImGuiRenderer\(ImGui::GetBackgroundDrawList\(\), font\);')
    ms = list(pat.finditer(s))
    if len(ms) != 1:
        raise ValueError("createFontTexture + m_renderer (%d)" % len(ms))
    m = ms[0]
    hdr = None
    for h in re.finditer(r'\n[A-Za-z_][\w:<>\*& ]*\s+[A-Za-z_]\w*::\w+\([^)]*\)\s*\n\{', s[:m.start()]):
        hdr = h
    if hdr is None:
        raise ValueError("initialize() basligi")
    s = s[:m.start()] + "\n" + EMOJI_CALL + s[m.start() + 1:]
    return s[:hdr.start() + 1] + EMOJI_GEN + s[hdr.start() + 1:]

def p_kbh(s):
    s = sub1(s, r'(SPACE,\s*SEND)(\s*\};)', lambda m: "SPACE,\n\t\tSEND,\n\t\tEMOJI" + m.group(2), "ExtendedKey enum")
    s = sub1(s, r'(AZE_UPPER,\s*NUM)(\s*\};)', lambda m: "AZE_UPPER,\n\t\tNUM,\n\t\tEMOJI" + m.group(2), "Type enum")
    s = sub1(s, r'class KeyboardLayoutNUM : public KeyboardLayout\n\{\npublic:\n\tKeyboardLayoutNUM\(\);\n\tvirtual void performLayout\(\) override;\n\};',
             lambda m: m.group(0) + "\n\nclass KeyboardLayoutEMOJI : public KeyboardLayout   // EMOJI_V5\n{\npublic:\n\tKeyboardLayoutEMOJI();\n\tvirtual void performLayout() override;\n};",
             "KeyboardLayoutNUM sinifi")
    s = once(s, "KeyboardLayoutNUM* m_layoutNUM;", "KeyboardLayoutNUM* m_layoutNUM;\n\tKeyboardLayoutEMOJI* m_layoutEMOJI;", "m_layoutNUM")
    return s

KB_EMOJI_LAYOUT = r'''/* KeyboardLayoutEMOJI */   // EMOJI_V5

KeyboardLayoutEMOJI::KeyboardLayoutEMOJI() : KeyboardLayout() {}

static std::string KbEmojiUtf8(int idx)
{
	const unsigned cp = 0xE000u + (unsigned)idx;
	std::string s;
	s += (char)(0xE0 | (cp >> 12));
	s += (char)(0x80 | ((cp >> 6) & 0x3F));
	s += (char)(0x80 | (cp & 0x3F));
	return s;
}

void KeyboardLayoutEMOJI::performLayout()
{
	ImVec2 keySize = ImVec2(width() / 6, UISettings::keyboardRowHeight());
	for (int r = 0; r < 3; ++r)
	{
		m_rows[r]->setPadding(0.0f);
		m_rows[r]->setSpacing(0.0f);
		for (int c = 0; c < 6; ++c)
		{
			const std::string e = KbEmojiUtf8(r * 6 + c);
			m_rows[r]->addKey(e, e);
		}
	}

	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 2, 0.0f));
	m_rows[3]->addKey(ExtendedKey::BACKSPACE);
	m_rows[3]->addKey(ExtendedKey::SEND);

	KeyboardLayout::performLayout();
}

'''

def p_kbcpp(s):
    s = once(s, "\tm_layoutNUM->setVisible(false);\n",
             "\tm_layoutNUM->setVisible(false);\n\n\tm_layoutEMOJI = new KeyboardLayoutEMOJI();   // EMOJI_V5\n\tthis->addChild(m_layoutEMOJI);\n\tm_layoutEMOJI->setPadding(0.0f);\n\tm_layoutEMOJI->setSpacing(0.0f);\n\tm_layoutEMOJI->setVisible(false);\n",
             "m_layoutNUM->setVisible(false)")
    s = once(s, "\tm_layoutNUM->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));\n",
             "\tm_layoutNUM->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));\n\n\t/* EMOJI */\n\tm_layoutEMOJI->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));\n\tm_layoutEMOJI->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));\n",
             "m_layoutNUM->setPosition")
    s = sub1(s, r'case KeyboardLayout::Type::NUM:\n\t\t\tm_layoutNUM->setVisible\(true\);\n\t\t\tm_activeLayout = m_layoutNUM;\n\t\t\tbreak;',
             lambda m: m.group(0) + "\n\n\t\tcase KeyboardLayout::Type::EMOJI:\n\t\t\tm_layoutEMOJI->setVisible(true);\n\t\t\tm_activeLayout = m_layoutEMOJI;\n\t\t\tbreak;",
             "setActiveLayout NUM")
    s = sub1(s, r'case ExtendedKey::LANG_AZE:\n\t\t\tthis->setCaption\(std::string\("AZE"\)\);\n\t\t\tbreak;',
             lambda m: m.group(0) + '\n\t\tcase ExtendedKey::EMOJI:\n\t\t\tthis->setCaption(std::string("\\xEE\\x80\\x80"));\n\t\t\tbreak;',
             "Key ctor LANG_AZE")
    s = sub1(s, r'case ExtendedKey::LANG_ENG:\n\t\t\tkeyboard->setActiveLayout\(KeyboardLayout::Type::ENG_LOWER\);\n\t\t\tbreak;',
             lambda m: m.group(0) + "\n\n\t\tcase ExtendedKey::EMOJI:\n\t\t\tkeyboard->setActiveLayout(KeyboardLayout::Type::EMOJI);\n\t\t\tbreak;",
             "touchPop LANG_ENG")
    n = s.count("m_rows[3]->addKey(ExtendedKey::LANG_AZE);")
    if n < 4:
        raise ValueError("alt sira LANG_AZE (%d)" % n)
    s = s.replace("m_rows[3]->addKey(ExtendedKey::LANG_AZE);", "m_rows[3]->addKey(ExtendedKey::LANG_AZE);\n\tm_rows[3]->addKey(ExtendedKey::EMOJI);")
    # EMOJI layout: yeni sinfin icinde de LANG_AZE var -> artiq yuxarida ikiqat yazilmasin: sinif sonda elave olunur
    s = once(s, "KeyboardHistory::KeyboardHistory()\n{", KB_EMOJI_LAYOUT + "KeyboardHistory::KeyboardHistory()\n{", "KeyboardHistory()")
    return s

# =============================================================== 4. TUNING (qaraj kamerasi)
GARAGE = r'''// ===== GARAGE_V5: qaraj kamerasi + tuning =====
namespace Garage
{
    bool  g_on = false;
    static float s_yaw = 0.8f, s_pitch = 0.20f, s_dist = 7.5f;
    static float s_lx = 0.0f, s_ly = 0.0f;
    static bool  s_drag = false;
    static float s_height = 0.0f;
    static CVehicleGTA* s_veh = nullptr;

    static void Begin(CVehicleGTA* v)
    {
        if (!v) return;
        s_veh = v; s_height = 0.0f; s_drag = false;
        s_dist = 7.5f; s_pitch = 0.20f;
        RwMatrix m;
        v->GetMatrix(&m);
        s_yaw = atan2f(m.up.y, m.up.x) + 0.8f;
        g_on = true;
    }

    static void End()
    {
        if (!g_on) return;
        g_on = false;
        CCamera::SetBehindPlayer();
    }

    static void Render(CVehicle* pVeh, float dt)
    {
        using namespace mm;
        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 disp = io.DisplaySize;
        CVehicleGTA* v = pVeh ? pVeh->m_pVehicle : nullptr;
        if (!v) { End(); return; }
        if (v != s_veh) { s_veh = v; s_height = 0.0f; }

        s_dl = ImGui::GetForegroundDrawList();
        s_A = 1.0f;
        s_U = std::min(disp.y / 720.0f, disp.x / 1000.0f);
        const MMTouch& t = g_mmTouch;

        const float bh = S(176.0f);
        const Rc bar{ disp.x * 0.5f - S(480.0f), disp.y - bh - S(10.0f), S(960.0f), bh };

        // kamera: bosh sahede surusdur = dondur
        if (t.pressed && s_act == 0 && !In(bar, t.x, t.y)) { s_drag = true; s_lx = t.x; s_ly = t.y; }
        if (s_drag) {
            if (t.down) {
                s_yaw   -= (t.x - s_lx) * 0.006f;
                s_pitch += (t.y - s_ly) * 0.004f;
                s_lx = t.x; s_ly = t.y;
                if (s_pitch < 0.02f) s_pitch = 0.02f;
                if (s_pitch > 1.2f)  s_pitch = 1.2f;
            } else s_drag = false;
        } else {
            s_yaw += dt * 0.12f;   // yavas avtomatik donme
        }

        RwMatrix m;
        v->GetMatrix(&m);
        const float cp = cosf(s_pitch), sp = sinf(s_pitch);
        ScriptCommand(&set_camera_position,
                      m.pos.x + cosf(s_yaw) * cp * s_dist,
                      m.pos.y + sinf(s_yaw) * cp * s_dist,
                      m.pos.z + 0.5f + sp * s_dist, 0.0f, 0.0f, 0.0f);
        ScriptCommand(&point_camera, m.pos.x, m.pos.y, m.pos.z + 0.3f, 2);

        // panel
        Fill(bar, Col(20, 22, 30, 236), S(22.0f));
        Stroke(bar, Col(255, 255, 255, 40), S(22.0f), S(1.0f));
        TxtB(S(20.0f), bar.x + S(22.0f), bar.y + S(12.0f), Col(240, 243, 250), "Tuning");
        Txt(S(13.0f), bar.x + S(110.0f), bar.y + S(17.0f), Col(124, 132, 152), "Boş yerdə sürüşdür = maşını fırlat");

        const float gap = S(12.0f);
        const float cwid = (bar.w - S(44.0f) - gap * 3.0f) / 4.0f;
        const float cy = bar.y + S(44.0f);

        auto stepper = [&](int id, const Rc& c, const char* label, const char* val) -> int {
            Fill(c, Col(255, 255, 255, 16), S(14.0f));
            Txt(S(13.0f), c.x + S(12.0f), c.y + S(6.0f), Col(150, 158, 180), label);
            const Rc bm{ c.x + S(8.0f), c.y + c.h - S(36.0f), S(40.0f), S(30.0f) };
            const Rc bp{ c.x + c.w - S(48.0f), c.y + c.h - S(36.0f), S(40.0f), S(30.0f) };
            int dir = 0;
            if (Tap(id, bm, true))     dir = -1;
            if (Tap(id + 1, bp, true)) dir = +1;
            const float p1 = Prs(id, dt), p2 = Prs(id + 1, dt);
            Fill(bm, Col(255, 255, 255, 24 + (int)(36.0f * p1)), S(9.0f));
            TxtC(S(20.0f), bm, Col(240, 244, 252), "-", true);
            Fill(bp, Col(255, 255, 255, 24 + (int)(36.0f * p2)), S(9.0f));
            TxtC(S(20.0f), bp, Col(240, 244, 252), "+", true);
            TxtC(S(16.0f), Rc{ c.x + S(50.0f), bm.y, c.w - S(100.0f), bm.h }, Col(236, 240, 250), val, true);
            return dir;
        };

        char vb[24];
        int d;

        // 1) hundurluk (butun suspansiya: yuxari ve asagi hedd birlikde)
        snprintf(vb, sizeof(vb), "%+.2f", s_height);
        d = stepper(110, Rc{ bar.x + S(22.0f), cy, cwid, S(76.0f) }, "Hündürlük (tam)", vb);
        if (d) {
            const float st = 0.02f * (float)d;
            float lo = 0.0f, up = 0.0f;
            if (HndExt_Get(pVeh, "susplower", &lo) && HndExt_Get(pVeh, "suspupper", &up)) {
                HndExt_Set(pVeh, "susplower", lo + st);
                HndExt_Set(pVeh, "suspupper", up + st);
                s_height += st;
            }
        }

        // 2) on / arxa balans
        float bias = 0.5f;
        const bool hb = HndExt_Get(pVeh, "suspbias", &bias);
        snprintf(vb, sizeof(vb), hb ? "%.2f" : "-", bias);
        d = stepper(112, Rc{ bar.x + S(22.0f) + (cwid + gap), cy, cwid, S(76.0f) }, "Ön / Arxa balans", vb);
        if (d && hb) HndExt_Set(pVeh, "suspbias", Clamp(bias + 0.05f * (float)d, 0.05f, 0.95f));

        // 3) serlik
        float force = 0.0f;
        const bool hf = HndExt_Get(pVeh, "suspforce", &force);
        snprintf(vb, sizeof(vb), hf ? "%.2f" : "-", force);
        d = stepper(114, Rc{ bar.x + S(22.0f) + (cwid + gap) * 2.0f, cy, cwid, S(76.0f) }, "Süspansiyon sərtliyi", vb);
        if (d && hf && force > 0.0001f) HndExt_Set(pVeh, "suspforce", force * (1.0f + 0.1f * (float)d));

        // 4) tekər olcusu (sinaq)
        float ws = v->m_wheelScale;
        if (ws < 0.05f || ws > 5.0f) ws = 1.0f;
        snprintf(vb, sizeof(vb), "%.2f", ws);
        d = stepper(116, Rc{ bar.x + S(22.0f) + (cwid + gap) * 3.0f, cy, cwid, S(76.0f) }, "Təkər ölçüsü (sınaq)", vb);
        if (d) v->m_wheelScale = Clamp(ws + 0.05f * (float)d, 0.5f, 2.0f);

        // alt duymeler
        const float yb = bar.y + bar.h - S(46.0f);
        if (mm2::SmallBtn(120, Rc{ bar.x + S(22.0f),  yb, S(130.0f), S(36.0f) }, "Yaxınlaş", dt, true, false)) s_dist = std::max(3.5f, s_dist - 0.8f);
        if (mm2::SmallBtn(121, Rc{ bar.x + S(162.0f), yb, S(130.0f), S(36.0f) }, "Uzaqlaş",  dt, true, false)) s_dist = std::min(14.0f, s_dist + 0.8f);
        if (mm2::SmallBtn(122, Rc{ bar.x + bar.w - S(318.0f), yb, S(140.0f), S(36.0f) }, "Sıfırla", dt, true, false)) {
            HndExt_Reset(pVeh);
            s_height = 0.0f;
        }
        if (mm2::SmallBtn(123, Rc{ bar.x + bar.w - S(162.0f), yb, S(140.0f), S(36.0f) }, "Bağla", dt, true, true)) End();

        if (!g_mmTouch.down) s_act = 0;
    }
}

'''

TUNING_TAB = r'''    else if (s_tab == 3)                                    // TUNING (GARAGE_V5)
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Tuning yalnız maşının içində işləyir");
        } else {
            Txt(S(17.0f), cx0, y0, Col(228, 232, 244), "Maşını qaraj kamerası ilə tuning et");
            Txt(S(13.0f), cx0, y0 + S(28.0f), Col(128, 136, 158), "Kamera maşının ətrafında dönür. Hündürlük, ön/arxa balans, sərtlik və təkər ölçüsü.");
            if (mm2::SmallBtn(100, Rc{ cx0, y0 + S(64.0f), S(330.0f), S(54.0f) }, "Tuning et  (qaraj kamerası)", dt, inputPage, true) && gtaVeh)
                Garage::Begin(gtaVeh);
            Txt(S(13.0f), cx0, y0 + S(136.0f), Col(128, 136, 158), "• Boş yerdə sürüşdür: maşını fırlat");
            Txt(S(13.0f), cx0, y0 + S(160.0f), Col(128, 136, 158), "• - / + ilə dəyişdir, Sıfırla standarta qaytarır");
            Txt(S(13.0f), cx0, y0 + S(184.0f), Col(128, 136, 158), "• Bağla düyməsi kameranı oyuna qaytarır");
        }
    }
'''

def p_gui(s):
    # qaraj modulu RenderHandlingDialog-dan evvel
    s = once(s, "\nvoid RenderHandlingDialog()\n{\n    using namespace mm;\n",
             "\n" + GARAGE + "void RenderHandlingDialog()\n{\n    using namespace mm;\n", "RenderHandlingDialog basi")
    # menyu baglananda qaraj da baglansin
    s = once(s, "\n    const bool want = g_bShowHandlingDlg;\n",
             "\n    const bool want = g_bShowHandlingDlg;\n    if (!want && Garage::g_on) Garage::End();   // GARAGE_V5\n", "want")
    # klaviatura bloku ile s_t=Clamp arasinda qaraj rejimi
    s = once(s, "\n    s_t = Clamp(s_t + (want ? dt / 0.38f : -dt / 0.18f), 0.0f, 1.0f);\n",
             "\n    if (want && Garage::g_on) {   // GARAGE_V5: panel gizlenir, yalniz alt zolaq\n        s_hndRectValid = false;\n        Garage::Render(pVeh, dt);\n        return;\n    }\n\n    s_t = Clamp(s_t + (want ? dt / 0.38f : -dt / 0.18f), 0.0f, 1.0f);\n",
             "s_t = Clamp")
    # 5 tab
    s = once(s, 'static const char* kTabs[4] = { "Maşın", "Handling", "Far", "Spidometr" };',
             'static const char* kTabs[5] = { "Maşın", "Handling", "Far", "Tuning", "Spidometr" };', "kTabs")
    s = once(s, "const float tw = seg.w / 4.0f;", "const float tw = seg.w / 5.0f;", "tw")
    s = sub1(s, r'for \(int i = 0; i < 4; \+\+i\) \{\n(\s*)const Rc ti\{ seg\.x \+ i \* tw',
             lambda m: "for (int i = 0; i < 5; ++i) {\n" + m.group(1) + "const Rc ti{ seg.x + i * tw", "tab dovru")
    # tuning sehifesi spidometrden evvel
    m = re.search(r'\n    else[ \t]+// SPIDOMETR\n', s)
    if not m:
        raise ValueError("SPIDOMETR sehifesi")
    return s[:m.start() + 1] + TUNING_TAB + s[m.start() + 1:]

# =============================================================== isle
if __name__ == "__main__":
    step("PLAYERTAG: MTA stili",           os.path.join(CPP, "playertags.cpp"), "PLAYERTAG_V5", p_tags)
    step("CHAT: Az herfleri + emoji kodu", os.path.join(CPP, "gui/samp_widgets/chat.cpp"), "CHATENC_V5", p_chat)
    step("CHAT: Java klaviatura Az kodu",  JAVA, "AZENC_V5", p_javaenc)
    step("EMOJI: atlas (imguiwrapper)",    os.path.join(CPP, "gui/imguiwrapper.cpp"), "EMOJI_V5", p_wrapper)
    step("EMOJI: klaviatura.h",            os.path.join(CPP, "gui/samp_widgets/keyboard.h"), "KeyboardLayoutEMOJI", p_kbh)
    step("EMOJI: klaviatura.cpp",          os.path.join(CPP, "gui/samp_widgets/keyboard.cpp"), "EMOJI_V5", p_kbcpp)
    step("TUNING: qaraj kamerasi (gui.cpp)", os.path.join(CPP, "gui/gui.cpp"), "GARAGE_V5", p_gui)
    print("\nBitdi. Geri qaytarmaq: her faylin yaninda *.bakv6")

#include "../main.h"
#include "imguiwrapper.h"
#include "uisettings.h"
#include <unistd.h>
#include <dirent.h>
#include <vector>
#include <string>
#include <algorithm>

#include "../game/game.h"
#include "../net/netgame.h"

// voice
#include "../voice_new/MicroIcon.h"
#include "../voice_new/SpeakerList.h"
#include "../voice_new/include/util/Render.h"


extern UI* pUI;
extern CNetGame* pNetGame;


ImGuiWrapper::ImGuiWrapper(const ImVec2& display_size, const std::string& font_path)
{
	m_displaySize = display_size;
	m_renderer = 0;
	m_fontRaster = nullptr;
	m_fontPath = font_path;

	m_vertexBuffer = nullptr;
	m_vertexBufferSize = 10000;
}

ImGuiWrapper::~ImGuiWrapper()
{
	shutdown();
}

// ===== EMOJI_V5: vektor emoji -> font atlasina (PUA U+E000..U+E011) =====
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

bool ImGuiWrapper::initialize()
{
	Log::traceLastFunc("ImGuiWrapper::initialize");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = m_displaySize;
	io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

	Log::addParameter("Loading font: %s", m_fontPath.c_str());
	ImFontConfig fontCfg;
	fontCfg.GlyphExtraSpacing.x = UISettings::outlineSize();

	static const ImWchar ranges[] =
    {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0100, 0x017F, // Latin Extended-A (Azərbaycan hərfləri: ç, ğ, ı, ö, ş, ü)
        0x0180, 0x024F, // Latin Extended-B (U+018F = Ə)
        0x0250, 0x02AF, // IPA Extensions (Kiçik 'ə' hərfi üçün)
        0x0400, 0x04FF, // Cyrillic + Cyrillic Supplement
		0x0E00, 0x0E7F, // Thai
        0x2DE0, 0x2DFF, // Cyrillic Extended-A
        0xA640, 0xA69F, // Cyrillic Extended-B
        0xF020, 0xF0FF,
		0
    };
	
	ImFont* font = io.Fonts->AddFontFromFileTTF(m_fontPath.c_str(),
		UISettings::fontSize(), &fontCfg, ranges);

	if (font == nullptr)
	{
		Log::addParameter("font", font);
		return false;
	}

	// ---- AZ_FALLBACK_FONT v2 (AZ_PATCH3) ----
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


	// NICKFONT_V4: oyuncu adi ucun oz fontun. Font qovlugunda nick.ttf (ve ya nick.otf) varsa istifade olunur.
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

	// PLATEFONT_V7: DIN 1451 (assets/Fonts/plate.ttf -> <storage>/SAMP/plate/plate.ttf) nomre lovhesi ucun
	{
		extern ImFont* g_pPlateFont;
		extern char* g_pszStorage;
		g_pPlateFont = nullptr;
		if (g_pszStorage)
		{
			const std::string plateP = std::string(g_pszStorage) + "/SAMP/plate/plate.ttf";
			if (access(plateP.c_str(), R_OK) == 0)
			{
				static const ImWchar plateRanges[] = { 0x0020, 0x0020, 0x0030, 0x0039, 0x0041, 0x005A, 0 };
				ImFontConfig pc;
				pc.PixelSnapH = true;
				g_pPlateFont = io.Fonts->AddFontFromFileTTF(plateP.c_str(), 128.0f, &pc, plateRanges);
				if (g_pPlateFont) Log::addParameter("Plate font: %s", plateP.c_str());
			}
		}
	}

	// EMOJI_V5: vektor emoji glifleri
	{
		extern void Emoji_Register(ImFontAtlas* atlas, ImFont* font, int size);
		extern void Emoji_Fill(ImFontAtlas* atlas);
		int es = (int)UISettings::fontSize();
		if (es < 24) es = 24;
		if (es > 64) es = 64;
		Emoji_Register(io.Fonts, font, es);
		Emoji_Fill(io.Fonts);
	}

	createFontTexture();

	m_renderer = new ImGuiRenderer(ImGui::GetBackgroundDrawList(), font);

	// voice 
	/*for (const auto& deviceInitCallback : Render::deviceInitCallbacks) {
		if (deviceInitCallback != nullptr) {
			deviceInitCallback();
		}
	}*/

	return true;
}

void ImGuiWrapper::render()
{
	ImGui::NewFrame();


	// voice
	/*if (pNetGame) {

		if (pUI->dialog()->visible() || pUI->playertablist()->visible() || pNetGame->GetTextDrawPool()->GetState()) {
			SpeakerList::Hide();
			MicroIcon::Hide();
		}
		else if (MicroIcon::hasShowed) {
			SpeakerList::Show();
			MicroIcon::Show();
		}

		for (const auto& renderCallback : Render::renderCallbacks) {
			if (renderCallback != nullptr) {
				renderCallback();
			}
		}
	}*/

	drawList();
	ImGui::EndFrame();
	ImGui::Render();
	renderDrawData(ImGui::GetDrawData());
}

void ImGuiWrapper::shutdown()
{
	Log::traceLastFunc("ImGuiWrapper::shutdown");

	// voice
	/*for (const auto& deviceFreeCallback : Render::deviceFreeCallbacks) {
		if (deviceFreeCallback != nullptr) {
			deviceFreeCallback();
		}
	}*/

	destroyFontTexture();
}

void ImGuiWrapper::setupRenderState(ImDrawData* draw_data)
{
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)0);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)0);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)1);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)0);
	RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
	RwRenderStateSet(rwRENDERSTATEBORDERCOLOR, (void*)0);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTION, (void*)rwALPHATESTFUNCTIONGREATER);
	RwRenderStateSet(rwRENDERSTATEALPHATESTFUNCTIONREF, (void*)2);
	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
	RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);

}

void ImGuiWrapper::checkVertexBuffer(ImDrawData* draw_data)
{
	if (m_vertexBuffer == nullptr || m_vertexBufferSize < draw_data->TotalVtxCount)
	{
		Log::traceLastFunc("ImGuiWrapper::checkVertexBuffer");

		if (m_vertexBuffer)
		{
			delete m_vertexBuffer;
			m_vertexBuffer = nullptr;
		}

		m_vertexBufferSize = draw_data->TotalVtxCount + 10000;
		m_vertexBuffer = new RwIm2DVertex[m_vertexBufferSize];
		if (!m_vertexBuffer)
		{
			Log::addParameter("Buffer allocation error. Size", m_vertexBufferSize);
			return;
		}

		Log::addParameter("Vertex buffer reallocated. Size", m_vertexBufferSize);
	}

}

#include "../game/sprite2d.h"
void ImGuiWrapper::renderDrawData(ImDrawData* draw_data)
{
	const RwReal nearScreenZ = CSprite2d::NearScreenZ;
	const RwReal recipNearClip = CSprite2d::RecipNearClip;

	checkVertexBuffer(draw_data);
	setupRenderState(draw_data);

	RwIm2DVertex* vtx_dst = m_vertexBuffer;
	int vtx_offset = 0;

	for (int n = 0; n < draw_data->CmdListsCount; n++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[n];
		const ImDrawVert* vtx_src = cmd_list->VtxBuffer.Data;
		const ImDrawIdx* idx_src = cmd_list->IdxBuffer.Data;

		for (int i = 0; i < cmd_list->VtxBuffer.Size; i++)
		{
			RwIm2DVertexSetScreenX(vtx_dst, vtx_src->pos.x);
			RwIm2DVertexSetScreenY(vtx_dst, vtx_src->pos.y);
			RwIm2DVertexSetScreenZ(vtx_dst, nearScreenZ);
			RwIm2DVertexSetRecipCameraZ(vtx_dst, recipNearClip);
			vtx_dst->emissiveColor = vtx_src->col;
			RwIm2DVertexSetU(vtx_dst, vtx_src->uv.x, recipCameraZ);
			RwIm2DVertexSetV(vtx_dst, vtx_src->uv.y, recipCameraZ);

			vtx_dst++;
			vtx_src++;
		}

		const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				ImVec4 clip_rect;
				clip_rect.x = pcmd->ClipRect.x;
				clip_rect.y = pcmd->ClipRect.w;
				clip_rect.z = pcmd->ClipRect.z;
				clip_rect.w = pcmd->ClipRect.y;
				SetScissorRect((void*)&clip_rect);

				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)pcmd->TextureId);
				RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST,
					&m_vertexBuffer[vtx_offset], (RwInt32)cmd_list->VtxBuffer.Size,
					(RwImVertexIndex*)idx_buffer, pcmd->ElemCount);
				RwRenderStateSet(rwRENDERSTATETEXTURERASTER, (void*)0);

				clip_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
				SetScissorRect((void*)&clip_rect);
			}

			idx_buffer += pcmd->ElemCount;
		}
		vtx_offset += cmd_list->VtxBuffer.Size;
	}

}

bool ImGuiWrapper::createFontTexture()
{
	Log::traceLastFunc("ImGuiWrapper::createFontTexture");

	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pxs;
	int width, height, bytes_per_pixel;
	io.Fonts->GetTexDataAsRGBA32(&pxs, &width, &height, &bytes_per_pixel);

	RwImage* font_img = RwImageCreate(width, height, bytes_per_pixel * 8);
	RwImageAllocatePixels(font_img);

	if (font_img == nullptr)
	{
		Log::addParameter("Font image pointer", font_img);
		return false;
	}

	RwUInt8* pixels = font_img->cpPixels;
	for (int y = 0; y < font_img->height; y++)
	{
		memcpy((void*)pixels, pxs + font_img->stride * y, font_img->stride);
		pixels += font_img->stride;
	}

	RwInt32 w, h, d, flags;
	RwImageFindRasterFormat(font_img, rwRASTERTYPETEXTURE, &w, &h, &d, &flags);
	m_fontRaster = RwRasterCreate(w, h, d, flags);
	if (m_fontRaster == nullptr)
	{
		Log::addParameter("font raster pointer", m_fontRaster);
		return false;
	}

	m_fontRaster = RwRasterSetFromImage(m_fontRaster, font_img);
	io.Fonts->TexID = (ImTextureID*)m_fontRaster;

	RwImageDestroy(font_img);

	Log::addParameter("Atlas width", width);
	Log::addParameter("Atlas height", height);
	return true;

}

void ImGuiWrapper::destroyFontTexture()
{
	if (m_fontRaster)
	{
		ImGuiIO& io = ImGui::GetIO();
		RwRasterDestroy(m_fontRaster);
		m_fontRaster = nullptr;
		io.Fonts->TexID = (ImTextureID)0;
	}

}

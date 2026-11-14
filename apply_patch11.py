#!/usr/bin/env python3
# apply_patch11.py
#  Merhele 5: modern klaviatura (yumsaq acilis animasiyasi, yuvarlaq duymeler, basma animasiyasi,
#             yenilenmis yazi sahesi + yanib-sonen kursor) + EMOJI sehifesi (:) duymesi) + chatda emoji gosterilmesi
#  Merhele 4: /mm = animasiyali "tablet" PROFIL sehifesi (ad, ID, skor, ping, HP, status, komandalar)
# Ilkin sert: apply_patch10.py artiq tetbiq olunmalidir (g_mmOpenPage ucun).
import os, re, shutil

ROOT = os.getcwd()
CPP = os.path.join(ROOT, "app/src/main/cpp/samp")
KB_H   = os.path.join(CPP, "gui/samp_widgets/keyboard.h")
KB_C   = os.path.join(CPP, "gui/samp_widgets/keyboard.cpp")
CHAT_C = os.path.join(CPP, "gui/samp_widgets/chat.cpp")
GUI_C  = os.path.join(CPP, "gui/gui.cpp")
WRAP_C = os.path.join(CPP, "gui/imguiwrapper.cpp")

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()

def sub1(pat, repl, s, flags=0):
    new, n = re.subn(pat, (lambda m: repl) if isinstance(repl, str) else repl, s, count=1, flags=flags)
    if n != 1: raise ValueError(pat[:70])
    return new

def run(name, path, marker, fn):
    if not os.path.exists(path):
        print("[YOX]   %s: %s tapilmadi" % (name, os.path.relpath(path, ROOT))); return
    s = rd(path)
    if marker in s:
        print("[KECDI] %s: artiq tetbiq olunub" % name); return
    try:
        n = fn(s)
    except ValueError as e:
        print("[KECDI] %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); return
    if not os.path.exists(path + ".bak11"): shutil.copy(path, path + ".bak11")
    with open(path, "w", encoding="utf-8") as f: f.write(n)
    print("[OK]    %s" % name)

# ======================================================================= keyboard.h
EMOJI_CLASS = '''
// EMOJI_PATCH11
class KeyboardLayoutEmoji : public KeyboardLayout
{
public:
	KeyboardLayoutEmoji();
	virtual void performLayout() override;
};
'''
def p_kbh(s):
    s = sub1(r'(\bSEND)(\s*\n[ \t]*\};)', lambda m: m.group(1) + ',\n\t\tEMOJI' + m.group(2), s)
    s = sub1(r'(\t\tNUM)(\s*\n[ \t]*\};)', lambda m: m.group(1) + ',\n\t\tEMOJI' + m.group(2), s)
    s = sub1(r'(class KeyboardLayoutNUM : public KeyboardLayout\s*\{.*?\n\};\n)', lambda m: m.group(1) + EMOJI_CLASS, s, re.S)
    s = sub1(r'(KeyboardLayoutNUM\* m_layoutNUM;)', lambda m: m.group(1) + '\n\tKeyboardLayoutEmoji* m_layoutEmoji;', s)
    s = sub1(r'(bool m_isStringValue;)', lambda m: m.group(1) + '\n\t\t\tfloat m_pressAnim = 0.0f;', s)
    return s

# ======================================================================= keyboard.cpp
KB_HELPERS = r'''

// ===== EMOJI_PATCH11 =====
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>

static std::chrono::steady_clock::time_point g_kbShowTP = std::chrono::steady_clock::now();
static float g_kbDt = 0.016f;

// ":smile:" kimi ASCII kodlari sistem fontundaki simvollara cevirir (chat ucun de istifade olunur)
std::string Emoji_Replace(const std::string& in)
{
	static const struct { const char* code; const char* glyph; } kMap[] = {
		{ ":smile:", "\xE2\x98\xBA" }, { ":sad:", "\xE2\x98\xB9" },   { ":heart:", "\xE2\x99\xA5" },
		{ ":star:", "\xE2\x98\x85" },  { ":sun:", "\xE2\x98\x80" },   { ":snow:", "\xE2\x9D\x84" },
		{ ":skull:", "\xE2\x98\xA0" }, { ":ok:", "\xE2\x9C\x94" },    { ":no:", "\xE2\x9C\x98" },
		{ ":bolt:", "\xE2\x9A\xA1" },  { ":music:", "\xE2\x99\xAB" }, { ":note:", "\xE2\x99\xAA" },
		{ ":phone:", "\xE2\x98\x8E" }, { ":mail:", "\xE2\x9C\x89" },  { ":plane:", "\xE2\x9C\x88" },
		{ ":crown:", "\xE2\x99\x9B" }, { ":flower:", "\xE2\x9C\xBF" },{ ":cloud:", "\xE2\x98\x81" },
		{ ":umbrella:", "\xE2\x98\x82" }, { ":peace:", "\xE2\x98\xAE" }, { ":hand:", "\xE2\x9C\x8C" },
		{ ":cross:", "\xE2\x9C\x9D" }, { ":yin:", "\xE2\x98\xAF" },   { ":flag:", "\xE2\x9A\x91" },
		{ ":)", "\xE2\x98\xBA" },      { ":(", "\xE2\x98\xB9" },      { "<3", "\xE2\x99\xA5" },
	};
	if (in.find(':') == std::string::npos && in.find("<3") == std::string::npos) return in;
	std::string out;
	out.reserve(in.size());
	size_t i = 0;
	while (i < in.size())
	{
		if (in[i] == ':' || in[i] == '<')
		{
			bool hit = false;
			for (const auto& e : kMap)
			{
				const size_t n = strlen(e.code);
				if (in.compare(i, n, e.code) == 0) { out += e.glyph; i += n; hit = true; break; }
			}
			if (hit) continue;
		}
		out += in[i++];
	}
	return out;
}
// ===== /EMOJI_PATCH11 =====
'''

KB_DRAW = r'''void Keyboard::draw(ImGuiRenderer* renderer)
{
	// yumsaq asagidan yuxari acilis + qaranliq gradient + narinci aksent xett
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	static std::chrono::steady_clock::time_point s_lastFrame = now;
	g_kbDt = std::chrono::duration<float>(now - s_lastFrame).count();
	s_lastFrame = now;
	if (g_kbDt > 0.1f) g_kbDt = 0.1f;

	const ImVec2 a = absolutePosition();
	const ImVec2 b = a + size();
	float t = std::chrono::duration<float>(now - g_kbShowTP).count() / 0.26f;
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
	const float top = a.y + (b.y - a.y) * (1.0f - e);

	renderer->pushClipRect(ImVec2(a.x, top), ImVec2(b.x, b.y), false);
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	dl->AddRectFilledMultiColor(a, b, IM_COL32(20, 23, 33, 250), IM_COL32(20, 23, 33, 250),
	                            IM_COL32(10, 12, 18, 255), IM_COL32(10, 12, 18, 255));
	dl->AddRectFilled(a, ImVec2(b.x, a.y + 2.0f), IM_COL32(245, 145, 50, 255));
	Widget::draw(renderer);
	renderer->popClipRect();
}'''

KBIN_DRAW = r'''void KeyboardInput::draw(ImGuiRenderer* renderer)
{
	const ImVec2 a = absolutePosition();
	const ImVec2 b = a + size();
	const float divLinePosY = b.y - UISettings::outlineSize();
	const float font_sz = UISettings::fontSize();
	const float textY = a.y + (height() - font_sz) / 2;
	ImDrawList* dl = ImGui::GetBackgroundDrawList();

	// yuvarlaq yazi sahesi
	const float pad = UISettings::padding() * 0.5f;
	dl->AddRectFilled(ImVec2(a.x + pad, a.y + 5.0f), ImVec2(b.x - pad, b.y - 7.0f), IM_COL32(0, 0, 0, 95), 12.0f);

	if (m_caption.empty())
		renderer->drawText(ImVec2(a.x + UISettings::padding(), textY), ImColor(1.0f, 1.0f, 1.0f, 0.35f),
		                   std::string("Mesaj yaz...  (:smile: :heart: <3)"), false, font_sz);
	else
		renderer->drawText(ImVec2(a.x + UISettings::padding(), textY), ImColor(1.0f, 1.0f, 1.0f), m_caption, false, font_sz);

	// yanib-sonen kursor
	const float blink = std::fmod(std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count(), 1.0f);
	if (blink < 0.6f)
	{
		const float tw = m_caption.empty() ? 0.0f : renderer->calculateTextSize(m_caption, font_sz).x;
		const float cx = a.x + UISettings::padding() + tw + 2.0f;
		dl->AddRectFilled(ImVec2(cx, textY), ImVec2(cx + 2.0f, textY + font_sz), IM_COL32(245, 145, 50, 255), 1.0f);
	}

	renderer->drawLine(
			ImVec2(a.x, divLinePosY),
			ImVec2(b.x, divLinePosY),
			ImColor(0xF5, 0x91, 0x32), UISettings::outlineSize()
	);

	Widget::draw(renderer);
}'''

KEY_BG = r'''{
		// MODERN DUYME: yuvarlaq, kolge, basanda aksent rengine yumsaq kecid
		const ImVec2 ka = absolutePosition();
		const ImVec2 kb = absolutePosition() + size();
		m_pressAnim += ((focused() ? 1.0f : 0.0f) - m_pressAnim) * std::min(1.0f, g_kbDt * 18.0f);
		const float ins = 3.0f + 2.0f * m_pressAnim;
		const ImVec2 p0(ka.x + ins, ka.y + ins), p1(kb.x - ins, kb.y - ins);
		int br = 35, bg = 40, bb = 54;
		if (m_extendedKey != ExtendedKey::NONE) { br = 46; bg = 52; bb = 70; }
		if (m_extendedKey == ExtendedKey::SEND) { br = 245; bg = 145; bb = 50; }
		const float k = (m_extendedKey == ExtendedKey::SEND) ? 0.0f : m_pressAnim;
		const int cr = (int)(br + (245 - br) * k), cg = (int)(bg + (145 - bg) * k), cb = (int)(bb + (50 - bb) * k);
		ImDrawList* kd = ImGui::GetBackgroundDrawList();
		kd->AddRectFilled(ImVec2(p0.x, p0.y + 2.0f), ImVec2(p1.x, p1.y + 2.0f), IM_COL32(0, 0, 0, 70), 9.0f);
		kd->AddRectFilled(p0, p1, IM_COL32(cr, cg, cb, 255), 9.0f);
		kd->AddRect(p0, p1, IM_COL32(255, 255, 255, 20), 9.0f, 0, 1.0f);
	}'''

EMOJI_IMPL = r'''

/* KeyboardLayoutEmoji (EMOJI_PATCH11) */

KeyboardLayoutEmoji::KeyboardLayoutEmoji() : KeyboardLayout() {}

void KeyboardLayoutEmoji::performLayout()
{
	static const char* kGlyph[24] = {
		"\xE2\x98\xBA", "\xE2\x98\xB9", "\xE2\x99\xA5", "\xE2\x98\x85", "\xE2\x98\x80", "\xE2\x9D\x84", "\xE2\x98\xA0", "\xE2\x9C\x94",
		"\xE2\x9C\x98", "\xE2\x9A\xA1", "\xE2\x99\xAB", "\xE2\x99\xAA", "\xE2\x98\x8E", "\xE2\x9C\x89", "\xE2\x9C\x88", "\xE2\x99\x9B",
		"\xE2\x9C\xBF", "\xE2\x98\x81", "\xE2\x98\x82", "\xE2\x98\xAE", "\xE2\x9C\x8C", "\xE2\x9C\x9D", "\xE2\x98\xAF", "\xE2\x9A\x91"
	};
	static const char* kCode[24] = {
		":smile:", ":sad:", ":heart:", ":star:", ":sun:", ":snow:", ":skull:", ":ok:",
		":no:", ":bolt:", ":music:", ":note:", ":phone:", ":mail:", ":plane:", ":crown:",
		":flower:", ":cloud:", ":umbrella:", ":peace:", ":hand:", ":cross:", ":yin:", ":flag:"
	};

	for (int r = 0; r < 3; ++r)
	{
		m_rows[r]->setPadding(0.0f);
		m_rows[r]->setSpacing(0.0f);
		for (int c = 0; c < 8; ++c)
			m_rows[r]->addKey(std::string(kGlyph[r * 8 + c]), std::string(kCode[r * 8 + c]));
	}

	ImVec2 keySize = ImVec2(width() / 10, UISettings::keyboardRowHeight());
	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 4, 0.0f));
	m_rows[3]->addKey(ExtendedKey::BACKSPACE);
	m_rows[3]->addKey(ExtendedKey::SEND)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	KeyboardLayout::performLayout();
}
'''

def p_kbc(s):
    s = sub1(r'(extern CJavaWrapper\* pJavaWrapper;[^\n]*\n)', lambda m: m.group(1) + KB_HELPERS, s)
    # konstruktor
    s = sub1(r'(\n[ \t]*m_layoutNUM->setVisible\(false\);[^\n]*\n)', lambda m: m.group(1) +
             '\n\tm_layoutEmoji = new KeyboardLayoutEmoji();   // EMOJI_PATCH11\n'
             '\tthis->addChild(m_layoutEmoji);\n'
             '\tm_layoutEmoji->setPadding(0.0f);\n'
             '\tm_layoutEmoji->setSpacing(0.0f);\n'
             '\tm_layoutEmoji->setVisible(false);\n', s)
    # performLayout
    s = sub1(r'([ \t]*m_layoutNUM->setPosition\(ImVec2\(0\.0f, UISettings::keyboardRowHeight\(\)\)\);[^\n]*\n)', lambda m: m.group(1) +
             '\n\t/* EMOJI */\n'
             '\tm_layoutEmoji->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));\n'
             '\tm_layoutEmoji->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));\n', s)
    # setActiveLayout
    s = sub1(r'(case KeyboardLayout::Type::NUM:\s*m_layoutNUM->setVisible\(true\);\s*m_activeLayout = m_layoutNUM;\s*break;\n)', lambda m: m.group(1) +
             '\n\t\tcase KeyboardLayout::Type::EMOJI:\n'
             '\t\t\tm_layoutEmoji->setVisible(true);\n'
             '\t\t\tm_activeLayout = m_layoutEmoji;\n'
             '\t\t\tbreak;\n', s)
    # Key konstruktoru + touchPopEvent
    s = sub1(r'(this->setCaption\(std::string\("AZE"\)\);\s*break;\n)', lambda m: m.group(1) +
             '\t\tcase ExtendedKey::EMOJI:\n\t\t\tthis->setCaption(std::string(":)"));\n\t\t\tbreak;\n', s)
    s = sub1(r'(keyboard->setActiveLayout\(KeyboardLayout::Type::NUM\);\s*break;\n)', lambda m: m.group(1) +
             '\n\t\tcase ExtendedKey::EMOJI:\n\t\t\tkeyboard->setActiveLayout(KeyboardLayout::Type::EMOJI);\n\t\t\tbreak;\n', s)
    # '?' duymesi -> emoji duymesi (yalniz 4 herf düzümündə, NUM-da qalir)
    s, n = re.subn(r'm_rows\[3\]->addKey\(std::string\("\?"\), \'\?\'\);(\s*\n\s*m_rows\[3\]->addKey\(std::string\("\."\), \'\.\'\);)',
                   lambda m: 'm_rows[3]->addKey(ExtendedKey::EMOJI);' + m.group(1), s)
    if n < 2: raise ValueError("'?' duymeleri (%d)" % n)
    # cizimler
    s = sub1(r'void Keyboard::draw\(ImGuiRenderer\* renderer\)\s*\{.*?\n\}', lambda m: KB_DRAW, s, re.S)
    s = sub1(r'void KeyboardInput::draw\(ImGuiRenderer\* renderer\)\s*\{.*?\n\}', lambda m: KBIN_DRAW, s, re.S)
    s = sub1(r'if \(focused\(\)\) renderer->drawRect\(absolutePosition\(\), absolutePosition\(\) \+ size\(\), ImColor\(0xF5, 0x91, 0x32\), true\);',
             lambda m: KEY_BG, s)
    s = sub1(r'(void Keyboard::show\(Widget\* caller\)\s*\{)', lambda m: m.group(1) + '\n\tg_kbShowTP = std::chrono::steady_clock::now();   // EMOJI_PATCH11', s)
    # yazi sahesinde emoji onizlemesi
    old = 'm_caption = Encoding::cp2utf(m_input);'
    if s.count(old) < 2: raise ValueError("m_caption (%d)" % s.count(old))
    s = s.replace(old, 'm_caption = Emoji_Replace(Encoding::cp2utf(m_input));')
    return s.rstrip("\n") + "\n" + EMOJI_IMPL

# ======================================================================= imguiwrapper.cpp
SYMBOLS = r'''	// ---- EMOJI_SYMBOLS_PATCH11: Misc Symbols + Dingbats (U+2600..U+27BF), :smile: kimi kodlar ucun ----
	{
		static const ImWchar symRanges[] = { 0x2600, 0x27BF, 0 };
		static const char* kSym[] = {
			"/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf",
			"/system/fonts/NotoSansSymbols-Regular.ttf",
			"/system/fonts/NotoSansSymbols2-Regular.ttf",
			"/system/fonts/DejaVuSans.ttf",
			"/system/fonts/DroidSansFallback.ttf",
			"/system/fonts/DroidSansFallbackFull.ttf", nullptr
		};
		int mergedSym = 0;
		for (int i = 0; kSym[i] && mergedSym < 3; i++)
		{
			if (access(kSym[i], R_OK) != 0) continue;
			ImFontConfig symCfg;
			symCfg.MergeMode = true;
			symCfg.PixelSnapH = true;
			symCfg.GlyphExtraSpacing.x = UISettings::outlineSize();
			if (io.Fonts->AddFontFromFileTTF(kSym[i], UISettings::fontSize(), &symCfg, symRanges))
			{
				Log::addParameter("Symbol font: %s", kSym[i]);
				mergedSym++;
			}
		}
	}

'''
def p_wrap(s):
    m = re.search(r'^[ \t]+createFontTexture\(\);', s, re.M)
    if not m: raise ValueError("createFontTexture()")
    return s[:m.start()] + SYMBOLS + s[m.start():]

# ======================================================================= chat.cpp
def p_chat(s):
    if "g_mmOpenPage" not in s: raise ValueError("apply_patch10 lazimdir (g_mmOpenPage yoxdur)")
    s = sub1(r'(^bool g_bShowHandlingDlg\s*=\s*false;[^\n]*\n)', lambda m: m.group(1) +
             'std::string Emoji_Replace(const std::string& in);   // EMOJI_PATCH11 (keyboard.cpp)\n', s, re.M)
    s = sub1(r'new MessageItem\(message, color\)', 'new MessageItem(Emoji_Replace(message), color)', s)
    s = sub1(r'new PlayerMessageItem\(message, nick, nick_color\)', 'new PlayerMessageItem(Emoji_Replace(message), nick, nick_color)', s)
    s = sub1(r'(if \(cmd == "/mm"\) \{[^\n]*\n[ \t]*)g_bShowHandlingDlg = !g_bShowHandlingDlg;',
             lambda m: m.group(1) + 'if (g_bShowHandlingDlg) { g_bShowHandlingDlg = false; }   // PROFILE_PATCH11: /mm profil\n'
                       + m.group(1).split('\n')[-1] + 'else { g_mmOpenPage = 5; g_bShowHandlingDlg = true; }', s)
    return s

# ======================================================================= gui.cpp (profil tableti)
PROFILE = r'''else if (s_page == 5)    // PROFIL + KOMANDALAR (PROFILE_PATCH11)
                {
                        static float s_profT = 0.0f, s_profTime = 0.0f;
                        static int   s_profFc = -100;
                        const int fc = ImGui::GetFrameCount();
                        if (fc - s_profFc > 3) s_profT = 0.0f;      // sehifeye yeniden girende animasiya basdan
                        s_profFc = fc;
                        s_profT = Clamp(s_profT + dt / 0.55f, 0.0f, 1.0f);
                        s_profTime += dt;
                        const float e  = 1.0f - powf(1.0f - s_profT, 3.0f);
                        const int   A  = (int)(255.0f * e);
                        const float oy = (1.0f - e) * S(34.0f);

                        // ---- melumatlar ----
                        char nick[40] = "Oyuncu";
                        int pid = -1, score = 0, ping = 0;
                        float hp = 0.0f;
                        const char* status = "Oyunda";
                        CPlayerPool* ppool = pNetGame ? pNetGame->GetPlayerPool() : nullptr;
                        if (ppool) {
                                const char* nm = ppool->GetLocalPlayerName();
                                if (nm && *nm) snprintf(nick, sizeof(nick), "%s", nm);
                                pid = (int)ppool->GetLocalPlayerID();
                                score = ppool->GetLocalPlayerScore();
                                ping = ppool->GetLocalPlayerPing();
                                CLocalPlayer* plp = ppool->GetLocalPlayer();
                                if (plp && plp->IsSpectating()) status = "Izleyir";
                        }
                        CPlayerPed* pedP = pGame ? pGame->FindPlayerPed() : nullptr;
                        if (pedP) {
                                hp = pedP->GetHealth();
                                if (status[0] == 'O' && pedP->IsInVehicle()) status = "Avtomobilde";
                        }
                        char initial[4] = { (char)((nick[0] >= 'a' && nick[0] <= 'z') ? nick[0] - 32 : nick[0]), 0, 0, 0 };

                        // ---- tablet ----
                        const Rc card{ cx0, P.y + S(124.0f) + oy, cw, S(158.0f) };
                        Fill(card, Col(12, 14, 20, A), S(22.0f));
                        Stroke(card, Col(76, 84, 106, A), S(22.0f), S(1.6f));
                        s_dl->AddCircleFilled(ImVec2(card.x + card.w * 0.5f, card.y + S(4.0f)), S(1.8f), Col(64, 70, 90, A), 8);
                        const Rc scr = Shrink(card, S(8.0f));
                        Fill(scr, Col(23, 27, 39, A), S(16.0f));
                        Fill(Rc{ scr.x, scr.y, scr.w, S(30.0f) }, Accent(0.0f, (int)(36.0f * e)), S(16.0f));

                        // status cubugu: saat + siqnal
                        time_t tnow = time(nullptr);
                        struct tm* lt = localtime(&tnow);
                        char clk[8];
                        snprintf(clk, sizeof(clk), "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
                        Txt(S(13.0f), scr.x + S(14.0f), scr.y + S(8.0f), Col(206, 212, 226, A), clk);
                        const int bars = ping < 60 ? 4 : (ping < 120 ? 3 : (ping < 200 ? 2 : 1));
                        for (int i = 0; i < 4; ++i) {
                                const float bh = S(5.0f + 3.0f * i);
                                const float bx = scr.x + scr.w - S(70.0f) + S(7.0f) * i;
                                const bool on = i < bars;
                                Fill(Rc{ bx, scr.y + S(21.0f) - bh, S(4.5f), bh },
                                     on ? Col(120, 215, 160, A) : Col(70, 76, 94, A), S(1.5f));
                        }
                        char pingTxt[16];
                        snprintf(pingTxt, sizeof(pingTxt), "%dms", ping);
                        Txt(S(12.0f), scr.x + scr.w - S(38.0f), scr.y + S(9.0f), Col(170, 178, 196, A), pingTxt);

                        // avatar (nefes alan halqa)
                        const ImVec2 av(scr.x + S(54.0f), scr.y + S(92.0f));
                        const float  ar = S(32.0f);
                        s_dl->AddCircleFilled(av, ar, Col(245, 145, 50, A), 32);
                        s_dl->AddCircle(av, ar + S(4.0f) + S(2.0f) * sinf(s_profTime * 2.4f),
                                        Col(245, 145, 50, (int)(110.0f * e)), 32, S(2.0f));
                        TxtC(S(30.0f), Rc{ av.x - ar, av.y - ar, ar * 2.0f, ar * 2.0f }, Col(255, 255, 255, A), initial, true);

                        // ad, ID, skor
                        const float ix = scr.x + S(104.0f);
                        TxtB(S(24.0f), ix, scr.y + S(44.0f), Col(240, 244, 255, A), nick);
                        char idl[48];
                        snprintf(idl, sizeof(idl), "ID: %d", pid);
                        Txt(S(14.0f), ix, scr.y + S(76.0f), Col(150, 158, 178, A), idl);

                        // status qutusu (zərif nəbz)
                        const Rc chip{ ix, scr.y + S(100.0f), S(122.0f), S(24.0f) };
                        Fill(chip, Col(46, 200, 120, (int)(46.0f * e)), S(12.0f));
                        s_dl->AddCircleFilled(ImVec2(chip.x + S(12.0f), chip.y + S(12.0f)),
                                              S(3.5f) + S(1.0f) * sinf(s_profTime * 3.0f), Col(80, 220, 140, A), 12);
                        Txt(S(13.0f), chip.x + S(24.0f), chip.y + S(4.0f), Col(150, 232, 185, A), status);

                        // HP cubugu (animasiya ile dolur)
                        const Rc hpr{ ix, scr.y + S(136.0f), S(214.0f), S(8.0f) };
                        Fill(hpr, Col(40, 44, 58, A), S(4.0f));
                        const float hpf = Clamp(hp / 100.0f, 0.0f, 1.0f) * e;
                        if (hpf > 0.0f)
                            Fill(Rc{ hpr.x, hpr.y, hpr.w * hpf, hpr.h },
                                 Col((int)(235 - 130 * hpf), (int)(70 + 140 * hpf), 80, A), S(4.0f));
                        char hpl[16];
                        snprintf(hpl, sizeof(hpl), "HP %d", (int)hp);
                        Txt(S(11.0f), hpr.x + hpr.w + S(8.0f), hpr.y - S(3.0f), Col(170, 178, 196, A), hpl);

                        // sag terefde statistika qutulari + server ucun yer
                        const float tx = scr.x + S(394.0f);
                        const float tw = S(108.0f);
                        char scl[16], pgl[16];
                        snprintf(scl, sizeof(scl), "%d", score);
                        snprintf(pgl, sizeof(pgl), "%d", ping);
                        struct Tile { const char* lb; const char* val; float x; };
                        const Tile tiles[2] = { { "SKOR", scl, tx }, { "PING", pgl, tx + tw + S(8.0f) } };
                        for (int i = 0; i < 2; ++i) {
                                const Rc tr{ tiles[i].x, scr.y + S(44.0f), tw, S(52.0f) };
                                Fill(tr, Col(33, 38, 54, A), S(10.0f));
                                Txt(S(11.0f), tr.x + S(10.0f), tr.y + S(6.0f), Col(138, 146, 168, A), tiles[i].lb);
                                Txt(S(22.0f), tr.x + S(10.0f), tr.y + S(21.0f), Accent(0.0f, A), tiles[i].val);
                        }
                        const Rc srv{ tx, scr.y + S(104.0f), tw * 2.0f + S(8.0f), S(40.0f) };
                        Fill(srv, Col(33, 38, 54, A), S(10.0f));
                        Stroke(srv, Col(78, 142, 235, (int)(80.0f * e)), S(10.0f), S(1.2f));
                        Txt(S(12.0f), srv.x + S(12.0f), srv.y + S(6.0f), Col(138, 146, 168, A), "SERVER PROFILI");
                        Txt(S(13.0f), srv.x + S(12.0f), srv.y + S(21.0f), Col(200, 206, 222, A), "Tezlikle: seviyye, pul, komanda");

                        // ---- komandalar ----
                        Txt(S(13.0f), cx0, P.y + S(296.0f) + oy, Col(138, 146, 168, A), "KOMANDALAR");
                        struct CmdInfo { const char* cmd; const char* desc; };
                        static const CmdInfo kCmds[] = {
                                { "/mm",      "Profil menyusu" },
                                { "/tg",      "Tuning menyusu" },
                                { "/sinyal",  "sol | sag | avariya | off" },
                                { "/far N",   "Far rengi (0-15)" },
                                { "/gizle",   "UI gizlet / geri qaytar" },
                                { "/yt",      "YouTube paneli" },
                                { "/suret N", "Maksimum surət" },
                                { "/manevr N","Manevr bucagi" },
                                { "/tormoz N","Tormoz gucu" },
                        };
                        const int nc = (int)(sizeof(kCmds) / sizeof(kCmds[0]));
                        const float colW = (cw - S(10.0f)) * 0.5f;
                        for (int i = 0; i < nc; ++i)
                        {
                                const int col = i % 2, rowi = i / 2;
                                const Rc row{ cx0 + col * (colW + S(10.0f)), P.y + S(318.0f + 40.0f * rowi) + oy, colW, S(34.0f) };
                                Fill(row, Col(27, 30, 40, A), S(9.0f));
                                Txt(S(14.0f), row.x + S(12.0f), row.y + (row.h - S(14.0f)) * 0.5f, Accent(0.0f, A), kCmds[i].cmd);
                                Txt(S(13.0f), row.x + S(100.0f), row.y + (row.h - S(13.0f)) * 0.5f, Col(200, 204, 216, A), kCmds[i].desc);
                        }
                }
                '''

def p_gui(s):
    if "g_mmOpenPage" not in s: raise ValueError("apply_patch10 lazimdir (g_mmOpenPage yoxdur)")
    inc = re.search(r'^#include[^\n]*\n', s, re.M)
    add = '#include <ctime>   // PROFILE_PATCH11\n#include <cmath>\n'
    s = (s[:inc.end()] + add + s[inc.end():]) if inc else (add + s)
    s = sub1(r'else if \(s_page == 5\)\s*// KOMANDALAR\s*\{.*?\n[ \t]*\}\n([ \t]*)else if \(s_page == 6\)',
             lambda m: PROFILE + 'else if (s_page == 6)', s, re.S)
    s = sub1(r'"Komandalar"', '"Profil"', s)
    s = sub1(r'"Bütün komandaların siyahısı"', '"Profil, status və komandalar"', s)
    return s

run("keyboard.h: EMOJI sehifesi + duyme animasiyasi sahesi", KB_H, "EMOJI_PATCH11", p_kbh)
run("keyboard.cpp: modern klaviatura + emoji sehifesi", KB_C, "EMOJI_PATCH11", p_kbc)
run("imguiwrapper.cpp: simvol fontu (emoji ucun)", WRAP_C, "EMOJI_SYMBOLS_PATCH11", p_wrap)
run("chat.cpp: chatda emoji + /mm profil", CHAT_C, "EMOJI_PATCH11", p_chat)
run("gui.cpp: /mm profil tableti", GUI_C, "PROFILE_PATCH11", p_gui)
print("\nBitdi. Geri qaytarmaq ucun her faylin yaninda *.bak11 var.")

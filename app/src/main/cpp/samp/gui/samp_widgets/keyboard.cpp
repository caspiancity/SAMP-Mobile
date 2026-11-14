#include "../../main.h"
#include "../gui.h"
#include "../../game/game.h"
#include "../../log.h"
#include "../settings.h"
#include "../java/jniutil.h"

extern CGame* pGame;
extern UI* pUI;
extern CSettings* pSettings;
extern CJavaWrapper* pJavaWrapper;


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

/* Keyboard */

Keyboard::Keyboard()
{
	m_input = new KeyboardInput();
	this->addChild(m_input);


	m_layoutLowerENG = new KeyboardLayoutLowerENG();
	this->addChild(m_layoutLowerENG);
	m_layoutLowerENG->setPadding(0.0f);
	m_layoutLowerENG->setSpacing(0.0f);
	m_layoutLowerENG->setVisible(false);

	m_layoutUpperENG = new KeyboardLayoutUpperENG();
	this->addChild(m_layoutUpperENG);
	m_layoutUpperENG->setPadding(0.0f);
	m_layoutUpperENG->setSpacing(0.0f);
	m_layoutUpperENG->setVisible(false);

	m_layoutLowerAZE = new KeyboardLayoutLowerAZE();
	this->addChild(m_layoutLowerAZE);
	m_layoutLowerAZE->setPadding(0.0f);
	m_layoutLowerAZE->setSpacing(0.0f);
	m_layoutLowerAZE->setVisible(false);

	m_layoutUpperAZE = new KeyboardLayoutUpperAZE();
	this->addChild(m_layoutUpperAZE);
	m_layoutUpperAZE->setPadding(0.0f);
	m_layoutUpperAZE->setSpacing(0.0f);
	m_layoutUpperAZE->setVisible(false);


	m_layoutNUM = new KeyboardLayoutNUM();
	this->addChild(m_layoutNUM);
	m_layoutNUM->setPadding(0.0f);
	m_layoutNUM->setSpacing(0.0f);
	m_layoutNUM->setVisible(false);

	m_layoutEmoji = new KeyboardLayoutEmoji();   // EMOJI_PATCH11
	this->addChild(m_layoutEmoji);
	m_layoutEmoji->setPadding(0.0f);
	m_layoutEmoji->setSpacing(0.0f);
	m_layoutEmoji->setVisible(false);

	m_activeLayout = m_layoutLowerENG;
	m_activeLayoutType = KeyboardLayout::Type::ENG_LOWER;
	m_activeLayout->setVisible(true);

	m_history = new KeyboardHistory();
}

void Keyboard::performLayout()
{
	m_input->setFixedSize(ImVec2(width(), UISettings::keyboardRowHeight()));
	m_input->setPosition(ImVec2(0.0f, 0.0f));

	/* ENG */
	m_layoutLowerENG->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));
	m_layoutLowerENG->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));
	m_layoutUpperENG->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));
	m_layoutUpperENG->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));

	/* AZE */
	m_layoutLowerAZE->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));
	m_layoutLowerAZE->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));
	m_layoutUpperAZE->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));
	m_layoutUpperAZE->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));

	/* NUM */
	m_layoutNUM->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));
	m_layoutNUM->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));

	/* EMOJI */
	m_layoutEmoji->setFixedSize(ImVec2(width(), height() - m_input->height()+50.0f));
	m_layoutEmoji->setPosition(ImVec2(0.0f, UISettings::keyboardRowHeight()));

	Widget::performLayout();
}

void Keyboard::draw(ImGuiRenderer* renderer)
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
}

void Keyboard::setActiveLayout(KeyboardLayout::Type type)
{
	m_activeLayout->setVisible(false);

	switch (type)
	{
		case KeyboardLayout::Type::ENG_LOWER:
			m_layoutLowerENG->setVisible(true);
			m_activeLayout = m_layoutLowerENG;
			break;

		case KeyboardLayout::Type::ENG_UPPER:
			m_layoutUpperENG->setVisible(true);
			m_activeLayout = m_layoutUpperENG;
			break;

		case KeyboardLayout::Type::AZE_LOWER:
			m_layoutLowerAZE->setVisible(true);
			m_activeLayout = m_layoutLowerAZE;
			break;

		case KeyboardLayout::Type::AZE_UPPER:
			m_layoutUpperAZE->setVisible(true);
			m_activeLayout = m_layoutUpperAZE;
			break;

		case KeyboardLayout::Type::NUM:
			m_layoutNUM->setVisible(true);
			m_activeLayout = m_layoutNUM;
			break;

		case KeyboardLayout::Type::EMOJI:
			m_layoutEmoji->setVisible(true);
			m_activeLayout = m_layoutEmoji;
			break;
	}

	m_activeLayoutType = type;
}

void Keyboard::changeCase(KeyboardLayout::KeyCase key_case)
{
	switch (key_case)
	{
		case KeyboardLayout::KeyCase::LOWER:
			if (m_activeLayoutType == KeyboardLayout::Type::ENG_UPPER) setActiveLayout(KeyboardLayout::Type::ENG_LOWER);
			if (m_activeLayoutType == KeyboardLayout::Type::AZE_UPPER) setActiveLayout(KeyboardLayout::Type::AZE_LOWER);
			break;

		case KeyboardLayout::KeyCase::UPPER:
			if (m_activeLayoutType == KeyboardLayout::Type::ENG_LOWER) setActiveLayout(KeyboardLayout::Type::ENG_UPPER);
			if (m_activeLayoutType == KeyboardLayout::Type::AZE_LOWER) setActiveLayout(KeyboardLayout::Type::AZE_UPPER);
			break;
	}
}

void Keyboard::show(Widget* caller)
{
	g_kbShowTP = std::chrono::steady_clock::now();   // EMOJI_PATCH11
	if(pSettings->Get().iAndroidKeyboard)
	{
		pJavaWrapper->ShowKeyboard();
	}
	else {
		m_history->reset();
		m_input->clear();
		this->setVisible(true);
		pGame->EnableGameInput(false);
	}
	m_caller = caller;
}

void Keyboard::hide()
{
	if(pSettings->Get().iAndroidKeyboard)
	{
		pJavaWrapper->HideKeyboard();
	}
	else {
		this->setVisible(false);
		pGame->EnableGameInput(true);
	}
	m_caller = nullptr;
}

void Keyboard::send()
{
	const std::string input = this->input()->inputString();
	m_history->add(input);
	if (m_caller) m_caller->keyboardEvent(input);
	this->hide();
}

void Keyboard::sendForGB(JNIEnv *pEnv, jobject thiz, jbyteArray str)
{
	std::string::size_type v8;
	v8 = pEnv->functions->GetArrayLength(pEnv, (jarray)str);
	char* buffer = (char*)malloc(v8 + 1);
	jbyte* elements = pEnv->GetByteArrayElements(str, NULL);
	memcpy(buffer, elements, v8);
	buffer[v8] = 0;
	std::string input = std::string(buffer);
	if (m_caller) m_caller->keyboardEvent(input);
	hide();
	pEnv->ReleaseByteArrayElements(str, elements, JNI_ABORT);
}

void Keyboard::activateEvent(bool active)
{
	if (active == false) this->hide();
}

/* KeyboardInput */
KeyboardInput::KeyboardInput()
{
	m_buttonTub = new Button("/", UISettings::fontSize());
	this->addChild(m_buttonTub);
	m_buttonPrev = new Button("<<", UISettings::fontSize());
	this->addChild(m_buttonPrev);
	m_buttonNext = new Button(">>", UISettings::fontSize());
	this->addChild(m_buttonNext);

	m_buttonPrev->setCallback([]()
							  {
								  std::string text = pUI->keyboard()->history()->prev();
								  pUI->keyboard()->input()->setInputString(text);
							  }
	);

	m_buttonNext->setCallback([]()
							  {
								  std::string text = pUI->keyboard()->history()->next();
								  pUI->keyboard()->input()->setInputString(text);
							  }
	);

	m_buttonTub->setCallback([]()
							 {
								 pUI->keyboard()->input()->setInputString("/");
							 }
	);

	m_buttonPrev->setVisible(true);
	m_buttonNext->setVisible(true);
	m_buttonTub->setVisible(true);
}

void KeyboardInput::performLayout()
{
	Widget::performLayout();

	m_buttonTub->setPosition(
			ImVec2(width() - (m_buttonTub->width() + UISettings::padding() + m_buttonPrev->width()*2.3 + UISettings::padding()),
				   (height() - m_buttonTub->height()) / 2));
	m_buttonPrev->setPosition(
			ImVec2(width() - (m_buttonPrev->width() + UISettings::padding() + m_buttonNext->width() + UISettings::padding()),
				   (height() - m_buttonPrev->height()) / 2));
	m_buttonNext->setPosition(
			ImVec2(width() - (m_buttonNext->width() + UISettings::padding()),
				   (height() - m_buttonNext->height()) / 2));
}

void KeyboardInput::addCharToInput(char value)
{
	m_input += value;
	m_caption = Emoji_Replace(Encoding::cp2utf(m_input));
}

void KeyboardInput::addStringToInput(const std::string& value)
{
	m_input += value;
	m_caption = Emoji_Replace(Encoding::cp2utf(m_input));
}

void KeyboardInput::popCharFromInput()
{
	if (m_input.empty()) return;

	// UTF-8 multibyte silinməsi üçün son simvolun uzunluğunu təyin edirik
	unsigned char c = m_input.back();
	int len = 1;
	if ((c & 0xE0) == 0xC0) len = 2;
	else if ((c & 0xF0) == 0xE0) len = 3;
	else if ((c & 0xF8) == 0xF0) len = 4;

	if (m_input.length() >= (size_t)len)
		m_input.erase(m_input.length() - len);
	else
		m_input.pop_back();

	m_caption = Emoji_Replace(Encoding::cp2utf(m_input));
}

void KeyboardInput::draw(ImGuiRenderer* renderer)
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
}

/* KeyboardLayout */

KeyboardLayout::KeyboardLayout()
		: Layout(Orientation::VERTICAL)
{
	for (int i = 0; i < 5; i++)
	{
		m_rows[i] = new Row();
		this->addChild(m_rows[i]);
	}
}

void KeyboardLayout::performLayout()
{
	Layout::performLayout();
}

/* KeyboardLayout::Row */

KeyboardLayout::Row::Row()
		: Layout(Orientation::HORIZONTAL)
{

}

KeyboardLayout::Row::Key* KeyboardLayout::Row::addKey(const std::string& caption, char value)
{
	Key* key = new Key(caption, value);
	this->addChild(key);
	return key;
}

KeyboardLayout::Row::Key* KeyboardLayout::Row::addKey(const std::string& caption, const std::string& valueStr)
{
	Key* key = new Key(caption, valueStr);
	this->addChild(key);
	return key;
}

KeyboardLayout::Row::Key* KeyboardLayout::Row::addKey(ExtendedKey extended_key)
{
	Key* key = new Key(extended_key);
	this->addChild(key);
	return key;
}

/* KeyboardLayout::Row::Key */

KeyboardLayout::Row::Key::Key(const std::string& caption, char value)
		: Button(caption)
{
	m_extendedKey = ExtendedKey::NONE;
	m_value = value;
	m_isStringValue = false;
}

KeyboardLayout::Row::Key::Key(const std::string& caption, const std::string& valueStr)
		: Button(caption)
{
	m_extendedKey = ExtendedKey::NONE;
	m_value = 0;
	m_valueStr = valueStr;
	m_isStringValue = true;
}

KeyboardLayout::Row::Key::Key(ExtendedKey extended_key)
		: Button("")
{
	switch (extended_key)
	{
		case ExtendedKey::NUM:
			this->setCaption(std::string("123"));
			break;
		case ExtendedKey::LANG_ENG:
			this->setCaption(std::string("ENG"));
			break;
		case ExtendedKey::LANG_AZE:
			this->setCaption(std::string("AZE"));
			break;
		case ExtendedKey::EMOJI:
			this->setCaption(std::string(":)"));
			break;
	}

	m_extendedKey = extended_key;
}

void KeyboardLayout::Row::Key::draw(ImGuiRenderer* renderer)
{
	{
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
	}

	switch (m_extendedKey)
	{
		case ExtendedKey::SHIFT_ON:
			renderer->drawTriangle(
					ImVec2(absolutePosition().x + width() * 0.37f, absolutePosition().y + height() * 0.50f),
					ImVec2(absolutePosition().x + width() * 0.50f, absolutePosition().y + height() * 0.25f),
					ImVec2(absolutePosition().x + width() * 0.63f, absolutePosition().y + height() * 0.50f),
					ImColor(1.0f, 1.0f, 1.0f), true
			);
			renderer->drawRect(
					ImVec2(absolutePosition().x + width() * 0.44f, absolutePosition().y + height() * 0.50f - 1),
					ImVec2(absolutePosition().x + width() * 0.56f, absolutePosition().y + height() * 0.68f),
					ImColor(1.0f, 1.0f, 1.0f), true
			);
			break;

		case ExtendedKey::SHIFT_OFF:
			renderer->drawTriangle(
					ImVec2(absolutePosition().x + width() * 0.37f, absolutePosition().y + height() * 0.50f),
					ImVec2(absolutePosition().x + width() * 0.50f, absolutePosition().y + height() * 0.25f),
					ImVec2(absolutePosition().x + width() * 0.63f, absolutePosition().y + height() * 0.50f),
					ImColor(0xF5, 0x91, 0x32), true
			);
			renderer->drawRect(
					ImVec2(absolutePosition().x + width() * 0.44f, absolutePosition().y + height() * 0.50f - 1),
					ImVec2(absolutePosition().x + width() * 0.56f, absolutePosition().y + height() * 0.68f),
					ImColor(0xF5, 0x91, 0x32), true
			);
			break;

		case ExtendedKey::BACKSPACE:
			static ImVec2 points[5];
			points[0] = ImVec2(absolutePosition().x + width() * 0.35f, absolutePosition().y + height() * 0.50f);
			points[1] = ImVec2(absolutePosition().x + width() * 0.45f, absolutePosition().y + height() * 0.25f);
			points[2] = ImVec2(absolutePosition().x + width() * 0.65f, absolutePosition().y + height() * 0.25f);
			points[3] = ImVec2(absolutePosition().x + width() * 0.65f, absolutePosition().y + height() * 0.65f);
			points[4] = ImVec2(absolutePosition().x + width() * 0.45f, absolutePosition().y + height() * 0.65f);
			renderer->drawConvexPolyFilled(points, 5, ImColor(1.0f, 1.0f, 1.0f));
			break;

		case ExtendedKey::SPACE:
			renderer->drawRect(
					ImVec2(absolutePosition().x + width() * 0.07f, absolutePosition().y + height() * 0.35f),
					ImVec2(absolutePosition().x + width() * 0.93f, absolutePosition().y + height() * 0.65f),
					ImColor(1.0f, 1.0f, 1.0f), true
			);
			break;

		case ExtendedKey::SEND:
			renderer->drawTriangle(
					ImVec2(absolutePosition().x + width() * 0.3f, absolutePosition().y + height() * 0.25f),
					ImVec2(absolutePosition().x + width() * 0.3f, absolutePosition().y + height() * 0.75f),
					ImVec2(absolutePosition().x + width() * 0.7f, absolutePosition().y + height() * 0.50f),
					ImColor(1.0f, 1.0f, 1.0f), true
			);
			break;
	}

	Widget::draw(renderer);
}

void KeyboardLayout::Row::Key::touchPopEvent()
{
	Keyboard* keyboard = dynamic_cast<Keyboard*>(this->parent()->parent()->parent());

	switch (m_extendedKey)
	{
		case ExtendedKey::BACKSPACE:
			keyboard->input()->popCharFromInput();
			break;

		case ExtendedKey::SPACE:
			keyboard->input()->addCharToInput(' ');
			break;

		case ExtendedKey::SEND:
			keyboard->send();
			break;

		case ExtendedKey::SHIFT_ON:
			keyboard->changeCase(KeyCase::UPPER);
			break;

		case ExtendedKey::SHIFT_OFF:
			keyboard->changeCase(KeyCase::LOWER);
			break;

		case ExtendedKey::LANG_AZE:
			keyboard->setActiveLayout(KeyboardLayout::Type::AZE_LOWER);
			break;

		case ExtendedKey::LANG_ENG:
			keyboard->setActiveLayout(KeyboardLayout::Type::ENG_LOWER);
			break;

		case ExtendedKey::NUM:
			keyboard->setActiveLayout(KeyboardLayout::Type::NUM);
			break;

		case ExtendedKey::EMOJI:
			keyboard->setActiveLayout(KeyboardLayout::Type::EMOJI);
			break;

		case ExtendedKey::NONE:
			if (m_isStringValue)
				keyboard->input()->addStringToInput(m_valueStr);
			else
				keyboard->input()->addCharToInput(m_value);
			break;
	}
}

/* KeyboardLayoutLowerENG */

KeyboardLayoutLowerENG::KeyboardLayoutLowerENG() : KeyboardLayout() {}

void KeyboardLayoutLowerENG::performLayout()
{
	m_rows[0]->setPadding(0.0f);
	m_rows[0]->setSpacing(0.0f);
	m_rows[0]->addKey(std::string("q"), 'q');
	m_rows[0]->addKey(std::string("w"), 'w');
	m_rows[0]->addKey(std::string("e"), 'e');
	m_rows[0]->addKey(std::string("r"), 'r');
	m_rows[0]->addKey(std::string("t"), 't');
	m_rows[0]->addKey(std::string("y"), 'y');
	m_rows[0]->addKey(std::string("u"), 'u');
	m_rows[0]->addKey(std::string("i"), 'i');
	m_rows[0]->addKey(std::string("o"), 'o');
	m_rows[0]->addKey(std::string("p"), 'p');

	ImVec2 keySize = ImVec2(width() / 10, UISettings::keyboardRowHeight());
	float padding = keySize.x / 2;
	m_rows[1]->setPadding(0.0f);
	m_rows[1]->setSpacing(0.0f);
	m_rows[1]->setPaddingLeft(padding);
	m_rows[1]->setPaddingRight(padding);
	m_rows[1]->addKey(std::string("a"), 'a');
	m_rows[1]->addKey(std::string("s"), 's');
	m_rows[1]->addKey(std::string("d"), 'd');
	m_rows[1]->addKey(std::string("f"), 'f');
	m_rows[1]->addKey(std::string("g"), 'g');
	m_rows[1]->addKey(std::string("h"), 'h');
	m_rows[1]->addKey(std::string("j"), 'j');
	m_rows[1]->addKey(std::string("k"), 'k');
	m_rows[1]->addKey(std::string("l"), 'l');

	m_rows[2]->setPadding(0.0f);
	m_rows[2]->setSpacing(0.0f);
	m_rows[2]->addKey(ExtendedKey::SHIFT_ON)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));
	m_rows[2]->addKey(std::string("z"), 'z');
	m_rows[2]->addKey(std::string("x"), 'x');
	m_rows[2]->addKey(std::string("c"), 'c');
	m_rows[2]->addKey(std::string("v"), 'v');
	m_rows[2]->addKey(std::string("b"), 'b');
	m_rows[2]->addKey(std::string("n"), 'n');
	m_rows[2]->addKey(std::string("m"), 'm');
	m_rows[2]->addKey(ExtendedKey::BACKSPACE)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::NUM);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 3, 0.0f));
	m_rows[3]->addKey(std::string("/"), '/');
	m_rows[3]->addKey(ExtendedKey::EMOJI);
	m_rows[3]->addKey(std::string("."), '.');
	m_rows[3]->addKey(ExtendedKey::SEND)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	KeyboardLayout::performLayout();
}

/* KeyboardLayoutUpperENG */

KeyboardLayoutUpperENG::KeyboardLayoutUpperENG() : KeyboardLayout() {}

void KeyboardLayoutUpperENG::performLayout()
{
	m_rows[0]->setPadding(0.0f);
	m_rows[0]->setSpacing(0.0f);
	m_rows[0]->addKey(std::string("Q"), 'Q');
	m_rows[0]->addKey(std::string("W"), 'W');
	m_rows[0]->addKey(std::string("E"), 'E');
	m_rows[0]->addKey(std::string("R"), 'R');
	m_rows[0]->addKey(std::string("T"), 'T');
	m_rows[0]->addKey(std::string("Y"), 'Y');
	m_rows[0]->addKey(std::string("U"), 'U');
	m_rows[0]->addKey(std::string("I"), 'I');
	m_rows[0]->addKey(std::string("O"), 'O');
	m_rows[0]->addKey(std::string("P"), 'P');

	ImVec2 keySize = ImVec2(width() / 10, UISettings::keyboardRowHeight());
	float padding = keySize.x / 2;
	m_rows[1]->setPadding(0.0f);
	m_rows[1]->setSpacing(0.0f);
	m_rows[1]->setPaddingLeft(padding);
	m_rows[1]->setPaddingRight(padding);
	m_rows[1]->addKey(std::string("A"), 'A');
	m_rows[1]->addKey(std::string("S"), 'S');
	m_rows[1]->addKey(std::string("D"), 'D');
	m_rows[1]->addKey(std::string("F"), 'F');
	m_rows[1]->addKey(std::string("G"), 'G');
	m_rows[1]->addKey(std::string("H"), 'H');
	m_rows[1]->addKey(std::string("J"), 'J');
	m_rows[1]->addKey(std::string("K"), 'K');
	m_rows[1]->addKey(std::string("L"), 'L');

	m_rows[2]->setPadding(0.0f);
	m_rows[2]->setSpacing(0.0f);
	m_rows[2]->addKey(ExtendedKey::SHIFT_OFF)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));
	m_rows[2]->addKey(std::string("Z"), 'Z');
	m_rows[2]->addKey(std::string("X"), 'X');
	m_rows[2]->addKey(std::string("C"), 'C');
	m_rows[2]->addKey(std::string("V"), 'V');
	m_rows[2]->addKey(std::string("B"), 'B');
	m_rows[2]->addKey(std::string("N"), 'N');
	m_rows[2]->addKey(std::string("M"), 'M');
	m_rows[2]->addKey(ExtendedKey::BACKSPACE)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::NUM);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 3, 0.0f));
	m_rows[3]->addKey(std::string("/"), '/');
	m_rows[3]->addKey(ExtendedKey::EMOJI);
	m_rows[3]->addKey(std::string("."), '.');
	m_rows[3]->addKey(ExtendedKey::SEND)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	KeyboardLayout::performLayout();
}

/* KeyboardLayoutLowerAZE (Azərbaycan klaviaturası - Kiçik hərflər və xüsusi hərflər) */

KeyboardLayoutLowerAZE::KeyboardLayoutLowerAZE() : KeyboardLayout() {}

void KeyboardLayoutLowerAZE::performLayout()
{
	m_rows[0]->setPadding(0.0f);
	m_rows[0]->setSpacing(0.0f);
	m_rows[0]->addKey("q", "q");
	m_rows[0]->addKey("w", "w");
	m_rows[0]->addKey("e", "e");
	m_rows[0]->addKey("r", "r");
	m_rows[0]->addKey("t", "t");
	m_rows[0]->addKey("y", "y");
	m_rows[0]->addKey("u", "u");
	m_rows[0]->addKey("ı", "ı");
	m_rows[0]->addKey("o", "o");
	m_rows[0]->addKey("p", "p");
	m_rows[0]->addKey("ü", "ü");

	m_rows[1]->setPadding(0.0f);
	m_rows[1]->setSpacing(0.0f);
	m_rows[1]->addKey("a", "a");
	m_rows[1]->addKey("s", "s");
	m_rows[1]->addKey("d", "d");
	m_rows[1]->addKey("f", "f");
	m_rows[1]->addKey("g", "g");
	m_rows[1]->addKey("h", "h");
	m_rows[1]->addKey("j", "j");
	m_rows[1]->addKey("k", "k");
	m_rows[1]->addKey("l", "l");
	m_rows[1]->addKey("ş", "ş");
	m_rows[1]->addKey("ö", "ö");

	ImVec2 keySize = ImVec2(width() / 11, UISettings::keyboardRowHeight());
	m_rows[2]->setPadding(0.0f);
	m_rows[2]->setSpacing(0.0f);
	m_rows[2]->addKey(ExtendedKey::SHIFT_ON)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));
	m_rows[2]->addKey("z", "z");
	m_rows[2]->addKey("x", "x");
	m_rows[2]->addKey("c", "c");
	m_rows[2]->addKey("v", "v");
	m_rows[2]->addKey("b", "b");
	m_rows[2]->addKey("n", "n");
	m_rows[2]->addKey("m", "m");
	m_rows[2]->addKey("ə", "ə");
	m_rows[2]->addKey("ç", "ç");
	m_rows[2]->addKey("ğ", "ğ");
	m_rows[2]->addKey(ExtendedKey::BACKSPACE)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::NUM);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 3, 0.0f));
	m_rows[3]->addKey(std::string("/"), '/');
	m_rows[3]->addKey(ExtendedKey::EMOJI);
	m_rows[3]->addKey(std::string("."), '.');
	m_rows[3]->addKey(ExtendedKey::SEND)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	KeyboardLayout::performLayout();
}

/* KeyboardLayoutUpperAZE (Azərbaycan klaviaturası - Böyük hərflər) */

KeyboardLayoutUpperAZE::KeyboardLayoutUpperAZE() : KeyboardLayout() {}

void KeyboardLayoutUpperAZE::performLayout()
{
	m_rows[0]->setPadding(0.0f);
	m_rows[0]->setSpacing(0.0f);
	m_rows[0]->addKey("Q", "Q");
	m_rows[0]->addKey("W", "W");
	m_rows[0]->addKey("E", "E");
	m_rows[0]->addKey("R", "R");
	m_rows[0]->addKey("T", "T");
	m_rows[0]->addKey("Y", "Y");
	m_rows[0]->addKey("U", "U");
	m_rows[0]->addKey("İ", "İ"); // 'ı' hərfinin böyük forması İ
	m_rows[0]->addKey("O", "O");
	m_rows[0]->addKey("P", "P");
	m_rows[0]->addKey("Ü", "Ü");

	m_rows[1]->setPadding(0.0f);
	m_rows[1]->setSpacing(0.0f);
	m_rows[1]->addKey("A", "A");
	m_rows[1]->addKey("S", "S");
	m_rows[1]->addKey("D", "D");
	m_rows[1]->addKey("F", "F");
	m_rows[1]->addKey("G", "G");
	m_rows[1]->addKey("H", "H");
	m_rows[1]->addKey("J", "J");
	m_rows[1]->addKey("K", "K");
	m_rows[1]->addKey("L", "L");
	m_rows[1]->addKey("Ş", "Ş");
	m_rows[1]->addKey("Ö", "Ö");

	ImVec2 keySize = ImVec2(width() / 11, UISettings::keyboardRowHeight());
	m_rows[2]->setPadding(0.0f);
	m_rows[2]->setSpacing(0.0f);
	m_rows[2]->addKey(ExtendedKey::SHIFT_OFF)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));
	m_rows[2]->addKey("Z", "Z");
	m_rows[2]->addKey("X", "X");
	m_rows[2]->addKey("C", "C");
	m_rows[2]->addKey("V", "V");
	m_rows[2]->addKey("B", "B");
	m_rows[2]->addKey("N", "N");
	m_rows[2]->addKey("M", "M");
	m_rows[2]->addKey("Ə", "Ə");
	m_rows[2]->addKey("Ç", "Ç");
	m_rows[2]->addKey("Ğ", "Ğ");
	m_rows[2]->addKey(ExtendedKey::BACKSPACE)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::NUM);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 3, 0.0f));
	m_rows[3]->addKey(std::string("/"), '/');
	m_rows[3]->addKey(ExtendedKey::EMOJI);
	m_rows[3]->addKey(std::string("."), '.');
	m_rows[3]->addKey(ExtendedKey::SEND)->setFixedSize(ImVec2(keySize.x * 1.5f, 0.0f));

	KeyboardLayout::performLayout();
}

/* KeyboardLayoutNUM */

KeyboardLayoutNUM::KeyboardLayoutNUM() : KeyboardLayout() {}

void KeyboardLayoutNUM::performLayout()
{
	m_rows[0]->setPadding(0.0f);
	m_rows[0]->setSpacing(0.0f);
	m_rows[0]->addKey(std::string("1"), '1');
	m_rows[0]->addKey(std::string("2"), '2');
	m_rows[0]->addKey(std::string("3"), '3');
	m_rows[0]->addKey(std::string("4"), '4');
	m_rows[0]->addKey(std::string("5"), '5');
	m_rows[0]->addKey(std::string("6"), '6');
	m_rows[0]->addKey(std::string("7"), '7');
	m_rows[0]->addKey(std::string("8"), '8');
	m_rows[0]->addKey(std::string("9"), '9');
	m_rows[0]->addKey(std::string("0"), '0');

	m_rows[1]->setPadding(0.0f);
	m_rows[1]->setSpacing(0.0f);
	m_rows[1]->addKey(std::string("@"), '@');
	m_rows[1]->addKey(std::string("#"), '#');
	m_rows[1]->addKey(std::string("$"), '$');
	m_rows[1]->addKey(std::string("%"), '%');
	m_rows[1]->addKey(std::string("\""), '\"');
	m_rows[1]->addKey(std::string("*"), '*');
	m_rows[1]->addKey(std::string("("), '(');
	m_rows[1]->addKey(std::string(")"), ')');
	m_rows[1]->addKey(std::string("-"), '-');
	m_rows[1]->addKey(std::string("_"), '_');

	m_rows[2]->setPadding(0.0f);
	m_rows[2]->setSpacing(0.0f);
	m_rows[2]->addKey(std::string("."), '.');
	m_rows[2]->addKey(std::string(":"), ':');
	m_rows[2]->addKey(std::string(";"), ';');
	m_rows[2]->addKey(std::string("+"), '+');
	m_rows[2]->addKey(std::string("="), '=');
	m_rows[2]->addKey(std::string("<"), '<');
	m_rows[2]->addKey(std::string(">"), '>');
	m_rows[2]->addKey(std::string("["), '[');
	m_rows[2]->addKey(std::string("]"), ']');
	m_rows[2]->addKey(ExtendedKey::BACKSPACE);

	ImVec2 keySize = ImVec2(width() / 10, UISettings::keyboardRowHeight());

	m_rows[3]->setPadding(0.0f);
	m_rows[3]->setSpacing(0.0f);
	m_rows[3]->addKey(ExtendedKey::NUM);
	m_rows[3]->addKey(ExtendedKey::LANG_ENG);
	m_rows[3]->addKey(ExtendedKey::LANG_AZE);
	m_rows[3]->addKey(ExtendedKey::SPACE)->setFixedSize(ImVec2(keySize.x * 4, 0.0f));
	m_rows[3]->addKey(std::string("/"), '/');
	m_rows[3]->addKey(std::string("?"), '?');
	m_rows[3]->addKey(std::string("!"), '!');
	m_rows[3]->addKey(ExtendedKey::SEND);

	KeyboardLayout::performLayout();
}

KeyboardHistory::KeyboardHistory()
{
	m_index = 0;
	m_count = 0;
	m_vMessages.resize(KEYBOARD_MAX_HISTORY_MESSAGES);
}

void KeyboardHistory::add(const std::string& input)
{
	for (int i = KEYBOARD_MAX_HISTORY_MESSAGES - 2; i > 0; i--)
	{
		std::string str = m_vMessages[i];
		m_vMessages[i] = m_vMessages[i - 1];
		m_vMessages[i - 1] = str;
	}

	m_vMessages[0] = input;
	if (m_count < KEYBOARD_MAX_HISTORY_MESSAGES) m_count++;
}

void KeyboardHistory::reset()
{
	m_index = 0;
}

const std::string KeyboardHistory::next()
{
	if (m_count == 0) return "";

	std::string str = m_vMessages[m_index];

	if (m_index < m_count - 1) m_index++;
	return str;
}

const std::string KeyboardHistory::prev()
{
	if (m_count == 0) return "";

	std::string str = m_vMessages[m_index];
	if (m_index > 0) m_index--;
	return str;
}


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

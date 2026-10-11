#include "../main.h"
#include "../game/game.h"
#include "../net/netgame.h"
#include "../game/vehicle.h"
#include "../game/VehTuner.h"
#include "gui.h"
#include "../playertags.h"
#include "../net/playerbubblepool.h"
#include "vendor/str_obfuscator/str_obfuscator.hpp"
// voice
#include "../voice_new/Plugin.h"
#include "../voice_new/MicroIcon.h"
#include "../voice_new/SpeakerList.h"
#include "../voice_new/Network.h"

#include "../gui/samp_widgets/voicebutton.h"
#include "game/Textures/TextureDatabaseRuntime.h"
#include "game/Streaming.h"
#include "game/Pools.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <deque>
#include <mutex>

extern CGame* pGame;
extern CNetGame* pNetGame;
extern CPlayerTags* pPlayerTags;
extern UI* pUI;

extern bool g_bHideAllUI;
extern bool g_bShowHandlingDlg;

// Diaqnostika loglari (FLog). Problem hell olunandan sonra 0 et.
#define HND_DEBUG 1
#if HND_DEBUG
#define HND_LOG(...) FLog(__VA_ARGS__)
#else
#define HND_LOG(...) ((void)0)
#endif

enum eTouchType
{
	TOUCH_POP = 1,
	TOUCH_PUSH = 2,
	TOUCH_MOVE = 3
};

// ============================================================================
//  TOXUNUS QUEUE-SU
//
//  Toxunus hadiseleri ayri thread-den gele biler. Burada onlar queue-ya yigilir,
//  UI::render() kadrdan ONCE bunlari ImGui-ye verir.
//  Qayda: bir kadrda en cox BIR dugme veziyyeti deyisir (basma / buraxma).
//  Bu, qisa toxunusun (basma+buraxma eyni anda) ImGui terefinden gorulmesini temin edir.
// ============================================================================
namespace
{
	struct TouchEv { int type; float x; float y; };

	std::mutex          s_touchMtx;
	std::deque<TouchEv> s_touchQ;
	bool                s_touchClearPos = false;

	void ImSetMousePos(float x, float y)
	{
		ImGuiIO& io = ImGui::GetIO();
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18700
		io.AddMousePosEvent(x, y);
#else
		io.MousePos = ImVec2(x, y);
#endif
	}

	void ImSetMouseDown(bool down)
	{
		ImGuiIO& io = ImGui::GetIO();
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18700
		io.AddMouseButtonEvent(0, down);
#else
		io.MouseDown[0] = down;
#endif
	}

	void PushTouch(int type, float x, float y)
	{
		std::lock_guard<std::mutex> lk(s_touchMtx);

		if (type == TOUCH_MOVE && !s_touchQ.empty() && s_touchQ.back().type == TOUCH_MOVE) {
			s_touchQ.back().x = x;
			s_touchQ.back().y = y;
			return;
		}
		if (s_touchQ.size() > 512) s_touchQ.clear();
		s_touchQ.push_back({ type, x, y });
	}

	// ImGui::NewFrame()-DEN ƏVVƏL çağırılmalıdır
	void ProcessTouchQueue()
	{
		std::lock_guard<std::mutex> lk(s_touchMtx);

		// Onceki kadrda buraxilmisdi -> isareci sil (hover/active qalmasin)
		if (s_touchClearPos) {
			s_touchClearPos = false;
			ImSetMousePos(-FLT_MAX, -FLT_MAX);
		}

		while (!s_touchQ.empty())
		{
			const TouchEv e = s_touchQ.front();
			s_touchQ.pop_front();

			ImSetMousePos(e.x, e.y);

			if (e.type == TOUCH_PUSH) { ImSetMouseDown(true);  break; }
			if (e.type == TOUCH_POP)  { ImSetMouseDown(false); s_touchClearPos = true; break; }
			// TOUCH_MOVE: novbetini davam et
		}
	}
}

UI::UI(const ImVec2& display_size, const std::string& font_path)
	: Widget(), ImGuiWrapper(display_size, font_path)
{
	UISettings::Initialize(display_size);
	this->setFixedSize(display_size);
}

bool UI::initialize()
{
	if (!ImGuiWrapper::initialize()) return false;

	m_splashScreen = new SplashScreen();
	this->addChild(m_splashScreen);
	m_splashScreen->setFixedSize(size());
	m_splashScreen->setPosition(ImVec2(0.0f, 0.0f));
	m_splashScreen->setVisible(true);

	m_chat = new Chat();
	this->addChild(m_chat);
	m_chat->setFixedSize(UISettings::chatSize());
	m_chat->setPosition(UISettings::chatPos());
	m_chat->setItemSize(UISettings::chatItemSize());
	m_chat->setVisible(false);

	m_buttonPanel = new ButtonPanel();
	this->addChild(m_buttonPanel);
	m_buttonPanel->setFixedSize(UISettings::buttonPanelSize());
	m_buttonPanel->setPosition(UISettings::buttonPanelPos());
	m_buttonPanel->setVisible(false);

	m_voiceButton = new VoiceButton();
	this->addChild(m_voiceButton);
	m_voiceButton->setFixedSize(UISettings::buttonVoiceSize());
	m_voiceButton->setPosition(UISettings::buttonVoicePos());
	m_voiceButton->setVisible(false);

	m_spawn = new Spawn();
	this->addChild(m_spawn);
	m_spawn->setFixedSize(UISettings::spawnSize());
	m_spawn->setPosition(UISettings::spawnPos());
	m_spawn->setVisible(false);

	m_dialog = new Dialog();
	this->addChild(m_dialog);
	m_dialog->setVisible(false);
	m_dialog->setMinSize(UISettings::dialogMinSize());
	m_dialog->setMaxSize(UISettings::dialogMaxSize());

	m_keyboard = new Keyboard();
	this->addChild(m_keyboard);
	m_keyboard->setFixedSize(UISettings::keyboardSize());
	m_keyboard->setPosition(UISettings::keyboardPos());
	m_keyboard->setVisible(false);

	m_playerTabList = new PlayerTabList();
	this->addChild(m_playerTabList);
	m_playerTabList->setMinSize(UISettings::dialogMinSize());
	m_playerTabList->setMaxSize(UISettings::dialogMaxSize());
	m_playerTabList->setVisible(false);

	label = new Label(" ", ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 2);
	pUI->addChild(label);

	label2 = new Label(" ", ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 2);
	pUI->addChild(label2);

	label3 = new Label(" ", ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 2);
	pUI->addChild(label3);

	label4 = new Label(" ", ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 2);
	pUI->addChild(label4);

	Label* d_label1;
	d_label1 = new Label(cryptor::create("SA:MP Mobile 2.10 x64").decrypt(), ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 3);
	this->addChild(d_label1);
	d_label1->setPosition(ImVec2(3.0, 3.0));

	return true;
}

void UI::render()
{
	// toxunuslari ImGui-ye NewFrame-den evvel ver
	ProcessTouchQueue();

	ImGuiWrapper::render();

	renderDebug();

	ProcessPushedTextdraws();
}

void UI::shutdown()
{
	ImGuiWrapper::shutdown();
}

// ============================================================================
//  HANDLING REDAKTORU
//
//  - Oz basliq cubugu: surukle (yer deyis), [H-][H+] hundurluk, [-] kicilt, [X] bagla
//  - Sag-alt kunc: surukleyerek olcunu deyis
//  - Olcu/movqe yadda qalir, ekrandan cixmir
//  - Hamisi toxunusla (barmaq) rahat isleyir
// ============================================================================
static bool   s_hndRectValid = false;
static ImVec2 s_hndPos(0.0f, 0.0f);    // yalniz toxunus yoxlamasi ucun (real pencere)
static ImVec2 s_hndSize(0.0f, 0.0f);

bool UI_IsPointInHandlingDlg(const ImVec2& p)
{
	if (!g_bShowHandlingDlg || !s_hndRectValid) return false;
	return p.x >= s_hndPos.x && p.x <= s_hndPos.x + s_hndSize.x &&
	       p.y >= s_hndPos.y && p.y <= s_hndPos.y + s_hndSize.y;
}

void RenderHandlingDialog()
{
	static bool          s_wasOpen = false;
	static CVehicleGTA*  s_lastVeh = nullptr;
	static VehTune       s_tune;

	// pencere veziyyeti (baglanib-acilanda yadda qalir)
	static bool   s_winInit   = false;
	static bool   s_collapsed = false;
	static ImVec2 s_winPos(0.0f, 0.0f);
	static ImVec2 s_winSize(0.0f, 0.0f);
	static ImVec2 s_dragOff(0.0f, 0.0f);
	static ImVec2 s_rsMouse0(0.0f, 0.0f);
	static ImVec2 s_rsSize0(0.0f, 0.0f);

	if (!g_bShowHandlingDlg) {
		s_wasOpen = false;
		s_hndRectValid = false;
		return;
	}

	CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
		g_bShowHandlingDlg = false;
		s_wasOpen = false;
		s_hndRectValid = false;
		return;
	}

	CVehicle* pVeh = nullptr;
	if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
		CVehiclePool* pPool = pNetGame->GetVehiclePool();
		VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
		if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
	}

	if (!pVeh || !pVeh->m_pVehicle) {
		s_hndRectValid = false;
		return;
	}

	CVehicleGTA* gtaVeh = pVeh->m_pVehicle;

	// Yeni acilis ve ya basqa masin -> deyerleri bu masinin real veziyyetinden oxu
	if (!s_wasOpen || gtaVeh != s_lastVeh) {
		s_tune    = VehTuner::Get(gtaVeh);
		s_lastVeh = gtaVeh;
		s_wasOpen = true;
		HND_LOG("[HND] dialog opened");
	}

	ImGuiIO& io = ImGui::GetIO();

	// ---- olculer ----
	const ImVec2 disp     = io.DisplaySize;
	const float  baseFont = ImGui::GetFont()->FontSize;     // miqyassiz esas srift
	const float  fs  = std::min(1.0f, (disp.y * 0.94f) / (baseFont * 20.0f));
	const float  f   = baseFont * fs;
	const float  pad = f * 0.6f;
	const float  hh  = f * 2.6f;       // basliq hundurluyu
	const float  bw  = f * 2.6f;       // basliq dugmesi eni
	const float  footerH = f * 2.4f + pad;

	if (!s_winInit) {
		s_winSize = ImVec2(std::min(disp.x * 0.62f, f * 34.0f), std::min(disp.y * 0.88f, f * 19.0f));
		s_winPos  = ImVec2((disp.x - s_winSize.x) * 0.5f, (disp.y - s_winSize.y) * 0.5f);
		s_winInit = true;
	}

	// olcu ve movqe hududlari
	s_winSize.x = std::max(f * 18.0f, std::min(s_winSize.x, disp.x * 0.98f));
	s_winSize.y = std::max(f * 10.0f, std::min(s_winSize.y, disp.y * 0.98f));
	const ImVec2 eff(s_winSize.x, s_collapsed ? (hh + pad) : s_winSize.y);
	s_winPos.x = std::max(0.0f, std::min(s_winPos.x, std::max(0.0f, disp.x - eff.x)));
	s_winPos.y = std::max(0.0f, std::min(s_winPos.y, std::max(0.0f, disp.y - eff.y)));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, f * 0.3f);
	ImGui::SetNextWindowPos(s_winPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(eff, ImGuiCond_Always);

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
	                               ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
	                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
	                               ImGuiWindowFlags_NoScrollWithMouse;

	if (ImGui::Begin("##VehicleHandlingEditor", nullptr, flags))
	{
		ImGui::SetWindowFontScale(fs);

		s_hndPos       = ImGui::GetWindowPos();
		s_hndSize      = ImGui::GetWindowSize();
		s_hndRectValid = true;

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 wp = ImGui::GetWindowPos();
		const float  ww = ImGui::GetWindowWidth();

		// ---------------- BASLIQ ----------------
		dl->AddRectFilled(wp, ImVec2(wp.x + ww, wp.y + hh), IM_COL32(25, 55, 105, 255));

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
		ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));

		// surukleme zonasi
		ImGui::InvisibleButton("##hnd_drag", ImVec2(std::max(1.0f, ww - bw * 4.0f), hh));
		if (ImGui::IsItemActivated()) {
			s_dragOff = ImVec2(io.MousePos.x - s_winPos.x, io.MousePos.y - s_winPos.y);
		}
		if (ImGui::IsItemActive()) {
			s_winPos = ImVec2(io.MousePos.x - s_dragOff.x, io.MousePos.y - s_dragOff.y);
		}
		dl->AddText(ImVec2(wp.x + pad, wp.y + (hh - f) * 0.5f), IM_COL32(255, 255, 255, 255), "Handling Editor APACHES");

		const ImVec2 hb(bw, hh);
		ImGui::SameLine(0.0f, 0.0f);
		if (ImGui::Button("H-", hb)) s_winSize.y -= f * 2.5f;
		ImGui::SameLine(0.0f, 0.0f);
		if (ImGui::Button("H+", hb)) s_winSize.y += f * 2.5f;
		ImGui::SameLine(0.0f, 0.0f);
		if (ImGui::Button(s_collapsed ? "[]" : "-", hb)) s_collapsed = !s_collapsed;
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
		if (ImGui::Button("X", hb)) g_bShowHandlingDlg = false;
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();   // FrameRounding

		// ---------------- GOVDE ----------------
		if (!s_collapsed)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(f * 0.5f, f * 0.45f));
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(f * 0.6f, f * 0.45f));
			ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,   f * 1.6f);
			ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, f * 1.1f);

			// sehife duymeleri
			static int s_page = 0;
			ImGui::SetCursorPos(ImVec2(pad, hh + pad));
			{
				static const char* kPages[4] = { "Muherrik", "Idare", "Asma", "Far" };
				const float spc = ImGui::GetStyle().ItemSpacing.x;
				const float tw  = (ww - pad * 2.0f - spc * 3.0f) / 4.0f;
				for (int i = 0; i < 4; ++i) {
					if (i) ImGui::SameLine();
					const bool sel = (s_page == i);
					if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.85f, 1.0f));
					if (ImGui::Button(kPages[i], ImVec2(tw, f * 2.2f))) s_page = i;
					if (sel) ImGui::PopStyleColor();
				}
			}

			ImGui::SetCursorPosX(pad);
			ImGui::BeginChild("##hnd_body", ImVec2(-pad, -footerH), false);

			bool changed = false;

			struct Spec { const char* label; const char* id; float* v; float mn; float mx; };

			auto drawSpecs = [&](const Spec* s, int n)
			{
				const int cols = (ImGui::GetContentRegionAvail().x > f * 26.0f) ? 2 : 1;
				if (cols == 2) ImGui::Columns(2, "##hnd_cols", false);
				const int half = (n + 1) / 2;
				for (int i = 0; i < n; ++i) {
					if (cols == 2 && i == half) ImGui::NextColumn();
					ImGui::TextUnformatted(s[i].label);
					ImGui::PushItemWidth(-1.0f);
					if (ImGui::SliderFloat(s[i].id, s[i].v, s[i].mn, s[i].mx, "%.0f%%")) changed = true;
					ImGui::PopItemWidth();
				}
				if (cols == 2) ImGui::Columns(1);
			};

			ImGui::TextUnformatted("HANDLING DƏYİŞİCİ");

			if (s_page == 0)          // MUHERRIK
			{
				const Spec sp[] = {
					{ "Yerinden Goturme (Tecil)",  "##acc", &s_tune.accelPct,   30.0f, 400.0f },
					{ "Maks. Suret",               "##spd", &s_tune.speedPct,   50.0f, 250.0f },
					{ "Muherrik Ataleti (az=tez)", "##ine", &s_tune.inertiaPct, 20.0f, 300.0f },
					{ "Ceki / Kutle",              "##mas", &s_tune.massPct,    30.0f, 300.0f },
					{ "Hava Muqavimeti",           "##drg", &s_tune.dragPct,    20.0f, 300.0f },
				};
				drawSpecs(sp, 5);
			}
			else if (s_page == 1)     // IDAREETME
			{
				const Spec sp[] = {
					{ "Tormoz Gucu",                "##brk", &s_tune.brakePct,    30.0f, 300.0f },
					{ "Manevr Bucagi",              "##str", &s_tune.steerPct,    50.0f, 150.0f },
					{ "Yapisma (Traction)",         "##trc", &s_tune.tractionPct, 50.0f, 200.0f },
					{ "Surusme Itkisi (az=yaxsi)",  "##trl", &s_tune.tracLossPct, 20.0f, 200.0f },
					{ "Firlanma Ataleti",           "##trn", &s_tune.turnPct,     30.0f, 300.0f },
				};
				drawSpecs(sp, 5);
			}
			else if (s_page == 2)     // ASMA / ZERBE
			{
				const Spec sp[] = {
					{ "Asma Gucu",           "##sf",  &s_tune.suspForcePct, 30.0f, 300.0f },
					{ "Asma Sonduruculeri",  "##sd",  &s_tune.suspDampPct,  30.0f, 300.0f },
					{ "Toqqusma Zereri",     "##col", &s_tune.collisionPct, 10.0f, 300.0f },
				};
				drawSpecs(sp, 3);
			}
			else                      // FAR RENGI (sonra tamamlanacaq)
			{
				if (s_tune.headlight == 0) ImGui::TextUnformatted("Far Rengi: Standart");
				else                       ImGui::Text("Far Rengi: #%d", (int)s_tune.headlight);

				const ImVec2 bs(f * 2.2f, f * 2.2f);
				for (int i = 0; i < VEHTUNER_COLOR_COUNT; ++i)
				{
					uint8_t r, g, b;
					VehTuner::GetPaletteColor(i, r, g, b);

					ImGui::PushID(i);
					if (i % 8 != 0) ImGui::SameLine();

					const ImVec4 col(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
					if (ImGui::ColorButton("##hl", col, ImGuiColorEditFlags_NoTooltip, bs)) {
						s_tune.headlight = (uint8_t)i;
						changed = true;
					}
					if (s_tune.headlight == i) {
						ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
						                                    IM_COL32(255, 255, 255, 255), 0.0f, 0, f * 0.18f);
					}
					ImGui::PopID();
				}
			}

			ImGui::EndChild();

			// ---------------- ALT SIRA: Sifirla + olcu kunci ----------------
			ImGui::SetCursorPosX(pad);
			if (ImGui::Button("Sifirla", ImVec2(f * 8.0f, f * 2.4f))) {
				VehTuner::Reset(gtaVeh);
				s_tune = VehTune();
				changed = false;
				if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[Handling]: Standart deyerler qaytarildi.");
			}

			const float gripS = f * 2.4f;
			ImGui::SameLine(ImGui::GetWindowWidth() - pad - gripS);
			ImGui::InvisibleButton("##hnd_grip", ImVec2(gripS, gripS));
			if (ImGui::IsItemActivated()) {
				s_rsMouse0 = io.MousePos;
				s_rsSize0  = s_winSize;
			}
			if (ImGui::IsItemActive()) {
				s_winSize.x = s_rsSize0.x + (io.MousePos.x - s_rsMouse0.x);
				s_winSize.y = s_rsSize0.y + (io.MousePos.y - s_rsMouse0.y);
			}
			{
				const ImVec2 a = ImGui::GetItemRectMin();
				const ImVec2 b = ImGui::GetItemRectMax();
				const ImU32  gc = ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255);
				dl->AddTriangleFilled(ImVec2(b.x, a.y + gripS * 0.2f), b, ImVec2(a.x + gripS * 0.2f, b.y), gc);
			}

			// Her deyisiklik canli tetbiq olunur (original-dan hesablandigi ucun yigilmir)
			if (changed) VehTuner::Set(gtaVeh, s_tune);

			ImGui::PopStyleVar(4);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);   // WindowPadding + WindowRounding
}

void UI::drawList()
{
	if (g_bHideAllUI) {
		RenderHandlingDialog();
		return;
	}

	if (!visible()) return;

	if (pPlayerTags) pPlayerTags->Render(renderer());
	if (pNetGame && pNetGame->GetTextLabelPool()) pNetGame->GetTextLabelPool()->Render(renderer());
	if (pNetGame && pNetGame->GetPlayerBubblePool()) pNetGame->GetPlayerBubblePool()->Render(renderer());

	RenderHandlingDialog();

	draw(renderer());
}

void UI::touchEvent(const ImVec2& pos, TouchType type)
{
	// Handling dialoqunun uzerine basanda alt widget-ler (chat, buttonpanel ...) toxunusu almasin
	if (UI_IsPointInHandlingDlg(pos))
		return;

	if (m_keyboard->visible() && m_keyboard->contains(pos))
	{
		m_keyboard->touchEvent(pos, type);
		return;
	}

	if (m_dialog->visible() && m_dialog->contains(pos))
	{
		m_dialog->touchEvent(pos, type);
		return;
	}

	Widget::touchEvent(pos, type);
}

bool UI::OnTouchEvent(int type, bool multi, int x, int y)
{
	(void)multi;

	if (type != TOUCH_MOVE)
		HND_LOG("[HND] touch type=%d x=%d y=%d dlg=%d", type, x, y, (int)g_bShowHandlingDlg);

	// "Hamisini gizlet" rejimi: ekrana toxunanda UI qayidir (handling dialoqu aciq deyilse)
	if (g_bHideAllUI && !g_bShowHandlingDlg && type == TOUCH_PUSH) {
		g_bHideAllUI = false;
		if (pUI && pUI->chat()) {
			pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz berpa edildi.");
		}
		return true;
	}

	PushTouch(type, (float)x, (float)y);
	return true;
}

#include "../settings.h"
extern CSettings* pSettings;
void UI::renderDebug()
{
	if(!pSettings->Get().iFPSCounter) return;

	char szStr[30];

	ImVec2 pos = ImVec2(pUI->ScaleX(40.0f), pUI->ScaleY(540.0f));

	static float fps = 120.f;
	static auto lastTick = CTimer::m_snTimeInMillisecondsNonClipped;
	if(CTimer::m_snTimeInMillisecondsNonClipped - lastTick > 500) {
		lastTick = CTimer::m_snTimeInMillisecondsNonClipped;
		fps = std::clamp(CTimer::game_FPS, 10.f, (float) 120);
	}
	snprintf(&szStr[0], sizeof(szStr), "FPS: %.0f", fps);

	label->setText(&szStr[0]);
	label->setPosition(pos);
}

void UI::PushToBufferedQueueTextDrawPressed(uint16_t textdrawId)
{
	BUFFERED_COMMAND_TEXTDRAW* pCmd = m_BufferedCommandTextdraws.WriteLock();

	pCmd->textdrawId = textdrawId;

	m_BufferedCommandTextdraws.WriteUnlock();
}

void UI::ProcessPushedTextdraws()
{
	BUFFERED_COMMAND_TEXTDRAW* pCmd = nullptr;
	while (pCmd = m_BufferedCommandTextdraws.ReadLock())
	{
		RakNet::BitStream bs;
		bs.Write(pCmd->textdrawId);
		pNetGame->GetRakClient()->RPC(&RPC_ClickTextDraw, &bs, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, 0);
		m_BufferedCommandTextdraws.ReadUnlock();
	}
}

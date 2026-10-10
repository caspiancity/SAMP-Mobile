#include "../main.h"
#include "../game/game.h"
#include "../net/netgame.h"
#include "../game/vehicle.h"
#include "../game/vehicle_tuning.h"
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
#include <cstdio>

extern CGame* pGame; // DUZELDILDI: pGame obyekti fayla elave edildi
extern CNetGame* pNetGame;
extern CPlayerTags* pPlayerTags;
extern UI* pUI;

extern bool g_bHideAllUI;
extern bool g_bShowHandlingDlg;

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

// ============================================================================
//  render(): kadrin sonunda ImGui toxunus vəziyyətini idarə edir
// ============================================================================
void UI::render()
{
	ImGuiWrapper::render();

    renderDebug();

    ProcessPushedTextdraws();

	ImGuiIO& io = ImGui::GetIO();

	// Basma (MouseDown) neçə kadr görünüb?
	if (io.MouseDown[0] && m_iDownFrames < 1000) m_iDownFrames++;

	// Qısa toxunuş: basma ən azı 2 kadr görünübsə, indi buraxırıq.
	if (m_bReleasePending && m_iDownFrames >= 2) {
		io.MouseDown[0] = false;
		m_bReleasePending = false;
		m_iClearMouseFrames = 2;   // ImGui buraxmanı görsün deyə mouse pos-u 1 kadr daha saxla
	}

	if (m_iClearMouseFrames > 0 && --m_iClearMouseFrames == 0) {
		io.MousePos = ImVec2(-1, -1);
	}
}

void UI::shutdown()
{
	ImGuiWrapper::shutdown();
}

// ============================================================================
//  HANDLING REDAKTORU
//
//  Tapilan problemler (kohne versiya):
//   - 500x420 sabit piksel pəncərə: ekrana gore miqyaslanmirdi, mövqe verilmirdi,
//     ImGuiCond_FirstUseEver sebebile ilk olcu yadda qalirdi -> "dar, yarisi gorunmur".
//   - Bagla X-i toxunus ucun cox kicik idi.
//   - static deyisenler yalniz ILK acilista doldurulurdu -> basqa masinda kohne deyerler.
//   - Slider araliqlari fayl vahidinde idi, oyundan oxunan deyerler ise oyun vahidinde.
//   - Qisa toxunus (basma+buraxma 1 kadrda) ImGui terefinden gorulmurdu (UI::OnTouchEvent).
// ============================================================================
static bool   s_hndRectValid = false;
static ImVec2 s_hndPos(0.0f, 0.0f);
static ImVec2 s_hndSize(0.0f, 0.0f);

bool UI_IsPointInHandlingDlg(const ImVec2& p)
{
	if (!g_bShowHandlingDlg || !s_hndRectValid) return false;
	return p.x >= s_hndPos.x && p.x <= s_hndPos.x + s_hndSize.x &&
	       p.y >= s_hndPos.y && p.y <= s_hndPos.y + s_hndSize.y;
}

void RenderHandlingDialog()
{
	static bool          s_wasOpen  = false;
	static CVehicleGTA*  s_lastVeh  = nullptr;
	static VehicleTuning s_tune;

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
		s_tune    = CVehicleTuning::Get(gtaVeh);
		s_lastVeh = gtaVeh;
		s_wasOpen = true;
	}

	// ---- olculer: ekrana ve sriftin real olcusune gore ----
	const ImVec2 disp     = ImGui::GetIO().DisplaySize;
	const float  baseFont = ImGui::GetFontSize();
	// Pencere ~28 srift-hundurluyu tutur. Ekran sigmirsa srifti kiçilt (scroll lazim olmasin).
	const float  fs = std::min(1.0f, (disp.y * 0.94f) / (baseFont * 28.0f));
	const float  f  = baseFont * fs;

	const ImVec2 winSize(std::min(disp.x * 0.70f, f * 40.0f),
	                     std::min(disp.y * 0.94f, f * 28.0f));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(f * 0.8f, f * 0.7f));
	ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);

	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
	                               ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Vehicle Handling Editor", nullptr, flags))
	{
		ImGui::SetWindowFontScale(fs);

		s_hndPos       = ImGui::GetWindowPos();
		s_hndSize      = ImGui::GetWindowSize();
		s_hndRectValid = true;

		// Barmaqla rahat idareetme ucun boyuk elementler
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(f * 0.5f, f * 0.45f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(f * 0.6f, f * 0.45f));
		ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,  f * 1.6f);
		ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, f * 1.1f);

		bool changed = false;

		ImGui::TextUnformatted("Canli Handling (100% = standart)");
		ImGui::Separator();

		auto sliderRow = [&](const char* label, const char* id, float* v, float mn, float mx)
		{
			ImGui::TextUnformatted(label);
			ImGui::PushItemWidth(-1.0f);
			if (ImGui::SliderFloat(id, v, mn, mx, "%.0f%%")) changed = true;
			ImGui::PopItemWidth();
		};

		ImGui::Columns(2, "hnd_cols", false);
		sliderRow("Maks. Suret",         "##spd", &s_tune.speedPct,  50.0f, 250.0f);
		sliderRow("Tecil",               "##acc", &s_tune.accelPct,  30.0f, 300.0f);
		sliderRow("Tormoz Gucu",         "##brk", &s_tune.brakePct,  30.0f, 300.0f);
		ImGui::NextColumn();
		sliderRow("Manevr Bucagi",       "##str", &s_tune.steerPct,  50.0f, 150.0f);
		sliderRow("Ceki / Kutle",        "##mas", &s_tune.massPct,   30.0f, 300.0f);
		ImGui::Columns(1);

		ImGui::Spacing();

		// ---- Far rengi: 16 boyuk reng duymesi (0 = standart) ----
		if (s_tune.headlight == 0) ImGui::TextUnformatted("Far Rengi: Standart");
		else                       ImGui::Text("Far Rengi: #%d", (int)s_tune.headlight);

		const ImVec2 bs(f * 2.2f, f * 2.2f);
		for (int i = 0; i < HEADLIGHT_COLOR_COUNT; ++i)
		{
			uint8_t r, g, b;
			CVehicleTuning::GetPaletteColor(i, r, g, b);

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

		ImGui::Spacing();

		const ImVec2 btn(f * 9.0f, f * 2.4f);
		if (ImGui::Button("Sifirla", btn)) {
			CVehicleTuning::Reset(gtaVeh);
			s_tune = VehicleTuning();
			changed = false;
			if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[Handling]: Standart deyerler qaytarildi.");
		}

		ImGui::SameLine(0.0f, f);
		if (ImGui::Button("Bagla", btn)) {
			g_bShowHandlingDlg = false;
		}

		// Her deyisiklik canli tetbiq olunur (original-dan hesablandigi ucun yigilmir)
		if (changed) CVehicleTuning::Set(gtaVeh, s_tune);

		ImGui::PopStyleVar(4);
	}
	ImGui::End();
	ImGui::PopStyleVar();   // WindowPadding
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

enum eTouchType
{
	TOUCH_POP = 1,
	TOUCH_PUSH = 2,
	TOUCH_MOVE = 3
};

bool UI::OnTouchEvent(int type, bool multi, int x, int y)
{
	if (g_bHideAllUI && type == TOUCH_PUSH) {
		g_bHideAllUI = false;
		if (pUI && pUI->chat()) {
			pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz berpa edildi.");
		}
		return true;
	}

	ImGuiIO& io = ImGui::GetIO();
	VoiceButton* vbutton = pUI->voicebutton();
	(void)vbutton;

	switch (type)
	{
	case TOUCH_PUSH:
		io.MousePos = ImVec2((float)x, (float)y);
		io.MouseDown[0] = true;
		m_iDownFrames = 0;
		m_bReleasePending = false;
		m_iClearMouseFrames = 0;
		break;

	case TOUCH_POP:
		io.MousePos = ImVec2((float)x, (float)y);
		if (m_iDownFrames >= 2) {
			// basma artiq kifayet qeder kadr gorunub -> indi burax
			io.MouseDown[0] = false;
			m_iClearMouseFrames = 1;
		} else {
			// qisa toxunus: buraxmani UI::render() geciktirecek
			m_bReleasePending = true;
		}
		break;

	case TOUCH_MOVE:
		io.MousePos = ImVec2((float)x, (float)y);
		break;
	}

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

#include "../main.h"
#include "../game/game.h"
#include "../net/netgame.h"
#include "../game/vehicle.h"
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
#include "game/vehicle_tuning.h"   // YENI

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

void UI::render()
{
	ImGuiIO& io = ImGui::GetIO();

	// YENI: buraxilis (POP) en azi 1 kadr basili gorunduyunden SONRA tetbiq olunur.
	// Evvel: tez toxunma (PUSH+POP iki kadr arasinda) ImGui terefinden tamamile itirilirdi.
	if (m_bPendingRelease && m_nDownFrames >= 1) {
		io.MouseDown[0] = false;
		m_bPendingRelease = false;
		m_nDownFrames = 0;
		m_bNeedClearMousePos = true; // pozisiya buraxilma kadrindan SONRA silinir (asagida)
	}

	ImGuiWrapper::render();

	if (io.MouseDown[0]) m_nDownFrames++;

    renderDebug();

    ProcessPushedTextdraws();

	if (m_bNeedClearMousePos) {
		io.MousePos = ImVec2(-1, -1);
		m_bNeedClearMousePos = false;
	}
}

void UI::shutdown()
{
	ImGuiWrapper::shutdown();
}

// ===========================================================================
//  Handling / far rengi redaktoru
// ===========================================================================

static bool   s_bDlgRectValid = false;
static ImVec2 s_dlgMin(0.0f, 0.0f);
static ImVec2 s_dlgMax(0.0f, 0.0f);

// Dialoqun altindaki vidjetler (chat, buttonpanel...) toxunmanı almasin
static bool IsPointInHandlingDialog(float x, float y)
{
	return s_bDlgRectValid && x >= s_dlgMin.x && x <= s_dlgMax.x && y >= s_dlgMin.y && y <= s_dlgMax.y;
}

namespace
{
	struct HeadlightPreset { const char* name; int r, g, b; };

	const HeadlightPreset kHeadlightPresets[] = {
		{ "Standart",  90,  90,  90 },   // 0-ci = override yoxdur
		{ "Ag",       255, 255, 255 },
		{ "Qirmizi",  255,  30,  30 },
		{ "Narinci",  255, 130,   0 },
		{ "Sari",     255, 220,   0 },
		{ "Yasil",     40, 255,  40 },
		{ "Firuze",     0, 255, 220 },
		{ "Mavi",      40,  90, 255 },
		{ "Benovse",  150,  60, 255 },
		{ "Cehrayi",  255,  80, 200 },
		{ "Lime",     170, 255,   0 },
		{ "Buz",      170, 220, 255 },
	};
	constexpr int kPresetCount = (int)(sizeof(kHeadlightPresets) / sizeof(kHeadlightPresets[0]));

	struct HandlingDlgState
	{
		CVehicleGTA*          veh = nullptr;
		VehicleTuning::Params p;
		int                   rgb[3] = { 255, 255, 255 };
	};
	HandlingDlgState s_st;
}

// [-] [=====slider=====] [+]  - barmaqla rahat istifade ucun
static void TuningRow(const char* id, const char* label, float& v, float mn, float mx, bool& changed)
{
	ImGui::TextUnformatted(label);
	ImGui::PushID(id);

	const float btn = ImGui::GetFrameHeight() * 1.5f;
	const float spc = ImGui::GetStyle().ItemSpacing.x;
	float sw = ImGui::GetContentRegionAvail().x - 2.0f * btn - 2.0f * spc;
	if (sw < 60.0f) sw = 60.0f;

	if (ImGui::Button("-", ImVec2(btn, 0.0f))) { v = std::max(mn, v - 5.0f); changed = true; }
	ImGui::SameLine();
	ImGui::PushItemWidth(sw);
	if (ImGui::SliderFloat("##s", &v, mn, mx, "%.0f%%")) changed = true;
	ImGui::PopItemWidth();
	ImGui::SameLine();
	if (ImGui::Button("+", ImVec2(btn, 0.0f))) { v = std::min(mx, v + 5.0f); changed = true; }

	ImGui::PopID();
}

void RenderHandlingDialog()
{
	if (!g_bShowHandlingDlg) {
		s_bDlgRectValid = false;
		s_st.veh = nullptr;
		return;
	}

	CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
		g_bShowHandlingDlg = false;
		s_bDlgRectValid = false;
		return;
	}

	CVehicle* pVeh = nullptr;
	if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
		CVehiclePool* pPool = pNetGame->GetVehiclePool();
		VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
		if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
	}

	if (!pVeh || !pVeh->m_pVehicle) {
		s_bDlgRectValid = false;
		return;
	}
	CVehicleGTA* gtaVeh = pVeh->m_pVehicle;

	const ImGuiIO& io = ImGui::GetIO();
	const float W = io.DisplaySize.x;
	const float H = io.DisplaySize.y;

	// dialoq acilanda ve ya masin deyisende slayderleri yeniden yukle
	// (kohne kodda "static float" yalniz ilk masinin deyerini saxlayirdi)
	if (s_st.veh != gtaVeh) {
		s_st.veh = gtaVeh;
		s_st.p = VehicleTuning::Get(gtaVeh);
		s_st.rgb[0] = s_st.p.r;
		s_st.rgb[1] = s_st.p.g;
		s_st.rgb[2] = s_st.p.b;
	}

	// olculer ekrana gore (kohne kod: sabit 500x420 piksel, UI ise 1920x1080-e gore miqyaslanir)
	float winW = W * 0.46f;
	if (winW < 560.0f) winW = std::min(560.0f, W * 0.96f);
	const float winH = H * 0.94f;
	const float pad  = H * 0.014f;

	float fontScale = (H * 0.034f) / ImGui::GetFontSize();
	fontScale = std::max(0.45f, std::min(1.0f, fontScale));

	// ekranin ORTASINDA: sol (sukan) ve sag (qaz/tormoz) oyun duymeleri azad qalir
	ImGui::SetNextWindowPos(ImVec2((W - winW) * 0.5f, (H - winH) * 0.5f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.93f);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(pad * 0.8f, pad * 0.7f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(pad * 0.8f, pad * 0.7f));
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, H * 0.045f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,   H * 0.050f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, pad * 0.8f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  pad * 0.5f);

	bool changed = false;
	bool doReset = false;
	bool doClose = false;

	// NoSavedSettings: imgui.ini kohne olcu/movqeni yadda saxlamasin
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
	                               ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##HandlingEditor", nullptr, flags))
	{
		ImGui::SetWindowFontScale(fontScale);

		const ImVec2 wp = ImGui::GetWindowPos();
		const ImVec2 ws = ImGui::GetWindowSize();
		s_dlgMin = wp;
		s_dlgMax = ImVec2(wp.x + ws.x, wp.y + ws.y);
		s_bDlgRectValid = true;

		// ---- yuxari hisse: HEMISE gorunur (scroll olsa da) ----
		ImGui::TextUnformatted("HANDLING / FAR EDITORU");

		const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		const float bh   = ImGui::GetFrameHeight() * 1.4f;
		if (ImGui::Button("Sifirla", ImVec2(half, bh))) doReset = true;
		ImGui::SameLine();
		if (ImGui::Button("Bagla", ImVec2(half, bh))) doClose = true;
		ImGui::Separator();

		// ---- scroll olunan hisse ----
		ImGui::BeginChild("##tune_body", ImVec2(0.0f, 0.0f), false, 0);
		{
			TuningRow("spd", "Maks suret",       s_st.p.speed,  25.0f, 300.0f, changed);
			TuningRow("acc", "Tecil",            s_st.p.accel,  25.0f, 500.0f, changed);
			TuningRow("brk", "Tormoz",           s_st.p.brake,  25.0f, 400.0f, changed);
			TuningRow("str", "Manevr (sukan)",   s_st.p.steer,  50.0f, 150.0f, changed);
			TuningRow("mas", "Kutle",            s_st.p.mass,   25.0f, 400.0f, changed);

			ImGui::Separator();
			ImGui::TextUnformatted(s_st.p.hl ? "Far rengi: OZEL (aktiv)" : "Far rengi: standart");

			const int   cols = 4;
			const float cw   = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (cols - 1)) / cols;
			const float ch   = ImGui::GetFrameHeight() * 1.3f;

			for (int i = 0; i < kPresetCount; ++i)
			{
				const HeadlightPreset& pr = kHeadlightPresets[i];
				if (i % cols) ImGui::SameLine();

				const ImVec4 col(pr.r / 255.0f, pr.g / 255.0f, pr.b / 255.0f, 1.0f);
				const bool dark = (0.299f * pr.r + 0.587f * pr.g + 0.114f * pr.b) < 140.0f;

				ImGui::PushStyleColor(ImGuiCol_Button,        col);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col);
				ImGui::PushStyleColor(ImGuiCol_Text, dark ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1));
				ImGui::PushID(i);

				if (ImGui::Button(pr.name, ImVec2(cw, ch)))
				{
					if (i == 0) {
						s_st.p.hl = false;
					} else {
						s_st.p.hl = true;
						s_st.rgb[0] = pr.r;
						s_st.rgb[1] = pr.g;
						s_st.rgb[2] = pr.b;
					}
					changed = true;
				}

				ImGui::PopID();
				ImGui::PopStyleColor(4);
			}

			ImGui::PushItemWidth(-1.0f);
			if (ImGui::SliderInt("##r", &s_st.rgb[0], 0, 255, "R: %d")) { s_st.p.hl = true; changed = true; }
			if (ImGui::SliderInt("##g", &s_st.rgb[1], 0, 255, "G: %d")) { s_st.p.hl = true; changed = true; }
			if (ImGui::SliderInt("##b", &s_st.rgb[2], 0, 255, "B: %d")) { s_st.p.hl = true; changed = true; }
			ImGui::PopItemWidth();

			ImGui::Separator();
			ImGui::TextWrapped("100%% = orijinal. Deyisiklikler avtomatik tetbiq olunur, yalniz sizin ekraninizda gorunur (client-side).");
		}
		ImGui::EndChild();
	}
	else
	{
		s_bDlgRectValid = false;
	}
	ImGui::End();
	ImGui::PopStyleVar(7);

	// ---- neticeleri tetbiq et ----
	if (doReset)
	{
		VehicleTuning::Reset(gtaVeh);
		s_st.p = VehicleTuning::Params{};
		s_st.rgb[0] = s_st.rgb[1] = s_st.rgb[2] = 255;
		if (pUI && pUI->chat())
			pUI->chat()->addDebugMessage("{00FF00}[Handling]: Orijinala qaytarildi.");
	}
	else if (changed)
	{
		s_st.p.r = (uint8_t)s_st.rgb[0];
		s_st.p.g = (uint8_t)s_st.rgb[1];
		s_st.p.b = (uint8_t)s_st.rgb[2];
		VehicleTuning::Set(gtaVeh, s_st.p);
	}

	if (doClose)
	{
		g_bShowHandlingDlg = false;
		s_bDlgRectValid = false;
	}
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
	// YENI: handling dialoqunun uzerindeki toxunma alttaki vidjetlere kecmesin
	if (IsPointInHandlingDialog(pos.x, pos.y))
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
	switch (type)
	{
	case TOUCH_PUSH:
		io.MousePos = ImVec2((float)x, (float)y);
		io.MouseDown[0] = true;
		m_bPendingRelease = false;
		m_nDownFrames = 0;
		m_bNeedClearMousePos = false;
		break;

	case TOUCH_POP:
		// birbasa buraxma: render() basmani ən azi 1 kadr gorenden sonra buraxir
		m_bPendingRelease = true;
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
        // YENI: pNetGame null olanda (serverden ayrilma) crash olmasin; ReadUnlock hemise cagirilir
        if (pNetGame && pNetGame->GetRakClient())
        {
            RakNet::BitStream bs;
            bs.Write(pCmd->textdrawId);
            pNetGame->GetRakClient()->RPC(&RPC_ClickTextDraw, &bs, HIGH_PRIORITY, RELIABLE_SEQUENCED, 0, false, UNASSIGNED_NETWORK_ID, 0);
        }
        m_BufferedCommandTextdraws.ReadUnlock();
    }
}

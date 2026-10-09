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
	ImGuiWrapper::render();

    renderDebug();

    ProcessPushedTextdraws();

	if (m_bNeedClearMousePos) {
		ImGuiIO& io = ImGui::GetIO();
		io.MousePos = ImVec2(-1, -1);
		m_bNeedClearMousePos = false;
	}
}

void UI::shutdown()
{
	ImGuiWrapper::shutdown();
}

void RenderHandlingDialog()
{
	if (!g_bShowHandlingDlg) return;

	CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
	if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
		g_bShowHandlingDlg = false;
		return;
	}

	CVehicle* pVeh = nullptr;
	if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
		CVehiclePool* pPool = pNetGame->GetVehiclePool();
		VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
		if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
	}

	if (!pVeh) return;

	ImGui::SetNextWindowSize(ImVec2(500, 420), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Vehicle Handling Editor", &g_bShowHandlingDlg))
	{
		static float fSpeed = pVeh->GetMaxSpeed();
		static float fAccel = pVeh->GetAcceleration();
		static float fBrake = pVeh->GetBrakePower();
		static float fSteer = pVeh->GetSteeringAngle();
		static float fMass = pVeh->GetMass();
		static int iFarColor = 0;

		ImGui::Text("Canli Handling Tenzimlemeleri");
		ImGui::Separator();

		ImGui::SliderFloat("Maks Suret (Velocity)", &fSpeed, 50.0f, 500.0f);
		ImGui::SliderFloat("Tecil (Acceleration)", &fAccel, 1.0f, 100.0f);
		ImGui::SliderFloat("Tormoz Gucu (Brake)", &fBrake, 1.0f, 50.0f);
		ImGui::SliderFloat("Manevr Bucagi (Steering)", &fSteer, 10.0f, 90.0f);
		ImGui::SliderFloat("Ceki/Kutle (Mass)", &fMass, 500.0f, 10000.0f);
		ImGui::SliderInt("Far Rengi (Headlight)", &iFarColor, 0, 15);

		ImGui::Spacing();
		if (ImGui::Button("Tetbiq Et", ImVec2(120, 40))) {
			pVeh->SetMaxSpeed(fSpeed);
			pVeh->SetAcceleration(fAccel);
			pVeh->SetBrakePower(fBrake);
			pVeh->SetSteeringAngle(fSteer);
			pVeh->SetMass(fMass);
			pVeh->SetHeadlightColor((uint8_t)iFarColor);
			pUI->chat()->addDebugMessage("{00FF00}[Handling]: Parametrler tetbiq edildi!");
		}

		ImGui::SameLine();
		if (ImGui::Button("Bagla", ImVec2(120, 40))) {
			g_bShowHandlingDlg = false;
		}
	}
	ImGui::End();
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
	switch (type)
	{
	case TOUCH_PUSH:
		io.MousePos = ImVec2(x, y);
		io.MouseDown[0] = true;
		break;

	case TOUCH_POP:
		io.MouseDown[0] = false;
		m_bNeedClearMousePos = true;
		break;

	case TOUCH_MOVE:
		io.MousePos = ImVec2(x, y);
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

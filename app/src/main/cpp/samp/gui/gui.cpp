#include "../main.h"
#include "../game/game.h"
#include "../net/netgame.h"
#include "gui.h"
#include "../playertags.h"
#include "../net/playerbubblepool.h"
#include "../CDebugInfo.h"
// voice
#include "../voice/Plugin.h"
#include "../voice/MicroIcon.h"
#include "../voice/SpeakerList.h"
#include "../voice/Network.h"

#include "../gui/samp_widgets/voicebutton.h"
#include "game/Textures/TextureDatabaseRuntime.h"
#include "game/Streaming.h"
#include "game/Pools.h"
#include "../java/jniutil.h"
#include "obfuscate/str_obfuscator.hpp"
#include "../settings.h"

#include <cmath>

extern CNetGame* pNetGame;
extern CPlayerTags* pPlayerTags;
extern UI* pUI;
extern CJavaWrapper* pJavaWrapper;
extern CGame *pGame;
extern CSettings* pSettings;

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

    d_label1 = new Label(cryptor::create("SA:MP 2.11 (arm64-v8a)").decrypt(), ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 3);
    this->addChild(d_label1);
    d_label1->setPosition(ImVec2(3.0, 3.0));

    m_debugInfo = new CDebugInfo();

    return true;
}

void UI::render()
{
    ImGuiWrapper::render();

    renderDebug();

    renderSpeedometer();
    renderCKDialog();

    ProcessPushedTextdraws();

    if (m_bNeedClearMousePos) {
        ImGuiIO& io = ImGui::GetIO();
        io.MousePos = ImVec2(-1, -1);
        m_bNeedClearMousePos = false;
    }
}

void UI::shutdown()
{
    if (m_debugInfo)
    {
        m_debugInfo->Shutdown(this);
        delete m_debugInfo;
        m_debugInfo = nullptr;
    }

    ImGuiWrapper::shutdown();
}

void UI::drawList()
{
    if (!visible()) return;

    if (pPlayerTags) pPlayerTags->Render(renderer());

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

void UI::renderDebug()
{
    if (m_debugInfo)
    {
        m_debugInfo->Render(this);
    }
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

void UI::renderSpeedometer()
{
    if (!pNetGame || !pNetGame->GetPlayerPool() || !pNetGame->GetPlayerPool()->GetLocalPlayer()) return;

    CPlayerPed* pPed = pNetGame->GetPlayerPool()->GetLocalPlayer()->GetPlayerPed();
    if (!pPed || !pPed->IsInVehicle()) return;

    VEHICLE_TYPE* pVeh = pPed->GetGtaVehicle();
    if (!pVeh) return;

    CVector vecMoveSpeed;
    pPed->GetStuffFromVehicle(&vecMoveSpeed);
    float fSpeed = sqrtf(vecMoveSpeed.x * vecMoveSpeed.x + vecMoveSpeed.y * vecMoveSpeed.y + vecMoveSpeed.z * vecMoveSpeed.z) * 180.0f;

    float fHealth = pPed->GetVehicleHealth();
    int healthPercent = (int)(fHealth / 10.0f);
    if (healthPercent > 100) healthPercent = 100;
    if (healthPercent < 0) healthPercent = 0;

    int fuelPercent = 85;

    ImGui::SetNextWindowPos(ImVec2(displaySize().x - ScaleX(320), displaySize().y - ScaleY(180)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ScaleX(300), ScaleY(150)), ImGuiCond_Always);

    ImGui::Begin("Speedometer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);

    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "SPEED: %.0f KM/H", fSpeed);

    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "HP: %d%%", healthPercent);
    ImGui::ProgressBar(healthPercent / 100.0f, ImVec2(ScaleX(280), ScaleY(15)), "");

    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.1f, 1.0f), "FUEL: %d%%", fuelPercent);
    ImGui::ProgressBar(fuelPercent / 100.0f, ImVec2(ScaleX(280), ScaleY(15)), "");

    ImGui::End();
}

void UI::renderCKDialog()
{
    if (!m_bCKDialogVisible) return;

    ImGui::SetNextWindowPos(ImVec2(displaySize().x * 0.5f - ScaleX(250), displaySize().y * 0.5f - ScaleY(200)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(ScaleX(500), ScaleY(400)), ImGuiCond_FirstUseEver);

    ImGui::Begin("Client Komandaları Siyahısı (/ck)", &m_bCKDialogVisible, ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "Mövcud Client Əmrləri:");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BulletText("/ck - Bu komanda siyahısı pəncərəsini açır");
    ImGui::BulletText("/headlight [r] [g] [b] - Faraların rəngini dəyişir");
    ImGui::BulletText("/handling [speed/accel/brake] [dəyər] - Avtomobil parametri dəyişir");
    ImGui::BulletText("/fpscam - Birinci şəxs (FPS) kamerasını açır/bağlayır");

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Bağla", ImVec2(ScaleX(100), ScaleY(35)))) {
        m_bCKDialogVisible = false;
    }

    ImGui::End();
}

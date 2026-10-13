#include "../gui.h"
#include "../../main.h"
#include "../../game/game.h"
#include "../../net/netgame.h"
#include <algorithm>
#include <sstream>
#include "../settings.h"
#include "java/jniutil.h"
#include "../../game/VehControls.h"

extern UI* pUI;
extern CGame* pGame;
extern CNetGame* pNetGame;
extern CSettings* pSettings;
extern CJavaWrapper *pJavaWrapper;

bool g_bHideAllUI = false;
bool g_bShowHandlingDlg = false;

Chat::Chat()
        : ListBox()
{

}

void Chat::addChatMessage(const std::string& message, const std::string& nick, const ImColor& nick_color)
{
        addPlayerMessage(message, nick, nick_color);
}

void Chat::addInfoMessage(const std::string& format, ...)
{
        char tmp_buf[512];

        va_list args;
        va_start(args, format);
        vsprintf(tmp_buf, format.c_str(), args);
        va_end(args);

        addMessage(std::string(tmp_buf), ImColor(0x00, 0xc8, 0xc8));
}

void Chat::addDebugMessage(const std::string& format, ...)
{
        char tmp_buf[512];

        va_list args;
        va_start(args, format);
        vsprintf(tmp_buf, format.c_str(), args);
        va_end(args);

        addMessage(std::string(tmp_buf), ImColor(0xbe, 0xbe, 0xbe));
}

void Chat::addClientMessage(const std::string& message, const ImColor& color)
{
        addMessage(message, color);
}

void Chat::addMessage(const std::string& message, const ImColor& color)
{
        if (this->itemsCount() > UISettings::chatMaxMessages())
        {
                this->removeItem(0);
        }

        MessageItem* item = new MessageItem(message, color);
        this->addItem(item);
        /*if(!active())*/ this->setScrollY(1.0f);
}

void Chat::addPlayerMessage(const std::string& message, const std::string& nick, const ImColor& nick_color)
{
        if (this->itemsCount() > UISettings::chatMaxMessages())
        {
                this->removeItem(0);
        }

        PlayerMessageItem* item = new PlayerMessageItem(message, nick, nick_color);
        this->addItem(item);
        /*if(!active())*/ this->setScrollY(1.0f);
}

void Chat::draw(ImGuiRenderer* renderer)
{
        ListBox::draw(renderer);
}

void Chat::activateEvent(bool active)
{
        if (active)
        {
                this->setScrollable(true);
        }
        else
        {
                this->setScrollable(false);
        }
}

void Chat::touchPopEvent()
{
        if (pUI->playertablist()->visible()) return;

        pUI->keyboard()->show(this);
}

bool ProcessClientCommands(const std::string& input)
{
        if (input.empty() || input[0] != '/') return false;

        std::stringstream ss(input);
        std::string cmd;
        ss >> cmd;

        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

        if (cmd == "/yt") {
                std::string rest;
                std::getline(ss, rest);
                size_t p = rest.find_first_not_of(" \t");
                rest = (p == std::string::npos) ? "" : rest.substr(p);
                if (pJavaWrapper) pJavaWrapper->YtCommand(rest.c_str());
                return true;
        }

        if (cmd == "/gizle") {
                g_bHideAllUI = !g_bHideAllUI;
                if (!g_bHideAllUI) {
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz aktiv edildi.");
                }
                return true;
        }

        if (cmd == "/tuning") {
                CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Xeta: Avtomobilde deyilsiniz!");
                        return true;
                }
                g_bShowHandlingDlg = !g_bShowHandlingDlg;
                return true;
        }

        if (cmd == "/sinyal")
        {
                CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Xeta: Avtomobilde deyilsiniz!");
                        return true;
                }

                CVehicle* pVeh = nullptr;
                if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
                        CVehiclePool* pPool = pNetGame->GetVehiclePool();
                        VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
                        if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
                }
                if (!pVeh || !pVeh->m_pVehicle) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Avtomobil tapilmadi!");
                        return true;
                }

                std::string arg;
                ss >> arg;
                std::transform(arg.begin(), arg.end(), arg.begin(), ::tolower);

                int mode = -1;
                if (arg == "sol")                          mode = VEH_SIGNAL_LEFT;
                else if (arg == "sag")                     mode = VEH_SIGNAL_RIGHT;
                else if (arg == "avariya")                 mode = VEH_SIGNAL_HAZARD;
                else if (arg == "off" || arg == "0")       mode = VEH_SIGNAL_OFF;

                if (mode < 0) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /sinyal [sol|sag|avariya|off]");
                        return true;
                }

                VehControls::SetSignal(pVeh->m_pVehicle, (uint8_t)mode);
                pUI->chat()->addDebugMessage("{00FF00}[Client]: Sinyal: %s", arg.c_str());
                return true;
        }

        if (cmd == "/far" || cmd == "/suret" || cmd == "/manevr" || cmd == "/tormoz")
        {
                CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Xeta: Avtomobilde deyilsiniz!");
                        return true;
                }

                CVehicle* pVeh = nullptr;
                if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
                        CVehiclePool* pPool = pNetGame->GetVehiclePool();
                        VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
                        if (vehID != INVALID_VEHICLE_ID) {
                                pVeh = pPool->GetAt(vehID);
                        }
                }

                if (!pVeh) {
                        pUI->chat()->addDebugMessage("{FF0000}[Client]: Avtomobil tapilmadi!");
                        return true;
                }

                float val = 0.0f;
                int intVal = 0;

                if (cmd == "/far") {
                        if (!(ss >> intVal)) {
                                pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /far [0-15]");
                                return true;
                        }
                        pVeh->SetHeadlightColor((uint8_t)intVal);
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Far rengi deyisdirildi: %d", intVal);
                }
                else if (cmd == "/suret") {
                        if (!(ss >> val)) {
                                pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /suret [maks_suret]");
                                return true;
                        }
                        pVeh->SetMaxSpeed(val);
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Maksimum suret deyisdirildi: %.1f", val);
                }
                else if (cmd == "/manevr") {
                        if (!(ss >> val)) {
                                pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /manevr [donme_bucagi]");
                                return true;
                        }
                        pVeh->SetSteeringAngle(val);
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Manevr bucagi deyisdirildi: %.1f", val);
                }
                else if (cmd == "/tormoz") {
                        if (!(ss >> val)) {
                                pUI->chat()->addDebugMessage("{FF0000}[Client]: Istifade: /tormoz [guc]");
                                return true;
                        }
                        pVeh->SetBrakePower(val);
                        pUI->chat()->addDebugMessage("{00FF00}[Client]: Tormoz gucu deyisdirildi: %.1f", val);
                }

                return true;
        }

        return false;
}

void Chat::keyboardEvent(const std::string& input)
{
        if (input.length() > 0 && pNetGame)
        {
                if (ProcessClientCommands(input)) return;

                if (input[0] == '/') pNetGame->SendChatCommand(input.c_str());
                else pNetGame->SendChatMessage(input.c_str());
        }
}

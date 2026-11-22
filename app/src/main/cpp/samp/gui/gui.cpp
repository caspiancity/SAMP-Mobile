#include "../main.h"
#include "../game/game.h"
#include "../net/netgame.h"
#include "../game/vehicle.h"
#include "../game/VehTuner.h"
#include "../game/VehControls.h"
#include "../game/AzPlate.h"
#include "../game/Coronas.h"
#include "../game/Camera.h"
#include "../game/scripting.h"
#include <string>
#include <cstdlib>
#include <cstring>
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
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>

extern CGame* pGame;
extern CNetGame* pNetGame;
extern CPlayerTags* pPlayerTags;
extern UI* pUI;

// ============================================================================
//  AZ_PATCH4: /cam  --  PC-stili kamera (mouse-look-un mobil versiyasi) + birinci shexs
//   /cam            : off -> pc -> fps -> off  (novbeli rejim)
//   /cam pc|fps|off : birbasa secim
//   /cam sens N     : hessaslig (1..20, standart 5)
//  Ekranin SAG terefinde (x>35%, y<72%) barmaqla surusdurende kamera donur.
//  Kamera SA-MP-nin oz script emrleri ile (set_camera_position + point_camera) her kadr yerlesdirilir.
// ============================================================================
// AZ_CAMX
bool CamX_Command(const char* a, const char* b, char* rep, int n);
int   CamX_GetMode();
void  CamX_Cycle();
bool  CamX_IsDrone();
void  CamX_ToggleDrone();
float CamX_GetFov();
void  CamX_SetFov(float v);

namespace CamCtl
{
        static int   s_mode   = 0;        // 0 = oyunun kamerasi, 1 = PC (3-cu shexs), 2 = birinci shexs
        static bool  s_active = false;
        static float s_yaw    = 0.0f;
        static float s_pitch  = -0.25f;
        static float s_sens   = 0.0045f;  // radian / piksel
        static int   s_ptr    = -1;
        static float s_lx = 0.0f, s_ly = 0.0f;
        static float s_dist   = 4.5f;

        static void ClampPitch()
        {
                const float lo = (s_mode == 2) ? -1.4f : -1.3f;
                const float hi = (s_mode == 2) ?  1.4f :  0.12f;   // 3-cu shexsde yuxari baxanda kamera yerin altina dusmesin
                if (s_pitch < lo) s_pitch = lo;
                if (s_pitch > hi) s_pitch = hi;
        }
}

void CamCtl_OnTouch(int type, int num, int x, int y)
{
        using namespace CamCtl;
        if (s_mode == 0) { s_ptr = -1; return; }

        const ImVec2 d = ImGui::GetIO().DisplaySize;
        const bool inZone = (float)x > d.x * 0.35f && (float)y < d.y * 0.72f;

        if (type == 2) {                         // push
                if (s_ptr < 0 && inZone) { s_ptr = num; s_lx = (float)x; s_ly = (float)y; }
        }
        else if (type == 3) {                    // move
                if (s_ptr < 0 && inZone) { s_ptr = num; s_lx = (float)x; s_ly = (float)y; return; }
                if (num == s_ptr) {
                        const float dx = (float)x - s_lx, dy = (float)y - s_ly;
                        s_lx = (float)x; s_ly = (float)y;
                        s_yaw   -= dx * s_sens;   // sağa surusdur -> sağa bax
                        s_pitch -= dy * s_sens;   // asagi surusdur -> asagi bax (mouse kimi)
                        ClampPitch();
                }
        }
        else if (type == 1) {                    // pop
                if (num == s_ptr) s_ptr = -1;
        }
}

void CamCtl_Update()
{
        using namespace CamCtl;

        if (s_mode == 0) {
                if (s_active) { s_active = false; s_ptr = -1; CCamera::SetBehindPlayer(); }
                return;
        }
        if (!pGame || !pNetGame || pGame->IsGamePaused()) return;

        CPlayerPed* ped = pGame->FindPlayerPed();
        if (!ped || !ped->m_pPed) return;

        RwMatrix m = ped->m_pPed->GetMatrix().ToRwMatrix();
        if (!s_active) {
                s_active = true;
                s_yaw = atan2f(m.up.y, m.up.x);               // oyuncunun baxdigi istiqamet
                s_pitch = (s_mode == 2) ? 0.0f : -0.25f;
        }
        ClampPitch();

        const float cp = cosf(s_pitch), sp = sinf(s_pitch);
        const float dx = cosf(s_yaw) * cp, dy = sinf(s_yaw) * cp, dz = sp;
        const float px = m.pos.x, py = m.pos.y, pz = m.pos.z;

        float cx, cy, cz, lx, ly, lz;
        if (s_mode == 2) {                                    // birinci shexs
                cx = px + cosf(s_yaw) * 0.15f;
                cy = py + sinf(s_yaw) * 0.15f;
                cz = pz + 0.68f;
                lx = cx + dx * 20.0f; ly = cy + dy * 20.0f; lz = cz + dz * 20.0f;
        } else {                                              // PC / 3-cu shexs
                const float dist = ped->IsInVehicle() ? 7.0f : s_dist;
                const float tz = pz + 0.8f;
                cx = px - dx * dist; cy = py - dy * dist; cz = tz - dz * dist;
                lx = px; ly = py; lz = tz;
        }

        ScriptCommand(&set_camera_position, cx, cy, cz, 0.0f, 0.0f, 0.0f);
        ScriptCommand(&point_camera, lx, ly, lz, 2);
}

// chat.cpp -> /cam
void CamCtl_Command(const char* a, const char* b, char* rep, int n)
{
        if (CamX_Command(a, b, rep, n)) return;   // AZ_CAMX
        using namespace CamCtl;
        std::string s = a ? a : "";
        for (size_t i = 0; i < s.size(); ++i) s[i] = (char)tolower((unsigned char)s[i]);

        if (s.empty())            s_mode = (s_mode + 1) % 3;
        else if (s == "pc")       s_mode = 1;
        else if (s == "fps" || s == "1") s_mode = 2;
        else if (s == "off")      s_mode = 0;
        else if (s == "sens") {
                const float v = b ? (float)atof(b) : 0.0f;
                if (v >= 0.5f && v <= 20.0f) { s_sens = v * 0.0009f; snprintf(rep, n, "Hessaslig: %.1f", v); }
                else snprintf(rep, n, "Istifade: /cam sens 1..20");
                return;
        }
        else { snprintf(rep, n, "Istifade: /cam [pc|fps|off|sens N]"); return; }

        s_active = false;   // novbeti kadrda yeniden baslat
        ClampPitch();
        const char* names[3] = { "oyunun kamerasi (off)", "PC rejimi (sag terefde surusdur)", "birinci shexs" };
        snprintf(rep, n, "Kamera: %s", names[s_mode]);
}


extern bool g_bHideAllUI;
extern bool g_bShowHandlingDlg;   // /mm (kohne addi: /tuning) ile acilir/baglanir

// Diaqnostika: 1 = toxunus noqtesi (qirmizi halqa) + loglar. Her sey islese 0 et.
#define HND_DEBUG 0
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
//  TOXUNUS: hadiseler queue-ya yigilir, her kadrin evvelinde oxunur.
//  Bir kadrda en cox BIR basma/buraxma emal olunur (qisa toxunus itmesin).
//  Mod menyu ImGui-nin input sistemini istifade ETMIR, oz veziyyetini saxlayir.
// ============================================================================
struct MMTouch
{
        float    x = -1.0f, y = -1.0f;
        bool     down = false, pressed = false, released = false;
        unsigned events = 0;
};
static MMTouch g_mmTouch;

namespace
{
        struct TouchEv { int type; float x; float y; };

        std::mutex          s_touchMtx;
        std::deque<TouchEv> s_touchQ;

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

        void ProcessTouchQueue()
        {
                std::lock_guard<std::mutex> lk(s_touchMtx);

                g_mmTouch.pressed  = false;
                g_mmTouch.released = false;

                while (!s_touchQ.empty())
                {
                        const TouchEv e = s_touchQ.front();
                        s_touchQ.pop_front();

                        g_mmTouch.x = e.x;
                        g_mmTouch.y = e.y;
                        g_mmTouch.events++;

                        if (e.type == TOUCH_PUSH) { g_mmTouch.down = true;  g_mmTouch.pressed  = true; break; }
                        if (e.type == TOUCH_POP)  { g_mmTouch.down = false; g_mmTouch.released = true; break; }
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
        d_label1 = new Label(cryptor::create("Apache Online - Beta").decrypt(), ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 3);
        this->addChild(d_label1);
        d_label1->setPosition(ImVec2(3.0, 3.0));

        return true;
}

void UI::render()
{
        ProcessTouchQueue();     // toxunuslari bu kadr ucun oxu

        AzPlate::ProcessPending();
        /* AZ_CAMX: eski /cam sondurulub */            // AZ_PATCH4: /cam   // nomre teksturalari (ImGui kadrindan evvel)

        ImGuiWrapper::render();

        renderDebug();

        ProcessPushedTextdraws();
}

void UI::shutdown()
{
        ImGuiWrapper::shutdown();
}

// ============================================================================
//  /mm MENYUSU  (kohne adi /tuning) — oz cizgisi, oz toxunusu, animasiyali
// ============================================================================
static bool   s_hndRectValid = false;
static ImVec2 s_hndPos(0.0f, 0.0f);    // toxunus bloklamasi ucun panelin real sahesi
static ImVec2 s_hndSize(0.0f, 0.0f);

bool UI_IsPointInHandlingDlg(const ImVec2& p)
{
        if (!g_bShowHandlingDlg || !s_hndRectValid) return false;
        return p.x >= s_hndPos.x && p.x <= s_hndPos.x + s_hndSize.x &&
               p.y >= s_hndPos.y && p.y <= s_hndPos.y + s_hndSize.y;
}

namespace mm
{
        struct Rc { float x, y, w, h; };

        static ImDrawList* s_dl = nullptr;
        static float s_U = 1.0f;       // olcu vahidi (piksel)
        static float s_A = 1.0f;       // umumi seffaflig
        static int   s_act = 0;        // hazirda basili olan element
        static float s_prs[96] = {};   // basma animasiyalari
        static float s_frac[8] = {};   // slider doldurma animasiyasi
        static float s_sw[3] = {};     // acar animasiyalari
        static float s_blink = 0.0f;   // sinyal yanib-sonme animasiyasi

        enum { ID_DRAG = 1, ID_CLOSE = 2, ID_ZOOMP = 3, ID_ZOOMM = 4, ID_RESET = 5, ID_NAV0 = 6,
               ID_SLIDER = 16, ID_SWATCH = 40, ID_TOGGLE = 70, ID_SIG = 76 };

#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 18200
        static const int kAllCorners = 0;
#else
        static const int kAllCorners = ImDrawCornerFlags_All;
#endif

        static inline float S(float v) { return v * s_U; }
        static inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
        static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        static inline float Smooth(float cur, float target, float dt, float speed) { return Lerp(cur, target, 1.0f - expf(-dt * speed)); }
        static inline float EaseOutCubic(float t) { const float p = 1.0f - t; return 1.0f - p * p * p; }
        static inline bool  In(const Rc& r, float px, float py) { return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h; }
        static inline Rc    Shrink(const Rc& r, float a) { return Rc{ r.x + a, r.y + a, r.w - 2.0f * a, r.h - 2.0f * a }; }

        static inline ImU32 Col(int r, int g, int b, int a = 255)
        {   // MM_V8: ag-qara-boz tema: az doymus (boz-mavi) tonlar saf boza cevrilir; real rengler (qirmizi, yasil...) qalir
                const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
                if (mx - mn <= 40) { const int l = (r * 30 + g * 59 + b * 11) / 100; r = g = b = l; }
                return IM_COL32(r, g, b, (int)((float)a * s_A));
        }
        // deqiq reng (swatch / onizleme) - mono suzgecsiz
        static inline ImU32 ColT(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, (int)((float)a * s_A)); }
        static inline ImU32 Accent(float /*istifade olunmur*/, int a = 255) { return Col(128, 128, 128, a); }
        static inline ImU32 Orange(int a = 255) { return Col(255, 153, 28, a); }

        static void Fill(const Rc& r, ImU32 c, float rad) { s_dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), c, rad); }
        static void Stroke(const Rc& r, ImU32 c, float rad, float th) { s_dl->AddRect(ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), c, rad, kAllCorners, th); }
        static void Dot(float x, float y, float rad, ImU32 c) { s_dl->AddCircleFilled(ImVec2(x, y), rad, c, 28); }

        static float TW(float size, const char* s) { return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x; }
        static void  Txt(float size, float x, float y, ImU32 c, const char* s) { s_dl->AddText(ImGui::GetFont(), size, ImVec2(x, y), c, s); }
        static void  TxtB(float size, float x, float y, ImU32 c, const char* s) { Txt(size, x, y, c, s); Txt(size, x + S(0.7f), y, c, s); }
        static void  TxtC(float size, const Rc& r, ImU32 c, const char* s, bool bold)
        {
                const float x = r.x + (r.w - TW(size, s)) * 0.5f;
                const float y = r.y + (r.h - size) * 0.5f;
                if (bold) TxtB(size, x, y, c, s); else Txt(size, x, y, c, s);
        }

        static float Prs(int id, float dt)
        {
                if (id < 0 || id >= 96) return 0.0f;
                float& p = s_prs[id];
                p = Lerp(p, (s_act == id && g_mmTouch.down) ? 1.0f : 0.0f, 1.0f - expf(-dt * 24.0f));
                return p;
        }

        // basma + buraxma eyni elementin uzerinde olarsa true
        static bool Tap(int id, const Rc& r, bool canInput)
        {
                if (!canInput) return false;
                if (g_mmTouch.pressed && s_act == 0 && In(r, g_mmTouch.x, g_mmTouch.y)) s_act = id;
                if (g_mmTouch.released && s_act == id) return In(r, g_mmTouch.x, g_mmTouch.y);
                return false;
        }

        static void HeaderBtn(int id, const Rc& r, float dt, const char* label, bool danger)
        {
                const float p = Prs(id, dt);
                const Rc d = Shrink(r, S(1.5f) * p);
                Fill(d, danger ? Col(196, 58, 70, 140 + (int)(70.0f * p)) : Col(255, 255, 255, 14 + (int)(34.0f * p)), S(8.0f));
                if (danger) {
                        const float cx = d.x + d.w * 0.5f, cy = d.y + d.h * 0.5f, k = S(6.0f);
                        s_dl->AddLine(ImVec2(cx - k, cy - k), ImVec2(cx + k, cy + k), Col(255, 255, 255, 235), S(2.0f));
                        s_dl->AddLine(ImVec2(cx - k, cy + k), ImVec2(cx + k, cy - k), Col(255, 255, 255, 235), S(2.0f));
                } else {
                        TxtC(S(15.0f), d, Col(226, 230, 240), label, true);
                }
        }

        // bir slider satiri; deyer deyisibse true
        static bool SliderRow(int idx, const Rc& row, const char* label, float* v, float mn, float mx, float dt, bool canInput, const char* unit = "%", float step = 5.0f)
        {
                const int id = ID_SLIDER + idx * 4;
                bool changed = false;

                const float cy = row.y + S(47.0f);
                const Rc minus{ row.x + S(14.0f), cy - S(15.0f), S(30.0f), S(30.0f) };
                const Rc plus { row.x + row.w - S(44.0f), cy - S(15.0f), S(30.0f), S(30.0f) };
                const float tx0 = row.x + S(60.0f), tx1 = row.x + row.w - S(60.0f);
                const Rc hit{ tx0 - S(16.0f), row.y + S(22.0f), (tx1 - tx0) + S(32.0f), row.h - S(22.0f) };

                if (canInput)
                {
                        const MMTouch& t = g_mmTouch;
                        if (t.pressed && s_act == 0) {
                                if (In(minus, t.x, t.y))      s_act = id + 1;
                                else if (In(plus, t.x, t.y))  s_act = id + 2;
                                else if (In(hit, t.x, t.y))   s_act = id;
                        }
                        if (s_act == id && t.down) {
                                const float f  = Clamp((t.x - tx0) / (tx1 - tx0), 0.0f, 1.0f);
                                const float nv = floorf(mn + f * (mx - mn) + 0.5f);
                                if (nv != *v) { *v = nv; changed = true; }
                        }
                        if (t.released) {
                                float nv = *v;
                                if (s_act == id + 1 && In(minus, t.x, t.y)) nv = Clamp(*v - step, mn, mx);
                                if (s_act == id + 2 && In(plus,  t.x, t.y)) nv = Clamp(*v + step, mn, mx);
                                if (nv != *v) { *v = nv; changed = true; }
                        }
                }

                const bool dragging = (s_act == id && g_mmTouch.down);
                const float target = Clamp((*v - mn) / (mx - mn), 0.0f, 1.0f);
                s_frac[idx] = Smooth(s_frac[idx], target, dt, dragging ? 40.0f : 14.0f);
                const float fr = s_frac[idx];

                // kart
                Fill(row, Col(27, 30, 40, 255), S(10.0f));
                if (dragging) Stroke(row, Accent(0.0f, 120), S(10.0f), S(1.2f));

                Txt(S(17.0f), row.x + S(16.0f), row.y + S(8.0f), Col(214, 220, 232), label);
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f%s", *v, unit);
                Txt(S(18.0f), row.x + row.w - S(16.0f) - TW(S(18.0f), buf), row.y + S(7.0f), Accent(0.0f), buf);

                // - / + duymeleri
                const float pm = Prs(id + 1, dt), pp = Prs(id + 2, dt);
                Dot(minus.x + minus.w * 0.5f, cy, S(15.0f) - S(1.5f) * pm, Col(255, 255, 255, 14 + (int)(36.0f * pm)));
                Dot(plus.x + plus.w * 0.5f,  cy, S(15.0f) - S(1.5f) * pp, Col(255, 255, 255, 14 + (int)(36.0f * pp)));
                {
                        const float k = S(5.5f), mcx = minus.x + minus.w * 0.5f, pcx = plus.x + plus.w * 0.5f;
                        s_dl->AddLine(ImVec2(mcx - k, cy), ImVec2(mcx + k, cy), Col(226, 230, 240, 220), S(2.0f));
                        s_dl->AddLine(ImVec2(pcx - k, cy), ImVec2(pcx + k, cy), Col(226, 230, 240, 220), S(2.0f));
                        s_dl->AddLine(ImVec2(pcx, cy - k), ImVec2(pcx, cy + k), Col(226, 230, 240, 220), S(2.0f));
                }

                // yol (track)
                const float th = S(6.0f), ty = cy - th * 0.5f;
                Fill(Rc{ tx0, ty, tx1 - tx0, th }, Col(255, 255, 255, 22), S(3.0f));
                const float kx = tx0 + (tx1 - tx0) * fr;
                if (kx - tx0 > 1.0f) Fill(Rc{ tx0, ty, kx - tx0, th }, Accent(0.0f), S(3.0f));

                // "standart (100%)" ish
                if (mn < 100.0f && mx > 100.0f && unit[0] == '%') {
                        const float sx = tx0 + (tx1 - tx0) * ((100.0f - mn) / (mx - mn));
                        s_dl->AddLine(ImVec2(sx, ty - S(6.0f)), ImVec2(sx, ty + th + S(6.0f)), Col(255, 255, 255, 60), S(1.4f));
                }

                // topuz
                const float kr = S(10.5f) + (dragging ? S(1.5f) : 0.0f);
                Dot(kx, cy + S(1.0f), kr + S(1.5f), Col(0, 0, 0, 70));
                Dot(kx, cy, kr, Col(245, 247, 252, 255));

                return changed;
        }

        // acar (switch) satiri; toxunulubsa true
        static bool ToggleRow(int idx, const Rc& row, const char* label, const char* sub, bool state, float dt, bool canInput)
        {
                const int id = ID_TOGGLE + idx;
                const bool tapped = Tap(id, row, canInput);
                const float p = Prs(id, dt);
                s_sw[idx] = Smooth(s_sw[idx], state ? 1.0f : 0.0f, dt, 18.0f);
                const float sw = s_sw[idx];

                const Rc d = Shrink(row, S(1.5f) * p);
                Fill(d, Col(27, 30, 40, 255), S(10.0f));
                Txt(S(18.0f), d.x + S(18.0f), d.y + S(11.0f), Col(228, 232, 242), label);
                Txt(S(13.0f), d.x + S(18.0f), d.y + S(36.0f), Col(124, 132, 152), sub);

                const Rc tr{ d.x + d.w - S(18.0f) - S(54.0f), d.y + (d.h - S(28.0f)) * 0.5f, S(54.0f), S(28.0f) };
                Fill(tr, Col((int)Lerp(62.0f, 78.0f, sw), (int)Lerp(68.0f, 142.0f, sw), (int)Lerp(84.0f, 235.0f, sw), 255), S(14.0f));
                Dot(tr.x + S(14.0f) + sw * (tr.w - S(28.0f)), tr.y + tr.h * 0.5f, S(10.5f), Col(245, 247, 252, 255));

                return tapped;
        }

        static void Arrow(float cx, float cy, float dir, ImU32 c)
        {
                const float tip = cx + dir * S(18.0f), base = cx + dir * S(2.0f), tail = cx - dir * S(18.0f);
                s_dl->AddTriangleFilled(ImVec2(tip, cy), ImVec2(base, cy - S(12.0f)), ImVec2(base, cy + S(12.0f)), c);
                const float x0 = std::min(base, tail), x1 = std::max(base, tail);
                s_dl->AddRectFilled(ImVec2(x0, cy - S(5.0f)), ImVec2(x1, cy + S(5.0f)), c);
        }

        // sinyal bloku: Sol / Avariya / Sag
        static void SignalBlock(const Rc& blk, uint8_t cur, CVehicleGTA* veh, float dt, bool canInput)
        {
                s_blink = Smooth(s_blink, VehControls::BlinkOn() ? 1.0f : 0.0f, dt, 28.0f);

                Fill(blk, Col(27, 30, 40, 255), S(10.0f));
                // BETA_V3
                const Rc bb{ blk.x + S(16.0f), blk.y + S(8.0f), S(44.0f), S(20.0f) };
                Fill(bb, Orange(46), S(10.0f));
                Stroke(bb, Orange(150), S(10.0f), S(1.0f));
                TxtC(S(12.0f), bb, Col(255, 190, 100, 255), "BETA", true);
                Txt(S(15.0f), bb.x + bb.w + S(10.0f), blk.y + S(10.0f), Col(124, 132, 152), "Sinyal");

                const float gap = S(10.0f);
                const float bw = (blk.w - S(32.0f) - gap * 2.0f) / 3.0f;
                static const char* kNames[3] = { "Sol", "Avariya", "Sağ" };
                static const uint8_t kModes[3] = { VEH_SIGNAL_LEFT, VEH_SIGNAL_HAZARD, VEH_SIGNAL_RIGHT };

                for (int k = 0; k < 3; ++k)
                {
                        const Rc b{ blk.x + S(16.0f) + k * (bw + gap), blk.y + S(34.0f), bw, blk.h - S(50.0f) };
                        const int id = ID_SIG + k;
                        if (Tap(id, b, canInput) && veh) VehControls::ToggleSignal(veh, kModes[k]);

                        const float p = Prs(id, dt);
                        const Rc d = Shrink(b, S(1.5f) * p);
                        const bool on = (cur == kModes[k]);
                        const float glow = on ? (0.28f + 0.72f * s_blink) : 0.0f;

                        Fill(d, on ? Orange(30 + (int)(110.0f * glow)) : Col(36, 40, 52, 255), S(9.0f));
                        if (on) Stroke(d, Orange(120 + (int)(100.0f * glow)), S(9.0f), S(1.3f));

                        const ImU32 ic = on ? Col(255, 183, 84, 255) : Col(150, 158, 178, 255);
                        const float cx = d.x + d.w * 0.5f, cy = d.y + d.h * 0.38f;
                        if (k == 0)      Arrow(cx, cy, -1.0f, ic);
                        else if (k == 2) Arrow(cx, cy, +1.0f, ic);
                        else {
                                s_dl->AddTriangle(ImVec2(cx, cy - S(14.0f)), ImVec2(cx - S(16.0f), cy + S(12.0f)), ImVec2(cx + S(16.0f), cy + S(12.0f)), ic, S(2.2f));
                                s_dl->AddLine(ImVec2(cx, cy - S(5.0f)), ImVec2(cx, cy + S(2.0f)), ic, S(2.2f));
                                Dot(cx, cy + S(7.0f), S(1.7f), ic);
                        }
                        TxtC(S(15.0f), Rc{ d.x, d.y + d.h * 0.62f, d.w, d.h * 0.34f }, on ? Col(255, 224, 180, 255) : Col(190, 196, 212, 255), kNames[k], true);
                }
        }
}

// ---- AZ nomre lovhesi onizlemesi (/mm -> Nomre) ----
static void PlatePreview(const mm::Rc& r, const char* text)
{
        using namespace mm;
        Fill(r, Col(14, 14, 14, 255), S(10.0f));
        const Rc in = Shrink(r, S(3.5f));
        Fill(in, Col(248, 248, 248, 255), S(8.0f));
        const float bandW = in.w * 0.14f;
        Fill(Rc{ in.x, in.y, bandW, in.h }, Col(0, 86, 170, 255), S(8.0f));
        const float fw = bandW * 0.62f, fh = in.h * 0.085f;
        const float fx = in.x + (bandW - fw) * 0.5f, fy = in.y + in.h * 0.17f;
        Fill(Rc{ fx, fy, fw, fh },               Col(0, 174, 239, 255), 0.0f);
        Fill(Rc{ fx, fy + fh, fw, fh },          Col(224, 60, 49, 255), 0.0f);
        Fill(Rc{ fx, fy + 2.0f * fh, fw, fh },   Col(0, 150, 63, 255),  0.0f);
        TxtC(in.h * 0.28f, Rc{ in.x, in.y + in.h * 0.52f, bandW, in.h * 0.36f }, Col(255, 255, 255, 255), "AZ", true);
        TxtC(in.h * 0.56f, Rc{ in.x + bandW, in.y, in.w - bandW, in.h }, Col(12, 12, 12, 255), text, true);
}

// SPEEDO_STYLES_PATCH: /mm -> Spidometr sehifesi (asagida, HUD bolmesinde teyin olunur)
static void MM_SpeedoPage(float cx0, float cw, const mm::Rc& P, float dt, bool canInput);

void RenderHandlingDialog();   // MM2_V2 (yeni, faylin sonunda)
static void RenderHandlingDialog_Old()
{
        using namespace mm;

        static bool          s_wasOpen = false;
        static CVehicleGTA*  s_lastVeh = nullptr;
        static VehTune       s_tune;

        // maşının real veziyyeti (yalniz acıq vaxt oxunur, baglanarkən kohne pointere toxunmuruq)
        static bool    s_eng = false, s_lig = false, s_lock = false;
        static uint8_t s_sig = 0;

        static float s_t = 0.0f;                 // acilma/baglanma animasiyasi 0..1
        static float s_cx = -1.0f, s_cy = -1.0f; // panel merkezi
        static float s_zoom = 1.0f;              // istifadeci olcusu
        static int   s_page = 0;
        static float s_pageAnim = 1.0f;
        static float s_navRel = 74.0f;           // sidebar gostericisi (u vahidinde)
        static float s_flash = 0.0f;
        static float s_dragDx = 0.0f, s_dragDy = 0.0f;
        static std::chrono::steady_clock::time_point s_last = std::chrono::steady_clock::now();

        // ---- real vaxt ----
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - s_last).count();
        s_last = now;
        dt = Clamp(dt, 0.0f, 0.05f);

        const bool want = g_bShowHandlingDlg;
        if (!want && s_t <= 0.0f) {
                s_wasOpen = false;
                s_hndRectValid = false;
                s_act = 0;
                return;
        }

        CVehicleGTA* gtaVeh = s_lastVeh;
        CVehicle*    pVehW  = nullptr;
        bool         noVeh  = false;   // MM_NO_VEHICLE_PATCH

        CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;

        if (want)
        {
                noVeh = !pLocalPlayer || !pLocalPlayer->IsInVehicle();
                if (noVeh) {
                        gtaVeh = nullptr; s_lastVeh = nullptr;
                        s_eng = s_lig = s_lock = false; s_sig = 0;
                        s_wasOpen = true;
                } else {

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

                pVehW  = pVeh;
                gtaVeh = pVeh->m_pVehicle;

                // Yeni acilis ve ya basqa masin -> deyerleri masinin real veziyyetinden oxu
                if (!s_wasOpen || gtaVeh != s_lastVeh) {
                        s_tune    = VehTuner::Get(gtaVeh);
                        s_lastVeh = gtaVeh;
                        s_wasOpen = true;
                        for (int i = 0; i < 8; ++i) s_frac[i] = 0.0f;
                        s_pageAnim = 0.0f;
                        HND_LOG("[HND] dialog opened");
                }

                // real veziyyet (her kadr)
                s_eng  = gtaVeh->m_nVehicleFlags.bEngineOn != 0;
                s_lig  = gtaVeh->m_nVehicleFlags.bLightsOn != 0;
                {
                        const int lk = (int)gtaVeh->m_nDoorLock;
                        s_lock = (lk == 2 || lk == 3);
                }
                s_sig = VehControls::GetSignal(gtaVeh);
                }
        }

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 disp = io.DisplaySize;

        // ---- animasiya zamani (sakit, siçrayissiz) ----
        s_t = Clamp(s_t + (want ? dt / 0.22f : -dt / 0.16f), 0.0f, 1.0f);
        const float eAnim = EaseOutCubic(s_t);
        s_A = Clamp(s_t * 2.4f, 0.0f, 1.0f);
        s_pageAnim = Clamp(s_pageAnim + dt / 0.22f, 0.0f, 1.0f);
        s_flash = Clamp(s_flash - dt / 1.4f, 0.0f, 1.0f);

        // ---- olculer ----
        const float W0 = 880.0f, H0 = 560.0f;
        float u0 = std::min(disp.y / 720.0f, std::min(disp.x * 0.96f / W0, disp.y * 0.96f / H0));
        u0 *= s_zoom;
        u0 = std::min(u0, std::min(disp.x * 0.98f / W0, disp.y * 0.98f / H0));
        s_U = u0 * (0.96f + 0.04f * eAnim);

        if (s_cx < 0.0f) { s_cx = disp.x * 0.5f; s_cy = disp.y * 0.5f; }

        auto clampCenter = [&]() {
                const float hw = W0 * u0 * 0.5f, hh = H0 * u0 * 0.5f;
                s_cx = Clamp(s_cx, hw, std::max(hw, disp.x - hw));
                s_cy = Clamp(s_cy, hh, std::max(hh, disp.y - hh));
        };
        clampCenter();

        bool canInput = want && s_t > 0.75f;
        const MMTouch& tch = g_mmTouch;

        auto panelRect = [&]() { return Rc{ s_cx - W0 * s_U * 0.5f, s_cy - H0 * s_U * 0.5f, W0 * s_U, H0 * s_U }; };
        Rc P = panelRect();

        // ---- panel surukleme (basliqdan) ----
        {
                const Rc dragRc{ P.x, P.y, P.w - S(168.0f), S(56.0f) };
                if (canInput) {
                        if (tch.pressed && s_act == 0 && In(dragRc, tch.x, tch.y)) {
                                s_act = ID_DRAG;
                                s_dragDx = tch.x - s_cx;
                                s_dragDy = tch.y - s_cy;
                        }
                        if (s_act == ID_DRAG && tch.down) {
                                s_cx = tch.x - s_dragDx;
                                s_cy = tch.y - s_dragDy;
                                clampCenter();
                                P = panelRect();
                        }
                }
        }

        s_hndRectValid = (s_t > 0.05f);
        s_hndPos  = ImVec2(P.x, P.y);
        s_hndSize = ImVec2(P.w, P.h);

        s_dl = ImGui::GetForegroundDrawList();

        // ======================= CIZGI =======================
        // kolge
        for (int k = 0; k < 3; ++k) {
                const float e = S(3.0f + k * 5.0f);
                Fill(Rc{ P.x - e, P.y - e + S(5.0f), P.w + 2.0f * e, P.h + 2.0f * e }, Col(0, 0, 0, 22 - k * 6), S(14.0f) + e);
        }
        // panel
        Fill(P, Col(17, 19, 26, 246), S(14.0f));
        Stroke(P, Col(255, 255, 255, 20), S(14.0f), S(1.0f));

        // --- basliq ---
        {
                Fill(Rc{ P.x + S(20.0f), P.y + S(15.0f), S(4.0f), S(26.0f) }, Accent(0.0f), S(2.0f));
                TxtB(S(20.0f), P.x + S(34.0f), P.y + S(8.0f), Col(238, 241, 248), "/MM");
                Txt(S(13.0f), P.x + S(34.0f), P.y + S(32.0f), Col(124, 132, 152), "Maşın menyusu");

                const Rc bClose{ P.x + P.w - S(54.0f),  P.y + S(10.0f), S(36.0f), S(36.0f) };
                const Rc bZoomP{ P.x + P.w - S(100.0f), P.y + S(10.0f), S(40.0f), S(36.0f) };
                const Rc bZoomM{ P.x + P.w - S(146.0f), P.y + S(10.0f), S(40.0f), S(36.0f) };

                if (Tap(ID_CLOSE, bClose, canInput)) { g_bShowHandlingDlg = false; HND_LOG("[HND] close"); }
                if (Tap(ID_ZOOMP, bZoomP, canInput)) s_zoom = Clamp(s_zoom + 0.1f, 0.6f, 1.4f);
                if (Tap(ID_ZOOMM, bZoomM, canInput)) s_zoom = Clamp(s_zoom - 0.1f, 0.6f, 1.4f);

                HeaderBtn(ID_ZOOMM, bZoomM, dt, "A-", false);
                HeaderBtn(ID_ZOOMP, bZoomP, dt, "A+", false);
                HeaderBtn(ID_CLOSE, bClose, dt, "", true);

                s_dl->AddLine(ImVec2(P.x + S(1.0f), P.y + S(58.0f)), ImVec2(P.x + P.w - S(1.0f), P.y + S(58.0f)), Col(255, 255, 255, 16), S(1.0f));
        }

        // --- sidebar ---
        static const char* kPages[8] = { "Nəzarət", "Mühərrik", "Sürücülük", "Asma", "Far", "Komandalar" , "Nömrə", "Spidometr" };
        static const char* kSubs[8]  = { "Mühərrik, fara, qapılar və sinyal", "Sürətlənmə, sürət və çəki", "Tormoz, manevr və yol tutuş", "Asma və toqquşma", "Far rəngi", "Bütün komandaların siyahısı" , "Azərbaycan nömrə lövhəsi (10-AA-123)", "Spidometr stilini və rəngini seç" };
        {
                s_navRel = Smooth(s_navRel, 74.0f + 56.0f * s_page, dt, 18.0f);
                const Rc ind{ P.x + S(16.0f), P.y + S(s_navRel), S(166.0f), S(48.0f) };
                Fill(ind, Col(255, 255, 255, 12), S(10.0f));
                Fill(Rc{ ind.x, ind.y + S(12.0f), S(3.0f), S(24.0f) }, Accent(0.0f), S(1.5f));

                for (int i = 0; i < 8; ++i)
                {
                        const Rc it{ P.x + S(16.0f), P.y + S(74.0f + 56.0f * i), S(166.0f), S(48.0f) };
                        if (Tap(ID_NAV0 + i, it, canInput) && s_page != i) {
                                s_page = i;
                                s_pageAnim = 0.0f;
                                for (int k = 0; k < 8; ++k) s_frac[k] = 0.0f;
                        }
                        Prs(ID_NAV0 + i, dt);
                        const bool  on = (s_page == i);
                        const float ty = it.y + (it.h - S(17.0f)) * 0.5f;
                        if (on) TxtB(S(17.0f), it.x + S(20.0f), ty, Col(240, 243, 250), kPages[i]);
                        else    Txt (S(17.0f), it.x + S(20.0f), ty, Col(140, 148, 168), kPages[i]);
                }
                s_dl->AddLine(ImVec2(P.x + S(198.0f), P.y + S(76.0f)), ImVec2(P.x + S(198.0f), P.y + P.h - S(20.0f)), Col(255, 255, 255, 16), S(1.0f));
        }

        const bool canInputAll = canInput;
        if (noVeh && (s_page <= 4 || s_page == 6)) canInput = false;   // masina bolmelerinde toxunus yoxdur

        // --- mezmun ---
        bool changed = false;
        {
                const float pe = EaseOutCubic(s_pageAnim);
                const float ox = (1.0f - pe) * S(14.0f);
                const float cx0 = P.x + S(220.0f) + ox;
                const float cw  = S(636.0f);
                const float saveA = s_A;
                s_A = saveA * pe;

                TxtB(S(24.0f), cx0, P.y + S(68.0f), Col(240, 243, 250), kPages[s_page]);
                Txt(S(14.0f), cx0, P.y + S(98.0f), Col(124, 132, 152), kSubs[s_page]);

                struct Spec { const char* label; float* v; float mn; float mx; };

                if (s_page == 0)          // NEZARET
                {
                        const Rc r0{ cx0, P.y + S(124.0f), cw, S(66.0f) };
                        const Rc r1{ cx0, P.y + S(196.0f), cw, S(66.0f) };
                        const Rc r2{ cx0, P.y + S(268.0f), cw, S(66.0f) };

                        if (ToggleRow(0, r0, "Mühərrik", s_eng ? "İşləyir" : "Söndürülüb", s_eng, dt, canInput) && pVehW)
                                pVehW->ApplyEngineState(s_eng ? 0 : 1);
                        if (ToggleRow(1, r1, "Fara", s_lig ? "Yanır" : "Sönülü", s_lig, dt, canInput) && pVehW)
                                pVehW->ApplyLightState(s_lig ? 0 : 1);
                        if (ToggleRow(2, r2, "Qapılar", s_lock ? "Kilidli" : "Açıq", s_lock, dt, canInput) && pVehW)
                                pVehW->SetDoorState(s_lock ? 0 : 1);

                        SignalBlock(Rc{ cx0, P.y + S(346.0f), cw, S(124.0f) }, s_sig, gtaVeh, dt, canInput);
                }
                else if (s_page == 4)     // FAR (serbest RGB) FAR_RGB_PATCH
                {
                        static CVehicleGTA* s_hlVeh = nullptr;
                        static float s_hl[3] = {
                            255.0f, 255.0f, 255.0f
                        };

                        if (gtaVeh != s_hlVeh) {
                                s_hlVeh = gtaVeh;

                                uint8_t r8 = 255, g8 = 255, b8 = 255;

                                if (gtaVeh &&
                                    CCoronas::GetHeadlightRGB(
                                        gtaVeh, r8, g8, b8
                                    )) {

                                        s_hl[0] = r8;
                                        s_hl[1] = g8;
                                        s_hl[2] = b8;

                                } else {
                                        s_hl[0] =
                                        s_hl[1] =
                                        s_hl[2] = 255.0f;
                                }
                        }

                        static const char* kRGB[3] = {
                            "Qirmizi (R)",
                            "Yasil (G)",
                            "Mavi (B)"
                        };

                        bool hlChanged = false;

                        for (int i = 0; i < 3; ++i) {
                                const Rc row{
                                    cx0,
                                    P.y + S(124.0f + 72.0f * (float)i),
                                    cw,
                                    S(66.0f)
                                };

                                if (SliderRow(
                                    i, row, kRGB[i],
                                    &s_hl[i],
                                    0.0f, 255.0f,
                                    dt, canInput,
                                    "", 5.0f
                                )) {
                                        hlChanged = true;
                                }
                        }

                        // onizleme + HEX + standart
                        const Rc pvr{
                            cx0,
                            P.y + S(346.0f),
                            S(150.0f),
                            S(44.0f)
                        };

                        Fill(
                            pvr,
                            Col(
                                (int)s_hl[0],
                                (int)s_hl[1],
                                (int)s_hl[2],
                                255
                            ),
                            S(10.0f)
                        );

                        Stroke(
                            pvr,
                            Col(255, 255, 255, 50),
                            S(10.0f),
                            S(1.2f)
                        );

                        char hx[24];

                        snprintf(
                            hx, sizeof(hx),
                            "#%02X%02X%02X",
                            (int)s_hl[0],
                            (int)s_hl[1],
                            (int)s_hl[2]
                        );

                        Txt(
                            S(18.0f),
                            cx0 + S(166.0f),
                            P.y + S(356.0f),
                            Col(214, 220, 232),
                            hx
                        );

                        const Rc bStd{
                            cx0 + S(300.0f),
                            P.y + S(346.0f),
                            S(150.0f),
                            S(44.0f)
                        };

                        if (Tap(90, bStd, canInput) && gtaVeh) {
                                CCoronas::ClearHeadlightRGB(gtaVeh);

                                s_tune.headlight = 0;
                                changed = true;

                                s_hl[0] =
                                s_hl[1] =
                                s_hl[2] = 255.0f;
                        }

                        {
                                const float p = Prs(90, dt);
                                const Rc d = Shrink(
                                    bStd, S(1.5f) * p
                                );

                                Fill(
                                    d,
                                    Col(255, 255, 255,
                                        14 + (int)(30.0f * p)),
                                    S(9.0f)
                                );

                                Stroke(
                                    d,
                                    Col(255, 255, 255, 40),
                                    S(9.0f),
                                    S(1.2f)
                                );

                                TxtC(
                                    S(16.0f),
                                    d,
                                    Col(240, 244, 252),
                                    "Standart",
                                    true
                                );
                        }

                        // hazir rengler
                        for (int i = 0;
                             i < VEHTUNER_COLOR_COUNT;
                             ++i) {

                                uint8_t r, g, b;

                                VehTuner::GetPaletteColor(
                                    i, r, g, b
                                );

                                const int col = i % 8;
                                const int rowi = i / 8;

                                const float cxs =
                                    cx0 + S(24.0f) +
                                    (float)col * S(58.0f);

                                const float cys =
                                    P.y + S(436.0f) +
                                    (float)rowi * S(54.0f);

                                const Rc sq{
                                    cxs - S(24.0f),
                                    cys - S(24.0f),
                                    S(48.0f),
                                    S(48.0f)
                                };

                                const int sid =
                                    ID_SWATCH + std::min(i, 23);

                                if (Tap(sid, sq, canInput) && gtaVeh) {
                                        s_hl[0] = r;
                                        s_hl[1] = g;
                                        s_hl[2] = b;
                                        hlChanged = true;
                                }

                                const float p = Prs(sid, dt);

                                Dot(
                                    cxs, cys,
                                    S(20.0f) - S(2.0f) * p,
                                    Col(r, g, b, 255)
                                );
                        }

                        if (hlChanged && gtaVeh) {
                                CCoronas::SetHeadlightRGB(
                                    gtaVeh,
                                    (uint8_t)s_hl[0],
                                    (uint8_t)s_hl[1],
                                    (uint8_t)s_hl[2]
                                );
                        }
                }
                else if (s_page == 5)    // KOMANDALAR
                {
                        struct CmdInfo { const char* cmd; const char* desc; };
                        static const CmdInfo kCmds[] = {
                                { "/mm",      "Maşın menyusunu aç/bağla" },
                                { "/sinyal",  "sol | sag | avariya | off" },
                                { "/far N",   "Far rəngi (0-15)" },
                                { "/suret N", "Maksimum sürət" },
                                { "/manevr N","Manevr bucağı" },
                                { "/tormoz N","Tormoz gücü" },
                                { "/gizle",   "Bütün UI-ı gizlət/geri qaytar" },
                                { "/yt",      "YouTube paneli" },
                                { "/cam",     "mta | fps | game | drone | fov N" },
                        };
                        const int n = (int)(sizeof(kCmds) / sizeof(kCmds[0]));
                        for (int i = 0; i < n; ++i)
                        {
                                const Rc row{ cx0, P.y + S(124.0f + 34.0f * i), cw, S(30.0f) };
                                Fill(row, Col(27, 30, 40, 255), S(9.0f));
                                Txt(S(15.0f), row.x + S(16.0f), row.y + (row.h - S(15.0f)) * 0.5f, Accent(0.0f), kCmds[i].cmd);
                                Txt(S(14.0f), row.x + S(160.0f), row.y + (row.h - S(14.0f)) * 0.5f, Col(200, 204, 216), kCmds[i].desc);
                        }
                        {   // AZ_CAMX: kamera paneli
                                const float y0 = P.y + S(124.0f + 34.0f * n + 14.0f);
                                static const char* kMn[3] = { "Oyun kamerasi", "MTA (el ile)", "Birinci shexs" };
                                const Rc rA{ cx0, y0, cw, S(36.0f) };
                                Fill(rA, Col(27, 30, 40, 255), S(9.0f));
                                Txt(S(15.0f), rA.x + S(16.0f), rA.y + (rA.h - S(15.0f)) * 0.5f, Col(200, 204, 216), "Kamera");
                                Txt(S(15.0f), rA.x + S(120.0f), rA.y + (rA.h - S(15.0f)) * 0.5f, Accent(0.0f), kMn[CamX_GetMode() % 3]);
                                const Rc bA{ rA.x + rA.w - S(112.0f), rA.y + S(5.0f), S(100.0f), S(26.0f) };
                                Fill(bA, Col(60, 66, 84, 255), S(7.0f));
                                Txt(S(14.0f), bA.x + S(22.0f), bA.y + (bA.h - S(14.0f)) * 0.5f, Col(240, 243, 250), "Deyish");
                                if (Tap(240, bA, canInput)) CamX_Cycle();

                                const Rc rB{ cx0, y0 + S(40.0f), cw, S(36.0f) };
                                Fill(rB, Col(27, 30, 40, 255), S(9.0f));
                                Txt(S(15.0f), rB.x + S(16.0f), rB.y + (rB.h - S(15.0f)) * 0.5f, Col(200, 204, 216), "Dron kamera");
                                Txt(S(15.0f), rB.x + S(120.0f), rB.y + (rB.h - S(15.0f)) * 0.5f, Accent(0.0f), CamX_IsDrone() ? "ACIQ" : "bagli");
                                const Rc bB{ rB.x + rB.w - S(112.0f), rB.y + S(5.0f), S(100.0f), S(26.0f) };
                                Fill(bB, CamX_IsDrone() ? Col(70, 130, 90, 255) : Col(60, 66, 84, 255), S(7.0f));
                                Txt(S(14.0f), bB.x + S(24.0f), bB.y + (bB.h - S(14.0f)) * 0.5f, Col(240, 243, 250), CamX_IsDrone() ? "Bagla" : "Ac");
                                if (Tap(241, bB, canInput)) CamX_ToggleDrone();

                                const Rc rC{ cx0, y0 + S(80.0f), cw, S(40.0f) };
                                Fill(rC, Col(27, 30, 40, 255), S(9.0f));
                                char fb[32]; snprintf(fb, sizeof(fb), "FOV  %.0f", CamX_GetFov());
                                Txt(S(15.0f), rC.x + S(16.0f), rC.y + (rC.h - S(15.0f)) * 0.5f, Col(200, 204, 216), fb);
                                const float tx0 = rC.x + S(120.0f), tx1 = rC.x + rC.w - S(24.0f), ty = rC.y + rC.h * 0.5f;
                                static bool s_fovDrag = false;
                                const bool inHit = g_mmTouch.x >= tx0 - S(12.0f) && g_mmTouch.x <= tx1 + S(12.0f) &&
                                                   g_mmTouch.y >= rC.y && g_mmTouch.y <= rC.y + rC.h;
                                if (g_mmTouch.pressed && inHit && canInput) s_fovDrag = true;
                                if (!g_mmTouch.down) s_fovDrag = false;
                                if (s_fovDrag) {
                                        float f = (g_mmTouch.x - tx0) / (tx1 - tx0);
                                        f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
                                        CamX_SetFov(40.0f + 70.0f * f);
                                }
                                float ff = (CamX_GetFov() - 40.0f) / 70.0f;
                                ff = ff < 0.0f ? 0.0f : (ff > 1.0f ? 1.0f : ff);
                                Fill(Rc{ tx0, ty - S(3.0f), tx1 - tx0, S(6.0f) }, Col(255, 255, 255, 40), S(3.0f));
                                Fill(Rc{ tx0, ty - S(3.0f), (tx1 - tx0) * ff, S(6.0f) }, Col(235, 235, 235, 255), S(3.0f));
                                Fill(Rc{ tx0 + (tx1 - tx0) * ff - S(8.0f), ty - S(8.0f), S(16.0f), S(16.0f) }, Col(255, 255, 255, 255), S(8.0f));
                        }
                }
                else if (s_page == 6)    // NOMRE (Azerbaycan)
                {
                        static int  s_plReg = 10, s_plL1 = 0, s_plL2 = 0, s_plNum = 123;
                        static CVehicleGTA* s_plVeh = nullptr;
                        static char  s_plMsg[96] = "";
                        static float s_plMsgT = 0.0f;

                        if (gtaVeh != s_plVeh) {
                                s_plVeh = gtaVeh;
                                std::string cur;
                                if (gtaVeh && AzPlate::Get(gtaVeh, cur) && cur.size() == 9) {
                                        s_plReg = (cur[0] - '0') * 10 + (cur[1] - '0');
                                        s_plL1  = cur[3] - 'A';
                                        s_plL2  = cur[4] - 'A';
                                        s_plNum = (cur[6] - '0') * 100 + (cur[7] - '0') * 10 + (cur[8] - '0');
                                }
                        }
                        s_plMsgT = Clamp(s_plMsgT - dt / 3.0f, 0.0f, 1.0f);

                        auto wrap = [](int v, int n) { return ((v % n) + n) % n; };

                        // [-10][-1] deyer [+1][+10]  (big)   ve ya   [-] deyer [+]
                        auto stepper = [&](int idBase, const Rc& row, const char* label, const char* val, bool big) -> int {
                                Fill(row, Col(27, 30, 40, 255), S(10.0f));
                                Txt(S(15.0f), row.x + S(14.0f), row.y + (row.h - S(15.0f)) * 0.5f, Col(200, 206, 220), label);

                                const int   side = big ? 2 : 1;
                                const int   nb = side * 2;
                                const float bw = S(44.0f), bh = S(34.0f), gap = S(6.0f), valW = S(72.0f);
                                const float total = nb * bw + nb * gap + valW;
                                float x = row.x + row.w - S(12.0f) - total;
                                const float by = row.y + (row.h - bh) * 0.5f;
                                static const char* lbBig[4]   = { "-10", "-1", "+1", "+10" };
                                static const char* lbSmall[2] = { "-", "+" };
                                static const int   dBig[4]    = { -10, -1, 1, 10 };
                                static const int   dSmall[2]  = { -1, 1 };
                                int delta = 0;
                                for (int k = 0; k < nb; ++k) {
                                        if (k == side) {
                                                TxtC(S(21.0f), Rc{ x, by, valW, bh }, Accent(0.0f), val, true);
                                                x += valW + gap;
                                        }
                                        const Rc b{ x, by, bw, bh };
                                        const int id = idBase + k;
                                        if (Tap(id, b, canInput)) delta = big ? dBig[k] : dSmall[k];
                                        const float p = Prs(id, dt);
                                        Fill(Shrink(b, S(1.2f) * p), Col(255, 255, 255, 16 + (int)(34.0f * p)), S(8.0f));
                                        TxtC(S(15.0f), b, Col(226, 230, 240), big ? lbBig[k] : lbSmall[k], true);
                                        x += bw + gap;
                                }
                                return delta;
                        };

                        char plate[16];
                        snprintf(plate, sizeof(plate), "%02d-%c%c-%03d", s_plReg, 'A' + s_plL1, 'A' + s_plL2, s_plNum);
                        PlatePreview(Rc{ cx0 + (cw - S(420.0f)) * 0.5f, P.y + S(124.0f), S(420.0f), S(105.0f) }, plate);

                        char sReg[8], sL1[4], sL2[4], sNum[8];
                        snprintf(sReg, sizeof(sReg), "%02d", s_plReg);
                        snprintf(sL1, sizeof(sL1), "%c", 'A' + s_plL1);
                        snprintf(sL2, sizeof(sL2), "%c", 'A' + s_plL2);
                        snprintf(sNum, sizeof(sNum), "%03d", s_plNum);

                        s_plReg = wrap(s_plReg + stepper(80, Rc{ cx0, P.y + S(244.0f), cw, S(46.0f) }, "Region kodu", sReg, false), 100);
                        s_plL1  = wrap(s_plL1  + stepper(82, Rc{ cx0, P.y + S(296.0f), cw * 0.49f, S(46.0f) }, "Hərf 1", sL1, false), 26);
                        s_plL2  = wrap(s_plL2  + stepper(84, Rc{ cx0 + cw * 0.51f, P.y + S(296.0f), cw * 0.49f, S(46.0f) }, "Hərf 2", sL2, false), 26);
                        s_plNum = wrap(s_plNum + stepper(86, Rc{ cx0, P.y + S(348.0f), cw, S(46.0f) }, "Rəqəmlər", sNum, true), 1000);

                        const Rc bApply{ cx0, P.y + S(412.0f), S(190.0f), S(44.0f) };
                        const Rc bRand { cx0 + S(204.0f), P.y + S(412.0f), S(190.0f), S(44.0f) };
                        const Rc bOff  { cx0 + S(408.0f), P.y + S(412.0f), S(190.0f), S(44.0f) };

                        if (Tap(90, bApply, canInput) && gtaVeh) {
                                std::string norm;
                                AzPlate::Normalize(plate, norm);
                                if (AzPlate::Set(gtaVeh, norm)) snprintf(s_plMsg, sizeof(s_plMsg), "Tətbiq edildi: %s", plate);
                                else {
                                    std::string dumpPath;
                                    AzPlate::Dump(gtaVeh, dumpPath);   // diaqnostika: vehdump.txt
                                    snprintf(s_plMsg, sizeof(s_plMsg), "Material tapılmadı (vehdump.txt yazıldı)");
                                }
                                s_plMsgT = 1.0f;
                        }
                        if (Tap(91, bRand, canInput)) {
                                static const int kReg[4] = { 10, 90, 99, 77 };
                                s_plReg = kReg[rand() % 4];
                                s_plL1 = rand() % 26;
                                s_plL2 = rand() % 26;
                                s_plNum = rand() % 1000;
                        }
                        if (Tap(92, bOff, canInput) && gtaVeh) {
                                AzPlate::Clear(gtaVeh);
                                snprintf(s_plMsg, sizeof(s_plMsg), "Standart nömrəyə qayıdıldı");
                                s_plMsgT = 1.0f;
                        }

                        auto btn = [&](int id, const Rc& b, const char* label, bool primary) {
                                const float p = Prs(id, dt);
                                const Rc d = Shrink(b, S(1.5f) * p);
                                if (primary) {
                                        Fill(d, Accent(0.0f, 190 + (int)(40.0f * p)), S(9.0f));
                                } else {
                                        Fill(d, Col(255, 255, 255, 14 + (int)(30.0f * p)), S(9.0f));
                                        Stroke(d, Col(255, 255, 255, 40), S(9.0f), S(1.2f));
                                }
                                TxtC(S(16.0f), d, Col(240, 244, 252), label, true);
                        };
                        btn(90, bApply, "Tətbiq et", true);
                        btn(91, bRand, "Təsadüfi", false);
                        btn(92, bOff, "Standart", false);

                        if (s_plMsgT > 0.01f)
                                Txt(S(15.0f), cx0, P.y + S(468.0f), Col(120, 215, 160, (int)(255.0f * s_plMsgT)), s_plMsg);
                        Txt(S(12.0f), cx0, P.y + S(500.0f), Col(124, 132, 152), "Qeyd: nömrə yalnız sizin ekranınızda görünür.");
                }
                else if (s_page == 7)    // SPIDOMETR
                {
                        MM_SpeedoPage(cx0, cw, P, dt, canInput);
                }
                else                      // HANDLING SEHIFELERI
                {
                        Spec sp[5];
                        int  n = 0;
                        if (s_page == 1) {
                                sp[n++] = { "Yerindən götürmə (təcil)",    &s_tune.accelPct,   30.0f, 400.0f };
                                sp[n++] = { "Maksimum sürət",              &s_tune.speedPct,   50.0f, 250.0f };
                                sp[n++] = { "Mühərrik ətaləti (az = tez)", &s_tune.inertiaPct, 20.0f, 300.0f };
                                sp[n++] = { "Çəki / kütlə",                &s_tune.massPct,    30.0f, 300.0f };
                                sp[n++] = { "Hava müqaviməti",             &s_tune.dragPct,    20.0f, 300.0f };
                        } else if (s_page == 2) {
                                sp[n++] = { "Tormoz gücü",                 &s_tune.brakePct,    30.0f, 300.0f };
                                sp[n++] = { "Manevr bucağı",               &s_tune.steerPct,    50.0f, 150.0f };
                                sp[n++] = { "Yol tutuş (traction)",        &s_tune.tractionPct, 50.0f, 200.0f };
                                sp[n++] = { "Sürüşmə itkisi (az = yaxşı)", &s_tune.tracLossPct, 20.0f, 200.0f };
                                sp[n++] = { "Fırlanma ətaləti",            &s_tune.turnPct,     30.0f, 300.0f };
                        } else {
                                sp[n++] = { "Asma gücü",                   &s_tune.suspForcePct, 30.0f, 300.0f };
                                sp[n++] = { "Asma amortizatorları",        &s_tune.suspDampPct,  30.0f, 300.0f };
                                sp[n++] = { "Toqquşma zərəri",             &s_tune.collisionPct, 10.0f, 300.0f };
                        }

                        for (int i = 0; i < n; ++i) {
                                const Rc row{ cx0, P.y + S(124.0f + 72.0f * i), cw, S(66.0f) };
                                if (SliderRow(i, row, sp[i].label, sp[i].v, sp[i].mn, sp[i].mx, dt, canInput)) changed = true;
                        }
                }

                // --- alt sira: Sifirla (yalniz handling sehifelerinde) + status ---
                if (s_page >= 1 && s_page <= 3)
                {
                        const Rc rb{ cx0, P.y + P.h - S(60.0f), S(150.0f), S(42.0f) };
                        if (Tap(ID_RESET, rb, canInput) && gtaVeh) {
                                VehTuner::Reset(gtaVeh);
                                s_tune = VehTune();
                                changed = false;
                                s_flash = 1.0f;
                                for (int k = 0; k < 8; ++k) s_frac[k] = 0.0f;
                                if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[MM]: Standart dəyərlər qaytarıldı.");
                        }
                        const float rp = Prs(ID_RESET, dt);
                        const Rc rd = Shrink(rb, S(1.5f) * rp);
                        Fill(rd, Accent(0.0f, 20 + (int)(36.0f * rp)), S(9.0f));
                        Stroke(rd, Accent(0.0f, 170), S(9.0f), S(1.3f));
                        TxtC(S(16.0f), rd, Col(232, 238, 250), "Sıfırla", true);

                        if (s_flash > 0.01f) {
                                Dot(cx0 + S(174.0f), rb.y + rb.h * 0.5f, S(4.5f), Col(90, 205, 140, (int)(255.0f * s_flash)));
                                Txt(S(15.0f), cx0 + S(188.0f), rb.y + (rb.h - S(15.0f)) * 0.5f, Col(120, 215, 160, (int)(255.0f * s_flash)), "Tətbiq edildi");
                        }
                }

                if (noVeh && (s_page <= 4 || s_page == 6))
                {
                        // masina yoxdur: bu bolme sondurulub
                        const Rc ov{ cx0 - S(8.0f), P.y + S(112.0f), cw + S(16.0f), P.h - S(128.0f) };
                        Fill(ov, Col(17, 19, 26, 205), S(12.0f));
                        TxtC(S(18.0f), Rc{ ov.x, ov.y + ov.h * 0.5f - S(30.0f), ov.w, S(26.0f) }, Col(238, 241, 248), "Masinda deyilsiniz", true);
                        TxtC(S(14.0f), Rc{ ov.x, ov.y + ov.h * 0.5f + S(2.0f), ov.w, S(22.0f) }, Col(124, 132, 152), "Bu bolme yalniz masinda isleyir", false);
                }
                canInput = canInputAll;

                s_A = saveA;
        }

        // Her deyisiklik canli tetbiq olunur (original-dan hesablandigi ucun yigilmir)
        if (changed && want && gtaVeh) {
                VehTuner::Set(gtaVeh, s_tune);
                s_flash = 1.0f;
        }

        if (tch.released || !tch.down) s_act = 0;

#if HND_DEBUG
        // Diaqnostika: qirmizi halqa = oyunun hesab etdiyi barmaq yeri
        {
                ImDrawList* d = ImGui::GetForegroundDrawList();
                if (tch.x >= 0.0f) {
                        d->AddCircle(ImVec2(tch.x, tch.y), 22.0f, tch.down ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 220, 60, 160), 24, 3.0f);
                }
                char dbg[96];
                snprintf(dbg, sizeof(dbg), "T %.0f,%.0f d%d ev%u disp %.0fx%.0f act%d", tch.x, tch.y, (int)tch.down, tch.events, disp.x, disp.y, s_act);
                d->AddText(ImGui::GetFont(), 18.0f, ImVec2(11.0f, disp.y - 29.0f), IM_COL32(0, 0, 0, 255), dbg);
                d->AddText(ImGui::GetFont(), 18.0f, ImVec2(10.0f, disp.y - 30.0f), IM_COL32(255, 255, 255, 255), dbg);
        }
#endif
}

// ============================================================================
//  DAİMİ HUD: avtomobildə olarkən hər zaman görünən 3 dairəvi göstərici
//  (Mühərrik / Fara / Qapı) + kiçik sinyal düymələri. Ekranın alt-sağ
//  bölgəsində, mərkəzdən bir az sağda — heç nəyə mane olmayacaq şəkildə.
//  Rəngi dəyişmək üçün göstərici qrupuna basılı tutmaq kifayətdir (uzun basma).
// ============================================================================
static int   s_hudColorIdx = 0;
static const ImU32 kHudPalette[5] = { IM_COL32(78,142,235,255), IM_COL32(78,235,142,255),
                                       IM_COL32(235,120,78,255), IM_COL32(235,78,201,255), IM_COL32(230,230,230,255) };
static float s_hudHoldT = 0.0f;
static bool  s_hudHolding = false;

// ============================================================================
//  SPEEDO_STYLES_PATCH: spidometr stilleri (0 = Klassik, 1 = Sport zolaq, 2 = Neon qovs)
//  Secim ve reng <storage>/hudstyle.txt faylinda saxlanir.
// ============================================================================
extern char* g_pszStorage;

static int s_hudStyle = 0;

static inline ImU32 HudA(ImU32 c, int a)
{
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        return (c & 0x00FFFFFFu) | ((ImU32)a << 24);
}

static void HudCfgLoad()
{
        static bool s_done = false;
        if (s_done) return;
        s_done = true;
        if (!g_pszStorage) return;

        char path[512];
        snprintf(path, sizeof(path), "%s/hudstyle.txt", g_pszStorage);
        FILE* f = fopen(path, "r");
        if (!f) return;
        int a = 0, b = 0;
        if (fscanf(f, "%d %d", &a, &b) == 2) {
                if (a >= 0 && a < 3) s_hudStyle = a;
                if (b >= 0 && b < 5) s_hudColorIdx = b;
        }
        fclose(f);
}

static void HudCfgSave()
{
        if (!g_pszStorage) return;
        char path[512];
        snprintf(path, sizeof(path), "%s/hudstyle.txt", g_pszStorage);
        FILE* f = fopen(path, "w");
        if (!f) return;
        fprintf(f, "%d %d\n", s_hudStyle, s_hudColorIdx);
        fclose(f);
}

static void HudText(ImDrawList* dl, ImFont* font, float size, float x, float y, ImU32 col, const char* s, int align)
{
        const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        if (align == 1) x -= sz.x * 0.5f;
        else if (align == 2) x -= sz.x;
        dl->AddText(font, size, ImVec2(x, y), col, s);
}

// ox: dir = -1 sola, +1 saga
static void HudArrow(ImDrawList* dl, float cx, float cy, float s, float dir, ImU32 col)
{
        const float tail = cx - dir * s;
        const float neck = cx - dir * s * 0.1f;
        dl->AddTriangleFilled(ImVec2(cx + dir * s, cy), ImVec2(neck, cy - s * 0.85f), ImVec2(neck, cy + s * 0.85f), col);
        dl->AddRectFilled(ImVec2(std::min(tail, neck), cy - s * 0.32f), ImVec2(std::max(tail, neck), cy + s * 0.32f), col);
}

static void HudChip(ImDrawList* dl, ImFont* font, float x, float y, float rad, bool on, const char* t, ImU32 accent)
{
        dl->AddCircleFilled(ImVec2(x, y), rad, on ? accent : IM_COL32(60, 64, 76, 210), 20);
        HudText(dl, font, rad * 1.15f, x, y - rad * 0.58f, on ? IM_COL32(10, 12, 18, 255) : IM_COL32(170, 174, 186, 255), t, 1);
}

// ===== HUD_FLOW_V4: yeni muasir axici speedometrler =====
//  1 = Axin halqasi (gradient qovs + axan zerreler)
//  2 = Susse zolaq (axici maye zolagi)
static ImU32 HudMix(ImU32 a, ImU32 b, float t)
{
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
        const int ar = (int)(a & 255u), ag = (int)((a >> 8) & 255u), ab = (int)((a >> 16) & 255u), aa = (int)((a >> 24) & 255u);
        const int br = (int)(b & 255u), bg = (int)((b >> 8) & 255u), bb = (int)((b >> 16) & 255u), ba = (int)((b >> 24) & 255u);
        return IM_COL32(ar + (int)((float)(br - ar) * t), ag + (int)((float)(bg - ag) * t),
                        ab + (int)((float)(bb - ab) * t), aa + (int)((float)(ba - aa) * t));
}

static void HudGradArc(ImDrawList* dl, const ImVec2& c, float rad, float a0, float a1, ImU32 c0, ImU32 c1, float th)
{
        const int seg = 40;
        for (int i = 0; i < seg; ++i) {
                const float t0 = (float)i / (float)seg, t1 = (float)(i + 1) / (float)seg;
                dl->PathArcTo(c, rad, a0 + (a1 - a0) * t0, a0 + (a1 - a0) * t1 + 0.015f, 3);
                dl->PathStroke(HudMix(c0, c1, (t0 + t1) * 0.5f), 0, th);
        }
}

static float HudNum(float& cur, float target)
{
        cur += (target - cur) * (1.0f - expf(-ImGui::GetIO().DeltaTime * 12.0f));
        return cur;
}

// ---- Stil 1: AXIN HALQASI ----
static void HudFlow(ImDrawList* dl, ImFont* font, const ImVec2& disp, float u, float kmh, const char* gear,
                    bool eng, bool lig, bool unlocked, ImU32 accent, float frac, const char* odo, uint8_t sig)
{
        const float PI = 3.14159265f;
        const float R  = 96.0f * u;
        const ImVec2 c(disp.x * 0.66f, disp.y - 14.0f * u - R * 1.08f);
        const float th = 9.0f * u;
        const float a0 = PI * 0.75f, sweep = PI * 1.5f;
        const float tm = (float)ImGui::GetTime();
        const bool  blink = VehControls::BlinkOn();
        const ImU32 white  = IM_COL32(255, 255, 255, 255);
        const ImU32 hot    = HudMix(accent, white, 0.55f);
        const ImU32 sigOn  = IM_COL32(255, 160, 30, 255);
        const ImU32 sigOff = IM_COL32(64, 68, 80, 210);

        static float s_num = 0.0f;
        const float shown = HudNum(s_num, kmh);

        // zemin
        dl->AddCircleFilled(c, R * 1.12f, IM_COL32(8, 10, 16, 150), 64);
        dl->AddCircle(c, R * 1.12f, IM_COL32(255, 255, 255, 22), 64, 1.2f * u);

        // yol
        dl->PathArcTo(c, R, a0, a0 + sweep, 56);
        dl->PathStroke(IM_COL32(255, 255, 255, 26), 0, th);
        for (int e = 0; e < 2; ++e) {
                const float a = a0 + sweep * (float)e;
                dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R, c.y + sinf(a) * R), th * 0.5f, IM_COL32(255, 255, 255, 26), 12);
        }

        // dolu hisse
        if (frac > 0.004f)
        {
                const float a1 = a0 + sweep * frac;
                HudGradArc(dl, c, R, a0, a1, HudA(accent, 10), HudA(accent, 46), th * 2.6f);   // yumsaq isiq
                HudGradArc(dl, c, R, a0, a1, HudA(accent, 90), hot, th);
                dl->AddCircleFilled(ImVec2(c.x + cosf(a0) * R, c.y + sinf(a0) * R), th * 0.5f, HudA(accent, 90), 12);

                // axan zerreler
                for (int k = 0; k < 3; ++k) {
                        const float p = fmodf(tm * 0.55f + (float)k / 3.0f, 1.0f);
                        const float a = a0 + (a1 - a0) * p;
                        dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R, c.y + sinf(a) * R), th * 0.22f,
                                            IM_COL32(255, 255, 255, (int)(200.0f * sinf(p * PI))), 10);
                }
                // bas
                const ImVec2 h(c.x + cosf(a1) * R, c.y + sinf(a1) * R);
                dl->AddCircleFilled(h, th * 1.25f, HudA(hot, 60), 20);
                dl->AddCircleFilled(h, th * 0.62f, white, 20);
        }

        // nuqteler
        for (int i = 0; i <= 9; ++i) {
                const float q = (float)i / 9.0f, a = a0 + sweep * q;
                dl->AddCircleFilled(ImVec2(c.x + cosf(a) * R * 0.80f, c.y + sinf(a) * R * 0.80f),
                                    (i % 3 == 0 ? 1.8f : 1.2f) * u, q <= frac ? HudA(hot, 190) : IM_COL32(255, 255, 255, 50), 8);
        }

        // suret
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(shown + 0.5f));
        const float ns = R * 0.58f;
        HudText(dl, font, ns, c.x + 1.2f * u, c.y - R * 0.50f + 1.5f * u, IM_COL32(0, 0, 0, 110), sp, 1);
        HudText(dl, font, ns, c.x, c.y - R * 0.50f, white, sp, 1);
        HudText(dl, font, R * 0.13f, c.x, c.y + R * 0.10f, hot, "KM/H", 1);

        // M F Q
        HudChip(dl, font, c.x - R * 0.26f, c.y + R * 0.40f, R * 0.075f, eng,      "M", accent);
        HudChip(dl, font, c.x,             c.y + R * 0.40f, R * 0.075f, lig,      "F", accent);
        HudChip(dl, font, c.x + R * 0.26f, c.y + R * 0.40f, R * 0.075f, unlocked, "Q", accent);

        // vites + sinyal
        const float gy = c.y + R * 0.76f, gw = R * 0.46f, gh = R * 0.24f;
        dl->AddRectFilled(ImVec2(c.x - gw * 0.5f, gy - gh * 0.5f), ImVec2(c.x + gw * 0.5f, gy + gh * 0.5f), IM_COL32(14, 16, 24, 230), gh * 0.5f);
        dl->AddRect(ImVec2(c.x - gw * 0.5f, gy - gh * 0.5f), ImVec2(c.x + gw * 0.5f, gy + gh * 0.5f), HudA(accent, 200), gh * 0.5f, mm::kAllCorners, 1.4f * u);
        HudText(dl, font, gh * 0.78f, c.x, gy - gh * 0.39f, white, gear, 1);

        const bool lOn = (sig == VEH_SIGNAL_LEFT  || sig == VEH_SIGNAL_HAZARD) && blink;
        const bool rOn = (sig == VEH_SIGNAL_RIGHT || sig == VEH_SIGNAL_HAZARD) && blink;
        HudArrow(dl, c.x - R * 0.50f, gy, R * 0.10f, -1.0f, lOn ? sigOn : sigOff);
        HudArrow(dl, c.x + R * 0.50f, gy, R * 0.10f, +1.0f, rOn ? sigOn : sigOff);

        HudText(dl, font, R * 0.10f, c.x, c.y + R * 0.95f, IM_COL32(190, 194, 208, 200), odo, 1);
}

// ---- Stil 2: SUSSE ZOLAQ ----
static void HudGlass(ImDrawList* dl, ImFont* font, const ImVec2& disp, float u, float kmh, const char* gear,
                     bool eng, bool lig, bool unlocked, ImU32 accent, float frac, const char* odo, uint8_t sig)
{
        const float PI = 3.14159265f; (void)PI;
        const float pw = 312.0f * u, ph = 116.0f * u;
        const float px = disp.x * 0.66f - pw * 0.5f, py = disp.y - 14.0f * u - ph;
        const float rr = 24.0f * u, pad = 20.0f * u;
        const float tm = (float)ImGui::GetTime();
        const bool  blink = VehControls::BlinkOn();
        const ImU32 white  = IM_COL32(255, 255, 255, 255);
        const ImU32 hot    = HudMix(accent, white, 0.55f);
        const ImU32 sigOn  = IM_COL32(255, 160, 30, 255);
        const ImU32 sigOff = IM_COL32(64, 68, 80, 210);

        static float s_num = 0.0f;
        const float shown = HudNum(s_num, kmh);

        // susse panel
        dl->AddRectFilled(ImVec2(px, py + 5.0f * u), ImVec2(px + pw, py + ph + 5.0f * u), IM_COL32(0, 0, 0, 60), rr);
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(14, 16, 24, 196), rr);
        dl->AddRectFilledMultiColor(ImVec2(px + rr * 0.5f, py + 1.5f * u), ImVec2(px + pw - rr * 0.5f, py + ph * 0.42f),
                                    IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 26), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->AddRect(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(255, 255, 255, 40), rr, mm::kAllCorners, 1.2f * u);

        // suret (sabit en: reqem sayi deyisende KM/H yerinden oynamasin)
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(shown + 0.5f));
        const float ns = ph * 0.50f;
        const float nw = font->CalcTextSizeA(ns, FLT_MAX, 0.0f, "888").x;
        HudText(dl, font, ns, px + pad + nw, py + ph * 0.08f, white, sp, 2);
        HudText(dl, font, ph * 0.13f, px + pad + nw + 8.0f * u, py + ph * 0.40f, hot, "KM/H", 0);

        // vites
        const float gr = ph * 0.20f, gcx = px + pw - pad - gr, gcy = py + ph * 0.34f;
        dl->AddCircleFilled(ImVec2(gcx, gcy), gr, IM_COL32(8, 10, 16, 230), 32);
        dl->AddCircle(ImVec2(gcx, gcy), gr, HudA(accent, 210), 32, 1.6f * u);
        HudText(dl, font, gr * 1.15f, gcx, gcy - gr * 0.58f, white, gear, 1);

        // axici maye zolagi
        const float bx = px + pad, bw = pw - pad * 2.0f, bh = ph * 0.11f, by = py + ph * 0.66f;
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), IM_COL32(255, 255, 255, 26), bh * 0.5f);
        if (frac > 0.004f)
        {
                float fw = bw * frac;
                if (fw < bh) fw = bh;
                dl->AddRectFilled(ImVec2(bx - 2.0f * u, by - 3.0f * u), ImVec2(bx + fw + 2.0f * u, by + bh + 3.0f * u), HudA(accent, 36), bh);
                dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + fw, by + bh), HudMix(accent, IM_COL32(0, 0, 0, 255), 0.30f), bh * 0.5f);

                dl->PushClipRect(ImVec2(bx, by), ImVec2(bx + fw, by + bh), true);
                const float inner = fw - bh * 0.5f;
                if (inner > 2.0f)
                        dl->AddRectFilledMultiColor(ImVec2(bx, by), ImVec2(bx + inner, by + bh),
                                                    HudA(hot, 0), HudA(hot, 220), HudA(hot, 220), HudA(hot, 0));
                const float bandW = bw * 0.22f;
                const float sx = bx - bandW + (bw + bandW * 2.0f) * fmodf(tm * 0.45f, 1.0f);
                dl->AddRectFilledMultiColor(ImVec2(sx, by), ImVec2(sx + bandW * 0.5f, by + bh),
                                            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 0));
                dl->AddRectFilledMultiColor(ImVec2(sx + bandW * 0.5f, by), ImVec2(sx + bandW, by + bh),
                                            IM_COL32(255, 255, 255, 120), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 120));
                dl->PopClipRect();

                const ImVec2 h(bx + fw - bh * 0.5f, by + bh * 0.5f);
                dl->AddCircleFilled(h, bh * 1.5f, HudA(hot, 60), 20);
                dl->AddCircleFilled(h, bh * 0.85f, white, 20);
        }

        // alt sira: sinyal oxlari, M F Q, yol sayaci
        const float ry = py + ph * 0.89f;
        const bool lOn = (sig == VEH_SIGNAL_LEFT  || sig == VEH_SIGNAL_HAZARD) && blink;
        const bool rOn = (sig == VEH_SIGNAL_RIGHT || sig == VEH_SIGNAL_HAZARD) && blink;
        HudArrow(dl, px + pad + ph * 0.07f, ry, ph * 0.07f, -1.0f, lOn ? sigOn : sigOff);
        HudArrow(dl, px + pad + ph * 0.25f, ry, ph * 0.07f, +1.0f, rOn ? sigOn : sigOff);
        HudChip(dl, font, px + pw * 0.54f, ry, ph * 0.06f, eng,      "M", accent);
        HudChip(dl, font, px + pw * 0.62f, ry, ph * 0.06f, lig,      "F", accent);
        HudChip(dl, font, px + pw * 0.70f, ry, ph * 0.06f, unlocked, "Q", accent);
        HudText(dl, font, ph * 0.09f, px + pw - pad, ry - ph * 0.045f, IM_COL32(190, 194, 208, 200), odo, 2);
}

// ---- /mm -> Spidometr sehifesi ----
static void HudPreview(int style, const mm::Rc& pv)
{
        using namespace mm;
        const float PI = 3.14159265f;
        const ImU32 white = Col(255, 255, 255, 235);

        if (style == 0)
        {
                const float cx = pv.x + pv.w * 0.5f, cy = pv.y + pv.h * 0.52f, rad = std::min(pv.w, pv.h) * 0.40f;
                const float a0 = PI * 0.75f, sw = PI * 1.5f;
                s_dl->PathArcTo(ImVec2(cx, cy), rad, a0, a0 + sw, 28);
                s_dl->PathStroke(Col(70, 76, 92, 220), 0, S(3.0f));
                s_dl->PathArcTo(ImVec2(cx, cy), rad, a0, a0 + sw * 0.62f, 28);
                s_dl->PathStroke(Accent(0.0f), 0, S(3.0f));
                s_dl->AddLine(ImVec2(cx, cy), ImVec2(cx + cosf(a0 + sw * 0.62f) * rad * 0.8f, cy + sinf(a0 + sw * 0.62f) * rad * 0.8f), white, S(1.6f));
        }
        else if (style == 1)
        {
                const float cx = pv.x + pv.w * 0.5f, cy = pv.y + pv.h * 0.54f, rad = std::min(pv.w, pv.h) * 0.36f;
                const float a0 = PI * 0.75f, sw = PI * 1.5f;
                s_dl->PathArcTo(ImVec2(cx, cy), rad, a0, a0 + sw, 28);
                s_dl->PathStroke(Col(255, 255, 255, 34), 0, S(4.0f));
                HudGradArc(s_dl, ImVec2(cx, cy), rad, a0, a0 + sw * 0.64f, HudA(Accent(0.0f), 90), Accent(0.0f), S(4.0f));
                TxtC(rad * 0.9f, Rc{ cx - rad, cy - rad * 0.55f, rad * 2.0f, rad * 0.9f }, white, "128", true);
        }
        else
        {
                Fill(Rc{ pv.x + pv.w * 0.08f, pv.y + pv.h * 0.16f, pv.w * 0.84f, pv.h * 0.68f }, Col(255, 255, 255, 16), S(9.0f));
                TxtC(pv.h * 0.30f, Rc{ pv.x + pv.w * 0.08f, pv.y + pv.h * 0.22f, pv.w * 0.5f, pv.h * 0.34f }, white, "128", true);
                const Rc tr{ pv.x + pv.w * 0.14f, pv.y + pv.h * 0.66f, pv.w * 0.72f, pv.h * 0.08f };
                Fill(tr, Col(255, 255, 255, 34), S(3.0f));
                Fill(Rc{ tr.x, tr.y, tr.w * 0.64f, tr.h }, Accent(0.0f), S(3.0f));
        }
}

static void MM_SpeedoPage(float cx0, float cw, const mm::Rc& P, float dt, bool canInput)
{
        using namespace mm;
        HudCfgLoad();

        static const char* kNames[3] = { "Klassik dairə", "Axın halqası", "Şüşə zolaq" };
        static const char* kDescs[3] = { "MTA üslubunda dairəvi ibrə və rəqəmlər",
                                         "Gradient halqa, axan işıq zərrələri və iri rəqəm",
                                         "Şüşə panel və axar maye zolaqı" };

        for (int i = 0; i < 3; ++i)
        {
                const Rc row{ cx0, P.y + S(124.0f + 112.0f * (float)i), cw, S(100.0f) };
                const int id = 93 + i;
                if (Tap(id, row, canInput) && s_hudStyle != i) { s_hudStyle = i; HudCfgSave(); }

                const float p = Prs(id, dt);
                const bool on = (s_hudStyle == i);
                const Rc d = Shrink(row, S(1.5f) * p);
                Fill(d, Col(27, 30, 40, 255), S(10.0f));
                if (on) Stroke(d, Accent(0.0f, 200), S(10.0f), S(1.6f));

                const Rc pv{ d.x + S(14.0f), d.y + S(12.0f), S(170.0f), d.h - S(24.0f) };
                Fill(pv, Col(10, 12, 18, 255), S(8.0f));
                HudPreview(i, pv);

                TxtB(S(19.0f), d.x + S(204.0f), d.y + S(24.0f), Col(238, 241, 248), kNames[i]);
                Txt(S(13.0f), d.x + S(204.0f), d.y + S(54.0f), Col(124, 132, 152), kDescs[i]);
                if (on) Txt(S(13.0f), d.x + S(204.0f), d.y + S(74.0f), Accent(0.0f), "Seçilib");
        }

        Txt(S(16.0f), cx0, P.y + S(470.0f), Col(214, 220, 232), "Rəng");
        for (int i = 0; i < 5; ++i)
        {
                const float cxs = cx0 + S(120.0f) + (float)i * S(54.0f), cys = P.y + S(480.0f);
                const Rc sq{ cxs - S(22.0f), cys - S(22.0f), S(44.0f), S(44.0f) };
                const int sid = ID_SWATCH + i;
                if (Tap(sid, sq, canInput)) { s_hudColorIdx = i; HudCfgSave(); }

                const float p = Prs(sid, dt);
                Dot(cxs, cys, S(18.0f) - S(2.0f) * p, HudA(kHudPalette[i], (int)(255.0f * s_A)));
                if (s_hudColorIdx == i) s_dl->AddCircle(ImVec2(cxs, cys), S(23.0f), Col(255, 255, 255, 230), 28, S(2.2f));
        }
        Txt(S(12.0f), cx0, P.y + S(524.0f), Col(124, 132, 152), "İpucu: sürət göstəricisinə uzun basaraq da rəngi dəyişə bilərsiniz.");
}

static void DrawGauge(ImDrawList* dl, float cx, float cy, float r, bool on, const char* label, ImU32 accent, float u)
{
        const ImU32 bg = IM_COL32(20, 22, 30, 190);
        dl->AddCircleFilled(ImVec2(cx, cy), r, bg, 36);
        dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(255, 255, 255, 30), 36, 1.4f * u);

        // doldurma qovsu: on = 3/4 hiss, off = qisa
        const float startA = -3.14159f * 0.5f - 1.0f;
        const float endA   = startA + (on ? 5.0f : 0.9f);
        dl->PathArcTo(ImVec2(cx, cy), r - 3.0f * u, startA, endA, 28);
        dl->PathStroke(on ? accent : IM_COL32(110, 114, 126, 220), 0, 3.0f * u);

        ImFont* f = ImGui::GetFont();
        const float ts = 10.5f * u;
        const float tw = f->CalcTextSizeA(ts, FLT_MAX, 0.0f, label).x;
        dl->AddText(f, ts, ImVec2(cx - tw * 0.5f, cy - ts * 0.5f), on ? IM_COL32(240,243,250,255) : IM_COL32(150,154,166,255), label);
}

#include <cmath>
#include <cstdio>

// ===== VTSYNC_V5: far rengi + sinyal diger oyunculara da gorunsun =====
//  client -> server : /_vt <masinID> <rgb|-1> <sinyal>                (yalniz surucu gonderir)
//  server -> client : SendClientMessage "~vt~<masinID> <rgb|-1> <sinyal>"   (chatda gorunmur)
//  Serverde vt_sync.pwn filterscript-i yuklu olmalidir.
#include <map>
namespace VtSync
{
        struct St { int rgb; int sig; St() : rgb(-1), sig(0) {} };

        static std::map<int, St> s_state;      // serverden gelen vezyet
        static std::map<int, St> s_sent;       // son gonderilen / bilinen (echo olmasin)
        static std::map<int, std::pair<CVehicleGTA*, uint32_t> > s_applied;
        static double s_lastSend = 0.0;

        static double Now()
        {
                return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        static void Apply(CVehicleGTA* v, const St& s)
        {
                if (!v) return;
                if (s.rgb >= 0) CCoronas::SetHeadlightRGB(v, (uint8_t)((s.rgb >> 16) & 255), (uint8_t)((s.rgb >> 8) & 255), (uint8_t)(s.rgb & 255));
                else            CCoronas::ClearHeadlightRGB(v);
                VehControls::SetSignal(v, (uint8_t)s.sig);
        }

        static CVehicle* LocalVeh(bool* isDriver, int* idOut)
        {
                CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
                if (!p || !p->IsInVehicle() || !p->m_pPed) return nullptr;
                if (!pNetGame || !pNetGame->GetVehiclePool()) return nullptr;
                CVehiclePool* pool = pNetGame->GetVehiclePool();
                const VEHICLEID id = pool->FindIDFromGtaPtr(p->m_pPed->pVehicle);
                if (id == INVALID_VEHICLE_ID) return nullptr;
                CVehicle* v = pool->GetAt(id);
                if (!v || !v->m_pVehicle) return nullptr;
                if (idOut) *idOut = (int)id;
                if (isDriver) *isDriver = ((void*)v->m_pVehicle->pDriver == (void*)p->m_pPed);
                return v;
        }

        void Update()
        {
                if (!pNetGame || !pNetGame->GetVehiclePool()) return;
                CVehiclePool* pool = pNetGame->GetVehiclePool();
                const double now = Now();

                // 1) serverden gelen vezyeti masinlara tetbiq et (masin yeniden yaranibsa yene)
                for (std::map<int, St>::iterator it = s_state.begin(); it != s_state.end(); ++it)
                {
                        CVehicle* v = pool->GetAt((VEHICLEID)it->first);
                        if (!v || !v->m_pVehicle) continue;
                        const uint32_t ct = (uint32_t)v->m_pVehicle->m_nCreationTime;
                        std::pair<CVehicleGTA*, uint32_t>& ap = s_applied[it->first];
                        if (ap.first != v->m_pVehicle || ap.second != ct) {
                                ap = std::make_pair(v->m_pVehicle, ct);
                                Apply(v->m_pVehicle, it->second);
                        }
                }

                // 2) oz masinimiz (yalniz suruculukde): deyisiklik olsa servere gonder
                static int s_prevId = -2;
                bool drv = false; int id = -1;
                CVehicle* lv = LocalVeh(&drv, &id);
                if (!lv || !drv) { s_prevId = -2; return; }

                St cur;
                uint8_t r = 0, g = 0, b = 0;
                if (CCoronas::GetHeadlightRGB(lv->m_pVehicle, r, g, b)) cur.rgb = ((int)r << 16) | ((int)g << 8) | (int)b;
                cur.sig = (int)VehControls::GetSignal(lv->m_pVehicle);

                if (id != s_prevId) { s_prevId = id; s_sent[id] = cur; return; }   // yeni masin: hazirki vezyet baza olur

                std::map<int, St>::iterator si = s_sent.find(id);
                if (si != s_sent.end() && si->second.rgb == cur.rgb && si->second.sig == cur.sig) return;
                if (now - s_lastSend < 0.2) return;

                s_lastSend = now;
                s_sent[id] = cur;
                char cmd[64];
                snprintf(cmd, sizeof(cmd), "/_vt %d %d %d", id, cur.rgb, cur.sig);
                pNetGame->SendChatCommand(cmd);
        }

        bool OnMessage(const std::string& msg)
        {
                if (msg.size() < 4 || msg.compare(0, 4, "~vt~") != 0) return false;
                int id = -1, rgb = -1, sig = 0;
                if (sscanf(msg.c_str() + 4, "%d %d %d", &id, &rgb, &sig) == 3 && id > 0 && id < MAX_VEHICLES)
                {
                        St s;
                        s.rgb = (rgb >= 0 && rgb <= 0xFFFFFF) ? rgb : -1;
                        s.sig = (sig >= 0 && sig < 8) ? sig : 0;
                        s_state[id] = s;
                        s_applied.erase(id);   // novbeti kadrda tetbiq olunsun
                        s_sent[id] = s;        // echo olmasin
                }
                return true;
        }
}

bool VtSync_OnMessage(const std::string& m) { return VtSync::OnMessage(m); }
void VtSync_Update() { VtSync::Update(); }

// ============================================================================
//  MTA stilli dairevi speedometr. Sinyal UI legv edilib (hele ki).
//  Uzun basma = reng deyisimi (kHudPalette). Benzin dairesi YOXDUR:
//  client-de yanacaq melumati yoxdur (serverin textdrawidir).
// ============================================================================
void RenderVehicleHud()
{
        VtSync_Update();   // VTSYNC_V5
        HudCfgLoad();
        if (g_bHideAllUI || g_bShowHandlingDlg) return;

        CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;
        if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) return;

        CVehicle* pVeh = nullptr;
        if (pNetGame && pNetGame->GetVehiclePool() && pLocalPlayer->m_pPed) {
                CVehiclePool* pPool = pNetGame->GetVehiclePool();
                VEHICLEID vehID = pPool->FindIDFromGtaPtr(pLocalPlayer->m_pPed->pVehicle);
                if (vehID != INVALID_VEHICLE_ID) pVeh = pPool->GetAt(vehID);
        }
        if (!pVeh || !pVeh->m_pVehicle) return;
        CVehicleGTA* gtaVeh = pVeh->m_pVehicle;

        ImGuiIO& io = ImGui::GetIO();
        const ImVec2 disp = io.DisplaySize;
        const float u = disp.y / 720.0f;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();

        const float r = 92.0f * u;
        const ImVec2 c(disp.x * 0.66f, disp.y - r - 14.0f * u);

        // uzun basma ile reng deyisimi
        const bool inRect = g_mmTouch.x >= c.x - r && g_mmTouch.x <= c.x + r &&
                            g_mmTouch.y >= c.y - r && g_mmTouch.y <= c.y + r;
        if (g_mmTouch.down && inRect) {
                s_hudHolding = true;
                s_hudHoldT += 1.0f / 60.0f;
                if (s_hudHoldT > 0.55f) { s_hudColorIdx = (s_hudColorIdx + 1) % 5; s_hudHoldT = 0.0f; }
        } else {
                s_hudHolding = false;
                s_hudHoldT = 0.0f;
        }
        const ImU32 accent = kHudPalette[s_hudColorIdx];

        // ---- melumat ----
        const float mvx = gtaVeh->m_vecMoveSpeed.x, mvy = gtaVeh->m_vecMoveSpeed.y, mvz = gtaVeh->m_vecMoveSpeed.z;
        const float kmh = sqrtf(mvx * mvx + mvy * mvy + mvz * mvz) * 180.0f;
        const float curSpd = gtaVeh->m_pHandlingData ? gtaVeh->m_pHandlingData->m_transmissionData.m_fCurrentSpeed : 0.0f;

        char gear[4];
        if (kmh < 1.5f)         snprintf(gear, sizeof(gear), "N");
        else if (curSpd < -0.01f) snprintf(gear, sizeof(gear), "R");
        else                    snprintf(gear, sizeof(gear), "%d", gtaVeh->m_nCurrentGear < 1 ? 1 : (int)gtaVeh->m_nCurrentGear);

        const bool eng = gtaVeh->m_nVehicleFlags.bEngineOn != 0;
        const bool lig = gtaVeh->m_nVehicleFlags.bLightsOn != 0;
        const int  lk  = (int)gtaVeh->m_nDoorLock;
        const bool locked = (lk == 2 || lk == 3);

        static float s_dialShown = 0.0f;
        float f = kmh / 270.0f;
        if (f > 1.0f) f = 1.0f;
        s_dialShown += (f - s_dialShown) * 0.2f;

        // yol sayaci (sessiya, metr)
        static CVehicleGTA* s_odoVeh = nullptr;
        static double s_odo = 0.0;
        if (s_odoVeh != gtaVeh) { s_odoVeh = gtaVeh; s_odo = 0.0; }
        s_odo += (double)(kmh / 3.6f) * (double)io.DeltaTime;
        char odo[16];
        snprintf(odo, sizeof(odo), "%07d", (int)(s_odo > 9999999.0 ? 9999999.0 : s_odo));

        auto ctext = [&](float x, float y, float size, ImU32 col, const char* s) {
                const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
                dl->AddText(font, size, ImVec2(x - sz.x * 0.5f, y), col, s);
        };

        // ---- cizim ----
        {
                static int s_savedClr = -1;
                if (s_savedClr != s_hudColorIdx) { if (s_savedClr != -1) HudCfgSave(); s_savedClr = s_hudColorIdx; }
        }
        if (s_hudStyle == 1) { HudFlow(dl, font, disp, u, kmh, gear, eng, lig, !locked, accent, s_dialShown, odo, VehControls::GetSignal(gtaVeh)); return; }
        if (s_hudStyle == 2) { HudGlass(dl, font, disp, u, kmh, gear, eng, lig, !locked, accent, s_dialShown, odo, VehControls::GetSignal(gtaVeh)); return; }

        const float a0 = 3.14159265f * 0.75f;
        const float sweep = 3.14159265f * 1.5f;

        dl->AddCircleFilled(c, r * 1.04f, IM_COL32(8, 10, 16, 170), 48);
        dl->AddCircle(c, r * 1.04f, IM_COL32(255, 255, 255, 28), 48, 1.4f * u);

        // esas qovs
        dl->PathArcTo(c, r * 0.92f, a0, a0 + sweep, 48);
        dl->PathStroke(IM_COL32(70, 76, 92, 200), 0, 5.0f * u);
        // qirmizi zona
        dl->PathArcTo(c, r * 0.92f, a0 + sweep * 0.78f, a0 + sweep, 16);
        dl->PathStroke(IM_COL32(235, 60, 80, 235), 0, 5.0f * u);
        // dolu hisse
        if (s_dialShown > 0.003f) {
                dl->PathArcTo(c, r * 0.92f, a0, a0 + sweep * s_dialShown, 48);
                dl->PathStroke(accent, 0, 5.0f * u);
        }

        // kicik cizgiler
        for (int k = 0; k <= 27; ++k) {
                if (k % 3 == 0) continue;
                const float a = a0 + sweep * (k / 27.0f);
                dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.84f, c.y + sinf(a) * r * 0.84f),
                            ImVec2(c.x + cosf(a) * r * 0.88f, c.y + sinf(a) * r * 0.88f),
                            IM_COL32(150, 154, 168, 180), 1.0f * u);
        }
        // boyuk cizgiler + reqemler (hər biri 30 km/h)
        for (int i = 0; i <= 9; ++i) {
                const float a = a0 + sweep * (i / 9.0f);
                const float ca = cosf(a), sa = sinf(a);
                dl->AddLine(ImVec2(c.x + ca * r * 0.80f, c.y + sa * r * 0.80f),
                            ImVec2(c.x + ca * r * 0.88f, c.y + sa * r * 0.88f),
                            IM_COL32(215, 219, 232, 230), 1.8f * u);
                char lb[8];
                snprintf(lb, sizeof(lb), "%d", i * 30);
                const float ts = 9.5f * u;
                const ImVec2 tsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, lb);
                dl->AddText(font, ts, ImVec2(c.x + ca * r * 0.67f - tsz.x * 0.5f, c.y + sa * r * 0.67f - tsz.y * 0.5f),
                            IM_COL32(200, 204, 218, 230), lb);
        }

        // ibre
        const float na = a0 + sweep * s_dialShown;
        dl->AddLine(ImVec2(c.x + cosf(na) * r * 0.20f, c.y + sinf(na) * r * 0.20f),
                    ImVec2(c.x + cosf(na) * r * 0.80f, c.y + sinf(na) * r * 0.80f),
                    IM_COL32(255, 255, 255, 240), 2.4f * u);

        // merkez: pillə (viteS)
        dl->AddCircleFilled(c, r * 0.22f, IM_COL32(14, 16, 24, 240), 28);
        dl->AddCircle(c, r * 0.22f, accent, 28, 1.8f * u);
        ctext(c.x, c.y - r * 0.13f, r * 0.26f, IM_COL32(255, 255, 255, 255), gear);

        // gostericiler (M = motor, F = fara, Q = qapi)
        auto chip = [&](float x, float y, bool on, const char* t) {
                const float cr = 9.0f * u;
                dl->AddCircleFilled(ImVec2(x, y), cr, on ? accent : IM_COL32(60, 64, 76, 210), 20);
                const float ts = 9.0f * u;
                const ImVec2 s = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, t);
                dl->AddText(font, ts, ImVec2(x - s.x * 0.5f, y - s.y * 0.5f),
                            on ? IM_COL32(10, 12, 18, 255) : IM_COL32(170, 174, 186, 255), t);
        };
        chip(c.x - r * 0.36f, c.y - r * 0.50f, eng, "M");
        chip(c.x,             c.y - r * 0.58f, lig, "F");
        chip(c.x + r * 0.36f, c.y - r * 0.50f, !locked, "Q");

        // sur'et
        char sp[8];
        snprintf(sp, sizeof(sp), "%d", (int)(kmh + 0.5f));
        ctext(c.x, c.y + r * 0.22f, r * 0.42f, IM_COL32(255, 255, 255, 255), sp);
        ctext(c.x, c.y + r * 0.66f, r * 0.14f, accent, "KM/H");
        ctext(c.x, c.y + r * 0.80f, r * 0.12f, IM_COL32(190, 194, 208, 220), odo);
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

        draw(renderer());

        RenderVehicleHud();
        RenderHandlingDialog();   // mod menyu hamisinin ustunde
}

// Toxunus mod menyuya "tutulubmu" (basma panelin icinde baslayibsa, barmaq disari cixsa da eyni toxunus bitene qeder tutulur)
static bool s_dlgCapture      = false;   // cari toxunus paneldedir
static bool s_dlgCapturedEvent = false;  // SON hadise menyuya aiddir (hooks.cpp bunu oxuyur)

bool UI_HandlingDlgCapturedEvent()
{
        return s_dlgCapturedEvent;
}

// hooks.cpp -> AND_TouchEvent_hook bu funksiyani cagirir (pUI->touchEvent(...))
void UI::touchEvent(const ImVec2& pos, TouchType type)
{
        const int tt = (type == TouchType::push) ? TOUCH_PUSH : ((type == TouchType::pop) ? TOUCH_POP : TOUCH_MOVE);

        if (tt != TOUCH_MOVE)
                HND_LOG("[HND] touch type=%d x=%.0f y=%.0f dlg=%d", tt, pos.x, pos.y, (int)g_bShowHandlingDlg);

        // "Hamisini gizlet" rejimi: ekrana toxunanda UI qayidir (mod menyu aciq deyilse)
        if (g_bHideAllUI && !g_bShowHandlingDlg) {
                // gizli rejim: yalniz CHAT sahesine basanda UI qayidir, qalan toxunuslar alt widget-lere getmir
                const ImVec2 dsz = ImGui::GetIO().DisplaySize;
                const bool hitChat = (pUI && pUI->chat() && pUI->chat()->contains(pos)) ||
                                     (pos.x >= dsz.x * 0.15f && pos.x <= dsz.x * 0.72f && pos.y <= dsz.y * 0.34f);
                if (tt == TOUCH_PUSH && hitChat) {
                        g_bHideAllUI = false;
                        if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz berpa edildi.");
                }
                s_dlgCapturedEvent = false;
                return;
        }

        // Mod menyu ucun her toxunusu queue-ya at
        PushTouch(tt, pos.x, pos.y);

        if (tt == TOUCH_PUSH) s_dlgCapture = UI_IsPointInHandlingDlg(pos);
        s_dlgCapturedEvent = s_dlgCapture;
        if (tt == TOUCH_POP) s_dlgCapture = false;

        // Mod menyunun uzerine basanda alt widget-ler (chat, buttonpanel ...) toxunusu almasin
        if (s_dlgCapturedEvent || UI_IsPointInHandlingDlg(pos))
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

// DIQQET: hooks.cpp-de bu funksiyanin cagirisi serhe alinib, real giris UI::touchEvent-dir.
// Eger bir gun hook-da OnTouchEvent aktiv edilse, UI::touchEvent ile cift toxunus gelmesin.
bool UI::OnTouchEvent(int type, bool multi, int x, int y)
{
        (void)multi;

        if (type != TOUCH_MOVE)
                HND_LOG("[HND] touch type=%d x=%d y=%d dlg=%d", type, x, y, (int)g_bShowHandlingDlg);

        // "Hamisini gizlet" rejimi: ekrana toxunanda UI qayidir (mod menyu aciq deyilse)
        if (g_bHideAllUI && !g_bShowHandlingDlg) {
                // gizli rejim: yalniz CHAT sahesine basanda UI qayidir
                const ImVec2 tp((float)x, (float)y);
                const ImVec2 dsz = ImGui::GetIO().DisplaySize;
                const bool hitChat = (pUI && pUI->chat() && pUI->chat()->contains(tp)) ||
                                     (tp.x >= dsz.x * 0.15f && tp.x <= dsz.x * 0.72f && tp.y <= dsz.y * 0.34f);
                if (type == TOUCH_PUSH && hitChat) {
                        g_bHideAllUI = false;
                        if (pUI && pUI->chat()) pUI->chat()->addDebugMessage("{00FF00}[Client]: Arayuz berpa edildi.");
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

// AZ_PATCH4

// ===== MM2_V2: iOS stili /mm menyusu (shushe panel, yay animasiyasi, scroll siyahi) =====
bool HndExt_Get(CVehicle*, const char*, float*);
bool HndExt_Set(CVehicle*, const char*, float);
bool HndExt_Reset(CVehicle*);
bool HndExt_SlotUsed(int);
bool HndExt_SaveSlot(CVehicle*, int);
bool HndExt_LoadSlot(CVehicle*, int);
bool HndExt_ImportCodeFile(CVehicle*);
std::string HndExt_GetCode(CVehicle*);
bool HndExt_ApplyFileSlot(CVehicle*, int, std::string&);   // HNDV3

// ===== HNDBAR_V5 =====
#include "../java/jniutil.h"
extern CJavaWrapper* pJavaWrapper;
std::string HndExt_SlotPath(int slot);   // vehicle.cpp

namespace mm2
{
    static char  s_msg[112] = "";
    static float s_msgT = 0.0f;

    static void SetMsg(const std::string& m)
    {
        snprintf(s_msg, sizeof(s_msg), "%s", m.c_str());
        s_msgT = 1.0f;
    }

    static CVehicle* LocalVeh()
    {
        CPlayerPed* p = pGame ? pGame->FindPlayerPed() : nullptr;
        if (!p || !p->IsInVehicle() || !p->m_pPed) return nullptr;
        if (!pNetGame || !pNetGame->GetVehiclePool()) return nullptr;
        CVehiclePool* pool = pNetGame->GetVehiclePool();
        const VEHICLEID id = pool->FindIDFromGtaPtr(p->m_pPed->pVehicle);
        if (id == INVALID_VEHICLE_ID) return nullptr;
        CVehicle* v = pool->GetAt(id);
        return (v && v->m_pVehicle) ? v : nullptr;
    }

    struct HF { const char* key; const char* label; const char* fmt; };
    static const HF kHF[] = {
        { "mass",          "Çəki",                          "%.1f" },
        { "turnmass",      "Fırlanma kütləsi",              "%.1f" },
        { "drag",          "Hava müqaviməti",               "%.4f" },
        { "comx",          "Ağırlıq mərkəzi X",             "%.3f" },
        { "comy",          "Ağırlıq mərkəzi Y",             "%.3f" },
        { "comz",          "Ağırlıq mərkəzi Z",             "%.3f" },
        { "buoyancy",      "Üzmə qüvvəsi",                  "%.3f" },
        { "traction",      "Yol tutuş",                     "%.3f" },
        { "accel",         "Təcil",                         "%.4f" },
        { "maxspeed",      "Maksimum sürət",                "%.4f" },
        { "brake",         "Tormoz gücü",                   "%.4f" },
        { "brakebias",     "Tormoz balansı (ön/arxa)",      "%.3f" },
        { "steering",      "Sükan bucağı",                  "%.3f" },
        { "tractionloss",  "Sürüşmə itkisi",                "%.3f" },
        { "tractionbias",  "Yol tutuş balansı (ön/arxa)",   "%.3f" },
        { "suspforce",     "Süspansiyon gücü",              "%.3f" },
        { "suspdamp",      "Süspansiyon amortizatoru",      "%.3f" },
        { "suspdampspeed", "Süspansiyon sürət sönməsi",     "%.3f" },
        { "suspupper",     "Süspansiyon yuxarı hədd",       "%.3f" },
        { "susplower",     "Süspansiyon aşağı hədd",        "%.3f" },
        { "suspbias",      "Süspansiyon balansı (ön/arxa)", "%.3f" },
        { "suspantidive",  "Anti-dive (burun enməsi)",      "%.3f" },
        { "collision",     "Toqquşma zərəri",               "%.3f" },
        { "seatoffset",    "Oturacaq məsafəsi",             "%.3f" },
    };
    static const int kHFN = (int)(sizeof(kHF) / sizeof(kHF[0]));

    struct EditCaller : public Widget
    {
        std::string key;
        std::string label;
        virtual void keyboardEvent(const std::string& input) override
        {
            CVehicle* v = LocalVeh();
            if (!v || key.empty()) { SetMsg("Avtomobil tapılmadı"); return; }
            std::string t = input;
            for (size_t i = 0; i < t.size(); ++i) if (t[i] == ',') t[i] = '.';
            char* end = nullptr;
            const float f = strtof(t.c_str(), &end);
            if (end == t.c_str()) { SetMsg("Yanlış dəyər"); return; }
            if (HndExt_Set(v, key.c_str(), f)) SetMsg(label + " = " + t);
            else SetMsg("Dəyər qəbul edilmədi");
        }
    };
    static EditCaller* s_caller = nullptr;

    // MMV3_LAYOUT: panelin olcusu/yeri SAMP/mm_layout.txt faylinda saxlanir
    static void LayoutLoad(float& z, float& cx, float& cy)
    {
        if (!g_pszStorage) return;
        char p[0x100];
        snprintf(p, sizeof(p), "%sSAMP/mm_layout.txt", g_pszStorage);
        FILE* f = fopen(p, "r");
        if (!f) return;
        float a = 1.0f, b = -1.0f, c = -1.0f;
        if (fscanf(f, "%f %f %f", &a, &b, &c) == 3 && a > 0.2f && a < 3.0f) { z = a; cx = b; cy = c; }
        fclose(f);
    }

    static void LayoutSave(float z, float cx, float cy)
    {
        if (!g_pszStorage) return;
        char p[0x100];
        snprintf(p, sizeof(p), "%sSAMP/mm_layout.txt", g_pszStorage);
        FILE* f = fopen(p, "w");
        if (!f) return;
        fprintf(f, "%.4f %.1f %.1f\n", z, cx, cy);
        fclose(f);
    }

    static float EaseOutBack(float t)
    {
        const float c1 = 1.0f, c3 = c1 + 1.0f;
        const float p = t - 1.0f;
        return 1.0f + c3 * p * p * p + c1 * p * p;
    }

    static void Hair(float x0, float x1, float y, int a = 22)
    {
        mm::s_dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), mm::Col(255, 255, 255, a), mm::S(1.0f));
    }

    // sheshe (frosted glass) panel
    static void Glass(const mm::Rc& P, float rad)
    {   // MM_V8: duz qara panel (shuseli/qradient yoxdur)
        using namespace mm;
        (void)rad;
        const float r = S(10.0f);
        Fill(Rc{ P.x - S(2.0f), P.y - S(2.0f), P.w + S(4.0f), P.h + S(4.0f) }, Col(0, 0, 0, 90), r + S(2.0f));
        Fill(P, Col(12, 12, 12, 250), r);
        Stroke(P, Col(255, 255, 255, 46), r, S(1.0f));
    }

    static void EmptyState(const mm::Rc& r, const char* title, const char* sub)
    {
        using namespace mm;
        Fill(r, Col(255, 255, 255, 12), S(18.0f));
        Dot(r.x + r.w * 0.5f, r.y + r.h * 0.5f - S(34.0f), S(26.0f), Col(255, 255, 255, 18));
        TxtC(S(26.0f), Rc{ r.x + r.w * 0.5f - S(26.0f), r.y + r.h * 0.5f - S(60.0f), S(52.0f), S(52.0f) }, Col(160, 168, 190), "!", true);
        TxtC(S(21.0f), Rc{ r.x, r.y + r.h * 0.5f + S(2.0f), r.w, S(28.0f) }, Col(236, 240, 250), title, true);
        TxtC(S(14.0f), Rc{ r.x, r.y + r.h * 0.5f + S(32.0f), r.w, S(20.0f) }, Col(130, 138, 160), sub, false);
    }

    // iOS acari
    static bool SwitchRow(int idx, const mm::Rc& row, const char* label, const char* sub, bool on, float dt, bool canInput)
    {
        using namespace mm;
        const int id = ID_TOGGLE + idx;
        const bool tapped = Tap(id, row, canInput);
        const float p = Prs(id, dt);
        s_sw[idx] = Smooth(s_sw[idx], on ? 1.0f : 0.0f, dt, 16.0f);
        const float sw = s_sw[idx];

        if (p > 0.02f) Fill(row, Col(255, 255, 255, (int)(16.0f * p)), 0.0f);
        Txt(S(19.0f), row.x + S(18.0f), row.y + S(8.0f), Col(238, 241, 250), label);
        Txt(S(13.0f), row.x + S(18.0f), row.y + S(32.0f), Col(128, 136, 158), sub);

        const Rc tr{ row.x + row.w - S(18.0f) - S(54.0f), row.y + (row.h - S(32.0f)) * 0.5f, S(54.0f), S(32.0f) };
        Fill(tr, Col((int)Lerp(72.0f, 52.0f, sw), (int)Lerp(76.0f, 199.0f, sw), (int)Lerp(92.0f, 89.0f, sw), 255), S(16.0f));
        const float kx = tr.x + S(16.0f) + sw * (tr.w - S(32.0f));
        Dot(kx, tr.y + tr.h * 0.5f + S(1.5f), S(14.0f), Col(0, 0, 0, 50));
        Dot(kx, tr.y + tr.h * 0.5f, S(13.0f) + S(1.0f) * p, Col(255, 255, 255, 255));
        return tapped;
    }

    static bool SmallBtn(int id, const mm::Rc& r, const char* label, float dt, bool canInput, bool primary)
    {
        using namespace mm;
        const bool hit = Tap(id, r, canInput);
        const float p = Prs(id, dt);
        const Rc d = Shrink(r, S(1.5f) * p);
        Fill(d, primary ? Col(238, 238, 238, 225 + (int)(30.0f * p)) : Col(255, 255, 255, 20 + (int)(30.0f * p)), S(12.0f));
        TxtC(S(15.0f), d, primary ? Col(12, 12, 12) : Col(244, 244, 244), label, true);
        return hit;
    }
}

// ===== GARAGE_V5: qaraj kamerasi + tuning =====
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

// ===== PLATEADJ_V7: nomre sehifesi + yerlesdirme rejimi =====
#include "../game/AzPlate.h"
extern ImFont* g_pPlateFont;

namespace PlateUi
{
    struct PlateCaller : public Widget
    {
        virtual void keyboardEvent(const std::string& input) override
        {
            CVehicle* v = mm2::LocalVeh();
            if (!v || !v->m_pVehicle) { mm2::SetMsg("Avtomobil tapılmadı"); return; }
            std::string out;
            if (!Plate::Normalize(input, out)) { mm2::SetMsg("Format: 10 AA 123"); return; }
            Plate::Set(v->m_pVehicle, out);
            mm2::SetMsg("Nömrə: " + out);
        }
    };
    static PlateCaller* s_caller = nullptr;
}

namespace PlateAdj
{
    bool  g_on = false;
    static bool  s_front = true, s_fine = true, s_drag = false;
    static float s_phi = 0.0f, s_theta = 0.12f, s_dist = 1.5f, s_lx = 0.0f, s_ly = 0.0f;
    static char  s_note[96] = "";
    static float s_noteT = 0.0f;

    static void Note(const char* t) { snprintf(s_note, sizeof(s_note), "%s", t); s_noteT = 1.0f; }

    static void Begin(CVehicleGTA* v, bool front)
    {
        if (!v) return;
        s_front = front; s_phi = 0.0f; s_theta = 0.12f; s_dist = 1.5f; s_drag = false;
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

        s_dl = ImGui::GetForegroundDrawList();
        s_A = 1.0f;
        s_U = std::min(disp.y / 720.0f, disp.x / 1000.0f);
        s_noteT = Clamp(s_noteT - dt / 3.0f, 0.0f, 1.0f);
        const MMTouch& t = g_mmTouch;

        const float bh = S(262.0f);
        const Rc bar{ disp.x * 0.5f - S(480.0f), disp.y - bh - S(8.0f), S(960.0f), bh };

        // kamera: bosh sahede surusdur = plakanin ətrafında fırlat
        if (t.pressed && s_act == 0 && !In(bar, t.x, t.y)) { s_drag = true; s_lx = t.x; s_ly = t.y; }
        if (s_drag) {
            if (t.down) {
                s_phi   -= (t.x - s_lx) * 0.005f;
                s_theta += (t.y - s_ly) * 0.004f;
                s_lx = t.x; s_ly = t.y;
                s_phi   = Clamp(s_phi, -0.95f, 0.95f);   // PLATE_V8
                s_theta = Clamp(s_theta, 0.0f, 0.9f);
            } else s_drag = false;
        }

        float pos[3], n[3], up[3];
        if (!Plate::WorldPose(v, s_front, pos, n, up)) { End(); return; }
        const float sd[3] = { up[1] * n[2] - up[2] * n[1], up[2] * n[0] - up[0] * n[2], up[0] * n[1] - up[1] * n[0] };
        const float ct = cosf(s_theta), st = sinf(s_theta), cp = cosf(s_phi), sp = sinf(s_phi);
        float cam[3];
        for (int i = 0; i < 3; ++i)
            cam[i] = pos[i] + (n[i] * cp * ct + sd[i] * sp * ct + up[i] * st) * s_dist;
        ScriptCommand(&set_camera_position, cam[0], cam[1], cam[2], 0.0f, 0.0f, 0.0f);
        ScriptCommand(&point_camera, pos[0], pos[1], pos[2], 2);

        // panel
        Fill(bar, Col(20, 22, 30, 238), S(22.0f));
        Stroke(bar, Col(255, 255, 255, 40), S(22.0f), S(1.0f));
        TxtB(S(20.0f), bar.x + S(22.0f), bar.y + S(12.0f), Col(240, 243, 250), "Nömrə yerləşdirmə");
        Txt(S(13.0f), bar.x + S(250.0f), bar.y + S(17.0f), Col(124, 132, 152), "Boş yerdə sürüşdür = kameranı fırlat");
        if (s_noteT > 0.01f)
            Txt(S(13.0f), bar.x + bar.w - S(310.0f), bar.y + S(17.0f), Col(120, 215, 160, (int)(255.0f * s_noteT)), s_note);

        PlateCfg c = Plate::GetCfg(v, s_front);
        bool changed = false;
        const float ps = s_fine ? 0.005f : 0.02f;   // metr
        const float rs = s_fine ? 0.5f : 2.0f;      // derece

        const float gap = S(12.0f);
        const float cwid = (bar.w - S(44.0f) - gap * 2.0f) / 3.0f;

        auto stepper = [&](int id, const Rc& cell, const char* label, const char* val) -> int {
            Fill(cell, Col(255, 255, 255, 16), S(14.0f));
            Txt(S(13.0f), cell.x + S(12.0f), cell.y + S(6.0f), Col(150, 158, 180), label);
            const Rc bm{ cell.x + S(8.0f), cell.y + cell.h - S(36.0f), S(40.0f), S(30.0f) };
            const Rc bp{ cell.x + cell.w - S(48.0f), cell.y + cell.h - S(36.0f), S(40.0f), S(30.0f) };
            int dir = 0;
            if (Tap(id, bm, true))     dir = -1;
            if (Tap(id + 1, bp, true)) dir = +1;
            const float p1 = Prs(id, dt), p2 = Prs(id + 1, dt);
            Fill(bm, Col(255, 255, 255, 24 + (int)(36.0f * p1)), S(9.0f));
            TxtC(S(20.0f), bm, Col(240, 244, 252), "-", true);
            Fill(bp, Col(255, 255, 255, 24 + (int)(36.0f * p2)), S(9.0f));
            TxtC(S(20.0f), bp, Col(240, 244, 252), "+", true);
            TxtC(S(16.0f), Rc{ cell.x + S(50.0f), bm.y, cell.w - S(100.0f), bm.h }, Col(236, 240, 250), val, true);
            return dir;
        };

        char vb[24];
        int d;
        const float r1 = bar.y + S(44.0f), r2 = bar.y + S(44.0f + 82.0f);
        const float colx[3] = { bar.x + S(22.0f), bar.x + S(22.0f) + cwid + gap, bar.x + S(22.0f) + (cwid + gap) * 2.0f };

        snprintf(vb, sizeof(vb), "%+.3f", c.pos[0]);
        d = stepper(140, Rc{ colx[0], r1, cwid, S(76.0f) }, "Sağ - Sol (X)", vb);
        if (d) { c.pos[0] += ps * (float)d; changed = true; }

        snprintf(vb, sizeof(vb), "%+.3f", c.pos[1]);
        d = stepper(142, Rc{ colx[1], r1, cwid, S(76.0f) }, "İrəli - Geri (Y)", vb);
        if (d) { c.pos[1] += ps * (float)d; changed = true; }

        snprintf(vb, sizeof(vb), "%+.3f", c.pos[2]);
        d = stepper(144, Rc{ colx[2], r1, cwid, S(76.0f) }, "Yuxarı - Aşağı (Z)", vb);
        if (d) { c.pos[2] += ps * (float)d; changed = true; }

        snprintf(vb, sizeof(vb), "%.1f", c.rot[2]);
        d = stepper(146, Rc{ colx[0], r2, cwid, S(76.0f) }, "Dönmə (sağa-sola)", vb);
        if (d) { c.rot[2] += rs * (float)d; changed = true; }

        snprintf(vb, sizeof(vb), "%.1f", c.rot[0]);
        d = stepper(148, Rc{ colx[1], r2, cwid, S(76.0f) }, "Əyilmə (önə-arxaya)", vb);
        if (d) { c.rot[0] += rs * (float)d; changed = true; }

        snprintf(vb, sizeof(vb), "%.3f", c.w);
        d = stepper(150, Rc{ colx[2], r2, cwid, S(76.0f) }, "Ölçü (en, metr)", vb);
        if (d) { c.w = Clamp(c.w + ps * (float)d, 0.2f, 1.2f); changed = true; }

        if (changed) Plate::SetCfg(v, s_front, c);

        // alt duymeler
        const float yb = bar.y + bar.h - S(46.0f);
        if (mm2::SmallBtn(160, Rc{ bar.x + S(22.0f),  yb, S(100.0f), S(36.0f) }, "Qabaq", dt, true, s_front))  { s_front = true;  s_phi = 0.0f; }
        if (mm2::SmallBtn(161, Rc{ bar.x + S(130.0f), yb, S(100.0f), S(36.0f) }, "Arxa",  dt, true, !s_front)) { s_front = false; s_phi = 0.0f; }
        if (mm2::SmallBtn(162, Rc{ bar.x + S(238.0f), yb, S(170.0f), S(36.0f) }, s_fine ? "İncə (5 mm)" : "Kobud (2 sm)", dt, true, false)) s_fine = !s_fine;

        if (mm2::SmallBtn(163, Rc{ bar.x + bar.w - S(342.0f), yb, S(190.0f), S(36.0f) }, "Yadda saxla", dt, true, true)) {
            char nb[64];
            if (Plate::SaveCfg()) { snprintf(nb, sizeof(nb), "Model %d üçün yadda saxlandı", (int)v->m_nModelIndex); Note(nb); }
            else Note(Plate::LastError());
        }
        if (mm2::SmallBtn(164, Rc{ bar.x + bar.w - S(142.0f), yb, S(120.0f), S(36.0f) }, "Bağla", dt, true, false)) End();
        if (mm2::SmallBtn(165, Rc{ bar.x + bar.w - S(472.0f), yb, S(120.0f), S(36.0f) }, "Sıfırla", dt, true, false)) {
            Plate::ResetCfg(v, s_front);
            Note("Standarta qaytarıldı");
        }

        if (!g_mmTouch.down) s_act = 0;
    }
}

// ===== MM_V8: handling (MTA tipli), qaraj tuning, detallar =====
#include "../game/VehDetach.h"
#include <set>

namespace mm3
{
    struct Ctx {
        float cx0, y0, cw, dt;
        bool  in, noVeh;
        CVehicle*    veh;
        CVehicleGTA* g;
        mm::Rc       P;
        int          rail;
    };

    // ------------------------------------------------------------ umumi elementlər
    static int Repeat(int id, const mm::Rc& r, float dt, bool can)
    {
        using namespace mm;
        static float s_hold = 0.0f, s_acc = 0.0f;
        if (!can) return 0;
        const MMTouch& t = g_mmTouch;
        int fire = 0;
        if (t.pressed && s_act == 0 && In(r, t.x, t.y)) {
            s_act = id; s_hold = 0.0f; s_acc = 0.0f; fire = 1;
        } else if (s_act == id && t.down && In(r, t.x, t.y)) {
            s_hold += dt;
            if (s_hold > 0.4f) {
                s_acc += dt;
                const float per = (s_hold > 1.6f) ? 0.03f : 0.09f;
                while (s_acc >= per && fire < 8) { s_acc -= per; ++fire; }
            }
        }
        return fire;
    }

    static void Glyph(const mm::Rc& r, bool plus, bool hot)
    {
        using namespace mm;
        Fill(r, Col(255, 255, 255, hot ? 70 : 30), S(6.0f));
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, k = S(5.0f);
        s_dl->AddLine(ImVec2(cx - k, cy), ImVec2(cx + k, cy), Col(240, 240, 240, 240), S(2.0f));
        if (plus) s_dl->AddLine(ImVec2(cx, cy - k), ImVec2(cx, cy + k), Col(240, 240, 240, 240), S(2.0f));
    }

    static int AdjRow(int id, const mm::Rc& row, const char* label, const char* val, float dt, bool can)
    {
        using namespace mm;
        Fill(row, Col(255, 255, 255, 14), S(8.0f));
        Txt(S(15.0f), row.x + S(12.0f), row.y + (row.h - S(15.0f)) * 0.5f, Col(228, 228, 228), label);
        const float bh = row.h - S(6.0f), bw = S(34.0f), y = row.y + S(3.0f);
        const Rc pl{ row.x + row.w - bw - S(3.0f), y, bw, bh };
        const Rc vr{ pl.x - S(6.0f) - S(96.0f), y, S(96.0f), bh };
        const Rc mn{ vr.x - S(6.0f) - bw, y, bw, bh };
        int n = 0;
        n -= Repeat(id, mn, dt, can);
        n += Repeat(id + 1, pl, dt, can);
        Glyph(mn, false, s_act == id);
        Glyph(pl, true,  s_act == id + 1);
        Fill(vr, Col(0, 0, 0, 100), S(6.0f));
        TxtC(S(15.0f), vr, Col(255, 255, 255), val, true);
        return n;
    }

    static bool Tile(int id, const mm::Rc& r, const char* label, bool on, bool dim, bool can)
    {
        using namespace mm;
        const bool hit = Tap(id, r, can);
        Fill(r, on ? Col(240, 240, 240) : Col(255, 255, 255, s_act == id ? 42 : 18), S(7.0f));
        TxtC(S(14.0f), r, on ? Col(12, 12, 12) : (dim ? Col(96, 96, 96) : Col(222, 222, 222)), label, on);
        return hit;
    }

    static bool Chip(int id, const mm::Rc& r, const char* label, const char* sub, bool on, float dt, bool can, int anim)
    {
        using namespace mm;
        static float s_a[32] = {};
        const bool hit = Tap(id, r, can);
        float& a = s_a[anim & 31];
        a = Smooth(a, on ? 1.0f : 0.0f, dt, 16.0f);
        Fill(r, Col(255, 255, 255, s_act == id ? 26 : 14), S(8.0f));
        Txt(S(16.0f), r.x + S(12.0f), r.y + S(5.0f), Col(238, 238, 238), label);
        Txt(S(12.0f), r.x + S(12.0f), r.y + r.h - S(19.0f), Col(132, 132, 132), sub);
        const Rc tr{ r.x + r.w - S(54.0f), r.y + (r.h - S(24.0f)) * 0.5f, S(42.0f), S(24.0f) };
        const int lv = (int)Lerp(70.0f, 240.0f, a);
        Fill(tr, Col(lv, lv, lv), S(12.0f));
        Dot(tr.x + S(12.0f) + a * (tr.w - S(24.0f)), tr.y + tr.h * 0.5f, S(9.0f), on ? Col(20, 20, 20) : Col(200, 200, 200));
        return hit;
    }

    static void NoVeh(const Ctx& c, const char* sub)
    {
        mm2::EmptyState(mm::Rc{ c.cx0, c.y0, c.cw, mm::S(300.0f) }, "Avtomobildə deyilsiniz", sub);
    }

    static void DrawMsg(float x, float y)
    {
        using namespace mm;
        if (mm2::s_msgT > 0.01f) Txt(S(13.0f), x, y, Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);
    }

    // ------------------------------------------------------------ diaqnostika (fayla yazir)
    static void WriteDiag(const Ctx& c)
    {
        if (!g_pszStorage || !c.g || !c.veh) { mm2::SetMsg("Diaqnostika mümkün deyil"); return; }
        char p[0x100];
        snprintf(p, sizeof(p), "%sSAMP/mm_diag.txt", g_pszStorage);
        FILE* f = fopen(p, "w");
        if (!f) { mm2::SetMsg("Fayl yazıla bilmədi"); return; }
        CVehicleGTA* g = c.g;
        fprintf(f, "model=%d veh=%p creation=%u subtype=%d\n", (int)g->m_nModelIndex, (void*)g, (unsigned)g->m_nCreationTime, c.veh->GetVehicleSubtype());
        float mass = 0.0f, acc = 0.0f, mx = 0.0f;
        const bool ok = HndExt_Get(c.veh, "mass", &mass);
        HndExt_Get(c.veh, "accel", &acc);
        HndExt_Get(c.veh, "maxspeed", &mx);
        fprintf(f, "handling ptr=%p get(mass)=%d %.3f direct=%.3f CPhysical.mass=%.3f accel=%.4f maxspeed=%.4f\n",
                (void*)g->m_pHandlingData, ok ? 1 : 0, mass, g->m_pHandlingData ? g->m_pHandlingData->m_fMass : -1.0f, g->m_fMass, acc, mx);
        fprintf(f, "upgrades:");
        for (int i = 0; i < 15; ++i) fprintf(f, " %d", (int)g->m_anUpgrades[i]);
        fprintf(f, "\nnitroBoosts=%d wheelScale=%.3f\n", (int)g->m_nNitroBoosts, g->m_wheelScale);
        VehDetach::Diag(g, f);
        fclose(f);
        mm2::SetMsg("Yazıldı: SAMP/mm_diag.txt");
    }

    // ============================================================ HANDLING (MTA tipli: sətir = ad + dəyər + bar)
    struct HGrp { const char* name; int n; const char* keys[8]; };
    static const HGrp kHG[5] = {
        { "Kütlə",         5, { "mass", "turnmass", "drag", "collision", "buoyancy" } },
        { "Mühərrik",      4, { "accel", "maxspeed", "brake", "brakebias" } },
        { "Sükan / yol",   4, { "steering", "traction", "tractionloss", "tractionbias" } },
        { "Süspansiyon",   7, { "suspforce", "suspdamp", "suspdampspeed", "suspupper", "susplower", "suspbias", "suspantidive" } },
        { "Mərkəz",        4, { "comx", "comy", "comz", "seatoffset" } },
    };

    static int HIdx(const char* key)
    {
        for (int i = 0; i < mm2::kHFN; ++i) if (!strcmp(mm2::kHF[i].key, key)) return i;
        return -1;
    }

    static void HRange(const char* key, float base, float& lo, float& hi)
    {
        if (!strncmp(key, "com", 3)) { lo = -2.0f; hi = 2.0f; }
        else if (fabsf(base) < 1.0e-4f) { lo = -1.0f; hi = 1.0f; }
        else { const float a = base * 0.25f, b = base * 3.0f; lo = a < b ? a : b; hi = a < b ? b : a; }
    }

    static float HStep(const char* k, float cur)
    {
        if (!strncmp(k, "com", 3)) return 0.05f;
        const float a = fabsf(cur);
        return a > 1.0e-4f ? a * 0.05f : 0.01f;
    }

    static void PageHandling(const Ctx& c, int& slot)
    {
        using namespace mm;
        static int  s_grp = 0, s_edit = -1;
        static char s_buf[20] = "";
        static CVehicleGTA* s_bv = nullptr;
        static float s_base[32];
        if (c.noVeh || !c.veh || !c.g) {
            s_edit = -1; s_bv = nullptr;
            NoVeh(c, "Handling yalnız maşının içində dəyişdirilir");
            return;
        }
        CVehicle* pVeh = c.veh;
        const float dt = c.dt;
        const bool lin = c.in && s_edit < 0;

        if (c.g != s_bv) {                       // maşın dəyişdi / sıfırlandı: orijinal dəyərləri yadda saxla
            s_bv = c.g;
            s_edit = -1;
            for (int i = 0; i < mm2::kHFN && i < 32; ++i) {
                float cv = 0.0f;
                s_base[i] = HndExt_Get(pVeh, mm2::kHF[i].key, &cv) ? cv : 0.0f;
            }
        }

        // 1) slot + fayl
        for (int i = 0; i < 5; ++i) {
            const Rc b{ c.cx0 + S(40.0f) * i, c.y0, S(36.0f), S(30.0f) };
            if (Tap(80 + i, b, lin)) slot = i + 1;
            const bool on = (slot == i + 1);
            Fill(b, on ? Col(238, 238, 238) : Col(255, 255, 255, 18), S(8.0f));
            char nb[4];
            snprintf(nb, sizeof(nb), "%d", i + 1);
            TxtC(S(15.0f), b, on ? Col(12, 12, 12) : Col(200, 200, 200), nb, true);
            if (HndExt_SlotUsed(i + 1)) Dot(b.x + b.w - S(6.0f), b.y + S(6.0f), S(3.0f), Col(80, 220, 140));
        }
        static const char* const kAct[3] = { "Fayl seç", "Tətbiq et", "Sıfırla" };
        {
            const float bx = c.cx0 + S(208.0f), gap = S(8.0f);
            const float bw = (c.cw - S(208.0f) - gap * 2.0f) / 3.0f;
            for (int k = 0; k < 3; ++k) {
                const Rc b{ bx + k * (bw + gap), c.y0, bw, S(30.0f) };
                if (!mm2::SmallBtn(85 + k, b, kAct[k], dt, lin, k == 1)) continue;
                if (k == 0) {
                    if (pJavaWrapper) {
                        const std::string dst = HndExt_SlotPath(slot);
                        pJavaWrapper->PickHandlingFile(slot, dst.c_str());
                        mm2::SetMsg("handling.cfg faylını seçin → slot " + std::to_string(slot));
                    }
                } else if (k == 1) {
                    std::string m;
                    HndExt_ApplyFileSlot(pVeh, slot, m);
                    mm2::SetMsg(m);
                    s_bv = nullptr;
                } else {
                    mm2::SetMsg(HndExt_Reset(pVeh) ? "Standarta qaytarıldı" : "Dəyişiklik yoxdur");
                    s_bv = nullptr;
                }
            }
        }

        // 2) qruplar
        {
            const float dgw = S(112.0f);
            const float pw = (c.cw - dgw - S(8.0f) - S(4.0f) * 4.0f) / 5.0f;
            for (int i = 0; i < 5; ++i) {
                const Rc b{ c.cx0 + (pw + S(4.0f)) * (float)i, c.y0 + S(36.0f), pw, S(26.0f) };
                if (Tap(310 + i, b, lin)) s_grp = i;
                const bool on = (s_grp == i);
                Fill(b, on ? Col(238, 238, 238) : Col(255, 255, 255, 16), S(8.0f));
                TxtC(S(13.0f), b, on ? Col(12, 12, 12) : Col(190, 190, 190), kHG[i].name, on);
            }
            if (mm2::SmallBtn(316, Rc{ c.cx0 + c.cw - dgw, c.y0 + S(36.0f), dgw, S(26.0f) }, "Diaqnostika", dt, lin, false)) WriteDiag(c);
        }
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(8.0f));

        // 3) sətirlər
        const HGrp& G = kHG[s_grp];
        const float ry0 = c.y0 + S(68.0f), rh = S(48.0f), rstep = S(52.0f);
        const MMTouch& t = g_mmTouch;
        for (int k = 0; k < G.n; ++k) {
            const char* key = G.keys[k];
            const int r = HIdx(key);
            if (r < 0) continue;
            const Rc row{ c.cx0, ry0 + rstep * (float)k, c.cw, rh };
            float cur = 0.0f;
            const bool have = HndExt_Get(pVeh, key, &cur);

            const float by = row.y + S(37.0f);
            const Rc mn{ row.x + S(8.0f), by - S(9.0f), S(30.0f), S(18.0f) };
            const Rc pl{ row.x + row.w - S(38.0f), by - S(9.0f), S(30.0f), S(18.0f) };
            const float tx0 = row.x + S(48.0f), tx1 = row.x + row.w - S(48.0f);
            const Rc hit{ tx0 - S(10.0f), row.y + S(24.0f), (tx1 - tx0) + S(20.0f), S(24.0f) };
            const int idBase = 500 + r * 4;

            float lo, hi;
            HRange(key, s_base[r], lo, hi);

            if (lin && have) {
                const int n = Repeat(idBase + 1, pl, dt, true) - Repeat(idBase, mn, dt, true);
                if (n != 0) {
                    float v = cur;
                    const int cnt = n < 0 ? -n : n;
                    for (int q = 0; q < cnt; ++q) v += (n < 0 ? -1.0f : 1.0f) * HStep(key, v);
                    if (strncmp(key, "com", 3) != 0 && v < 0.0f) v = 0.0f;
                    if (!strcmp(key, "mass") && v < 1.0f) v = 1.0f;
                    HndExt_Set(pVeh, key, v);
                }
                if (t.pressed && s_act == 0 && In(hit, t.x, t.y)) s_act = idBase + 2;
                if (s_act == idBase + 2 && t.down) {
                    const float f = Clamp((t.x - tx0) / (tx1 - tx0), 0.0f, 1.0f);
                    HndExt_Set(pVeh, key, lo + (hi - lo) * f);
                }
                HndExt_Get(pVeh, key, &cur);
            }

            // çəkim
            Fill(row, Col(255, 255, 255, 12), S(8.0f));
            Txt(S(15.0f), row.x + S(10.0f), row.y + S(4.0f), Col(232, 232, 232), mm2::kHF[r].label);
            {
                char ob[40], ob2[64];
                snprintf(ob, sizeof(ob), mm2::kHF[r].fmt, s_base[r]);
                snprintf(ob2, sizeof(ob2), "orijinal %s", ob);
                Txt(S(11.0f), row.x + S(10.0f) + TW(S(15.0f), mm2::kHF[r].label) + S(10.0f), row.y + S(8.0f), Col(112, 112, 112), ob2);
            }
            char vb[32] = "-";
            if (have) snprintf(vb, sizeof(vb), mm2::kHF[r].fmt, cur);
            const Rc vbx{ row.x + row.w - S(124.0f), row.y + S(3.0f), S(114.0f), S(24.0f) };
            if (lin && have && Tap(idBase + 3, vbx, true)) { s_edit = r; snprintf(s_buf, sizeof(s_buf), "%.5g", cur); }
            Fill(vbx, Col(0, 0, 0, 130), S(6.0f));
            Stroke(vbx, Col(255, 255, 255, 60), S(6.0f), S(1.0f));
            TxtC(S(14.0f), vbx, Col(255, 255, 255), vb, true);

            Glyph(mn, false, s_act == idBase);
            Glyph(pl, true,  s_act == idBase + 1);
            Fill(Rc{ tx0, by - S(3.0f), tx1 - tx0, S(6.0f) }, Col(255, 255, 255, 40), S(3.0f));
            const float f = (hi > lo) ? Clamp((cur - lo) / (hi - lo), 0.0f, 1.0f) : 0.0f;
            if (have) {
                Fill(Rc{ tx0, by - S(3.0f), (tx1 - tx0) * f, S(6.0f) }, Col(235, 235, 235), S(3.0f));
                Dot(tx0 + (tx1 - tx0) * f, by, s_act == idBase + 2 ? S(10.0f) : S(8.0f), Col(255, 255, 255));
            }
        }

        // 4) menyu daxili rəqəm paneli
        if (s_edit >= 0) {
            Fill(c.P, Col(0, 0, 0, 150), S(10.0f));
            const Rc np{ c.P.x + (c.P.w - S(300.0f)) * 0.5f, c.P.y + S(112.0f), S(300.0f), S(344.0f) };
            Fill(np, Col(20, 20, 20, 252), S(12.0f));
            Stroke(np, Col(255, 255, 255, 60), S(12.0f), S(1.0f));
            TxtB(S(16.0f), np.x + S(16.0f), np.y + S(12.0f), Col(240, 240, 240), mm2::kHF[s_edit].label);
            const Rc fld{ np.x + S(16.0f), np.y + S(40.0f), np.w - S(32.0f), S(46.0f) };
            Fill(fld, Col(0, 0, 0, 140), S(8.0f));
            std::string shown = s_buf;
            shown += "|";
            TxtC(S(22.0f), fld, Col(255, 255, 255), shown.c_str(), true);

            static const char* const kKeys[15] = { "7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "-", "Sil", "Ləğv", "OK" };
            const float kw = (np.w - S(32.0f) - S(12.0f)) / 3.0f, kh = S(40.0f);
            for (int i = 0; i < 15; ++i) {
                const Rc kr{ np.x + S(16.0f) + (float)(i % 3) * (kw + S(6.0f)), np.y + S(98.0f) + (float)(i / 3) * (kh + S(6.0f)), kw, kh };
                if (!mm2::SmallBtn(400 + i, kr, kKeys[i], dt, c.in, i == 14)) continue;
                const char* k = kKeys[i];
                const size_t len = strlen(s_buf);
                if (i < 12) {
                    if (k[0] == '.') {
                        if (!strchr(s_buf, '.') && len < 14) {
                            if (len == 0 || (len == 1 && s_buf[0] == '-')) strcat(s_buf, "0");
                            strcat(s_buf, ".");
                        }
                    } else if (k[0] == '-') {
                        if (s_buf[0] == '-') memmove(s_buf, s_buf + 1, len);
                        else if (len < 14) { memmove(s_buf + 1, s_buf, len + 1); s_buf[0] = '-'; }
                    } else if (len < 14) {
                        s_buf[len] = k[0];
                        s_buf[len + 1] = 0;
                    }
                } else if (i == 12) {
                    if (len) s_buf[len - 1] = 0;
                } else if (i == 13) {
                    s_edit = -1;
                } else {
                    char* end = nullptr;
                    const float fv = strtof(s_buf, &end);
                    if (s_buf[0] == 0 || end == s_buf) mm2::SetMsg("Yanlış dəyər");
                    else if (HndExt_Set(pVeh, mm2::kHF[s_edit].key, fv)) mm2::SetMsg(std::string(mm2::kHF[s_edit].label) + " = " + s_buf);
                    else mm2::SetMsg("Dəyər qəbul edilmədi");
                    s_edit = -1;
                }
                break;
            }
        }
    }

    // ============================================================ TUNING: komponent cedvelleri (SA-MP standart ID-ler)
    struct PCat { const char* name; const int* ids; int n; };
#define MM_N(a) ((int)(sizeof(a) / sizeof((a)[0])))
    static const int kHood[]    = { 1004, 1005, 1011, 1012 };
    static const int kSpoiler[] = { 1000, 1001, 1002, 1003, 1014, 1015, 1016, 1023, 1049, 1050, 1058, 1060, 1138, 1139, 1146, 1147, 1158, 1162, 1163, 1164 };
    static const int kRoof[]    = { 1006, 1032, 1033, 1035, 1038, 1053, 1054, 1055, 1061, 1067, 1068, 1088, 1091, 1103, 1128, 1130 };
    static const int kSkirt[]   = { 1007, 1017, 1026, 1027, 1030, 1031, 1036, 1039, 1040, 1041, 1042, 1047, 1048, 1051, 1052, 1056, 1057, 1062, 1063, 1069, 1070, 1071, 1072, 1090, 1093, 1094, 1095, 1099, 1101, 1102, 1106, 1107, 1108, 1118, 1119, 1120, 1121, 1122, 1124, 1133, 1134, 1137 };
    static const int kFront[]   = { 1117, 1152, 1153, 1155, 1157, 1160, 1165, 1166, 1169, 1170, 1171, 1172, 1173, 1174, 1175, 1179, 1181, 1182, 1185, 1188, 1189, 1191 };
    static const int kRear[]    = { 1140, 1141, 1148, 1149, 1150, 1151, 1154, 1156, 1159, 1161, 1167, 1168, 1176, 1177, 1178, 1180, 1183, 1184, 1186, 1187, 1190, 1192, 1193 };
    static const int kExh[]     = { 1018, 1019, 1020, 1021, 1022, 1028, 1029, 1034, 1037, 1043, 1044, 1045, 1046, 1059, 1064, 1065, 1066, 1089, 1092, 1104, 1105, 1113, 1114, 1126, 1127, 1129, 1132, 1135, 1136 };
    static const int kVent[]    = { 1142, 1143, 1144, 1145 };
    static const int kExtra[]   = { 1013, 1024, 1100, 1109, 1110, 1115, 1116, 1123, 1125 };
    static const int kWid[17]   = { 1025, 1073, 1074, 1075, 1076, 1077, 1078, 1079, 1080, 1081, 1082, 1083, 1084, 1085, 1096, 1097, 1098 };
    static const char* const kWn[17] = { "Offroad", "Shadow", "Mega", "Rimshine", "Wires", "Classic", "Twist", "Cutter", "Switch", "Grove", "Import", "Dollar", "Trance", "Atomic", "Ahab", "Virtual", "Access" };
    static const int kNitro[3]  = { 1009, 1008, 1010 };
    static const int kHyd[1]    = { 1087 };
    static const int kStereo[1] = { 1086 };

    static const PCat kPC[9] = {
        { "Kapot", kHood, MM_N(kHood) }, { "Spoyler", kSpoiler, MM_N(kSpoiler) }, { "Dam", kRoof, MM_N(kRoof) },
        { "Yan pərdə", kSkirt, MM_N(kSkirt) }, { "Ön bufer", kFront, MM_N(kFront) }, { "Arxa bufer", kRear, MM_N(kRear) },
        { "Ekzoz", kExh, MM_N(kExh) }, { "Hava girişi", kVent, MM_N(kVent) }, { "Əlavə (duman fara, bullbar)", kExtra, MM_N(kExtra) },
    };

    static std::set<int> s_fail;
    static int s_failModel = -1;

    static int CurrentIn(CVehicleGTA* g, const int* ids, int n)
    {
        for (int i = 0; i < n; ++i) if (g->HasUpgrade(ids[i])) return ids[i];
        return 0;
    }

    // eyni qrupdakini sok, yenisini qoy; olmursa evvelkini qaytar. Hamisi skript (request -> load -> add) yolu ile
    static bool Install(CVehicle* v, CVehicleGTA* g, int id, const int* ids, int n)
    {
        const int cur = CurrentIn(g, ids, n);
        if (cur == id) { v->RemoveComponent(id); mm2::SetMsg("Sökülüb"); return true; }
        if (cur) v->RemoveComponent(cur);
        v->AddComponent(id);
        if (g->HasUpgrade(id)) { mm2::SetMsg("Qoşuldu"); return true; }
        s_fail.insert(id);
        if (cur) v->AddComponent(cur);
        mm2::SetMsg("Bu maşına uyğun deyil");
        return false;
    }

    static void PagePartGrid(const Ctx& c, const PCat& pc)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), pc.name);
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(6.0f));
        const int cur = CurrentIn(g, pc.ids, pc.n);
        if (mm2::SmallBtn(520, Rc{ c.cx0 + c.cw - S(110.0f), c.y0 - S(2.0f), S(110.0f), S(28.0f) }, "Sök", c.dt, c.in && cur != 0, false) && cur) {
            v->RemoveComponent(cur);
            mm2::SetMsg("Sökülüb");
        }
        const int cols = 8;
        const float gap = S(6.0f);
        const float tw = (c.cw - gap * (float)(cols - 1)) / (float)cols, th = S(38.0f);
        for (int i = 0; i < pc.n; ++i) {
            const Rc r{ c.cx0 + (float)(i % cols) * (tw + gap), c.y0 + S(38.0f) + (float)(i / cols) * (th + gap), tw, th };
            char lb[8];
            snprintf(lb, sizeof(lb), "%d", pc.ids[i]);
            if (Tile(600 + i, r, lb, g->HasUpgrade(pc.ids[i]), s_fail.count(pc.ids[i]) != 0, c.in))
                Install(v, g, pc.ids[i], pc.ids, pc.n);
        }
    }

    static void PagePaint(const Ctx& c)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Boya");
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(6.0f));
        int c1 = g->m_nPrimaryColor, c2 = g->m_nSecondaryColor;
        char b[12];
        snprintf(b, sizeof(b), "%d", c1);
        int n = AdjRow(700, Rc{ c.cx0, c.y0 + S(38.0f), c.cw, S(40.0f) }, "Əsas rəng (0 - 126)", b, c.dt, c.in);
        if (n != 0) { c1 = (int)Clamp((float)(c1 + n), 0.0f, 126.0f); v->SetColor((uint8_t)c1, (uint8_t)c2); }
        snprintf(b, sizeof(b), "%d", c2);
        n = AdjRow(703, Rc{ c.cx0, c.y0 + S(86.0f), c.cw, S(40.0f) }, "İkinci rəng (0 - 126)", b, c.dt, c.in);
        if (n != 0) { c2 = (int)Clamp((float)(c2 + n), 0.0f, 126.0f); v->SetColor((uint8_t)c1, (uint8_t)c2); }
        Txt(S(14.0f), c.cx0, c.y0 + S(146.0f), Col(190, 190, 190), "Paintjob");
        for (int i = 0; i < 3; ++i) {
            char lb[8];
            snprintf(lb, sizeof(lb), "%d", i + 1);
            if (Tile(710 + i, Rc{ c.cx0 + (S(70.0f) + S(6.0f)) * (float)i, c.y0 + S(170.0f), S(70.0f), S(38.0f) }, lb, false, false, c.in)) {
                v->SetPaintJob((uint8_t)i);
                mm2::SetMsg("Paintjob tətbiq olundu (modeldə varsa)");
            }
        }
    }

    static void PageWheels(const Ctx& c)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Disk və təkər");
        DrawMsg(c.cx0 + S(240.0f), c.y0 + S(6.0f));
        const int cur = CurrentIn(g, kWid, 17);
        if (mm2::SmallBtn(521, Rc{ c.cx0 + c.cw - S(110.0f), c.y0 - S(2.0f), S(110.0f), S(28.0f) }, "Sök", c.dt, c.in && cur != 0, false) && cur) {
            v->RemoveComponent(cur);
            mm2::SetMsg("Sökülüb");
        }
        const int cols = 6;
        const float gap = S(6.0f);
        const float tw = (c.cw - gap * (float)(cols - 1)) / (float)cols, th = S(38.0f);
        for (int i = 0; i < 17; ++i) {
            const Rc r{ c.cx0 + (float)(i % cols) * (tw + gap), c.y0 + S(38.0f) + (float)(i / cols) * (th + gap), tw, th };
            if (Tile(650 + i, r, kWn[i], g->HasUpgrade(kWid[i]), s_fail.count(kWid[i]) != 0, c.in))
                Install(v, g, kWid[i], kWid, 17);
        }

        static CVehicleGTA* s_bv = nullptr;
        static float s_baseLow = 0.0f, s_baseUp = 0.0f;
        if (g != s_bv) {
            s_bv = g;
            float lo = 0.0f, up = 0.0f;
            s_baseLow = HndExt_Get(v, "susplower", &lo) ? lo : 0.0f;
            s_baseUp  = HndExt_Get(v, "suspupper", &up) ? up : 0.0f;
        }
        char vb[32];
        const float yr = c.y0 + S(38.0f) + 3.0f * (th + gap) + S(14.0f);
        {
            float ws = g->m_wheelScale;
            if (ws < 0.05f || ws > 5.0f) ws = 1.0f;
            snprintf(vb, sizeof(vb), "%.2f", ws);
            const int n = AdjRow(720, Rc{ c.cx0, yr, c.cw, S(40.0f) }, "Təkər ölçüsü", vb, c.dt, c.in);
            if (n != 0) g->m_wheelScale = Clamp(ws + 0.05f * (float)n, 0.5f, 2.0f);
        }
        {
            float lo = 0.0f, up = 0.0f;
            const bool ok = HndExt_Get(v, "susplower", &lo) && HndExt_Get(v, "suspupper", &up);
            snprintf(vb, sizeof(vb), ok ? "%+.2f" : "-", lo - s_baseLow);
            const int n = AdjRow(723, Rc{ c.cx0, yr + S(48.0f), c.cw, S(40.0f) }, "Maşının hündürlüyü", vb, c.dt, c.in && ok);
            if (ok && n != 0) {
                const float st = 0.02f * (float)n;
                if (fabsf((lo - s_baseLow) + st) <= 0.4f) {
                    HndExt_Set(v, "susplower", lo + st);
                    HndExt_Set(v, "suspupper", up + st);
                }
            }
        }
        if (mm2::SmallBtn(522, Rc{ c.cx0, yr + S(96.0f), S(220.0f), S(34.0f) }, "Ölçü və hündürlüyü sıfırla", c.dt, c.in, false)) {
            g->m_wheelScale = 1.0f;
            HndExt_Set(v, "susplower", s_baseLow);
            HndExt_Set(v, "suspupper", s_baseUp);
            mm2::SetMsg("Sıfırlandı");
        }
    }

    static void PageSystems(const Ctx& c)
    {
        using namespace mm;
        CVehicle* v = c.veh; CVehicleGTA* g = c.g;
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Nitro / Hidravlika / Stereo");
        DrawMsg(c.cx0 + S(300.0f), c.y0 + S(6.0f));
        struct Q { int id; const char* name; const int* grp; int n; };
        static const Q kQ[5] = {
            { 1009, "Nitro x2", kNitro, 3 }, { 1008, "Nitro x5", kNitro, 3 }, { 1010, "Nitro x10", kNitro, 3 },
            { 1087, "Hidravlika", kHyd, 1 }, { 1086, "Stereo", kStereo, 1 } };
        for (int i = 0; i < 5; ++i) {
            const Rc r{ c.cx0, c.y0 + S(38.0f) + (S(50.0f) + S(6.0f)) * (float)i, c.cw, S(50.0f) };
            const bool has = g->HasUpgrade(kQ[i].id);
            char sub[24];
            snprintf(sub, sizeof(sub), "ID %d", kQ[i].id);
            if (Chip(390 + i, r, kQ[i].name, has ? "Qoşulub" : sub, has, c.dt, c.in, 16 + i))
                Install(v, g, kQ[i].id, kQ[i].grp, kQ[i].n);
        }
    }

    static void PageTuning(const Ctx& c)
    {
        if (c.noVeh || !c.veh || !c.g) { NoVeh(c, "Tuning yalnız maşının içində işləyir"); return; }
        if (s_failModel != (int)c.g->m_nModelIndex) { s_failModel = (int)c.g->m_nModelIndex; s_fail.clear(); }
        switch (c.rail) {
            case 0:  PagePaint(c); break;
            case 1:  PageWheels(c); break;
            case 11: PageSystems(c); break;
            default: if (c.rail >= 2 && c.rail <= 10) PagePartGrid(c, kPC[c.rail - 2]); break;
        }
    }

    // ============================================================ DETALLAR (sokme)
    static void PageDetach(const Ctx& c)
    {
        using namespace mm;
        if (c.noVeh || !c.veh || !c.g) { NoVeh(c, "Detallar yalnız maşının içində sökülür"); return; }
        CVehicleGTA* g = c.g;
        const float dt = c.dt;
        static CVehicleGTA* s_av = nullptr;
        static uint32_t s_ct = 0, s_mask = 0;
        if (g != s_av || (uint32_t)g->m_nCreationTime != s_ct) {
            s_av = g;
            s_ct = (uint32_t)g->m_nCreationTime;
            s_mask = 0;
            for (int p = 0; p < VehDetach::P_COUNT; ++p) if (VehDetach::Available(g, p)) s_mask |= (1u << p);
        }
        TxtB(S(17.0f), c.cx0, c.y0 + S(2.0f), Col(240, 240, 240), "Detalları sök");
        DrawMsg(c.cx0 + S(200.0f), c.y0 + S(6.0f));
        if (mm2::SmallBtn(420, Rc{ c.cx0 + c.cw - S(250.0f), c.y0 - S(2.0f), S(120.0f), S(28.0f) }, "Hamısını sök", dt, c.in, false)) VehDetach::HideAll(g);
        if (mm2::SmallBtn(421, Rc{ c.cx0 + c.cw - S(124.0f), c.y0 - S(2.0f), S(124.0f), S(28.0f) }, "Hamısını qaytar", dt, c.in, true)) VehDetach::ShowAll(g);

        int shown = 0;
        const float colW = (c.cw - S(8.0f)) * 0.5f, rh = S(44.0f), rg = S(6.0f);
        for (int p = 0; p < VehDetach::P_COUNT; ++p) {
            if (!((s_mask >> p) & 1u)) continue;
            const int col = shown / 8, ri = shown % 8;
            ++shown;
            const Rc r{ c.cx0 + (float)col * (colW + S(8.0f)), c.y0 + S(38.0f) + (rh + rg) * (float)ri, colW, rh };
            const bool hid = VehDetach::IsHidden(g, p);
            if (Chip(430 + p, r, VehDetach::kLabel[p], hid ? "Sökülüb" : "Yerindədir", hid, dt, c.in, p)) VehDetach::Toggle(g, p);
        }
        if (shown == 0)
            Txt(S(14.0f), c.cx0, c.y0 + S(46.0f), Col(170, 170, 170), "Bu modeldə sökülə bilən ayrıca detal tapılmadı. Diaqnostika düyməsini bas.");
        if (mm2::SmallBtn(423, Rc{ c.cx0 + c.cw - S(130.0f), c.y0 + S(38.0f) + (rh + rg) * 8.0f - S(2.0f), S(130.0f), S(30.0f) }, "Diaqnostika", dt, c.in, false)) WriteDiag(c);
    }
}


void RenderHandlingDialog()
{
    using namespace mm;

    static bool          s_wasOpen = false;
    static CVehicleGTA*  s_lastVeh = nullptr;
    static bool          s_eng = false, s_lig = false, s_lock = false;
    static uint8_t       s_sig = 0;
    static float         s_t = 0.0f;
    static int           s_tab = 0;
    static float         s_pill = 0.0f, s_tabAnim = 1.0f, s_tabDir = 1.0f;
    static bool          s_outDown = false;
    static int           s_slot = 1;
    // MMV3_LAYOUT
    static float         s_zoom = 1.0f, s_cx = -1.0f, s_cy = -1.0f;
    static float         s_dragDx = 0.0f, s_dragDy = 0.0f, s_rsOff = 0.0f, s_rsLeft = 0.0f, s_rsTop = 0.0f;
    static bool          s_layLoaded = false;
    static float         s_hOff = 0.0f, s_hVel = 0.0f, s_hLastY = 0.0f, s_hDownY = 0.0f;
    static bool          s_hDrag = false, s_hMoved = false;
    static std::chrono::steady_clock::time_point s_last = std::chrono::steady_clock::now();

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_last).count();
    s_last = now;
    dt = Clamp(dt, 0.0f, 0.05f);

    const bool want = g_bShowHandlingDlg;
    if (!want && Garage::g_on) Garage::End();   // GARAGE_V5
    if (!want && PlateAdj::g_on) PlateAdj::End();   // PLATEADJ_V7
    if (!want && s_t <= 0.0f) {
        s_wasOpen = false;
        s_hndRectValid = false;
        s_act = 0;
        s_hDrag = false;
        return;
    }

    CVehicle*    pVeh   = nullptr;
    CVehicleGTA* gtaVeh = nullptr;
    bool         noVeh  = true;
    if (want) {
        pVeh = mm2::LocalVeh();
        noVeh = (pVeh == nullptr);
        if (pVeh) {
            gtaVeh = pVeh->m_pVehicle;
            if (!s_wasOpen || gtaVeh != s_lastVeh) { s_lastVeh = gtaVeh; s_hOff = 0.0f; s_hVel = 0.0f; }
            s_eng = gtaVeh->m_nVehicleFlags.bEngineOn != 0;
            s_lig = gtaVeh->m_nVehicleFlags.bLightsOn != 0;
            { const int lk = (int)gtaVeh->m_nDoorLock; s_lock = (lk == 2 || lk == 3); }
            s_sig = VehControls::GetSignal(gtaVeh);
        }
        s_wasOpen = true;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 disp = io.DisplaySize;

    // klaviatura aciq olanda (deyer yazilir) menyu gizlenir, yuxarida kicik yazi qalir
    const bool kbOpen = pUI && pUI->keyboard() && pUI->keyboard()->visible();
    if (kbOpen && want) {
        s_hndRectValid = false;
        s_act = 0;
        s_dl = ImGui::GetForegroundDrawList();
        s_A = 1.0f;
        s_U = std::min(disp.y / 720.0f, disp.x / 880.0f);
        const Rc b{ disp.x * 0.5f - S(220.0f), S(14.0f), S(440.0f), S(44.0f) };
        Fill(b, Col(26, 29, 40, 236), S(22.0f));
        Stroke(b, Col(255, 255, 255, 46), S(22.0f), S(1.0f));
        TxtC(S(16.0f), b, Col(238, 241, 250), mm2::s_caller ? (mm2::s_caller->label + " — yeni dəyəri yaz").c_str() : "Yeni dəyəri yaz", true);
        return;
    }

    if (want && Garage::g_on) {   // GARAGE_V5: panel gizlenir, yalniz alt zolaq
        s_hndRectValid = false;
        Garage::Render(pVeh, dt);
        return;
    }
    if (want && PlateAdj::g_on) {   // PLATEADJ_V7
        s_hndRectValid = false;
        PlateAdj::Render(pVeh, dt);
        return;
    }

    s_t = Clamp(s_t + (want ? dt / 0.38f : -dt / 0.18f), 0.0f, 1.0f);
    const float open = want ? mm2::EaseOutBack(s_t) : EaseOutCubic(s_t);
    s_A = Clamp(s_t * 2.2f, 0.0f, 1.0f);
    s_tabAnim = Clamp(s_tabAnim + dt / 0.26f, 0.0f, 1.0f);
    mm2::s_msgT = Clamp(mm2::s_msgT - dt / 3.5f, 0.0f, 1.0f);

    const float W0 = 880.0f, H0 = 560.0f;      // MMV3_LAYOUT
    const float uBase = std::min(disp.y / 720.0f, std::min(disp.x * 0.96f / W0, disp.y * 0.96f / H0));
    const float uFit  = std::min(disp.x * 0.98f / W0, disp.y * 0.98f / H0);
    if (!s_layLoaded) { s_layLoaded = true; mm2::LayoutLoad(s_zoom, s_cx, s_cy); }
    s_zoom = Clamp(s_zoom, 0.45f, 1.6f);

    // basliqdan surusdur / sag-alt kunceden olcunu deyis (evvelki kadrin sahesine gore)
    if (want && s_t > 0.8f && s_hndRectValid) {
        const MMTouch& mt = g_mmTouch;
        const Rc pp{ s_hndPos.x, s_hndPos.y, s_hndSize.x, s_hndSize.y };
        const Rc hdr{ pp.x, pp.y, pp.w - S(64.0f), S(58.0f) };
        const Rc hdl{ pp.x + pp.w - S(44.0f), pp.y + pp.h - S(44.0f), S(44.0f), S(44.0f) };
        if (mt.pressed && s_act == 0) {
            if (In(hdl, mt.x, mt.y)) {
                s_act = ID_ZOOMP; s_rsOff = (pp.x + pp.w) - mt.x; s_rsLeft = pp.x; s_rsTop = pp.y;
            } else if (In(hdr, mt.x, mt.y)) {
                s_act = ID_DRAG; s_dragDx = mt.x - s_cx; s_dragDy = mt.y - s_cy;
            }
        }
        if (mt.down && s_act == ID_DRAG) {
            s_cx = mt.x - s_dragDx;
            s_cy = mt.y - s_dragDy;
        } else if (mt.down && s_act == ID_ZOOMP) {
            const float wantU = (mt.x + s_rsOff - s_rsLeft) / W0;
            s_zoom = Clamp(wantU / uBase, 0.45f, 1.6f);
            const float nu = std::min(uBase * s_zoom, uFit);
            s_cx = s_rsLeft + W0 * nu * 0.5f;
            s_cy = s_rsTop  + H0 * nu * 0.5f;
        }
        if (mt.released && (s_act == ID_DRAG || s_act == ID_ZOOMP))
            mm2::LayoutSave(s_zoom, s_cx, s_cy);
    }

    const float u0 = std::min(uBase * s_zoom, uFit);
    s_U = u0 * (0.90f + 0.10f * open);
    if (s_cx < 0.0f || s_cy < 0.0f) { s_cx = disp.x * 0.5f; s_cy = disp.y * 0.5f; }
    {
        const float hw = W0 * u0 * 0.5f, hh = H0 * u0 * 0.5f;
        s_cx = Clamp(s_cx, hw, std::max(hw, disp.x - hw));
        s_cy = Clamp(s_cy, hh, std::max(hh, disp.y - hh));
    }

    const Rc P{ s_cx - W0 * s_U * 0.5f, s_cy - H0 * s_U * 0.5f, W0 * s_U, H0 * s_U };
    const bool canInput = want && s_t > 0.8f;
    const MMTouch& tch = g_mmTouch;

    s_hndRectValid = (s_t > 0.05f);
    s_hndPos  = ImVec2(P.x, P.y);
    s_hndSize = ImVec2(P.w, P.h);
    s_dl = ImGui::GetForegroundDrawList();

    // fonu qaraldan perde
    s_dl->AddRectFilled(ImVec2(0.0f, 0.0f), disp, IM_COL32(0, 0, 0, (int)(115.0f * Clamp(s_t, 0.0f, 1.0f))));

    // paneldan kenara toxunus = bagla
    if (canInput) {
        if (tch.pressed && s_act == 0 && !In(P, tch.x, tch.y)) s_outDown = true;
        if (tch.released) {
            if (s_outDown && !In(P, tch.x, tch.y)) g_bShowHandlingDlg = false;
            s_outDown = false;
        }
    }

    mm2::Glass(P, S(28.0f));
    // MMV3_LAYOUT: olcu tutacagi (sag-alt)
    {
        const float gx = P.x + P.w - S(14.0f), gy = P.y + P.h - S(14.0f);
        for (int k = 1; k <= 3; ++k) {
            const float d = S(4.5f) * (float)k;
            s_dl->AddLine(ImVec2(gx - d, gy), ImVec2(gx, gy - d), Col(255, 255, 255, 80), S(1.6f));
        }
    }

    // grabber + basliq + bagla
    Fill(Rc{ P.x + P.w * 0.5f - S(22.0f), P.y + S(9.0f), S(44.0f), S(5.0f) }, Col(255, 255, 255, 54), S(2.5f));
    TxtC(S(22.0f), Rc{ P.x, P.y + S(18.0f), P.w, S(34.0f) }, Col(244, 247, 253), "Maşın menyusu", true);
    {
        const Rc cl{ P.x + P.w - S(54.0f), P.y + S(16.0f), S(36.0f), S(36.0f) };
        if (Tap(ID_CLOSE, cl, canInput)) g_bShowHandlingDlg = false;
        const float p = Prs(ID_CLOSE, dt);
        Dot(cl.x + cl.w * 0.5f, cl.y + cl.h * 0.5f, S(15.0f) - S(1.5f) * p, Col(255, 255, 255, 26 + (int)(30.0f * p)));
        const float cx = cl.x + cl.w * 0.5f, cy = cl.y + cl.h * 0.5f, k = S(5.0f);
        s_dl->AddLine(ImVec2(cx - k, cy - k), ImVec2(cx + k, cy + k), Col(236, 240, 250, 230), S(2.0f));
        s_dl->AddLine(ImVec2(cx - k, cy + k), ImVec2(cx + k, cy - k), Col(236, 240, 250, 230), S(2.0f));
    }

    // segmentli idareetme (MM_V8: 4 esas bolme; Tuning-in sol kateqoriya siyahisi)
    static const char* kTabs[4] = { "Maşın", "Handling", "Tuning", "Spidometr" };
    static const char* kRail[15] = { "Boya", "Disk və təkər", "Kapot", "Spoyler", "Dam", "Yan pərdə", "Ön bufer", "Arxa bufer",
                                     "Ekzoz", "Hava girişi", "Əlavə", "Nitro / Hidravlika", "Detalları sök", "Far rəngi", "Nömrə" };
    static int s_rail = 0;
    {
        const Rc seg{ P.x + S(24.0f), P.y + S(66.0f), P.w - S(48.0f), S(40.0f) };
        Fill(seg, Col(255, 255, 255, 18), S(10.0f));
        const float tw = seg.w / 4.0f;
        s_pill = Smooth(s_pill, (float)s_tab, dt, 16.0f);
        Fill(Rc{ seg.x + S(3.0f) + s_pill * tw, seg.y + S(3.0f), tw - S(6.0f), seg.h - S(6.0f) }, Col(255, 255, 255, 50), S(8.0f));
        for (int i = 0; i < 4; ++i) {
            const Rc ti{ seg.x + i * tw, seg.y, tw, seg.h };
            if (Tap(ID_NAV0 + i, ti, canInput) && s_tab != i) {
                s_tabDir = (i > s_tab) ? 1.0f : -1.0f;
                s_tab = i;
                s_tabAnim = 0.0f;
            }
            Prs(ID_NAV0 + i, dt);
            const bool on = (s_tab == i);
            TxtC(S(16.0f), ti, on ? Col(248, 248, 248) : Col(150, 150, 150), kTabs[i], on);
        }
        if (s_tab == 2) {
            const float rx = P.x + S(24.0f), ry = P.y + S(124.0f), rw = S(194.0f), rh = S(27.0f);
            Fill(Rc{ rx - S(4.0f), ry - S(4.0f), rw + S(8.0f), rh * 15.0f + S(8.0f) }, Col(255, 255, 255, 8), S(8.0f));
            for (int i = 0; i < 15; ++i) {
                const Rc it{ rx, ry + rh * (float)i, rw, rh };
                if (Tap(200 + i, it, canInput) && s_rail != i) { s_rail = i; s_tabAnim = 0.0f; s_tabDir = 1.0f; }
                const bool on = (s_rail == i);
                if (on) {
                    Fill(it, Col(255, 255, 255, 26), S(6.0f));
                    Fill(Rc{ it.x, it.y + S(5.0f), S(3.0f), it.h - S(10.0f) }, Col(240, 240, 240), S(1.5f));
                }
                Txt(S(14.0f), it.x + S(12.0f), it.y + (it.h - S(14.0f)) * 0.5f, on ? Col(248, 248, 248) : Col(160, 160, 160), kRail[i]);
            }
        }
    }
    // bolme -> sehife:  0 Masin, 1 Handling, 3 Tuning(kateqoriyalar), 7 Detallar, 2 Far rengi, 4 Nomre, 5 Spidometr
    const int pg = (s_tab == 2) ? (s_rail == 12 ? 7 : (s_rail == 13 ? 2 : (s_rail == 14 ? 4 : 3))) : ((s_tab == 3) ? 5 : s_tab);

    // ---- sehife mezmunu ----
    const float saveA = s_A;
    const float pe = EaseOutCubic(s_tabAnim);
    s_A = saveA * pe;
    float cx0 = P.x + S(24.0f) + (1.0f - pe) * S(26.0f) * s_tabDir;
    float cw  = P.w - S(48.0f);
    if (s_tab == 2) { cx0 += S(206.0f); cw -= S(206.0f); }   // MM_V8: sol kateqoriya siyahisi yeri
    const float y0  = P.y + S(124.0f);
    const bool  inputPage = canInput && s_tabAnim > 0.6f;

    if (s_tab == 0)                                         // MASIN
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Bu bölmə yalnız maşının içində işləyir");
        } else {
            const Rc grp{ cx0, y0, cw, S(168.0f) };
            Fill(grp, Col(255, 255, 255, 15), S(18.0f));
            Stroke(grp, Col(255, 255, 255, 22), S(18.0f), S(1.0f));
            const char* labs[3] = { "Mühərrik", "Fara", "Qapılar" };
            const char* subs[3] = { s_eng ? "İşləyir" : "Söndürülüb", s_lig ? "Yanır" : "Sönülü", s_lock ? "Kilidli" : "Açıq" };
            const bool  st[3]   = { s_eng, s_lig, s_lock };
            for (int i = 0; i < 3; ++i) {
                const Rc row{ cx0, y0 + S(56.0f) * i, cw, S(56.0f) };
                if (i > 0) mm2::Hair(row.x + S(18.0f), row.x + row.w, row.y);
                if (mm2::SwitchRow(i, row, labs[i], subs[i], st[i], dt, inputPage) && pVeh) {
                    if (i == 0)      pVeh->ApplyEngineState(s_eng ? 0 : 1);
                    else if (i == 1) pVeh->ApplyLightState(s_lig ? 0 : 1);
                    else             pVeh->SetDoorState(s_lock ? 0 : 1);
                }
            }
            SignalBlock(Rc{ cx0, y0 + S(186.0f), cw, S(124.0f) }, s_sig, gtaVeh, dt, inputPage);
        }
    }
    else if (s_tab == 1)                                    // HANDLING (MM_V8)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P, s_rail };
        mm3::PageHandling(cc, s_slot);
    }
    else if (pg == 2)                                       // FAR RENGI
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Far rəngi yalnız maşının içində dəyişdirilir");
        } else {
            static CVehicleGTA* s_hlVeh = nullptr;
            static float s_hl[3] = { 255.0f, 255.0f, 255.0f };
            if (gtaVeh != s_hlVeh) {
                s_hlVeh = gtaVeh;
                uint8_t r8 = 255, g8 = 255, b8 = 255;
                if (gtaVeh && CCoronas::GetHeadlightRGB(gtaVeh, r8, g8, b8)) { s_hl[0] = r8; s_hl[1] = g8; s_hl[2] = b8; }
                else { s_hl[0] = s_hl[1] = s_hl[2] = 255.0f; }
            }
            static const char* kRGB[3] = { "Qırmızı (R)", "Yaşıl (G)", "Mavi (B)" };
            bool hlChanged = false;
            for (int i = 0; i < 3; ++i) {
                const Rc row{ cx0, y0 + S(72.0f) * (float)i - S(4.0f) + S(4.0f), cw, S(66.0f) };
                if (SliderRow(i, row, kRGB[i], &s_hl[i], 0.0f, 255.0f, dt, inputPage, "", 5.0f)) hlChanged = true;
            }
            const Rc pvr{ cx0, y0 + S(224.0f), S(150.0f), S(44.0f) };
            Fill(pvr, ColT((int)s_hl[0], (int)s_hl[1], (int)s_hl[2], 255), S(12.0f));
            Stroke(pvr, Col(255, 255, 255, 60), S(12.0f), S(1.2f));
            char hx[24];
            snprintf(hx, sizeof(hx), "#%02X%02X%02X", (int)s_hl[0], (int)s_hl[1], (int)s_hl[2]);
            Txt(S(19.0f), cx0 + S(166.0f), y0 + S(234.0f), Col(228, 232, 244), hx);
            if (mm2::SmallBtn(90, Rc{ cx0 + S(300.0f), y0 + S(224.0f), S(150.0f), S(44.0f) }, "Standart", dt, inputPage, false) && gtaVeh) {
                CCoronas::ClearHeadlightRGB(gtaVeh);
                s_hl[0] = s_hl[1] = s_hl[2] = 255.0f;
            }
            for (int i = 0; i < VEHTUNER_COLOR_COUNT; ++i) {
                uint8_t r, g, b;
                VehTuner::GetPaletteColor(i, r, g, b);
                const int col = i % 8, rowi = i / 8;
                const float cxs = cx0 + S(24.0f) + (float)col * S(58.0f);
                const float cys = y0 + S(320.0f) + (float)rowi * S(54.0f);
                const Rc sq{ cxs - S(24.0f), cys - S(24.0f), S(48.0f), S(48.0f) };
                const int sid = ID_SWATCH + std::min(i, 23);
                if (Tap(sid, sq, inputPage) && gtaVeh) { s_hl[0] = r; s_hl[1] = g; s_hl[2] = b; hlChanged = true; }
                const float p = Prs(sid, dt);
                Dot(cxs, cys, S(20.0f) - S(2.0f) * p, ColT(r, g, b, 255));
            }
            if (hlChanged && gtaVeh) CCoronas::SetHeadlightRGB(gtaVeh, (uint8_t)s_hl[0], (uint8_t)s_hl[1], (uint8_t)s_hl[2]);
        }
    }
    else if (pg == 3)                                       // TUNING (MM_V8)
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P, s_rail };
        mm3::PageTuning(cc);
    }
    else if (pg == 7)                                       // DETALLARI SOK
    {
        mm3::Ctx cc{ cx0, y0, cw, dt, inputPage, noVeh, pVeh, gtaVeh, P, s_rail };
        mm3::PageDetach(cc);
    }
    else if (pg == 4)                                       // NOMRE
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Nömrə yalnız maşının içində təyin olunur");
        } else {
            std::string cur;
            const bool has = gtaVeh && Plate::Get(gtaVeh, cur);
            bool pf = true, pr = true;
            if (has) Plate::GetSides(gtaVeh, pf, pr);

            // onizleme
            const Rc pv{ cx0, y0, S(440.0f), S(440.0f) / 4.523809f };
            void* ras = Plate::BaseRaster();
            if (ras) {
                s_dl->AddImage((ImTextureID)ras, ImVec2(pv.x, pv.y), ImVec2(pv.x + pv.w, pv.y + pv.h));
                if (has) {
                    ImFont* pfont = g_pPlateFont ? g_pPlateFont : ImGui::GetFont();
                    const float bx0 = pv.x + pv.w * 0.181f, bx1 = pv.x + pv.w * 0.966f;
                    const float by0 = pv.y + pv.h * 0.11f,  by1 = pv.y + pv.h * 0.806f;
                    float fs = (by1 - by0) * 0.92f;
                    ImVec2 ts = pfont->CalcTextSizeA(fs, FLT_MAX, 0.0f, cur.c_str());
                    if (ts.x > (bx1 - bx0) * 0.94f) {
                        fs *= (bx1 - bx0) * 0.94f / ts.x;
                        ts = pfont->CalcTextSizeA(fs, FLT_MAX, 0.0f, cur.c_str());
                    }
                    s_dl->AddText(pfont, fs, ImVec2((bx0 + bx1) * 0.5f - ts.x * 0.5f, (by0 + by1) * 0.5f - ts.y * 0.5f), IM_COL32(15, 15, 15, 255), cur.c_str());
                }
            } else {
                Fill(pv, Col(255, 255, 255, 20), S(10.0f));
                TxtC(S(13.0f), pv, Col(255, 160, 130), Plate::LastError(), false);
            }

            Txt(S(15.0f), cx0 + S(460.0f), y0 + S(6.0f),  Col(228, 232, 244), has ? cur.c_str() : "Nömrə təyin olunmayıb");
            Txt(S(13.0f), cx0 + S(460.0f), y0 + S(30.0f), Col(128, 136, 158), "Format: 10 AA 123");
            if (mm2::s_msgT > 0.01f)
                Txt(S(13.0f), cx0 + S(460.0f), y0 + S(52.0f), Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);

            const float by = y0 + pv.h + S(30.0f);
            const float bw = (cw - S(12.0f)) / 2.0f;
            char lab[40];

            if (mm2::SmallBtn(130, Rc{ cx0, by, bw, S(46.0f) }, "Nömrəni yaz", dt, inputPage, true)) {
                if (!PlateUi::s_caller) PlateUi::s_caller = new PlateUi::PlateCaller();
                if (pUI && pUI->keyboard()) {
                    pUI->keyboard()->show(PlateUi::s_caller);
                    pUI->keyboard()->input()->setInputString(has ? cur : std::string());
                }
            }
            if (mm2::SmallBtn(131, Rc{ cx0 + bw + S(12.0f), by, bw, S(46.0f) }, "Nömrəni sil", dt, inputPage, false) && gtaVeh) {
                Plate::Remove(gtaVeh);
                mm2::SetMsg("Nömrə silindi");
            }

            snprintf(lab, sizeof(lab), "Qabağa vur: %s", pf ? "açıq" : "bağlı");
            if (mm2::SmallBtn(132, Rc{ cx0, by + S(58.0f), bw, S(46.0f) }, lab, dt, inputPage, false) && gtaVeh) {
                if (has) Plate::SetSides(gtaVeh, !pf, pr); else mm2::SetMsg("Əvvəl nömrə yaz");
            }
            snprintf(lab, sizeof(lab), "Arxaya vur: %s", pr ? "açıq" : "bağlı");
            if (mm2::SmallBtn(133, Rc{ cx0 + bw + S(12.0f), by + S(58.0f), bw, S(46.0f) }, lab, dt, inputPage, false) && gtaVeh) {
                if (has) Plate::SetSides(gtaVeh, pf, !pr); else mm2::SetMsg("Əvvəl nömrə yaz");
            }

            if (mm2::SmallBtn(134, Rc{ cx0, by + S(116.0f), cw, S(50.0f) }, "Tənzimlə  (kamera plakaya yaxınlaşır)", dt, inputPage, true) && gtaVeh) {
                if (has) PlateAdj::Begin(gtaVeh, pf || !pr);
                else mm2::SetMsg("Əvvəl nömrə yaz");
            }
        }
    }
    else                                                    // SPIDOMETR
    {
        MM_SpeedoPage(cx0, cw, P, dt, inputPage);
    }
    s_A = saveA;

    if (!g_mmTouch.down) s_act = 0;
}
// MM_V8

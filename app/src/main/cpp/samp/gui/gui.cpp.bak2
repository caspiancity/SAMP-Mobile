#include "../main.h"
#include "../game/game.h"
#include "../net/netgame.h"
#include "../game/vehicle.h"
#include "../game/VehTuner.h"
#include "../game/VehControls.h"
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
        d_label1 = new Label(cryptor::create("SA:MP Mobile 2.10 x64").decrypt(), ImColor(1.0f, 1.0f, 1.0f), true, UISettings::fontSize() / 3);
        this->addChild(d_label1);
        d_label1->setPosition(ImVec2(3.0, 3.0));

        return true;
}

void UI::render()
{
        ProcessTouchQueue();     // toxunuslari bu kadr ucun oxu

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

        static inline ImU32 Col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, (int)((float)a * s_A)); }
        static inline ImU32 Accent(float /*istifade olunmur*/, int a = 255) { return Col(78, 142, 235, a); }
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
        static bool SliderRow(int idx, const Rc& row, const char* label, float* v, float mn, float mx, float dt, bool canInput)
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
                                if (s_act == id + 1 && In(minus, t.x, t.y)) nv = Clamp(*v - 5.0f, mn, mx);
                                if (s_act == id + 2 && In(plus,  t.x, t.y)) nv = Clamp(*v + 5.0f, mn, mx);
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
                snprintf(buf, sizeof(buf), "%.0f%%", *v);
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
                if (mn < 100.0f && mx > 100.0f) {
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
                Txt(S(15.0f), blk.x + S(18.0f), blk.y + S(10.0f), Col(124, 132, 152), "Sinyal");

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

void RenderHandlingDialog()
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

        CPlayerPed* pLocalPlayer = pGame ? pGame->FindPlayerPed() : nullptr;

        if (want)
        {
                if (!pLocalPlayer || !pLocalPlayer->IsInVehicle()) {
                        g_bShowHandlingDlg = false;
                        s_t = 0.0f; s_wasOpen = false; s_hndRectValid = false; s_act = 0;
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

        const bool canInput = want && s_t > 0.75f;
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
        static const char* kPages[6] = { "Nəzarət", "Mühərrik", "Sürücülük", "Asma", "Far", "Komandalar" };
        static const char* kSubs[6]  = { "Mühərrik, fara, qapılar və sinyal", "Sürətlənmə, sürət və çəki", "Tormoz, manevr və yol tutuş", "Asma və toqquşma", "Far rəngi", "Bütün komandaların siyahısı" };
        {
                s_navRel = Smooth(s_navRel, 74.0f + 56.0f * s_page, dt, 18.0f);
                const Rc ind{ P.x + S(16.0f), P.y + S(s_navRel), S(166.0f), S(48.0f) };
                Fill(ind, Col(255, 255, 255, 12), S(10.0f));
                Fill(Rc{ ind.x, ind.y + S(12.0f), S(3.0f), S(24.0f) }, Accent(0.0f), S(1.5f));

                for (int i = 0; i < 6; ++i)
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
                else if (s_page == 4)     // FAR
                {
                        char hb[40];
                        if (s_tune.headlight == 0) snprintf(hb, sizeof(hb), "Far rəngi: Standart");
                        else                       snprintf(hb, sizeof(hb), "Far rəngi: #%d", (int)s_tune.headlight);
                        Txt(S(17.0f), cx0, P.y + S(128.0f), Col(214, 220, 232), hb);
                        Txt(S(13.0f), cx0, P.y + S(152.0f), Col(124, 132, 152), "Hazırda yalnız hazır palitra — sərbəst rəng seçimi üçün VehTuner/Coronas faylları lazımdır.");

                        for (int i = 0; i < VEHTUNER_COLOR_COUNT; ++i)
                        {
                                uint8_t r, g, b;
                                VehTuner::GetPaletteColor(i, r, g, b);

                                const int   col = i % 8, rowi = i / 8;
                                const float cxs = cx0 + S(24.0f) + col * S(58.0f);
                                const float cys = P.y + S(200.0f) + rowi * S(58.0f);
                                const Rc    sq{ cxs - S(24.0f), cys - S(24.0f), S(48.0f), S(48.0f) };

                                const int sid = ID_SWATCH + std::min(i, 23);
                                if (Tap(sid, sq, canInput)) { s_tune.headlight = (uint8_t)i; changed = true; }

                                const float p = Prs(sid, dt);
                                Dot(cxs, cys, S(20.0f) - S(2.0f) * p, Col(r, g, b, 255));
                                if (s_tune.headlight == i) s_dl->AddCircle(ImVec2(cxs, cys), S(25.0f), Col(255, 255, 255, 230), 28, S(2.2f));
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
                        };
                        const int n = (int)(sizeof(kCmds) / sizeof(kCmds[0]));
                        for (int i = 0; i < n; ++i)
                        {
                                const Rc row{ cx0, P.y + S(124.0f + 52.0f * i), cw, S(44.0f) };
                                Fill(row, Col(27, 30, 40, 255), S(9.0f));
                                Txt(S(15.0f), row.x + S(16.0f), row.y + (row.h - S(15.0f)) * 0.5f, Accent(0.0f), kCmds[i].cmd);
                                Txt(S(14.0f), row.x + S(160.0f), row.y + (row.h - S(14.0f)) * 0.5f, Col(200, 204, 216), kCmds[i].desc);
                        }
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

// ============================================================================
//  MTA stilli dairevi speedometr. Sinyal UI legv edilib (hele ki).
//  Uzun basma = reng deyisimi (kHudPalette). Benzin dairesi YOXDUR:
//  client-de yanacaq melumati yoxdur (serverin textdrawidir).
// ============================================================================
void RenderVehicleHud()
{
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

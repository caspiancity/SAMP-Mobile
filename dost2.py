# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v5.py -- layihenin kokunde isle:  python3 apply_v5.py      (evvelce apply_v4.py isleyib)
#   1) SINXRON: far rengi + sinyal diger oyunculara da gorunur   (gui.cpp, chat.cpp)  + serverde vt_sync.pwn
#   2) HANDLING: menyuda "Rəqəm | Bar" rejimi, "Fayl seç" duymesi (daxili yaddasdan handling.cfg -> slot)
#        gui.cpp, vehicle.cpp, jniutil.h/.cpp, SAMP.java
#   3) KLAVIATURA: iOS stili yumru duymeler, basilan herf balonu, KURSOR (herfe toxun -> orada duzelt)
#        keyboard.h, keyboard.cpp
# Geri qaytarmaq: her faylin yaninda *.bakv5. Anker tapilmasa fayla toxunulmur.
import os, re, shutil

ROOT = os.getcwd()
CPP  = os.path.join(ROOT, "app/src/main/cpp/samp")
JAVA = os.path.join(ROOT, "app/src/main/java/com/samp/mobile/game/SAMP.java")

def rd(p):
    with open(p, "rb") as f:
        t = f.read().decode("utf-8")
    crlf = "\r\n" in t
    return t.replace("\r\n", "\n"), crlf

def wr(p, t, crlf):
    if crlf:
        t = t.replace("\n", "\r\n")
    with open(p, "wb") as f:
        f.write(t.encode("utf-8"))

def step(name, path, marker, fn):
    if not os.path.exists(path):
        print("[YOX]   %s: %s tapilmadi" % (name, os.path.relpath(path, ROOT))); return
    s, crlf = rd(path)
    if marker in s:
        print("[KECDI] %s: artiq tetbiq olunub" % name); return
    try:
        n = fn(s)
    except ValueError as e:
        print("[KECDI] %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); return
    if not os.path.exists(path + ".bakv5"):
        shutil.copy(path, path + ".bakv5")
    wr(path, n, crlf)
    print("[OK]    %s" % name)

def once(s, old, new, what):
    if s.count(old) != 1:
        raise ValueError("%s (%d tapildi)" % (what, s.count(old)))
    return s.replace(old, new, 1)

# =============================================================== 1. SINXRON
VTSYNC = r'''// ===== VTSYNC_V5: far rengi + sinyal diger oyunculara da gorunsun =====
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

'''

def p_vt(s):
    m = re.search(r'\n// =+\n//\s+MTA stilli', s)
    if not m:
        raise ValueError("MTA stilli speedometr basligi")
    s = s[:m.start() + 1] + VTSYNC + s[m.start() + 1:]
    pat = re.compile(r'(void RenderVehicleHud\(\)\n\{\n)(\s*HudCfgLoad\(\);)')
    s, n = pat.subn(lambda mm: mm.group(1) + "        VtSync_Update();   // VTSYNC_V5\n" + mm.group(2), s, count=1)
    if n != 1:
        raise ValueError("RenderVehicleHud()")
    return s

def p_chat(s):
    old = "void Chat::addMessage(const std::string& message, const ImColor& color)\n{\n"
    new = ("bool VtSync_OnMessage(const std::string& msg);   // VTSYNC_V5 (gui.cpp)\n"
           "void Chat::addMessage(const std::string& message, const ImColor& color)\n{\n"
           "        if (VtSync_OnMessage(message)) return;   // VTSYNC_V5: gizli sinxron mesaji chatda gorunmur\n")
    return once(s, old, new, "Chat::addMessage")

# =============================================================== 2. HANDLING (bar + fayl secici)
HND_PRE = r'''// ===== HNDBAR_V5 =====
#include "../java/jniutil.h"
extern CJavaWrapper* pJavaWrapper;
std::string HndExt_SlotPath(int slot);   // vehicle.cpp

'''

HND_BLOCK = r'''    else if (s_tab == 1)                                    // HANDLING
    {
        if (noVeh) {
            mm2::EmptyState(Rc{ cx0, y0, cw, S(300.0f) }, "Avtomobildə deyilsiniz", "Handling yalnız maşının içində dəyişdirilir");
        } else {
            static int          s_barMode  = 0;          // HNDBAR_V5: 0 = rəqəm, 1 = bar
            static float        s_baseV[mm2::kHFN];
            static CVehicleGTA* s_baseVeh  = nullptr;
            static int          s_slideRow = -1;

            // bar araligi: masinin hazirki (acilis / sifirlama anindaki) deyerine gore
            if (gtaVeh != s_baseVeh) {
                s_baseVeh = gtaVeh;
                for (int r = 0; r < mm2::kHFN; ++r) {
                    float cv = 0.0f;
                    s_baseV[r] = HndExt_Get(pVeh, mm2::kHF[r].key, &cv) ? cv : 0.0f;
                }
            }
            auto rangeOf = [&](int r, float& lo, float& hi) {
                const float b = s_baseV[r];
                if (strncmp(mm2::kHF[r].key, "com", 3) == 0) { lo = -2.0f; hi = 2.0f; }
                else if (b > -1e-6f && b < 1e-6f)            { lo = -1.0f; hi = 1.0f; }
                else { const float a = b * 0.25f, c = b * 3.0f; lo = std::min(a, c); hi = std::max(a, c); }
            };

            // slot secimi
            for (int i = 0; i < 5; ++i) {
                const Rc c{ cx0 + S(52.0f) * i, y0, S(46.0f), S(40.0f) };
                if (Tap(80 + i, c, inputPage)) s_slot = i + 1;
                const bool on = (s_slot == i + 1);
                Fill(c, on ? Accent(0.0f, 215) : Col(255, 255, 255, 18), S(12.0f));
                char nb[4];
                snprintf(nb, sizeof(nb), "%d", i + 1);
                TxtC(S(18.0f), c, on ? Col(255, 255, 255) : Col(190, 198, 216), nb, true);
                if (HndExt_SlotUsed(i + 1)) Dot(c.x + c.w - S(8.0f), c.y + S(8.0f), S(3.2f), Col(80, 220, 140, 255));
            }

            // Fayl sec / Tetbiq et / Sifirla
            static const char* kAct[3] = { "Fayl seç", "Tətbiq et", "Sıfırla" };
            {
                const float bx = cx0 + S(272.0f);
                const float bw = (cw - S(272.0f) - S(12.0f)) / 3.0f;
                for (int k = 0; k < 3; ++k) {
                    const Rc b{ bx + k * (bw + S(6.0f)), y0, bw, S(40.0f) };
                    if (mm2::SmallBtn(85 + k, b, kAct[k], dt, inputPage, k == 1) && pVeh) {
                        if (k == 0) {
                            if (pJavaWrapper) {
                                const std::string dst = HndExt_SlotPath(s_slot);
                                pJavaWrapper->PickHandlingFile(s_slot, dst.c_str());
                                mm2::SetMsg("handling.cfg faylını seçin → slot " + std::to_string(s_slot));
                            }
                        } else if (k == 1) {
                            std::string m;
                            HndExt_ApplyFileSlot(pVeh, s_slot, m);
                            mm2::SetMsg(m);
                            s_baseVeh = nullptr;
                        } else {
                            mm2::SetMsg(HndExt_Reset(pVeh) ? "Standarta qaytarıldı" : "Dəyişiklik yoxdur");
                            s_baseVeh = nullptr;
                        }
                    }
                }
            }

            // rejim: Rəqəm | Bar
            {
                const Rc seg{ cx0, y0 + S(48.0f), S(160.0f), S(30.0f) };
                Fill(seg, Col(255, 255, 255, 18), S(10.0f));
                const float hw = seg.w * 0.5f;
                Fill(Rc{ seg.x + S(2.0f) + (float)s_barMode * hw, seg.y + S(2.0f), hw - S(4.0f), seg.h - S(4.0f) }, Col(255, 255, 255, 50), S(8.0f));
                for (int i = 0; i < 2; ++i) {
                    const Rc ti{ seg.x + (float)i * hw, seg.y, hw, seg.h };
                    if (Tap(97 + i, ti, inputPage)) s_barMode = i;
                    Prs(97 + i, dt);
                    TxtC(S(14.0f), ti, s_barMode == i ? Col(248, 250, 255) : Col(150, 158, 180), i == 0 ? "Rəqəm" : "Bar", s_barMode == i);
                }
            }
            if (mm2::s_msgT > 0.01f)
                Txt(S(13.0f), cx0 + S(176.0f), y0 + S(54.0f), Col(120, 215, 160, (int)(255.0f * mm2::s_msgT)), mm2::s_msg);
            else
                Txt(S(13.0f), cx0 + S(176.0f), y0 + S(54.0f), Col(128, 136, 158), s_barMode ? "Barı sürüşdür, sətrə toxun = rəqəm yaz" : "Sətrə toxun və yeni dəyəri yaz");

            // sahe siyahisi (scroll)
            const float rowH = s_barMode ? S(68.0f) : S(46.0f);
            const Rc lr{ cx0, y0 + S(88.0f), cw, P.y + P.h - S(18.0f) - (y0 + S(88.0f)) };
            const float contentH = rowH * mm2::kHFN;
            const float maxOff = std::max(0.0f, contentH - lr.h);

            if (inputPage) {
                if (tch.pressed && s_act == 0 && In(lr, tch.x, tch.y)) {
                    bool onBar = false;
                    const int hitRow = (int)((tch.y - lr.y + s_hOff) / rowH);
                    if (s_barMode && hitRow >= 0 && hitRow < mm2::kHFN) {
                        const float ry = lr.y - s_hOff + rowH * hitRow;
                        const Rc br{ lr.x + S(10.0f), ry + rowH - S(36.0f), lr.w - S(20.0f), S(32.0f) };
                        onBar = In(br, tch.x, tch.y);
                        if (onBar) { s_act = 96; s_slideRow = hitRow; }
                    }
                    if (!onBar) {
                        s_act = 95; s_hDrag = true; s_hMoved = false; s_hLastY = tch.y; s_hDownY = tch.y; s_hVel = 0.0f;
                    }
                }
                if (s_act == 96 && s_slideRow >= 0 && s_slideRow < mm2::kHFN) {
                    if (tch.down) {
                        float lo, hi;
                        rangeOf(s_slideRow, lo, hi);
                        const float bx2 = lr.x + S(18.0f), bw2 = lr.w - S(36.0f);
                        const float t = Clamp((tch.x - bx2) / bw2, 0.0f, 1.0f);
                        HndExt_Set(pVeh, mm2::kHF[s_slideRow].key, lo + (hi - lo) * t);
                    }
                    if (tch.released) s_slideRow = -1;
                }
                if (s_hDrag && s_act == 95) {
                    if (tch.down) {
                        const float dy = tch.y - s_hLastY;
                        s_hLastY = tch.y;
                        if (fabsf(tch.y - s_hDownY) > S(8.0f)) s_hMoved = true;
                        if (s_hMoved) {
                            const float over = (s_hOff < 0.0f) ? -s_hOff : (s_hOff > maxOff ? s_hOff - maxOff : 0.0f);
                            const float k = 1.0f / (1.0f + over / S(60.0f));
                            s_hOff -= dy * k;
                            s_hVel = Lerp(s_hVel, -dy / std::max(dt, 0.001f), 0.35f);
                        }
                    }
                    if (tch.released) {
                        if (!s_hMoved && In(lr, tch.x, tch.y)) {
                            const int row = (int)((tch.y - lr.y + s_hOff) / rowH);
                            if (row >= 0 && row < mm2::kHFN && pUI && pUI->keyboard()) {
                                if (!mm2::s_caller) mm2::s_caller = new mm2::EditCaller();
                                mm2::s_caller->key = mm2::kHF[row].key;
                                mm2::s_caller->label = mm2::kHF[row].label;
                                pUI->keyboard()->show(mm2::s_caller);
                                float cur = 0.0f;
                                if (HndExt_Get(pVeh, mm2::kHF[row].key, &cur)) {
                                    char vb[32];
                                    snprintf(vb, sizeof(vb), "%.4f", cur);
                                    pUI->keyboard()->input()->setInputString(vb);
                                }
                            }
                            s_hVel = 0.0f;
                        }
                        s_hDrag = false;
                    }
                }
            }
            if (!(s_hDrag && s_act == 95)) {
                if (s_hOff < 0.0f || s_hOff > maxOff) {
                    s_hOff = Smooth(s_hOff, Clamp(s_hOff, 0.0f, maxOff), dt, 14.0f);
                    s_hVel = 0.0f;
                } else {
                    s_hOff += s_hVel * dt;
                    s_hVel *= expf(-dt * 3.5f);
                    if (fabsf(s_hVel) < 5.0f) s_hVel = 0.0f;
                }
            }

            Fill(lr, Col(255, 255, 255, 15), S(18.0f));
            Stroke(lr, Col(255, 255, 255, 22), S(18.0f), S(1.0f));
            s_dl->PushClipRect(ImVec2(lr.x, lr.y + S(1.0f)), ImVec2(lr.x + lr.w, lr.y + lr.h - S(1.0f)), true);
            for (int r = 0; r < mm2::kHFN; ++r) {
                const float ry = lr.y - s_hOff + rowH * r;
                if (ry + rowH < lr.y || ry > lr.y + lr.h) continue;
                if (s_hDrag && !s_hMoved && s_act == 95 && tch.y >= ry && tch.y < ry + rowH)
                    Fill(Rc{ lr.x, ry, lr.w, rowH }, Col(255, 255, 255, 16), 0.0f);
                if (r > 0) mm2::Hair(lr.x + S(18.0f), lr.x + lr.w, ry);

                float cur = 0.0f;
                char vb[40] = "-";
                const bool have = HndExt_Get(pVeh, mm2::kHF[r].key, &cur);
                if (have) snprintf(vb, sizeof(vb), mm2::kHF[r].fmt, cur);

                const float ty = s_barMode ? (ry + S(9.0f)) : (ry + (rowH - S(17.0f)) * 0.5f);
                Txt(S(17.0f), lr.x + S(18.0f), ty, Col(228, 232, 244), mm2::kHF[r].label);
                Txt(S(17.0f), lr.x + lr.w - S(s_barMode ? 22.0f : 40.0f) - TW(S(17.0f), vb), ty, Accent(0.0f), vb);

                if (!s_barMode) {
                    Txt(S(18.0f), lr.x + lr.w - S(26.0f), ry + (rowH - S(18.0f)) * 0.5f, Col(110, 118, 140), ">");
                } else if (have) {
                    float lo, hi;
                    rangeOf(r, lo, hi);
                    float t = (hi > lo) ? (cur - lo) / (hi - lo) : 0.0f;
                    t = Clamp(t, 0.0f, 1.0f);
                    const float bx2 = lr.x + S(18.0f), bw2 = lr.w - S(36.0f), by2 = ry + rowH - S(24.0f), bh2 = S(8.0f);
                    Fill(Rc{ bx2, by2, bw2, bh2 }, Col(255, 255, 255, 34), bh2 * 0.5f);
                    if (t > 0.0f) Fill(Rc{ bx2, by2, std::max(bh2, bw2 * t), bh2 }, Accent(0.0f), bh2 * 0.5f);
                    const bool act = (s_act == 96 && s_slideRow == r);
                    Dot(bx2 + bw2 * t, by2 + bh2 * 0.5f, S(act ? 11.0f : 9.0f), Col(255, 255, 255, 255));
                }
            }
            s_dl->PopClipRect();
            if (maxOff > 1.0f) {
                const float th = std::max(S(26.0f), lr.h * (lr.h / contentH));
                const float ty = lr.y + S(4.0f) + (lr.h - S(8.0f) - th) * Clamp(s_hOff / maxOff, 0.0f, 1.0f);
                Fill(Rc{ lr.x + lr.w - S(6.0f), ty, S(3.0f), th }, Col(255, 255, 255, 60), S(1.5f));
            }
        }
    }
'''

def p_hnd(s):
    i = s.find("\nnamespace mm2\n{")
    if i < 0 or s.count("\nnamespace mm2\n{") != 1:
        raise ValueError("namespace mm2")
    m1 = re.search(r'\n    else if \(s_tab == 1\)[ \t]*// HANDLING\n', s)
    m2 = re.search(r'\n    else if \(s_tab == 2\)[ \t]*// FAR\n', s)
    if not m1 or not m2 or m1.start() >= m2.start():
        raise ValueError("HANDLING / FAR bloklari")
    s = s[:m1.start() + 1] + HND_BLOCK + s[m2.start() + 1:]
    i = s.find("\nnamespace mm2\n{")
    return s[:i + 1] + HND_PRE + s[i + 1:]

def p_slotpath(s):
    old = "\nbool HndExt_SlotUsed(int slot)\n{"
    new = ('\nstd::string HndExt_SlotPath(int slot)   // HNDV5_PATH: Java fayl secici bura yazir\n'
           '{\n'
           '    char nm[32];\n'
           '    snprintf(nm, sizeof(nm), "hslot_%d.cfg", (slot < 1 ? 1 : (slot > HNDX_SLOTS ? HNDX_SLOTS : slot)));\n'
           '    return HndxPath(nm);\n'
           '}\n'
           '\nbool HndExt_SlotUsed(int slot)\n{')
    return once(s, old, new, "HndExt_SlotUsed")

def p_jnih(s):
    s = once(s, "void YtCommand(const char* arg);", "void YtCommand(const char* arg);\n\tvoid PickHandlingFile(int slot, const char* destPath);   // PICKFILE_V5", "YtCommand decl")
    s = once(s, "jmethodID s_ytCommand;", "jmethodID s_ytCommand;\n\tjmethodID s_pickFile;", "s_ytCommand")
    return s

def p_jnicpp(s):
    old = '    s_ytCommand = env->GetMethodID(clas, "ytCommand", "([B)V");'
    new = (old + '\n'
           '    s_pickFile = env->GetMethodID(clas, "pickHandlingFile", "(ILjava/lang/String;)V");   // PICKFILE_V5\n'
           '    if (env->ExceptionCheck()) { env->ExceptionClear(); s_pickFile = nullptr; }')
    s = once(s, old, new, "ytCommand GetMethodID")
    s = s.rstrip() + r'''

void CJavaWrapper::PickHandlingFile(int slot, const char* destPath)
{
    JNIEnv* env = nullptr;
    javaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (!env || !s_pickFile) return;

    jstring js = env->NewStringUTF(destPath ? destPath : "");
    env->CallVoidMethod(activity, s_pickFile, slot, js);
    env->DeleteLocalRef(js);
    EXCEPTION_CHECK(env);
}
'''
    return s

JAVA_PICK = r'''    // REQ_HND_PICK: menyudan handling.cfg fayl secici (native -> pickHandlingFile)
    private static final int REQ_HND_PICK = 0x4801;
    private String mHndDest = null;
    private int mHndSlot = 0;

    public void pickHandlingFile(final int slot, final String dest) {
        mHndSlot = slot;
        mHndDest = dest;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    android.content.Intent it = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
                    it.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                    it.setType("*/*");
                    startActivityForResult(it, REQ_HND_PICK);
                } catch (Throwable t) {
                    Log.e(TAG, "pickHandlingFile: " + t);
                    Toast.makeText(SAMP.this, "Fayl seçici açılmadı", Toast.LENGTH_SHORT).show();
                }
            }
        });
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_HND_PICK || resultCode != RESULT_OK || data == null || data.getData() == null || mHndDest == null) return;
        try {
            java.io.InputStream in = getContentResolver().openInputStream(data.getData());
            if (in == null) return;
            File out = new File(mHndDest);
            File parent = out.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();
            java.io.FileOutputStream os = new java.io.FileOutputStream(out);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            os.close();
            in.close();
            Toast.makeText(this, "Handling faylı slot " + mHndSlot + "-ə əlavə olundu", Toast.LENGTH_SHORT).show();
        } catch (Throwable t) {
            Log.e(TAG, "handling copy: " + t);
            Toast.makeText(this, "Fayl oxunmadı", Toast.LENGTH_SHORT).show();
        }
    }

'''

def p_java(s):
    return once(s, "    private native void initializeSAMP();", JAVA_PICK + "    private native void initializeSAMP();", "initializeSAMP")

# =============================================================== 3. KLAVIATURA
def p_kbh(s):
    pat = re.compile(r'void setInputString\(const std::string& string\)[^\n]*')
    if len(pat.findall(s)) != 1:
        raise ValueError("setInputString")
    s = pat.sub(lambda m: "void setInputString(const std::string& string) { m_input = string; m_caption = string; m_caret = (int)m_input.size(); }   // KBIOS_V5", s, count=1)
    s = once(s, "void clear() { m_input.clear(); m_caption.clear(); }", "void clear() { m_input.clear(); m_caption.clear(); m_caret = 0; }", "clear()")
    pat = re.compile(r'(std::string m_caption;\s*\n(\s*)std::string m_input;)')
    if len(pat.findall(s)) != 1:
        raise ValueError("m_caption/m_input")
    s = pat.sub(lambda m: m.group(1) + "\n" + m.group(2) + "int m_caret = 0;", s, count=1)
    old = "virtual void performLayout() override;"
    i = s.find(old)
    if i < 0:
        raise ValueError("KeyboardInput::performLayout")
    return s[:i + len(old)] + "\n\tvirtual void touchEvent(const ImVec2& pos, TouchType type) override;" + s[i + len(old):]

KB_INPUT = r'''// KBIOS_V5: kursor (caret) ile redakte
static int Utf8PrevStart(const std::string& s, int pos)
{
	if (pos <= 0) return 0;
	int i = pos - 1;
	while (i > 0 && (((unsigned char)s[i]) & 0xC0) == 0x80) --i;
	return i;
}

void KeyboardInput::addCharToInput(char value)
{
	if (m_caret < 0 || m_caret > (int)m_input.size()) m_caret = (int)m_input.size();
	m_input.insert((size_t)m_caret, 1, value);
	m_caret += 1;
	m_caption = m_input;
}

void KeyboardInput::addStringToInput(const std::string& value)
{
	if (m_caret < 0 || m_caret > (int)m_input.size()) m_caret = (int)m_input.size();
	m_input.insert((size_t)m_caret, value);
	m_caret += (int)value.size();
	m_caption = m_input;
}

void KeyboardInput::popCharFromInput()
{
	if (m_caret < 0 || m_caret > (int)m_input.size()) m_caret = (int)m_input.size();
	if (m_caret == 0) return;
	const int st = Utf8PrevStart(m_input, m_caret);
	m_input.erase((size_t)st, (size_t)(m_caret - st));
	m_caret = st;
	m_caption = m_input;
}

void KeyboardInput::touchEvent(const ImVec2& pos, TouchType type)
{
	Widget::touchEvent(pos, type);
	if (type != TouchType::push) return;
	if (!contains(pos)) return;
	if (pos.x >= m_buttonTub->absolutePosition().x - UISettings::padding()) return;   // /  <<  >> duymeleri

	const float font_sz = UISettings::fontSize();
	const float tx = absolutePosition().x + UISettings::padding();
	const int n = (int)m_input.size();
	int best = 0, i = 0;
	float bestD = 1e9f;
	while (true)
	{
		const float w = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, m_input.c_str(), m_input.c_str() + i).x;
		const float x = tx + w;
		const float d = (x > pos.x) ? (x - pos.x) : (pos.x - x);
		if (d < bestD) { bestD = d; best = i; }
		if (i >= n) break;
		++i;
		while (i < n && (((unsigned char)m_input[i]) & 0xC0) == 0x80) ++i;
	}
	m_caret = best;
}

void KeyboardInput::draw(ImGuiRenderer* renderer)
{
	float divLinePosY = (absolutePosition().y + size().y) - UISettings::outlineSize();
	float font_sz = UISettings::fontSize();
	const float tx = absolutePosition().x + UISettings::padding();
	const float ty = absolutePosition().y + (height() - font_sz) / 2;

	renderer->drawText(ImVec2(tx, ty), ImColor(1.0f, 1.0f, 1.0f), m_caption, false, font_sz);

	// kursor (yanib-soner)
	if (m_caret < 0 || m_caret > (int)m_input.size()) m_caret = (int)m_input.size();
	const float cw = ImGui::GetFont()->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, m_input.c_str(), m_input.c_str() + m_caret).x;
	if (((int)(ImGui::GetTime() * 2.0)) % 2 == 0)
		renderer->drawLine(ImVec2(tx + cw, ty), ImVec2(tx + cw, ty + font_sz), ImColor(0xF5, 0x91, 0x32), 2.0f);

	renderer->drawLine(
			ImVec2(absolutePosition().x, divLinePosY),
			ImVec2(absolutePosition().x + size().x, divLinePosY),
			ImColor(0xF5, 0x91, 0x32), UISettings::outlineSize()
	);

	Widget::draw(renderer);
}

'''

KB_KEYDRAW = r'''void KeyboardLayout::Row::Key::draw(ImGuiRenderer* renderer)
{
	// KBIOS_V5: iOS stilinde yumru duymeler + basilan herf balonu
	{
		const bool pressed = focused();
		const bool special = (m_extendedKey != ExtendedKey::NONE);
		const float m = height() * 0.07f;
		const float ax = absolutePosition().x + m * 0.6f,          ay = absolutePosition().y + m;
		const float bx = absolutePosition().x + width() - m * 0.6f, by = absolutePosition().y + height() - m;
		const float rad = height() * 0.2f;
		ImDrawList* dl = ImGui::GetBackgroundDrawList();

		ImU32 fill;
		if (special) fill = pressed ? IM_COL32(120, 120, 128, 255) : IM_COL32(62, 62, 68, 255);
		else         fill = pressed ? IM_COL32(150, 150, 158, 255) : IM_COL32(92, 92, 98, 255);
		if (m_extendedKey == ExtendedKey::SEND) fill = pressed ? IM_COL32(255, 160, 60, 255) : IM_COL32(245, 145, 50, 255);

		dl->AddRectFilled(ImVec2(ax, ay + 1.5f), ImVec2(bx, by + 1.5f), IM_COL32(0, 0, 0, 90), rad);
		dl->AddRectFilled(ImVec2(ax, ay), ImVec2(bx, by), fill, rad);

		if (pressed && m_extendedKey == ExtendedKey::NONE && !caption().empty())
		{
			const float pw = width() * 1.35f, ph = height() * 1.25f;
			const float px = absolutePosition().x + (width() - pw) * 0.5f;
			const float py = absolutePosition().y - ph - height() * 0.05f;
			ImDrawList* fg = ImGui::GetForegroundDrawList();
			fg->AddRectFilled(ImVec2(px, py + 2.0f), ImVec2(px + pw, py + ph + 2.0f), IM_COL32(0, 0, 0, 110), rad * 1.4f);
			fg->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(150, 150, 158, 255), rad * 1.4f);
			const float fs = UISettings::fontSize() * 1.5f;
			const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, caption().c_str());
			fg->AddText(ImGui::GetFont(), fs, ImVec2(px + (pw - ts.x) * 0.5f, py + (ph - ts.y) * 0.5f), IM_COL32(255, 255, 255, 255), caption().c_str());
		}
	}
'''

def p_kbcpp(s):
    pat = re.compile(r'void KeyboardInput::addCharToInput\(char value\)\n\{.*?\n\}\n\n(?=/\* KeyboardLayout \*/)', re.S)
    s, n = pat.subn(lambda m: KB_INPUT, s, count=1)
    if n != 1:
        raise ValueError("KeyboardInput bloku")

    pat = re.compile(r'void KeyboardLayout::Row::Key::draw\(ImGuiRenderer\* renderer\)\n\{\n\tif \(focused\(\)\) renderer->drawRect\(absolutePosition\(\), absolutePosition\(\) \+ size\(\), ImColor\(0xF5, 0x91, 0x32\), true\);\n')
    s, n = pat.subn(lambda m: KB_KEYDRAW, s, count=1)
    if n != 1:
        raise ValueError("Key::draw basi")

    old = "renderer->drawRect(absolutePosition(), absolutePosition() + size(), UISettings::keyboardBackgroundColor(), true);"
    s = once(s, old, "renderer->drawRect(absolutePosition(), absolutePosition() + size(), ImColor(28, 28, 32, 250), true);   // KBIOS_V5", "Keyboard::draw fonu")
    return s

# =============================================================== isle
if __name__ == "__main__":
    gui = os.path.join(CPP, "gui/gui.cpp")
    step("SINXRON: gui.cpp (far+sinyal)",      gui, "VTSYNC_V5", p_vt)
    step("SINXRON: chat.cpp (gizli mesaj)",    os.path.join(CPP, "gui/samp_widgets/chat.cpp"), "VTSYNC_V5", p_chat)
    step("HANDLING: vehicle.cpp (slot yolu)",  os.path.join(CPP, "game/vehicle.cpp"), "HNDV5_PATH", p_slotpath)
    step("HANDLING: jniutil.h",                os.path.join(CPP, "java/jniutil.h"), "PICKFILE_V5", p_jnih)
    step("HANDLING: jniutil.cpp",              os.path.join(CPP, "java/jniutil.cpp"), "PICKFILE_V5", p_jnicpp)
    step("HANDLING: SAMP.java (fayl secici)",  JAVA, "REQ_HND_PICK", p_java)
    step("HANDLING: gui.cpp (Rəqəm|Bar + Fayl seç)", gui, "HNDBAR_V5", p_hnd)
    step("KLAVIATURA: keyboard.h",             os.path.join(CPP, "gui/samp_widgets/keyboard.h"), "KBIOS_V5", p_kbh)
    step("KLAVIATURA: keyboard.cpp",           os.path.join(CPP, "gui/samp_widgets/keyboard.cpp"), "KBIOS_V5", p_kbcpp)
    print("\nBitdi. Geri qaytarmaq: her faylin yaninda *.bakv5")
    print("Serverde vt_sync.pwn filterscript-ini yukle (server.cfg: filterscripts ... vt_sync).")

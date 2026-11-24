#pragma once
// PerfTweaks.h - avtomatik draw distance (far clip) tenzimi.
// Yalniz game.cpp-den include olunur (qlobal deyiserler burada teyin olunur).
//
// Mentiq: FPS hedefin altina dusurse uzaqliq 12%-le azalir, hedefden yuxari olanda yavas-yavas geri qalxir.
// Hedef: settings.ini [gui] PerfTargetFPS (0 = ekranin yenilenme tezliyi, Java terefden verilir; yoxdursa 60).
// Sondurmek ucun: [gui] PerfAuto=0.   Alt hedd: [gui] PerfMinFarClip=450.

#include <stdint.h>
#include <time.h>
#include <algorithm>
#include <jni.h>
#include "TimeCycle.h"

int g_perfAuto   = 1;     // [gui] PerfAuto
int g_perfTarget = 0;     // [gui] PerfTargetFPS
int g_perfMinFar = 450;   // [gui] PerfMinFarClip

namespace PerfTweaks
{
    static float s_refreshHz = 0.0f;     // FpsBoost.java verir
    static float s_cap       = 3000.0f;  // cari uzaqliq limiti (3000 = tehdid yoxdur)
    static bool  s_camTouched = false;

    // CTimeCycle::Update()-den SONRA, her kadr cagirilir
    inline void AfterTimeCycleUpdate()
    {
        if (!g_perfAuto) return;

        CColourSet& cc = CTimeCycle::m_CurrentColours;
        float natural = cc.m_fFarClip;
        const bool live = natural > 50.0f && natural < 20000.0f;
        if (!live) natural = 2000.0f;

        static unsigned frames = 0;
        static uint64_t t0 = 0;
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
        if (t0 == 0) t0 = now;
        ++frames;

        const uint64_t el = now - t0;
        if (el >= 750)
        {
            // yukleme/pauza kimi uzun donmalari olcmeyek
            if (el < 1600 && frames >= 15)
            {
                const float fps    = (float)frames * 1000.0f / (float)el;
                const float target = g_perfTarget > 0 ? (float)g_perfTarget
                                   : (s_refreshHz > 1.0f ? s_refreshHz : 60.0f);
                const float minFar = (float)std::max(g_perfMinFar, 150);
                const float old    = s_cap;

                if (fps < target * 0.85f)
                    s_cap = std::max(minFar, std::min(s_cap, natural) * 0.88f);
                else if (fps > target * 1.10f && s_cap < 3000.0f)
                    s_cap = std::min(3000.0f, s_cap * 1.12f);
                else if (fps > target * 0.95f && s_cap < 3000.0f)
                    s_cap = std::min(3000.0f, s_cap * 1.05f);

                if (s_cap != old)
                    FLog("Perf: fps=%.0f hedef=%.0f uzaqliq=%.0f (tebii %.0f, %s)",
                         fps, target, s_cap, natural, live ? "timecycle" : "kamera");
            }
            frames = 0;
            t0 = now;
        }

        if (s_cap < natural)
        {
            if (live) cc.m_fFarClip = s_cap;
            if (Scene.m_pRwCamera) { RwCameraSetFarClipPlane(Scene.m_pRwCamera, s_cap); s_camTouched = true; }
        }
        else if (s_camTouched)
        {
            // limit tebii deyerden yuxari qalxdi: kamerani bir defe tebii deyere qaytar
            if (Scene.m_pRwCamera) RwCameraSetFarClipPlane(Scene.m_pRwCamera, natural);
            s_camTouched = false;
        }
    }
}

// Java: FpsBoost.nativeSetRefresh(float) - secilen ekran rejiminin yenilenme tezliyi
extern "C" JNIEXPORT void JNICALL Java_com_nvidia_devtech_FpsBoost_nativeSetRefresh(JNIEnv*, jclass, jfloat hz)
{
    PerfTweaks::s_refreshHz = hz;
}

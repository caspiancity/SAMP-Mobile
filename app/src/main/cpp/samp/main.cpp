#include <jni.h>
#include <pthread.h>
#include <syscall.h>
#include <signal.h>
#include <ucontext.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <EGL/egl.h>

#include "main.h"
#include "game/game.h"
#include "net/netgame.h"
#include "gui/gui.h"
#include "playertags.h"
#include "audiostream.h"
#include "java/jniutil.h"
#include <dlfcn.h>
#include "StackTrace.h"
#include <time.h>

// voice
#include "voice_new/Plugin.h"

#include "vendor/armhook/patch.h"
#include "vendor/str_obfuscator/str_obfuscator.hpp"

#include "settings.h"
#include "crashlytics.h"

/*
Peerapol Unarak
*/

JavaVM* javaVM;

char* g_pszStorage = nullptr;

UI* pUI = nullptr;
CGame *pGame = nullptr;

CNetGame *pNetGame = nullptr;
CPlayerTags* pPlayerTags = nullptr;
CSnapShotHelper* pSnapShotHelper = nullptr;
CAudioStream* pAudioStream = nullptr;
CJavaWrapper* pJavaWrapper = nullptr;
CSettings* pSettings = nullptr;

MaterialTextGenerator* pMaterialTextGenerator = nullptr;

bool bDebug = false;
bool bGameInited = false;
bool bNetworkInited = false;

uintptr_t g_libGTASA = 0x00;
uintptr_t g_libSAMP = 0x00;

void ApplyGlobalPatches();
void ApplyPatches_level0();
void ApplyMultiTouchPatches();
void InstallGlobalHooks();
void InstallSpecialHooks();
void InitRenderWareFunctions();
void FLog(const char* fmt, ...);

int work = 0;

// ===== VSync bypass (Uncapped Max FPS) =====
// Evvelki variant eglSwapInterval-i hook edirdi, amma orijinal funksiya hec vaxt
// cagirilmirdi (orig_eglSwapInterval == nullptr idi), buna gore interval
// heรง vaxt 0-a qoyulmurdu. Indi render thread-inde eglSwapInterval(dpy, 0)
// birbasa cagirilir ve oyun onu geri 1-e qaytarsa, periodik olaraq yeniden tetbiq olunur.
typedef EGLDisplay (*eglGetCurrentDisplay_t)(void);
typedef EGLContext (*eglGetCurrentContext_t)(void);
typedef EGLBoolean (*eglSwapInterval_t)(EGLDisplay, EGLint);

static void ApplyUncappedFPS()
{
    static eglGetCurrentDisplay_t pGetDisplay = nullptr;
    static eglGetCurrentContext_t pGetContext = nullptr;
    static eglSwapInterval_t pSwapInterval = nullptr;
    static bool bResolved = false;
    static uint32_t uFrame = 0;

    if (!bResolved)
    {
        bResolved = true;
        pGetDisplay = (eglGetCurrentDisplay_t)dlsym(RTLD_DEFAULT, "eglGetCurrentDisplay");
        pGetContext = (eglGetCurrentContext_t)dlsym(RTLD_DEFAULT, "eglGetCurrentContext");
        pSwapInterval = (eglSwapInterval_t)dlsym(RTLD_DEFAULT, "eglSwapInterval");
    }

    if (!pGetDisplay || !pGetContext || !pSwapInterval) return;

    // her 120 kadrda bir (ilk kadrda da) tetbiq et
    if ((uFrame++ % 120) != 0) return;

    // yalniz EGL context-i olan thread-de ise dussun
    if (pGetContext() == EGL_NO_CONTEXT) return;

    EGLDisplay dpy = pGetDisplay();
    if (dpy == EGL_NO_DISPLAY) return;

    pSwapInterval(dpy, 0);
}

void ReadSettingFile()
{
    if (pSettings) return;

    pSettings = new CSettings();

    if (pSettings)
    {
        firebase::crashlytics::SetUserId(pSettings->Get().szNickName);
    }
}

int hashing(const char* str) {
    int hashing = 5381;
    int c;
    while ((c = *str++)) {
        hashing = ((hashing << 5) + hashing) + c;
        if (hashing < 0) hashing = 100;
    }
    if (hashing < 0) hashing = 100;
    return hashing;
}

void DoDebugLoop()
{
    // ...
}

void DoDebugStuff()
{
    RwMatrix mat = pGame->FindPlayerPed()->m_pPed->GetMatrix().ToRwMatrix();

    for (int i = 0; i < 100; i++)
    {
        CPlayerPed* ped = pGame->NewPlayer(i, mat.pos.x + i, mat.pos.y, mat.pos.z, 0.0f, false, false);
    }
}

// ===== Crash handler =====
// Evvel 4 eyni handler var idi ve her biri ONCE kohne handler-i cagirirdi
// (crashlytics/sistem prosesi oldururdu, log yazilmamis qalirdi).
// Ayrica struct sigaction-lar sifirlanmamisdi. Indi: bir handler, evvel log, sonra zencir.
static struct sigaction g_oldActions[NSIG];
static volatile sig_atomic_t g_bInCrash = 0;

extern int g_iLastProcessedSkinCollision, g_iLastProcessedEntityCollision, g_iLastRenderedObject;
extern uintptr_t g_dwLastRetAddrCrash;

static const char* GetSignalName(int signum)
{
    switch (signum)
    {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGFPE:  return "SIGFPE";
    case SIGBUS:  return "SIGBUS";
    default:      return "SIGNAL";
    }
}

static void CrashHandler(int signum, siginfo_t *info, void* contextPtr)
{
    ucontext_t* context = (ucontext_t*)contextPtr;

    if (!g_bInCrash)
    {
        g_bInCrash = 1;

        FLog("%s | Fault address: %p", GetSignalName(signum), info ? info->si_addr : nullptr);
        PRINT_CRASH_STATES(context);
        CStackTrace::printBacktrace();
    }

    struct sigaction& old = g_oldActions[signum];

    if ((old.sa_flags & SA_SIGINFO) && old.sa_sigaction)
    {
        old.sa_sigaction(signum, info, contextPtr);
        return;
    }

    if (!(old.sa_flags & SA_SIGINFO) &&
        old.sa_handler != SIG_DFL &&
        old.sa_handler != SIG_IGN &&
        old.sa_handler != nullptr)
    {
        old.sa_handler(signum);
        return;
    }

    // Kohne handler yoxdur -> default-a qaytar ki, proses sonsuz dovre dusmesin
    signal(signum, SIG_DFL);
}

static void InstallCrashHandlers()
{
    const int signals[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS };

    for (int sig : signals)
    {
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_sigaction = CrashHandler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = SA_SIGINFO;
        sigaction(sig, &act, &g_oldActions[sig]);
    }
}

void DoInitStuff()
{
    if (bGameInited == false)
    {
        // UI hazir deyilse (InitGui hele cagirilmayib) gozle
        if (!pUI) return;

        pPlayerTags = new CPlayerTags();
        pSnapShotHelper = new CSnapShotHelper();
        pMaterialTextGenerator = new MaterialTextGenerator();
        pAudioStream = new CAudioStream();
        pAudioStream->Initialize();

        pUI->splashscreen()->setVisible(false);
        pUI->chat()->setVisible(true);
        pUI->buttonpanel()->setVisible(true);

        pGame->Initialize();
        pGame->SetMaxStats();
        pGame->ToggleThePassingOfTime(false);

        LogVoice("[dbg:samp:load] : module loaded");

        if (bDebug)
        {
            CCamera& TheCamera = *reinterpret_cast<CCamera*>(g_libGTASA + (VER_x32 ? 0x00951FA8 : 0xBBA8D0));
            CCamera::SetBehindPlayer();
            pGame->DisplayHUD(true);
            pGame->EnableClock(false);

            DoDebugStuff();
        }

        bGameInited = true;
    }

    if (!bNetworkInited && !bDebug)
    {
        ReadSettingFile();

        if (pSettings)
        {
            pNetGame = new CNetGame(
                pSettings->Get().szHost, 
                pSettings->Get().iPort, 
                pSettings->Get().szNickName, 
                pSettings->Get().szPassword
            );
        }
        else
        {
            pNetGame = new CNetGame("127.0.0.1", 7777, "Player", "");
        }

        bNetworkInited = true;

        FLog("DoInitStuff end");
    }
}

extern "C" {
    JNIEXPORT void JNICALL Java_com_samp_mobile_game_SAMP_initializeSAMP(JNIEnv *pEnv, jobject thiz)
    {
        pJavaWrapper = new CJavaWrapper(pEnv, thiz);
    }

    JNIEXPORT void JNICALL Java_com_samp_mobile_game_SAMP_onInputEnd(JNIEnv *pEnv, jobject thiz, jbyteArray str)
    {
        if(pUI)
        {
            pUI->keyboard()->sendForGB(pEnv, thiz, str);
        }
    }

    JNIEXPORT void JNICALL Java_com_samp_mobile_game_SAMP_onEventBackPressed(JNIEnv *pEnv, jobject thiz)
    {
        if(pSettings && pJavaWrapper)
        {
            if(pSettings->Get().iAndroidKeyboard)
                pJavaWrapper->HideKeyboard();
        }
    }

    JNIEXPORT void JNICALL Java_com_samp_mobile_game_ui_dialog_DialogManager_sendDialogResponse(JNIEnv* pEnv, jobject thiz, jint i3, jint i, jint i2, jbyteArray str)
    {
        if (!str) return;

        jbyte* pMsg = pEnv->GetByteArrayElements(str, nullptr);
        if (!pMsg) return;

        jsize length = pEnv->GetArrayLength(str);

        std::string szStr((char*)pMsg, length);

        if(pNetGame) {
            pNetGame->SendDialogResponse(i, i3, i2, (char*)szStr.c_str());
        }

        pEnv->ReleaseByteArrayElements(str, pMsg, JNI_ABORT);
    }
}


static void LogMainLoopFPS()
{
    static uint64_t last = 0;
    static int frames = 0;
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
    frames++;
    if (now - last >= 1000) {
        FLog("MainLoop FPS: %d", frames);
        frames = 0;
        last = now;
    }
}

void MainLoop()
{
    if (!pGame || pGame->bIsGameExiting) return;

    DoInitStuff();

    if (bDebug) {
        DoDebugLoop();
    }

    if (pNetGame) {
        pNetGame->Process();
    }

    if (pAudioStream) {
        pAudioStream->Process();
    }

    ApplyUncappedFPS();
}

void InitGui()
{
    /* Samp-voice bypass to prevent crash on Android 15
    if (pSettings && pSettings->Get().bVoiceChatEnable) {
        Plugin::OnPluginLoad();
        Plugin::OnSampLoad();
    }
    */

    std::string font_path = string_format("%sSAMP/fonts/%s", g_pszStorage, FONT_NAME);
    pUI = new UI(ImVec2(RsGlobal->maximumWidth, RsGlobal->maximumHeight), font_path.c_str());
    pUI->initialize();
    pUI->performLayout();
}

#include "game/multitouch.h"
#include "armhook/patch.h"
#include "util/CUtil.h"

void SetUpGLHooks();

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    javaVM = vm;
    LOGI("SA-MP library loaded! Build time: " __DATE__ " " __TIME__);

    g_libGTASA = CUtil::FindLib("libGTASA.so");
    if (g_libGTASA == 0x00) {
        LOGE("libGTASA.so address was not found! ");
        return JNI_VERSION_1_6;
    }

    g_libSAMP = CUtil::FindLib("libsamp.so");
    if (g_libSAMP == 0x00) {
        LOGE("libsamp.so address was not found! ");
        return JNI_VERSION_1_6;
    }

    firebase::crashlytics::Initialize();

    uintptr_t libgtasa = g_libGTASA;
    uintptr_t libsamp = g_libSAMP;
    uintptr_t libc = CUtil::FindLib("libc.so");

    FLog("libGTASA.so: 0x%" PRIxPTR, libgtasa);
    FLog("libsamp.so: 0x%" PRIxPTR, libsamp);
    FLog("libc.so: 0x%" PRIxPTR, libc);

    char str[100];

    snprintf(str, sizeof(str), "0x%" PRIxPTR, libgtasa);
    firebase::crashlytics::SetCustomKey("libGTASA.so", str);

    snprintf(str, sizeof(str), "0x%" PRIxPTR, libsamp);
    firebase::crashlytics::SetCustomKey("libsamp.so", str);

    snprintf(str, sizeof(str), "0x%" PRIxPTR, libc);
    firebase::crashlytics::SetCustomKey("libc.so", str);

    CHook::InitHookStuff();
    InstallSpecialHooks();
    ApplyPatches_level0();
    InitRenderWareFunctions();
    MultiTouch::initialize();

    pGame = new CGame();

    InstallCrashHandlers();

    return JNI_VERSION_1_6;
}

uint32_t GetTickCount()
{
    return CTimer::m_snTimeInMillisecondsNonClipped;
}        

void FLog(const char* fmt, ...)
{
    char buffer[0xFF];
    static FILE* flLog = nullptr;
    const char* pszStorage = g_pszStorage;

    if (flLog == nullptr && pszStorage != nullptr)
    {
        snprintf(buffer, sizeof(buffer), "%s/samp_log.txt", pszStorage);
        flLog = fopen(buffer, "a");
    }

    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);

    LOGI("%s", buffer);
    firebase::crashlytics::Log(buffer);

    if (flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);

    return;
}

void ChatLog(const char* fmt, ...)
{
    char buffer[0xFF];
    static FILE* flLog = nullptr;
    const char* pszStorage = g_pszStorage;

    if (flLog == nullptr && pszStorage != nullptr)
    {
        snprintf(buffer, sizeof(buffer), "%s/chat_log.txt", pszStorage);
        flLog = fopen(buffer, "a");
    }

    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);

    if (flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);

    return;
}

void MyLog(const char* fmt, ...)
{
    char buffer[0xFF];
    static FILE* flLog = nullptr;
    const char* pszStorage = g_pszStorage;

    if (flLog == nullptr && pszStorage != nullptr)
    {
        snprintf(buffer, sizeof(buffer), "%s/samp_log.txt", pszStorage);
        flLog = fopen(buffer, "a");
    }

    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);

    if (flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);

    return;
}

void MyLog2(const char* fmt, ...)
{
    char buffer[0xFF];
    static FILE* flLog = nullptr;
    const char* pszStorage = g_pszStorage;

    if (flLog == nullptr && pszStorage != nullptr)
    {
        snprintf(buffer, sizeof(buffer), "%s/samp_log.txt", pszStorage);
        flLog = fopen(buffer, "a");
    }

    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);

    if (pUI) pUI->chat()->addDebugMessage(buffer);

    if (flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);
    return;
}

void LogVoice(const char* fmt, ...)
{
    char buffer[0xFF];
    static FILE* flLog = nullptr;
    const char* pszStorage = g_pszStorage;

    if (flLog == nullptr && pszStorage != nullptr)
    {
        snprintf(buffer, sizeof(buffer), "%sSAMP/%s", pszStorage, SV::kLogFileName);
        flLog = fopen(buffer, "w");
    }

    memset(buffer, 0, sizeof(buffer));

    va_list arg;
    va_start(arg, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, arg);
    va_end(arg);

    __android_log_write(ANDROID_LOG_INFO, "AXL", buffer);

    if (flLog == nullptr) return;
    fprintf(flLog, "%s\n", buffer);
    fflush(flLog);

    return;
}
#include "main.h"
#include "settings.h"
#include "vendor/inih/cpp/INIReader.h"

#include "vendor/SimpleIni/SimpleIni.h"
#include "game/game.h"

extern CGame *pGame;

// ===== CRASHLOG_PATCH13 / STABIL_PATCH13 =====
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <unwind.h>
#include <ucontext.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <chrono>

namespace crashlog
{
    struct BtState { void** cur; void** end; };

    static _Unwind_Reason_Code UnwindCb(struct _Unwind_Context* ctx, void* arg)
    {
        BtState* s = (BtState*)arg;
        const uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
        if (pc) {
            if (s->cur == s->end) return _URC_END_OF_STACK;
            *s->cur++ = (void*)pc;
        }
        return _URC_NO_REASON;
    }

    static char g_path[300] = "";
    static struct sigaction g_old[NSIG];
    static volatile sig_atomic_t g_in = 0;
    static const int kSigs[] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSYS };

    static void Out(int fd, const char* s) { if (fd >= 0) { ssize_t r = write(fd, s, strlen(s)); (void)r; } }

    static void Frame(int fd, int idx, uintptr_t pc)
    {
        char line[420];
        Dl_info di;
        memset(&di, 0, sizeof(di));
        if (dladdr((void*)pc, &di) && di.dli_fname) {
            const char* base = strrchr(di.dli_fname, '/');
            base = base ? base + 1 : di.dli_fname;
            snprintf(line, sizeof(line), "  #%02d  %s + 0x%lx   [%s]\n", idx, base,
                     (unsigned long)(pc - (uintptr_t)di.dli_fbase), di.dli_sname ? di.dli_sname : "?");
        } else {
            snprintf(line, sizeof(line), "  #%02d  pc 0x%lx (modul tapilmadi)\n", idx, (unsigned long)pc);
        }
        Out(fd, line);
    }

    static void Handler(int sig, siginfo_t* info, void* uc)
    {
        if (!g_in) {
            g_in = 1;
            const int fd = open(g_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (fd >= 0) {
                char buf[360];
                snprintf(buf, sizeof(buf), "\n===== CRASH signal=%d code=%d addr=%p time=%ld build=%s %s =====\n",
                         sig, info ? info->si_code : 0, info ? info->si_addr : nullptr, (long)time(nullptr), __DATE__, __TIME__);
                Out(fd, buf);

                uintptr_t pc = 0, lr = 0;
                ucontext_t* c = (ucontext_t*)uc;
#if defined(__aarch64__)
                if (c) { pc = (uintptr_t)c->uc_mcontext.pc; lr = (uintptr_t)c->uc_mcontext.regs[30]; }
#elif defined(__arm__)
                if (c) { pc = (uintptr_t)c->uc_mcontext.arm_pc; lr = (uintptr_t)c->uc_mcontext.arm_lr; }
#elif defined(__x86_64__)
                if (c) { pc = (uintptr_t)c->uc_mcontext.gregs[REG_RIP]; }
#endif
                Out(fd, "cokme yeri (PC) ve LR:\n");
                Frame(fd, 0, pc);
                if (lr) Frame(fd, 1, lr);

                Out(fd, "backtrace:\n");
                void* frames[40];
                BtState st{ frames, frames + 40 };
                _Unwind_Backtrace(UnwindCb, &st);
                for (void** p = frames; p < st.cur; ++p) Frame(fd, (int)(p - frames), (uintptr_t)*p);
                close(fd);
            }
        }

        // evvelki handler-e ver (Firebase ve s.)
        const struct sigaction& o = g_old[sig];
        if (o.sa_flags & SA_SIGINFO) {
            if (o.sa_sigaction) { o.sa_sigaction(sig, info, uc); return; }
        } else if (o.sa_handler != SIG_DFL && o.sa_handler != SIG_IGN && o.sa_handler) {
            o.sa_handler(sig);
            return;
        }
        signal(sig, SIG_DFL);
        raise(sig);
    }

    static void Install(const char* path)
    {
        snprintf(g_path, sizeof(g_path), "%s", path);
        static char altStack[64 * 1024];
        stack_t ss;
        ss.ss_sp = altStack;
        ss.ss_size = sizeof(altStack);
        ss.ss_flags = 0;
        sigaltstack(&ss, nullptr);

        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = Handler;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        for (size_t i = 0; i < sizeof(kSigs) / sizeof(kSigs[0]); ++i)
            sigaction(kSigs[i], &sa, &g_old[kSigs[i]]);
    }
}

// 150ms-den uzun kadrlari SAMP/perf.log-a yazir (CGame::Process her kadr cagirir)
void PerfLog_Frame()
{
    using namespace std::chrono;
    static steady_clock::time_point last = steady_clock::now();
    static int written = 0;
    const steady_clock::time_point now = steady_clock::now();
    const float ms = duration<float, std::milli>(now - last).count();
    last = now;
    if (ms < 150.0f || ms > 4000.0f || written >= 300 || !g_pszStorage) return;

    char path[0x100];
    snprintf(path, sizeof(path), "%sSAMP/perf.log", g_pszStorage);
    FILE* f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "%ld  kadr=%.0fms\n", (long)time(nullptr), ms);
    fclose(f);
    ++written;
}
// ===== /CRASHLOG_PATCH13 =====

CSettings::CSettings()
{
	FLog("Loading settings..");	

	char buff[0x7F];
	sprintf(buff, "%sSAMP/settings.ini", g_pszStorage);

	INIReader reader(buff);

	if(reader.ParseError() < 0)
	{
		FLog("Error: can't load %s", buff);
		std::terminate();
		return;
	}

	// client
	size_t length = 0;
	sprintf(buff, "__android_%d%d", rand() % 1000, rand() % 1000);
	length = reader.Get("client", "name", buff).copy(m_Settings.szNickName, 24);
	m_Settings.szNickName[length] = '\0';
	length = reader.Get("client", "host", "127.0.0.1").copy(m_Settings.szHost, MAX_SETTINGS_STRING);
	m_Settings.szHost[length] = '\0';
	length = reader.Get("client", "password", "").copy(m_Settings.szPassword, MAX_SETTINGS_STRING);
	m_Settings.szPassword[length] = '\0';
    length = reader.Get("client", "version", "0.3.7").copy(m_Settings.szVersion, MAX_SETTINGS_STRING);
    m_Settings.szVersion[length] = '\0';
	m_Settings.iPort = reader.GetInteger("client", "port", 7777);
    m_Settings.bAutoAim = reader.GetBoolean("client", "autoaim", false);

	// debug
	m_Settings.bDebug = reader.GetBoolean("debug", "debug", false);
	m_Settings.bOnline = reader.GetBoolean("debug", "online", true);

	// gui
	length = reader.Get("gui", "Font", "arial.ttf").copy(m_Settings.szFont, MAX_SETTINGS_STRING);
	m_Settings.szFont[length] = '\0';
	m_Settings.fFontSize = reader.GetReal("gui", "FontSize", 30.0f);
	m_Settings.iFontOutline = reader.GetInteger("gui", "FontOutline", 2);

	// chat
	m_Settings.fChatPosX = reader.GetReal("gui", "ChatPosX", 325.0f);
	m_Settings.fChatPosY = reader.GetReal("gui", "ChatPosY", 25.0f);
	m_Settings.fChatSizeX = reader.GetReal("gui", "ChatSizeX", 1150.0f);
	m_Settings.fChatSizeY = reader.GetReal("gui", "ChatSizeY", 220.0f);
	m_Settings.iChatMaxMessages = reader.GetInteger("gui", "ChatMaxMessages", 6);

	// spawnscreen
	m_Settings.fSpawnScreenPosX = reader.GetReal("gui", "SpawnScreenPosX", 660.0f);
	m_Settings.fSpawnScreenPosY = reader.GetReal("gui", "SpawnScreenPosY", 950.0f);
	m_Settings.fSpawnScreenSizeX = reader.GetReal("gui", "SpawnScreenSizeX", 600.0f);
	m_Settings.fSpawnScreenSizeY = reader.GetReal("gui", "SpawnScreenSizeY", 100.0f);

	// nametags
	m_Settings.fHealthBarWidth = reader.GetReal("gui", "HealthBarWidth", 100.0f);
	m_Settings.fHealthBarHeight = reader.GetReal("gui", "HealthBarHeight", 10.0f);

	// scoreboard
	m_Settings.fScoreBoardSizeX = reader.GetReal("gui", "ScoreBoardSizeX", 846.0f);
	m_Settings.fScoreBoardSizeY = reader.GetReal("gui", "ScoreBoardSizeY", 614.0f);

	// passenger
	m_Settings.bPassengerUseTexture = reader.GetBoolean("gui", "PassengerUseTexture", true);
	m_Settings.fPassengerTextureSize = reader.GetReal("gui", "PassengerTextureSize", 30.0f);
	m_Settings.fPassengerTextureX = reader.GetReal("gui", "PassengerTexturePosX", 120.0f);
	m_Settings.fPassengerTextureY = reader.GetReal("gui", "PassengerTexturePosY", 430.0f);

	m_Settings.iDialog = reader.GetBoolean("gui", "Dialog", true);

	m_Settings.bVoiceChatEnable = reader.GetBoolean("gui", "VoiceChatEnable", false);
	m_Settings.iVoiceChatKey = reader.GetInteger("gui", "VoiceChatKey", 66);
	m_Settings.fVoiceChatSize = reader.GetReal("gui", "VoiceChatSize", 30.0f);
	m_Settings.fVoiceChatPosX = reader.GetReal("gui", "VoiceChatPosX", 1520.0f);
	m_Settings.fVoiceChatPosY = reader.GetReal("gui", "VoiceChatPosY", 480.0f);

	m_Settings.iAndroidKeyboard = reader.GetBoolean("gui", "androidkeyboard", false);
	m_Settings.iFirstPerson = reader.GetBoolean("gui", "firstperson", true);
	m_Settings.iCutout = reader.GetBoolean("gui", "cutout", false);
	m_Settings.iFPSCounter = reader.GetBoolean("gui", "fps", true);
	m_Settings.iFPSCount = reader.GetInteger("gui", "FPSLimit", 120);
	m_Settings.iHPArmourText = reader.GetBoolean("gui", "hparmourtext", false);
	m_Settings.iOutfitGuns = reader.GetBoolean("gui", "outfitguns", false);
	m_Settings.iPCMoney = reader.GetBoolean("gui", "pcmoney", false);
	m_Settings.iRadarRect = reader.GetBoolean("gui", "radarrect", false);
	m_Settings.iSkyBox = reader.GetBoolean("gui", "skybox", false);
	m_Settings.iSnow = reader.GetBoolean("gui", "snow", false);
	FLog("Settings loaded.");

	{   // CRASHLOG
		char cpath[0x100];
		snprintf(cpath, sizeof(cpath), "%sSAMP/crash.log", g_pszStorage);
		crashlog::Install(cpath);
	}
}

const stSettings& CSettings::GetReadOnly()
{
    return m_Settings;
}

stSettings& CSettings::GetWrite()
{
    return m_Settings;
}

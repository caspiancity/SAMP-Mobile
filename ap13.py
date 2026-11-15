#!/usr/bin/env python3
# apply_patch13.py  --  STABILLIK
#  1) crash logger: oyun cokende SAMP/crash.log-a modul+offset ile backtrace yazilir
#  2) perf logger : 150ms-den uzun kadrlar SAMP/perf.log-a yazilir (FPS dusmesinin sebebini tapmaq ucun)
#  3) hidravlik / nitro: komponentler RPC-nin icinde yox, kadr emalinda (CVehiclePool::Process) 1-1 tetbiq olunur
#  4) /cam komandasi silinir
# Hem temiz bazada, hem de kohne patch-lerin ustunde isleyir. Her fayl ya tam deyisir, ya hec.
import os, re, shutil

ROOT = os.getcwd()
CPP = os.path.join(ROOT, "app/src/main/cpp/samp")
P = {
    "settings":  os.path.join(CPP, "settings.cpp"),
    "game":      os.path.join(CPP, "game/game.cpp"),
    "events":    os.path.join(CPP, "game/events.cpp"),
    "vehicle":   os.path.join(CPP, "game/vehicle.cpp"),
    "netrpc":    os.path.join(CPP, "net/netrpc.cpp"),
    "poolh":     os.path.join(CPP, "net/vehiclepool.h"),
    "poolc":     os.path.join(CPP, "net/vehiclepool.cpp"),
    "chat":      os.path.join(CPP, "gui/samp_widgets/chat.cpp"),
}
MARK = "STABIL_PATCH13"

def rd(p):
    with open(p, encoding="utf-8") as f: return f.read()

def sub1(pat, repl, s, flags=0):
    new, n = re.subn(pat, (lambda m: repl) if isinstance(repl, str) else repl, s, count=1, flags=flags)
    if n != 1: raise ValueError(pat[:70])
    return new

def run(name, key, fn, marker=MARK):
    path = P[key]
    if not os.path.exists(path):
        print("[YOX]   %s: %s tapilmadi" % (name, os.path.relpath(path, ROOT))); return
    s = rd(path)
    if marker in s:
        print("[KECDI] %s: artiq tetbiq olunub" % name); return
    try:
        n = fn(s)
    except ValueError as e:
        print("[KECDI] %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); return
    if not os.path.exists(path + ".bak13"): shutil.copy(path, path + ".bak13")
    with open(path, "w", encoding="utf-8") as f: f.write(n)
    print("[OK]    %s" % name)

# ================================================================ 1+2: settings.cpp
CRASH_BLOCK = r'''
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
'''

def p_settings(s):
    s = sub1(r'(extern CGame \*pGame;[^\n]*\n)', lambda m: m.group(1) + CRASH_BLOCK, s)
    s = sub1(r'(\tFLog\("Settings loaded\."\);[^\n]*\n)', lambda m: m.group(1) +
             '\n\t{   // CRASHLOG_PATCH13\n'
             '\t\tchar cpath[0x100];\n'
             '\t\tsnprintf(cpath, sizeof(cpath), "%sSAMP/crash.log", g_pszStorage);\n'
             '\t\tcrashlog::Install(cpath);\n'
             '\t}\n', s)
    return s

def p_game(s):
    return sub1(r'(void CGame::Process\(\)\s*\{\s*if\s*\(bIsGameExiting\)\s*return;)',
                lambda m: m.group(1) + '\n    PerfLog_Frame();   // STABIL_PATCH13', s) \
        .replace('void CGame::Process()', 'void PerfLog_Frame();   // STABIL_PATCH13 (settings.cpp)\nvoid CGame::Process()', 1)

# ================================================================ 3: komponentler
def p_events(s):
    new = ('case EVENT_TYPE_CARCOMPONENT:   // STABIL_PATCH13: kadr emalinda tetbiq olunur\n'
           '        if (pVehiclePool) pVehiclePool->QueueComponent((VEHICLEID)dwParam1, (int)dwParam2);\n'
           '\t\tbreak;')
    return sub1(r'case EVENT_TYPE_CARCOMPONENT:.*?break;', new, s, re.S)

def p_vehicle(s):
    new = r'''void CVehicle::AddComponent(int iComponentID)   // STABIL_PATCH13
{
    if (!m_pVehicle || !GamePool_Vehicle_GetAt(m_dwGTAId)) return;
    if (GetVehicleSubtype() != VEHICLE_SUBTYPE_CAR) return;
    if (iComponentID < 1000 || iComponentID > 1193) return;

    ScriptCommand(&request_car_component, iComponentID);
    if (!CStreaming::TryLoadModel(iComponentID)) return;
    if (!ScriptCommand(&is_component_available, iComponentID)) return;

    uint32_t dwRet;
    ScriptCommand(&add_car_component, m_dwGTAId, iComponentID, &dwRet);
}'''
    return sub1(r'void CVehicle::AddComponent\(int iComponentID\)\s*\{.*?\n\}', lambda m: new, s, re.S)

VEH_ADD = r'''void WorldVehicleAdd(RPCParameters* rpcParams)   // STABIL_PATCH13
{
	Log::traceLastFunc("[RPC-IN] World vehicle add");

	unsigned char* Data = reinterpret_cast<unsigned char*>(rpcParams->input);
	int iBitLength = rpcParams->numberOfBitsOfData;

	NEW_VEHICLE VehicleInfo;
	RakNet::BitStream bsData(Data, (iBitLength / 8) + 1, false);

	CVehiclePool* pVehiclePool = pNetGame->GetVehiclePool();
	if (!pVehiclePool) return;

	memset(&VehicleInfo, 0, sizeof(NEW_VEHICLE));
	bsData.Read((char*)& VehicleInfo, sizeof(NEW_VEHICLE));

	if (VehicleInfo.iVehicleType < 400 || VehicleInfo.iVehicleType > 611) return;

	if (!pVehiclePool->New(&VehicleInfo)) return;

	// hazir modlar: burada yox, CVehiclePool::Process-de kadrda 1 dene tetbiq olunur
	for (int i = 0; i < 14; i++)
	{
		const uint8_t byteMod = VehicleInfo.byteModSlots[i];
		if (byteMod) pVehiclePool->QueueComponent(VehicleInfo.VehicleID, byteMod + 999);
	}
}
'''

def p_netrpc(s):
    f = s.find("void WorldVehicleAdd(")
    e = s.find("void WorldVehicleRemove(")
    if f < 0 or e < 0 or e < f: raise ValueError("WorldVehicleAdd/Remove")
    region = s[f:e]
    tail = ""
    stripped = region.rstrip()
    if stripped.endswith("// 0.3.7"):
        tail = "// 0.3.7\n"
    return s[:f] + VEH_ADD + tail + s[e:]

def p_poolh(s):
    s = sub1(r'(#pragma once\s*\n)', lambda m: m.group(1) + '#include <vector>   // STABIL_PATCH13\n#include <utility>\n', s)
    s = sub1(r'([ \t]*void NotifyVehicleDeath\(VEHICLEID VehicleID\);[^\n]*\n)', lambda m: m.group(1) +
             '\tvoid QueueComponent(VEHICLEID VehicleID, int iComponentID);   // STABIL_PATCH13\n', s)
    s = sub1(r'([ \t]*CVehicle\* m_pVehicles\[MAX_VEHICLES\];[^\n]*\n)', lambda m: m.group(1) +
             '\tstd::vector<std::pair<VEHICLEID, int>> m_pendingComponents;   // STABIL_PATCH13\n', s)
    return s

def p_poolc(s):
    # Delete: bu masinin gozleyen komponentlerini sil
    s = sub1(r'(bool CVehiclePool::Delete\(VEHICLEID VehicleID\)\s*\{\s*if\(!GetSlotState\(VehicleID\) \|\| !m_pVehicles\[VehicleID\]\)\s*return false;)',
             lambda m: m.group(1) +
             '\n\n    for (size_t i = 0; i < m_pendingComponents.size();)   // STABIL_PATCH13\n'
             '        if (m_pendingComponents[i].first == VehicleID) m_pendingComponents.erase(m_pendingComponents.begin() + i);\n'
             '        else ++i;', s)
    queue = r'''void CVehiclePool::QueueComponent(VEHICLEID VehicleID, int iComponentID)   // STABIL_PATCH13
{
    if (VehicleID >= MAX_VEHICLES || m_pendingComponents.size() > 512) return;
    m_pendingComponents.emplace_back(VehicleID, iComponentID);
}

'''
    s = sub1(r'(void CVehiclePool::Process\(\)\s*\{)', lambda m: queue + m.group(1) +
             '\n    // STABIL_PATCH13: bir kadrda en cox 1 komponent (modelin yuklenmesi donma/crash vermesin)\n'
             '    if (!m_pendingComponents.empty())\n'
             '    {\n'
             '        const std::pair<VEHICLEID, int> item = m_pendingComponents.front();\n'
             '        m_pendingComponents.erase(m_pendingComponents.begin());\n'
             '        CVehicle* pV = GetAt(item.first);\n'
             '        if (pV && pV->m_pVehicle && m_bIsActive[item.first] && !m_bIsWasted[item.first]) {\n'
             '            try { pV->AddComponent(item.second); } catch (...) {}\n'
             '        }\n'
             '    }\n', s)
    return s

# ================================================================ 4: /cam silinir
def p_chat(s):
    return sub1(r'[ \t]*if \(cmd == "/cam"\) \{.*?return true;\s*\}\n\n?', '', s, re.S)

run("settings.cpp: crash logger + perf logger", "settings", p_settings)
run("game.cpp: kadr olcusu (perf.log)", "game", p_game)
run("events.cpp: komponent -> novbe", "events", p_events)
run("vehicle.cpp: AddComponent (request + yoxlama)", "vehicle", p_vehicle)
run("netrpc.cpp: WorldVehicleAdd (modlar novbeye)", "netrpc", p_netrpc)
run("vehiclepool.h: QueueComponent", "poolh", p_poolh)
run("vehiclepool.cpp: novbe + kadrda tetbiq", "poolc", p_poolc)
run("chat.cpp: /cam silindi", "chat", p_chat, marker="@@YOX@@")
print("\nBitdi. Geri qaytarmaq ucun her faylin yaninda *.bak13 var.")
print("Crash olsa: SAMP/crash.log   |   FPS dusmeleri: SAMP/perf.log  (settings.ini ile eyni qovluq)")

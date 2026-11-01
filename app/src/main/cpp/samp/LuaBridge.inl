// =====================================================================
//  LuaBridge.inl
//
//  Bu fayl LuaManager.cpp-nin icinde #include "LuaBridge.inl" ile
//  daxil edilir (ayri .cpp DEYIL) -> CMakeLists.txt-e toxunmaq lazim deyil.
//  LuaManager.cpp ile eyni qovluqda dayansin.
//
//  Icindekiler:
//    1) findSymbol / findSymbols / symbolInfo   (.symtab diskden oxunur)
//    2) hook / unhook / argf / setArg / setArgf (Lua-dan inline hook)
//    3) java*                                   (JNI: sinif, metod, sahe)
//    4) files/lua/*.lua modul yukleyicisi
//    5) saveData / loadData / deleteData
//    6) readHex / patchCode / bitsToFloat / floatToBits
// =====================================================================
#include "LuaBridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <jni.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

std::recursive_timed_mutex g_luaMutex;

namespace LuaBridge {

// =====================================================================
//  Kilid
// =====================================================================
static thread_local int t_lockDepth = 0;

struct Guard {
    bool ok;
    // timeoutMs < 0  -> limitsiz gozle
    explicit Guard(int timeoutMs = -1) : ok(false) {
        if (timeoutMs < 0) { g_luaMutex.lock(); ok = true; }
        else ok = g_luaMutex.try_lock_for(std::chrono::milliseconds(timeoutMs));
        if (ok) t_lockDepth++;
    }
    ~Guard() { if (ok) { t_lockDepth--; g_luaMutex.unlock(); } }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

// Orijinal native funksiyani cagirarken kilidi (bu thread-in butun derinliyi ile) buraxiriq
static int ReleaseAll() {
    int n = t_lockDepth;
    for (int i = 0; i < n; i++) g_luaMutex.unlock();
    t_lockDepth = 0;
    return n;
}
static void ReacquireAll(int n) {
    for (int i = 0; i < n; i++) g_luaMutex.lock();
    t_lockDepth = n;
}

static lua_State* s_main = nullptr;

static int Fail(lua_State* S, const char* msg) {
    lua_pushnil(S);
    lua_pushstring(S, msg);
    return 2;
}

// Render thread-i olmayan yerlerden gelen xetalar burada yigilir, DispatchRender chata yazir
static std::vector<std::string> s_errq;
static void QueueError(const char* msg) {
    FLog("[Lua] %s", msg);
    if (s_errq.size() < 20) s_errq.push_back(msg);
}
static void FlushErrors() {
    if (s_errq.empty()) return;
    if (pUI) for (size_t i = 0; i < s_errq.size(); i++)
        pUI->chat()->addDebugMessage("{FF0000}[Lua]: %s", s_errq[i].c_str());
    s_errq.clear();
}
// stack-in yuxarisindaki xeta mesajini yazir ve pop edir (render thread-i ucun)
static void ReportTop(lua_State* S, const char* what) {
    const char* err = lua_tostring(S, -1);
    FLog("[Lua] %s: %s", what, err ? err : "?");
    if (pUI && err) pUI->chat()->addDebugMessage("{FF0000}[Lua/%s]: %s", what, err);
    lua_pop(S, 1);
}

static float BitsToFloat(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static uint32_t FloatToBits(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }

static bool IsIntegerValue(lua_State* S, int idx) {
#if LUA_VERSION_NUM >= 503
    return lua_isinteger(S, idx) != 0;
#else
    lua_Number n = lua_tonumber(S, idx);
    return n == (lua_Number)(lua_Integer)n;
#endif
}

// =====================================================================
//  1) SIMVOL AXTARISI  (libsamp.so-nun oz .symtab cedveli, diskden)
// =====================================================================
struct SymEnt { uintptr_t addr; uint8_t bind; };
struct LibSyms {
    bool tried = false;
    std::string err;
    uintptr_t bias = 0;
    std::unordered_map<std::string, SymEnt> map;
};
static std::unordered_map<std::string, LibSyms> s_syms;

static bool PRead(int fd, unsigned long long off, void* buf, size_t n) {
    uint8_t* p = (uint8_t*)buf;
    while (n) {
        ssize_t r = pread64(fd, p, n, (off64_t)off);
        if (r <= 0) return false;
        p += r; off += (unsigned long long)r; n -= (size_t)r;
    }
    return true;
}

struct PhdrFind { const char* lib; uintptr_t bias; std::string name; bool found; };
static int PhdrCb(struct dl_phdr_info* info, size_t, void* data) {
    PhdrFind* c = (PhdrFind*)data;
    if (info->dlpi_name && info->dlpi_name[0] && strstr(info->dlpi_name, c->lib)) {
        c->bias = (uintptr_t)info->dlpi_addr;
        c->name = info->dlpi_name;
        c->found = true;
        return 1;
    }
    return 0;
}

// lib-in yuklenme bazasi + diskdeki yolu + ELF-in fayl daxilindeki baslangic ofseti
static bool LocateLib(const char* lib, uintptr_t& bias, std::string& path,
                      unsigned long long& fileOff, std::string& err) {
    PhdrFind pf{ lib, 0, std::string(), false };
    dl_iterate_phdr(PhdrCb, &pf);
    if (!pf.found) { err = std::string(lib) + " yuklenmeyib"; return false; }
    bias = pf.bias;

    path.clear();
    fileOff = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if (f) {
        char line[1024];
        std::string firstByName;
        unsigned long long firstByNameOff = 0;
        bool gotExact = false;
        while (fgets(line, sizeof(line), f)) {
            unsigned long long s = 0, e = 0, off = 0;
            char perms[8] = { 0 };
            int n = 0;
            if (sscanf(line, "%llx-%llx %7s %llx %*s %*s %n", &s, &e, perms, &off, &n) < 4 || n <= 0) continue;
            std::string p = line + n;
            while (!p.empty() && (p.back() == '\n' || p.back() == ' ' || p.back() == '\r')) p.pop_back();
            if (p.empty()) continue;
            size_t bang = p.find("!/");
            if (bang != std::string::npos) p = p.substr(0, bang);
            if (s == (unsigned long long)bias) {
                path = p; fileOff = off; gotExact = true; break;
            }
            if (firstByName.empty() && strstr(line, lib)) { firstByName = p; firstByNameOff = off; }
        }
        fclose(f);
        if (!gotExact && !firstByName.empty()) { path = firstByName; fileOff = firstByNameOff; }
    }
    if (path.empty()) {
        if (pf.name.find("!/") == std::string::npos) path = pf.name;
        fileOff = 0;
    }
    if (path.empty()) { err = "diskdeki yol tapilmadi"; return false; }
    return true;
}

static void LoadSyms(const char* lib, LibSyms& ls) {
    ls.err.clear();
    ls.map.clear();
    std::string path;
    unsigned long long base = 0;
    if (!LocateLib(lib, ls.bias, path, base, ls.err)) return;

    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) { ls.err = "fayl acilmadi: " + path; return; }

    ElfW(Ehdr) eh;
    if (!PRead(fd, base, &eh, sizeof(eh)) || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0) {
        close(fd); ls.err = "ELF basligi oxunmadi"; return;
    }
#if defined(__LP64__)
    const unsigned char wantClass = ELFCLASS64;
#else
    const unsigned char wantClass = ELFCLASS32;
#endif
    if (eh.e_ident[EI_CLASS] != wantClass) { close(fd); ls.err = "ELF sinfi uygun deyil"; return; }
    if (eh.e_shoff == 0 || eh.e_shnum == 0 || eh.e_shentsize != sizeof(ElfW(Shdr))) {
        close(fd); ls.err = "section cedveli yoxdur (strip olunub? CMake-de -s qalib?)"; return;
    }

    std::vector<ElfW(Shdr)> sh(eh.e_shnum);
    if (!PRead(fd, base + (unsigned long long)eh.e_shoff, sh.data(), sizeof(ElfW(Shdr)) * eh.e_shnum)) {
        close(fd); ls.err = "section cedveli oxunmadi"; return;
    }
    int si = -1;
    for (int i = 0; i < (int)eh.e_shnum; i++) if (sh[i].sh_type == SHT_SYMTAB) { si = i; break; }
    if (si < 0) { close(fd); ls.err = ".symtab yoxdur (lib strip olunub)"; return; }
    if (sh[si].sh_link >= eh.e_shnum) { close(fd); ls.err = "strtab yanlisdir"; return; }
    const ElfW(Shdr)& symSh = sh[si];
    const ElfW(Shdr)& strSh = sh[symSh.sh_link];
    if (symSh.sh_size > (256u << 20) || strSh.sh_size > (256u << 20)) { close(fd); ls.err = "cedvel cox boyukdur"; return; }

    std::vector<uint8_t> symBuf((size_t)symSh.sh_size);
    std::vector<char> strBuf((size_t)strSh.sh_size + 1, 0);
    bool ok = PRead(fd, base + (unsigned long long)symSh.sh_offset, symBuf.data(), symBuf.size()) &&
              PRead(fd, base + (unsigned long long)strSh.sh_offset, strBuf.data(), (size_t)strSh.sh_size);
    close(fd);
    if (!ok) { ls.err = "simvol cedveli oxunmadi"; return; }

    size_t cnt = symBuf.size() / sizeof(ElfW(Sym));
    const ElfW(Sym)* syms = (const ElfW(Sym)*)symBuf.data();
    ls.map.reserve(cnt);
    for (size_t i = 0; i < cnt; i++) {
        const ElfW(Sym)& s = syms[i];
        if (s.st_shndx == SHN_UNDEF || s.st_shndx >= SHN_LORESERVE) continue;
        if (s.st_name == 0 || s.st_name >= strBuf.size() - 1) continue;
        unsigned type = s.st_info & 0xF;
        unsigned bind = s.st_info >> 4;
        if (type != STT_NOTYPE && type != STT_OBJECT && type != STT_FUNC && type != 10 /*GNU_IFUNC*/) continue;
        const char* nm = strBuf.data() + s.st_name;
        if (nm[0] == '\0' || nm[0] == '$') continue;     // arm "$t/$a/$d" nisanlari
        uintptr_t addr = ls.bias + (uintptr_t)s.st_value;
        std::unordered_map<std::string, SymEnt>::iterator it = ls.map.find(nm);
        if (it == ls.map.end()) {
            SymEnt e; e.addr = addr; e.bind = (uint8_t)bind;
            ls.map.insert(std::make_pair(std::string(nm), e));
        } else if (it->second.bind == 0 && bind != 0) {     // lokal yerine global/weak ustun
            it->second.addr = addr; it->second.bind = (uint8_t)bind;
        }
    }
    if (ls.map.empty()) ls.err = "cedvel bosdur";
    FLog("[Lua] %s: %u simvol oxundu (base=%p)", lib, (unsigned)ls.map.size(), (void*)ls.bias);
}

static LibSyms& GetSyms(const char* lib) {
    LibSyms& ls = s_syms[lib];
    if (!ls.tried) {
        LoadSyms(lib, ls);
        if (ls.err.empty()) ls.tried = true;     // ugursuzdursa novbeti cagirisda yeniden cehd edilsin
    }
    return ls;
}

// findSymbol(ad [, lib="libsamp.so"]) -> adres  |  nil, sebeb
static int l_findSymbol(lua_State* S) {
    const char* name = luaL_checkstring(S, 1);
    const char* lib = luaL_optstring(S, 2, "libsamp.so");
    LibSyms& ls = GetSyms(lib);
    if (!ls.err.empty()) return Fail(S, ls.err.c_str());
    std::unordered_map<std::string, SymEnt>::iterator it = ls.map.find(name);
    if (it == ls.map.end()) return Fail(S, "simvol tapilmadi");
    lua_pushinteger(S, (lua_Integer)it->second.addr);
    return 1;
}

// findSymbols(hisse [, lib [, max=50]]) -> { {name=, addr=}, ... }, umumi_say
static int l_findSymbols(lua_State* S) {
    const char* pat = luaL_checkstring(S, 1);
    const char* lib = luaL_optstring(S, 2, "libsamp.so");
    int maxN = (int)luaL_optinteger(S, 3, 50);
    LibSyms& ls = GetSyms(lib);
    if (!ls.err.empty()) return Fail(S, ls.err.c_str());
    lua_newtable(S);
    int n = 0, total = 0;
    for (std::unordered_map<std::string, SymEnt>::iterator it = ls.map.begin(); it != ls.map.end(); ++it) {
        if (it->first.find(pat) == std::string::npos) continue;
        total++;
        if (n >= maxN) continue;
        n++;
        lua_newtable(S);
        lua_pushstring(S, it->first.c_str());
        lua_setfield(S, -2, "name");
        lua_pushinteger(S, (lua_Integer)it->second.addr);
        lua_setfield(S, -2, "addr");
        lua_rawseti(S, -2, n);
    }
    lua_pushinteger(S, total);
    return 2;
}

// symbolInfo([lib]) -> simvol_sayi, baza  |  nil, sebeb
static int l_symbolInfo(lua_State* S) {
    const char* lib = luaL_optstring(S, 1, "libsamp.so");
    LibSyms& ls = GetSyms(lib);
    if (!ls.err.empty()) return Fail(S, ls.err.c_str());
    lua_pushinteger(S, (lua_Integer)ls.map.size());
    lua_pushinteger(S, (lua_Integer)ls.bias);
    return 2;
}

// =====================================================================
//  Kod yamasi
// =====================================================================
static bool WriteCode(uintptr_t addr, const void* data, size_t n) {
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t start = addr & ~(uintptr_t)(ps - 1);
    uintptr_t end = (addr + n + (uintptr_t)ps - 1) & ~(uintptr_t)(ps - 1);
    if (mprotect((void*)start, end - start, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        if (mprotect((void*)start, end - start, PROT_READ | PROT_WRITE) != 0) return false;
    }
    memcpy((void*)addr, data, n);
    __builtin___clear_cache((char*)addr, (char*)(addr + n));
    mprotect((void*)start, end - start, PROT_READ | PROT_EXEC);
    return true;
}

static int HexVal(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// readHex(addr, n) -> "DE AD BE EF"  |  nil
static int l_readHex(lua_State* S) {
    uintptr_t a = (uintptr_t)luaL_checkinteger(S, 1);
    int n = (int)luaL_checkinteger(S, 2);
    if (n <= 0 || n > 4096) return Fail(S, "n 1..4096 olmalidir");
    std::vector<uint8_t> buf(n);
    if (!MemRead(a, buf.data(), (size_t)n)) return Fail(S, "oxunmadi");
    std::string out;
    char t[4];
    for (int i = 0; i < n; i++) { snprintf(t, sizeof(t), "%02X", buf[i]); if (i) out += ' '; out += t; }
    lua_pushstring(S, out.c_str());
    return 1;
}

// patchCode(addr, "90 00 A0 E3 ...")  -- kod sehifesine yazir (mprotect)
static int l_patchCode(lua_State* S) {
    uintptr_t a = (uintptr_t)luaL_checkinteger(S, 1);
    const char* hex = luaL_checkstring(S, 2);
#if defined(__arm__)
    a &= ~(uintptr_t)1;           // thumb biti
#endif
    std::vector<uint8_t> bytes;
    int hi = -1;
    for (const char* p = hex; *p; p++) {
        if (*p == ' ' || *p == ',' || *p == '\t') continue;
        int v = HexVal(*p);
        if (v < 0) return Fail(S, "hex yanlisdir");
        if (hi < 0) hi = v; else { bytes.push_back((uint8_t)(hi * 16 + v)); hi = -1; }
    }
    if (hi >= 0 || bytes.empty() || bytes.size() > 4096) return Fail(S, "hex uzunlugu yanlisdir");
    if (a < 0x10000) return Fail(S, "yanlis adres");
    lua_pushboolean(S, WriteCode(a, bytes.data(), bytes.size()));
    return 1;
}

static int l_bitsToFloat(lua_State* S) {
    lua_pushnumber(S, BitsToFloat((uint32_t)luaL_checkinteger(S, 1)));
    return 1;
}
static int l_floatToBits(lua_State* S) {
    lua_pushinteger(S, (lua_Integer)FloatToBits((float)luaL_checknumber(S, 1)));
    return 1;
}

// =====================================================================
//  2) HOOK
//
//  Usul: "bərpa et -> orijinali cagir -> yeniden yama". Instruksiya relokasiyasi
//  yoxdur, ona gore arm32 (thumb/arm) ve arm64-de eyni isleyir.
//  Mehdudiyyet: 8 tam eded/gosterici arqument (arm64-de elave 8 float reyestri
//  oldugu kimi kecirilir). Qaytarma: tam eded/gosterici ve ya float ("f").
//  Cox kicik (<8 bayt) funksiyalari hook etmeyin.
// =====================================================================
#if defined(__aarch64__) || defined(__arm__)
#define LB_HOOK_OK 1
#else
#define LB_HOOK_OK 0
#endif

#define LB_MAX_HOOKS 32

struct HookResult { uintptr_t i; float f; };

struct HookRec {
    std::atomic<bool> inUse{ false };
    uintptr_t target = 0;          // thumb biti daxil
    uintptr_t codeStart = 0;       // real yamanin yazildigi yer (thumb biti olmadan)
    uint8_t saved[16];
    uint8_t patch[16];
    size_t patchLen = 0;
    int luaRef = LUA_NOREF;
    bool retFloat = false;
    bool luaDead = false;
    std::mutex m;
    int active = 0;
    bool patched = false;
};
static HookRec g_hooks[LB_MAX_HOOKS];

struct HookCtx {
    HookRec* h;
    uintptr_t a[8];
    double d[8];
    bool called;
    HookResult last;
};
static thread_local HookCtx* t_ctx = nullptr;

static bool BuildPatch(uintptr_t target, uintptr_t stub, uint8_t* out, size_t& len, uintptr_t& codeStart) {
#if defined(__aarch64__)
    codeStart = target;
    uint32_t i0 = 0x58000050u;     // ldr x16, #8
    uint32_t i1 = 0xD61F0200u;     // br  x16
    uint64_t a = (uint64_t)stub;
    memcpy(out, &i0, 4); memcpy(out + 4, &i1, 4); memcpy(out + 8, &a, 8);
    len = 16;
    return true;
#elif defined(__arm__)
    if (target & 1) {              // thumb
        codeStart = target & ~(uintptr_t)1;
        size_t o = 0;
        if (codeStart & 2) { out[o++] = 0x00; out[o++] = 0xBF; }               // nop (literal 4-e hizalansin)
        out[o++] = 0xDF; out[o++] = 0xF8; out[o++] = 0x00; out[o++] = 0xF0;    // ldr.w pc, [pc]
        uint32_t a = (uint32_t)stub;
        memcpy(out + o, &a, 4); o += 4;
        len = o;
        return true;
    }
    codeStart = target;            // arm
    uint32_t i0 = 0xE51FF004u;     // ldr pc, [pc, #-4]
    uint32_t a = (uint32_t)stub;
    memcpy(out, &i0, 4); memcpy(out + 4, &a, 4);
    len = 8;
    return true;
#else
    (void)target; (void)stub; (void)out; (void)len; (void)codeStart;
    return false;
#endif
}

// h.m kilidi altinda cagirilir
static void InstallPatch(HookRec& h) {
    if (WriteCode(h.codeStart, h.patch, h.patchLen)) h.patched = true;
}
static void RestoreOriginal(HookRec& h) {
    if (WriteCode(h.codeStart, h.saved, h.patchLen)) h.patched = false;
}

#if defined(__aarch64__)
#define LB_ARG_TYPES uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, \
                     double, double, double, double, double, double, double, double
typedef uintptr_t (*OrigFnI)(LB_ARG_TYPES);
typedef float     (*OrigFnF)(LB_ARG_TYPES);
#else
typedef uintptr_t (*OrigFnI)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
#endif

static HookResult CallOrig(HookRec& h, const uintptr_t* a, const double* d) {
    {
        std::lock_guard<std::mutex> g(h.m);
        if (h.active++ == 0 && h.patched) RestoreOriginal(h);
    }
    HookResult r; r.i = 0; r.f = 0.0f;
#if defined(__aarch64__)
    if (h.retFloat) r.f = ((OrigFnF)h.target)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                                              d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    else            r.i = ((OrigFnI)h.target)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                                              d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
#else
    (void)d;
    r.i = ((OrigFnI)h.target)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    if (h.retFloat) r.f = BitsToFloat((uint32_t)r.i);
#endif
    {
        std::lock_guard<std::mutex> g(h.m);
        if (--h.active == 0 && h.inUse.load() && !h.patched) InstallPatch(h);
    }
    return r;
}

static int LB_HookCall(lua_State* S);

static HookResult Dispatch(int idx, const uintptr_t* a, const double* d) {
    HookRec& h = g_hooks[idx];
    HookCtx ctx;
    ctx.h = &h;
    for (int i = 0; i < 8; i++) { ctx.a[i] = a[i]; ctx.d[i] = d[i]; }
    ctx.called = false;
    ctx.last.i = 0; ctx.last.f = 0.0f;

    bool needOrig = true;
    HookResult out; out.i = 0; out.f = 0.0f;

    if (h.inUse.load() && !h.luaDead) {
        Guard lk(100);
        if (lk.ok && s_main && h.inUse.load() && h.luaRef != LUA_NOREF) {
            HookCtx* prev = t_ctx;
            t_ctx = &ctx;
            lua_State* T = lua_newthread(s_main);
            int tref = luaL_ref(s_main, LUA_REGISTRYINDEX);
            lua_rawgeti(T, LUA_REGISTRYINDEX, h.luaRef);
            lua_pushcfunction(T, LB_HookCall);                      // orig(...)
            for (int i = 0; i < 8; i++) lua_pushinteger(T, (lua_Integer)ctx.a[i]);
            int rc = lua_pcall(T, 9, 1, 0);
            if (rc != LUA_OK) {
                const char* e = lua_tostring(T, -1);
                char buf[400];
                snprintf(buf, sizeof(buf), "hook#%d: %s", idx + 1, e ? e : "?");
                QueueError(buf);
                h.luaDead = true;                 // spam olmasin; orijinal ise normal davam edir
                needOrig = !ctx.called;
                out = ctx.last;
            } else if (lua_isnil(T, -1)) {
                needOrig = !ctx.called;           // callback hecne qaytarmadi -> orijinal
                out = ctx.last;
            } else {
                needOrig = false;
                if (h.retFloat) {
                    out.f = (float)lua_tonumber(T, -1);
                } else if (lua_isboolean(T, -1)) {
                    out.i = lua_toboolean(T, -1) ? 1 : 0;
                } else {
                    int isnum = 0;
                    lua_Integer v = lua_tointegerx(T, -1, &isnum);
                    if (!isnum) v = (lua_Integer)lua_tonumberx(T, -1, &isnum);
                    out.i = (uintptr_t)v;
                }
            }
            luaL_unref(s_main, LUA_REGISTRYINDEX, tref);
            t_ctx = prev;
        }
    }
    if (needOrig) out = CallOrig(h, ctx.a, ctx.d);
    return out;
}

// callback-in birinci arqumenti: orig([a1..a8])  (nil olan arqument deyismir)
static int LB_HookCall(lua_State* S) {
    HookCtx* c = t_ctx;
    if (!c) return Fail(S, "orig: hook konteksti yoxdur");
    uintptr_t a[8];
    double d[8];
    memcpy(a, c->a, sizeof(a));
    memcpy(d, c->d, sizeof(d));
    int n = lua_gettop(S);
    for (int i = 0; i < 8 && i < n; i++) {
        if (lua_isnil(S, i + 1)) continue;
        int isnum = 0;
        lua_Integer v = lua_tointegerx(S, i + 1, &isnum);
        if (!isnum) v = (lua_Integer)lua_tonumberx(S, i + 1, &isnum);
        a[i] = (uintptr_t)v;
    }
    int saved = ReleaseAll();
    HookResult r = CallOrig(*c->h, a, d);
    ReacquireAll(saved);
    c->called = true;
    c->last = r;
    if (c->h->retFloat) lua_pushnumber(S, r.f);
    else lua_pushinteger(S, (lua_Integer)r.i);
    return 1;
}

// argf(i)  -> i-ci arqumenti float kimi (arm64: i-ci float reyestri, arm32: i-ci slot)
static int l_argf(lua_State* S) {
    HookCtx* c = t_ctx;
    int i = (int)luaL_checkinteger(S, 1);
    if (!c || i < 1 || i > 8) return Fail(S, "argf: yanlis cagiris");
#if defined(__aarch64__)
    uint64_t u; memcpy(&u, &c->d[i - 1], 8);
    lua_pushnumber(S, BitsToFloat((uint32_t)u));
#else
    lua_pushnumber(S, BitsToFloat((uint32_t)c->a[i - 1]));
#endif
    return 1;
}
// setArg(i, deyer)   -- orig() cagirilmazdan evvel tam eded arqumenti deyisir
static int l_setArg(lua_State* S) {
    HookCtx* c = t_ctx;
    int i = (int)luaL_checkinteger(S, 1);
    if (!c || i < 1 || i > 8) return Fail(S, "setArg: yanlis cagiris");
    c->a[i - 1] = (uintptr_t)luaL_checkinteger(S, 2);
    lua_pushboolean(S, 1);
    return 1;
}
// setArgf(i, float)
static int l_setArgf(lua_State* S) {
    HookCtx* c = t_ctx;
    int i = (int)luaL_checkinteger(S, 1);
    float v = (float)luaL_checknumber(S, 2);
    if (!c || i < 1 || i > 8) return Fail(S, "setArgf: yanlis cagiris");
#if defined(__aarch64__)
    uint64_t u = (uint64_t)FloatToBits(v);
    memcpy(&c->d[i - 1], &u, 8);
#else
    c->a[i - 1] = (uintptr_t)FloatToBits(v);
#endif
    lua_pushboolean(S, 1);
    return 1;
}

#if defined(__aarch64__)
template<int I> static uintptr_t StubI(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                       uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                                       double d0, double d1, double d2, double d3,
                                       double d4, double d5, double d6, double d7) {
    uintptr_t a[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
    double d[8] = { d0, d1, d2, d3, d4, d5, d6, d7 };
    return Dispatch(I, a, d).i;
}
template<int I> static float StubF(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                   uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7,
                                   double d0, double d1, double d2, double d3,
                                   double d4, double d5, double d6, double d7) {
    uintptr_t a[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
    double d[8] = { d0, d1, d2, d3, d4, d5, d6, d7 };
    return Dispatch(I, a, d).f;
}
#define LB_SF(n) (void*)&StubF<n>
#else
template<int I> static uintptr_t StubI(uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                                       uintptr_t a4, uintptr_t a5, uintptr_t a6, uintptr_t a7) {
    uintptr_t a[8] = { a0, a1, a2, a3, a4, a5, a6, a7 };
    double d[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    HookResult r = Dispatch(I, a, d);
    return g_hooks[I].retFloat ? (uintptr_t)FloatToBits(r.f) : r.i;
}
#define LB_SF(n) (void*)&StubI<n>      // arm32 softfp: float qaytarma da r0-dadir
#endif
#define LB_SI(n) (void*)&StubI<n>
#define LB_LIST(M) M(0),M(1),M(2),M(3),M(4),M(5),M(6),M(7),M(8),M(9),M(10),M(11),M(12),M(13),M(14),M(15), \
                   M(16),M(17),M(18),M(19),M(20),M(21),M(22),M(23),M(24),M(25),M(26),M(27),M(28),M(29),M(30),M(31)
static void* const g_stubI[LB_MAX_HOOKS] = { LB_LIST(LB_SI) };
static void* const g_stubF[LB_MAX_HOOKS] = { LB_LIST(LB_SF) };

// hook(adres, function(orig, a1..a8) ... end [, "i"|"f"]) -> id  |  nil, sebeb
//   callback nil qaytarsa ve orig() cagirmayibsa -> orijinal avtomatik cagirilir
//   eded qaytarsa -> hooklanan funksiyanin qaytarmasi o olur (orijinal cagirilmir)
static int l_hook(lua_State* S) {
#if LB_HOOK_OK
    uintptr_t addr = (uintptr_t)luaL_checkinteger(S, 1);
    luaL_checktype(S, 2, LUA_TFUNCTION);
    const char* rt = luaL_optstring(S, 3, "i");
    bool retF = (rt[0] == 'f');
    if (addr < 0x10000) return Fail(S, "yanlis adres");
    int idx = -1;
    for (int i = 0; i < LB_MAX_HOOKS; i++) {
        if (g_hooks[i].inUse.load()) { if (g_hooks[i].target == addr) return Fail(S, "bu adres artiq hook olunub"); }
        else if (idx < 0) idx = i;
    }
    if (idx < 0) return Fail(S, "hook limiti (32) dolub");

    HookRec& h = g_hooks[idx];
    std::lock_guard<std::mutex> g(h.m);
    h.target = addr;
    h.retFloat = retF;
    h.luaDead = false;
    h.active = 0;
    h.patched = false;
    if (!BuildPatch(addr, (uintptr_t)(retF ? g_stubF[idx] : g_stubI[idx]), h.patch, h.patchLen, h.codeStart))
        return Fail(S, "bu arxitektura desteklenmir");
    if (!MemRead(h.codeStart, h.saved, h.patchLen)) return Fail(S, "hedef kod oxunmadi");
    lua_pushvalue(S, 2);
    h.luaRef = luaL_ref(S, LUA_REGISTRYINDEX);
    h.inUse.store(true);
    InstallPatch(h);
    if (!h.patched) {
        h.inUse.store(false);
        luaL_unref(S, LUA_REGISTRYINDEX, h.luaRef);
        h.luaRef = LUA_NOREF;
        return Fail(S, "yama yazilmadi (mprotect)");
    }
    lua_pushinteger(S, idx + 1);
    return 1;
#else
    (void)S;
    return Fail(S, "hook bu arxitekturada desteklenmir");
#endif
}

static void UnhookSlot(int idx, lua_State* S) {
    HookRec& h = g_hooks[idx];
    if (!h.inUse.load()) return;
    {
        std::lock_guard<std::mutex> g(h.m);
        h.inUse.store(false);
        if (h.patched) RestoreOriginal(h);
    }
    if (S && h.luaRef != LUA_NOREF) luaL_unref(S, LUA_REGISTRYINDEX, h.luaRef);
    h.luaRef = LUA_NOREF;
}

// unhook(id) -> true/false
static int l_unhook(lua_State* S) {
    int id = (int)luaL_checkinteger(S, 1);
    if (id < 1 || id > LB_MAX_HOOKS || !g_hooks[id - 1].inUse.load()) { lua_pushboolean(S, 0); return 1; }
    UnhookSlot(id - 1, S);
    lua_pushboolean(S, 1);
    return 1;
}

// =====================================================================
//  3) JAVA (JNI)
// =====================================================================
static JavaVM* s_vm = nullptr;
static jobject s_loader = nullptr;      // tetbiqin ClassLoader-i (global ref)
static jmethodID s_loadClass = nullptr;
static jobject s_app = nullptr;         // Application konteksti (global ref)
static std::vector<jobject> s_jh;       // handle cedveli (handle = indeks+1)

static JavaVM* GetVM() {
    if (s_vm) return s_vm;
    typedef jint (*GetVMsFn)(JavaVM**, jsize, jsize*);
    GetVMsFn fn = (GetVMsFn)dlsym(RTLD_DEFAULT, "JNI_GetCreatedJavaVMs");
    if (fn) {
        JavaVM* v[4] = { nullptr, nullptr, nullptr, nullptr };
        jsize n = 0;
        if (fn(v, 4, &n) == JNI_OK && n > 0) s_vm = v[0];
    }
    if (!s_vm) {     // ehtiyat: libsamp.so-nun oz global deyiseni
        static const char* names[] = { "javaVM", "g_javaVM", "g_JavaVM", "jvm", "g_jvm", "gJavaVM", "mJavaVM", nullptr };
        LibSyms& ls = GetSyms("libsamp.so");
        for (int i = 0; names[i] && !s_vm; i++) {
            std::unordered_map<std::string, SymEnt>::iterator it = ls.map.find(names[i]);
            if (it == ls.map.end()) continue;
            uintptr_t p = 0;
            if (MemRead(it->second.addr, &p, sizeof(p)) && p > 0x10000) s_vm = (JavaVM*)p;
        }
    }
    return s_vm;
}

static JNIEnv* GetEnvJ() {
    JavaVM* vm = GetVM();
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    jint r = vm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (r == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    } else if (r != JNI_OK) {
        return nullptr;
    }
    return env;
}

static int NewHandle(JNIEnv* env, jobject local) {
    if (!local) return 0;
    jobject g = env->NewGlobalRef(local);
    if (!g) return 0;
    for (size_t i = 0; i < s_jh.size(); i++) {
        if (!s_jh[i]) { s_jh[i] = g; return (int)i + 1; }
    }
    s_jh.push_back(g);
    return (int)s_jh.size();
}
static jobject GetHandle(lua_Integer h) {
    if (h <= 0 || h > (lua_Integer)s_jh.size()) return nullptr;
    return s_jh[(size_t)h - 1];
}

// ActivityThread.currentApplication().getClassLoader()  -> tetbiqin oz siniflarini tapmaq ucun
static bool EnsureLoader(JNIEnv* env) {
    if (s_loader && s_loadClass) return true;
    env->PushLocalFrame(16);
    bool ok = false;
    do {
        jclass at = env->FindClass("android/app/ActivityThread");
        if (!at) { env->ExceptionClear(); break; }
        jmethodID cur = env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
        if (!cur) { env->ExceptionClear(); break; }
        jobject app = env->CallStaticObjectMethod(at, cur);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!app) break;
        jclass ac = env->GetObjectClass(app);
        jmethodID gcl = env->GetMethodID(ac, "getClassLoader", "()Ljava/lang/ClassLoader;");
        if (!gcl) { env->ExceptionClear(); break; }
        jobject ld = env->CallObjectMethod(app, gcl);
        if (env->ExceptionCheck()) { env->ExceptionClear(); break; }
        if (!ld) break;
        jclass lc = env->FindClass("java/lang/ClassLoader");
        if (!lc) { env->ExceptionClear(); break; }
        jmethodID lcm = env->GetMethodID(lc, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (!lcm) { env->ExceptionClear(); break; }
        s_app = env->NewGlobalRef(app);
        s_loader = env->NewGlobalRef(ld);
        s_loadClass = lcm;
        ok = true;
    } while (false);
    env->PopLocalFrame(nullptr);
    return ok;
}

// javaContext() -> Application handle
static int l_javaContext(lua_State* S) {
    JNIEnv* env = GetEnvJ();
    if (!env) return Fail(S, "JNIEnv alinmadi (JavaVM tapilmadi)");
    if (!EnsureLoader(env) || !s_app) return Fail(S, "Application tapilmadi");
    lua_pushinteger(S, NewHandle(env, s_app));
    return 1;
}

// javaClass("com.foo.Bar")  -> sinif handle  |  nil, sebeb
static int l_javaClass(lua_State* S) {
    const char* name = luaL_checkstring(S, 1);
    JNIEnv* env = GetEnvJ();
    if (!env) return Fail(S, "JNIEnv alinmadi (JavaVM tapilmadi)");
    std::string dots(name), slashes(name);
    for (size_t i = 0; i < dots.size(); i++) if (dots[i] == '/') dots[i] = '.';
    for (size_t i = 0; i < slashes.size(); i++) if (slashes[i] == '.') slashes[i] = '/';

    env->PushLocalFrame(16);
    jclass found = nullptr;
    if (EnsureLoader(env)) {
        jstring js = env->NewStringUTF(dots.c_str());
        found = (jclass)env->CallObjectMethod(s_loader, s_loadClass, js);
        if (env->ExceptionCheck()) { env->ExceptionClear(); found = nullptr; }
    }
    if (!found) {
        found = env->FindClass(slashes.c_str());
        if (env->ExceptionCheck()) { env->ExceptionClear(); found = nullptr; }
    }
    int h = found ? NewHandle(env, found) : 0;
    env->PopLocalFrame(nullptr);
    if (!h) return Fail(S, "sinif tapilmadi");
    lua_pushinteger(S, h);
    return 1;
}

// javaRelease(handle)
static int l_javaRelease(lua_State* S) {
    lua_Integer h = luaL_checkinteger(S, 1);
    JNIEnv* env = GetEnvJ();
    if (!env || h <= 0 || h > (lua_Integer)s_jh.size() || !s_jh[(size_t)h - 1]) { lua_pushboolean(S, 0); return 1; }
    env->DeleteGlobalRef(s_jh[(size_t)h - 1]);
    s_jh[(size_t)h - 1] = nullptr;
    lua_pushboolean(S, 1);
    return 1;
}

static bool ParseSig(const char* sig, std::vector<std::string>& params, std::string& ret) {
    if (!sig || sig[0] != '(') return false;
    const char* p = sig + 1;
    while (*p && *p != ')') {
        const char* st = p;
        while (*p == '[') p++;
        if (*p == 'L') { while (*p && *p != ';') p++; if (*p != ';') return false; p++; }
        else if (*p && strchr("ZBCSIJFD", *p)) p++;
        else return false;
        params.push_back(std::string(st, (size_t)(p - st)));
    }
    if (*p != ')') return false;
    p++;
    if (!*p) return false;
    ret = p;
    return true;
}

static bool MarshalArg(JNIEnv* env, lua_State* S, int idx, const std::string& t, jvalue& v, std::string& err) {
    memset(&v, 0, sizeof(v));
    int ty = lua_type(S, idx);
    switch (t[0]) {
        case 'Z':
            v.z = (ty == LUA_TBOOLEAN ? lua_toboolean(S, idx) != 0 : lua_tointeger(S, idx) != 0) ? JNI_TRUE : JNI_FALSE;
            return true;
        case 'B': v.b = (jbyte)lua_tointeger(S, idx); return true;
        case 'C': v.c = (jchar)lua_tointeger(S, idx); return true;
        case 'S': v.s = (jshort)lua_tointeger(S, idx); return true;
        case 'I': v.i = (jint)lua_tointeger(S, idx); return true;
        case 'J': v.j = (jlong)lua_tointeger(S, idx); return true;
        case 'F': v.f = (jfloat)lua_tonumber(S, idx); return true;
        case 'D': v.d = (jdouble)lua_tonumber(S, idx); return true;
        default: break;
    }
    if (ty == LUA_TNIL || ty == LUA_TNONE) { v.l = nullptr; return true; }
    if (ty == LUA_TSTRING) {
        if (t == "Ljava/lang/String;" || t == "Ljava/lang/Object;" || t == "Ljava/lang/CharSequence;") {
            v.l = env->NewStringUTF(lua_tostring(S, idx));
            return true;
        }
        err = "string arqumenti bu tipe uygun deyil: " + t;
        return false;
    }
    if (ty == LUA_TNUMBER) {
        jobject o = GetHandle(lua_tointeger(S, idx));
        if (!o) { err = "yanlis java handle"; return false; }
        v.l = o;
        return true;
    }
    err = "desteklenmeyen arqument tipi";
    return false;
}

static int PushRet(JNIEnv* env, lua_State* S, const std::string& rt, const jvalue& r) {
    switch (rt[0]) {
        case 'V': lua_pushboolean(S, 1); return 1;
        case 'Z': lua_pushboolean(S, r.z != JNI_FALSE); return 1;
        case 'B': lua_pushinteger(S, r.b); return 1;
        case 'C': lua_pushinteger(S, r.c); return 1;
        case 'S': lua_pushinteger(S, r.s); return 1;
        case 'I': lua_pushinteger(S, r.i); return 1;
        case 'J': lua_pushinteger(S, (lua_Integer)r.j); return 1;
        case 'F': lua_pushnumber(S, r.f); return 1;
        case 'D': lua_pushnumber(S, r.d); return 1;
        default: break;
    }
    if (!r.l) { lua_pushnil(S); return 1; }
    if (rt == "Ljava/lang/String;") {
        const char* s = env->GetStringUTFChars((jstring)r.l, nullptr);
        lua_pushstring(S, s ? s : "");
        if (s) env->ReleaseStringUTFChars((jstring)r.l, s);
        return 1;
    }
    lua_pushinteger(S, NewHandle(env, r.l));
    return 1;
}

enum { JM_NEW = 0, JM_CALL = 1, JM_STATIC = 2 };

static int JavaInvoke(lua_State* S, int mode) {
    JNIEnv* env = GetEnvJ();
    if (!env) return Fail(S, "JNIEnv alinmadi (JavaVM tapilmadi)");

    int a = 1;
    jobject recv = nullptr;
    jclass cls = nullptr;
    const char* mname = "<init>";
    if (mode == JM_CALL) {
        recv = GetHandle(luaL_checkinteger(S, 1));
        mname = luaL_checkstring(S, 2);
        a = 3;
    } else {
        cls = (jclass)GetHandle(luaL_checkinteger(S, 1));
        if (mode == JM_STATIC) { mname = luaL_checkstring(S, 2); a = 3; }
        else a = 2;
    }
    const char* sig = luaL_checkstring(S, a);
    a++;
    if (mode == JM_CALL ? !recv : !cls) return Fail(S, "yanlis java handle");

    std::vector<std::string> params;
    std::string ret;
    if (!ParseSig(sig, params, ret)) return Fail(S, "yanlis imza");
    int given = lua_gettop(S) - a + 1;
    if (given != (int)params.size()) return Fail(S, "arqument sayi imza ile uygun deyil");

    env->PushLocalFrame(16 + (jint)params.size() * 2);
    jclass useCls = (mode == JM_CALL) ? env->GetObjectClass(recv) : cls;
    jmethodID mid = (mode == JM_STATIC) ? env->GetStaticMethodID(useCls, mname, sig)
                                        : env->GetMethodID(useCls, mname, sig);
    if (!mid) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return Fail(S, "metod tapilmadi");
    }
    std::vector<jvalue> av(params.size());
    std::string err;
    for (size_t i = 0; i < params.size(); i++) {
        if (!MarshalArg(env, S, a + (int)i, params[i], av[i], err)) {
            env->PopLocalFrame(nullptr);
            return Fail(S, err.c_str());
        }
    }
    jvalue r;
    memset(&r, 0, sizeof(r));
    const jvalue* args = av.empty() ? nullptr : av.data();
    if (mode == JM_NEW) {
        r.l = env->NewObjectA(cls, mid, args);
        ret = "Ljava/lang/Object;";
    } else if (mode == JM_STATIC) {
        switch (ret[0]) {
            case 'V': env->CallStaticVoidMethodA(cls, mid, args); break;
            case 'Z': r.z = env->CallStaticBooleanMethodA(cls, mid, args); break;
            case 'B': r.b = env->CallStaticByteMethodA(cls, mid, args); break;
            case 'C': r.c = env->CallStaticCharMethodA(cls, mid, args); break;
            case 'S': r.s = env->CallStaticShortMethodA(cls, mid, args); break;
            case 'I': r.i = env->CallStaticIntMethodA(cls, mid, args); break;
            case 'J': r.j = env->CallStaticLongMethodA(cls, mid, args); break;
            case 'F': r.f = env->CallStaticFloatMethodA(cls, mid, args); break;
            case 'D': r.d = env->CallStaticDoubleMethodA(cls, mid, args); break;
            default:  r.l = env->CallStaticObjectMethodA(cls, mid, args); break;
        }
    } else {
        switch (ret[0]) {
            case 'V': env->CallVoidMethodA(recv, mid, args); break;
            case 'Z': r.z = env->CallBooleanMethodA(recv, mid, args); break;
            case 'B': r.b = env->CallByteMethodA(recv, mid, args); break;
            case 'C': r.c = env->CallCharMethodA(recv, mid, args); break;
            case 'S': r.s = env->CallShortMethodA(recv, mid, args); break;
            case 'I': r.i = env->CallIntMethodA(recv, mid, args); break;
            case 'J': r.j = env->CallLongMethodA(recv, mid, args); break;
            case 'F': r.f = env->CallFloatMethodA(recv, mid, args); break;
            case 'D': r.d = env->CallDoubleMethodA(recv, mid, args); break;
            default:  r.l = env->CallObjectMethodA(recv, mid, args); break;
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();      // logcat-a yazir ve temizleyir
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return Fail(S, "Java istisnasi (logcat-a bax)");
    }
    int n = PushRet(env, S, ret, r);   // obyektler burada global ref-e cevrilir
    env->PopLocalFrame(nullptr);
    return n;
}

// javaNew(sinif, "(imza)V", ...)           -> obyekt handle
static int l_javaNew(lua_State* S)        { return JavaInvoke(S, JM_NEW); }
// javaCall(obyekt, "ad", "(imza)R", ...)   -> netice
static int l_javaCall(lua_State* S)       { return JavaInvoke(S, JM_CALL); }
// javaCallStatic(sinif, "ad", "(imza)R", ...)
static int l_javaCallStatic(lua_State* S) { return JavaInvoke(S, JM_STATIC); }

static int JavaField(lua_State* S, bool isStatic, bool isSet) {
    JNIEnv* env = GetEnvJ();
    if (!env) return Fail(S, "JNIEnv alinmadi (JavaVM tapilmadi)");
    jobject h = GetHandle(luaL_checkinteger(S, 1));
    const char* name = luaL_checkstring(S, 2);
    const char* sig = luaL_checkstring(S, 3);
    if (!h) return Fail(S, "yanlis java handle");
    std::string t(sig);
    if (t.empty()) return Fail(S, "yanlis imza");

    env->PushLocalFrame(16);
    jclass c = isStatic ? (jclass)h : env->GetObjectClass(h);
    jfieldID fid = isStatic ? env->GetStaticFieldID(c, name, sig) : env->GetFieldID(c, name, sig);
    if (!fid) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return Fail(S, "sahe tapilmadi");
    }
    if (isSet) {
        jvalue v;
        std::string err;
        if (!MarshalArg(env, S, 4, t, v, err)) { env->PopLocalFrame(nullptr); return Fail(S, err.c_str()); }
#define JF_SET(T, f, ch) case ch: if (isStatic) env->SetStatic##T##Field(c, fid, v.f); else env->Set##T##Field(h, fid, v.f); break;
        switch (t[0]) {
            JF_SET(Boolean, z, 'Z') JF_SET(Byte, b, 'B') JF_SET(Char, c, 'C') JF_SET(Short, s, 'S')
            JF_SET(Int, i, 'I') JF_SET(Long, j, 'J') JF_SET(Float, f, 'F') JF_SET(Double, d, 'D')
            default:
                if (isStatic) env->SetStaticObjectField(c, fid, v.l); else env->SetObjectField(h, fid, v.l);
                break;
        }
#undef JF_SET
        bool bad = env->ExceptionCheck();
        if (bad) { env->ExceptionDescribe(); env->ExceptionClear(); }
        env->PopLocalFrame(nullptr);
        if (bad) return Fail(S, "Java istisnasi (logcat-a bax)");
        lua_pushboolean(S, 1);
        return 1;
    }
    jvalue r;
    memset(&r, 0, sizeof(r));
#define JF_GET(T, f, ch) case ch: r.f = isStatic ? env->GetStatic##T##Field(c, fid) : env->Get##T##Field(h, fid); break;
    switch (t[0]) {
        JF_GET(Boolean, z, 'Z') JF_GET(Byte, b, 'B') JF_GET(Char, c, 'C') JF_GET(Short, s, 'S')
        JF_GET(Int, i, 'I') JF_GET(Long, j, 'J') JF_GET(Float, f, 'F') JF_GET(Double, d, 'D')
        default:
            r.l = isStatic ? env->GetStaticObjectField(c, fid) : env->GetObjectField(h, fid);
            break;
    }
#undef JF_GET
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return Fail(S, "Java istisnasi (logcat-a bax)");
    }
    int n = PushRet(env, S, t, r);
    env->PopLocalFrame(nullptr);
    return n;
}
static int l_javaGetField(lua_State* S)       { return JavaField(S, false, false); }
static int l_javaGetStaticField(lua_State* S) { return JavaField(S, true, false); }
static int l_javaSetField(lua_State* S)       { return JavaField(S, false, true); }
static int l_javaSetStaticField(lua_State* S) { return JavaField(S, true, true); }

static void JavaShutdown() {
    JNIEnv* env = s_vm ? GetEnvJ() : nullptr;
    if (env) {
        for (size_t i = 0; i < s_jh.size(); i++) if (s_jh[i]) env->DeleteGlobalRef(s_jh[i]);
        if (s_loader) env->DeleteGlobalRef(s_loader);
        if (s_app) env->DeleteGlobalRef(s_app);
    }
    s_jh.clear();
    s_loader = nullptr;
    s_app = nullptr;
    s_loadClass = nullptr;
}

// =====================================================================
//  5) saveData / loadData  (files/lua_data/<acar>.lua)
// =====================================================================
static bool ValidKey(const char* k) {
    size_t n = strlen(k);
    if (n == 0 || n > 64 || k[0] == '.') return false;
    for (size_t i = 0; i < n; i++) {
        char c = k[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}
static std::string DataDir() { return std::string(g_pszStorage) + "/lua_data"; }
static std::string DataPath(const char* key) { return DataDir() + "/" + key + ".lua"; }

static void SerNumber(lua_State* S, int idx, std::string& out) {
    char b[64];
    if (IsIntegerValue(S, idx)) {
        snprintf(b, sizeof(b), "%lld", (long long)lua_tointeger(S, idx));
        out += b;
        return;
    }
    lua_Number n = lua_tonumber(S, idx);
    if (n != n) { out += "(0/0)"; return; }
    if (n == HUGE_VAL) { out += "1e9999"; return; }
    if (n == -HUGE_VAL) { out += "-1e9999"; return; }
    snprintf(b, sizeof(b), "%.17g", (double)n);
    if (!strpbrk(b, ".eEn")) strncat(b, ".0", sizeof(b) - strlen(b) - 1);   // float olaraq qalsin
    out += b;
}
static void SerString(const char* s, size_t len, std::string& out) {
    out += '"';
    char b[8];
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c < 32 || c == 127) { snprintf(b, sizeof(b), "\\%03d", (int)c); out += b; }
        else out += (char)c;
    }
    out += '"';
}
static bool SerializeValue(lua_State* S, int idx, std::string& out, int depth, std::string& err) {
    idx = lua_absindex(S, idx);
    if (out.size() > (4u << 20)) { err = "data cox boyukdur (4MB)"; return false; }
    switch (lua_type(S, idx)) {
        case LUA_TNIL:     out += "nil"; return true;
        case LUA_TBOOLEAN: out += lua_toboolean(S, idx) ? "true" : "false"; return true;
        case LUA_TNUMBER:  SerNumber(S, idx, out); return true;
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(S, idx, &len);
            SerString(s, len, out);
            return true;
        }
        case LUA_TTABLE: {
            if (depth >= 12) { err = "cedvel cox deriNdir (>12)"; return false; }
            out += '{';
            lua_pushnil(S);
            while (lua_next(S, idx)) {
                int kt = lua_type(S, -2);
                out += '[';
                if (kt == LUA_TNUMBER) SerNumber(S, -2, out);
                else if (kt == LUA_TSTRING) { size_t kl = 0; const char* ks = lua_tolstring(S, -2, &kl); SerString(ks, kl, out); }
                else { lua_pop(S, 2); err = "acar tipi desteklenmir"; return false; }
                out += "]=";
                if (!SerializeValue(S, -1, out, depth + 1, err)) { lua_pop(S, 2); return false; }
                out += ',';
                lua_pop(S, 1);
            }
            out += '}';
            return true;
        }
        default:
            err = std::string("tip desteklenmir: ") + lua_typename(S, lua_type(S, idx));
            return false;
    }
}

// saveData(acar, deyer)  -- deyer=nil olsa silir. eded/string/boolean/cedvel
static int l_saveData(lua_State* S) {
    const char* key = luaL_checkstring(S, 1);
    if (!ValidKey(key)) return Fail(S, "yanlis acar (herf/reqem/_-. , max 64)");
    if (lua_isnone(S, 2) || lua_isnil(S, 2)) {
        unlink(DataPath(key).c_str());
        lua_pushboolean(S, 1);
        return 1;
    }
    std::string out = "return ";
    std::string err;
    if (!SerializeValue(S, 2, out, 0, err)) return Fail(S, err.c_str());
    out += "\n";
    mkdir(DataDir().c_str(), 0755);
    std::string path = DataPath(key);
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return Fail(S, "fayl yazilmadi");
    size_t w = fwrite(out.data(), 1, out.size(), f);
    int cl = fclose(f);
    if (w != out.size() || cl != 0 || rename(tmp.c_str(), path.c_str()) != 0) {
        unlink(tmp.c_str());
        return Fail(S, "fayl yazilmadi");
    }
    lua_pushboolean(S, 1);
    return 1;
}

// loadData(acar [, ilkin_deyer]) -> deyer
static int l_loadData(lua_State* S) {
    const char* key = luaL_checkstring(S, 1);
    if (!ValidKey(key)) return Fail(S, "yanlis acar");
    FILE* f = fopen(DataPath(key).c_str(), "rb");
    if (!f) {
        if (lua_isnone(S, 2)) lua_pushnil(S); else lua_pushvalue(S, 2);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > (4 << 20)) { fclose(f); return Fail(S, "fayl cox boyukdur"); }
    std::vector<char> buf((size_t)sz);
    size_t rd = sz ? fread(buf.data(), 1, (size_t)sz, f) : 0;
    fclose(f);
    if (rd != (size_t)sz) return Fail(S, "fayl oxunmadi");

    if (luaL_loadbufferx(S, buf.data(), buf.size(), "=data", "t") != LUA_OK) {
        std::string m = lua_tostring(S, -1) ? lua_tostring(S, -1) : "?";
        lua_pop(S, 1);
        return Fail(S, ("fayl korlanib: " + m).c_str());
    }
    lua_newtable(S);                               // bos mühit: heç bir qlobal funksiya yoxdur
    if (!lua_setupvalue(S, -2, 1)) lua_pop(S, 1);
    if (lua_pcall(S, 0, 1, 0) != LUA_OK) {
        std::string m = lua_tostring(S, -1) ? lua_tostring(S, -1) : "?";
        lua_pop(S, 1);
        return Fail(S, ("fayl korlanib: " + m).c_str());
    }
    return 1;
}

static int l_deleteData(lua_State* S) {
    const char* key = luaL_checkstring(S, 1);
    if (!ValidKey(key)) return Fail(S, "yanlis acar");
    lua_pushboolean(S, unlink(DataPath(key).c_str()) == 0);
    return 1;
}

// =====================================================================
//  4) files/lua/*.lua MODUL YUKLEYICISI
//  Her modul oz mühitinde (env, __index=_G) isleyir. Modulun oz
//  onRender / onCommand / onUnload funksiyalari ayrica cagirilir.
// =====================================================================
struct Module {
    std::string name;
    int env;
    bool renderDead;
};
static std::vector<Module> s_modules;

static void UnloadModules(lua_State* L) {
    for (size_t i = 0; i < s_modules.size(); i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s_modules[i].env);
        lua_pushstring(L, "onUnload");
        lua_rawget(L, -2);
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) ReportTop(L, s_modules[i].name.c_str());
        } else {
            lua_pop(L, 1);
        }
        lua_pop(L, 1);                                  // env
        luaL_unref(L, LUA_REGISTRYINDEX, s_modules[i].env);
    }
    s_modules.clear();
}

void LoadModules(lua_State* L) {
    Guard lk;
    UnloadModules(L);

    std::string dir = std::string(g_pszStorage) + "/lua";
    DIR* d = opendir(dir.c_str());
    if (!d) { FLog("[Lua] modul qovlugu yoxdur: %s", dir.c_str()); return; }
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".lua") == 0 && n != "core.lua") names.push_back(n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());

    for (size_t i = 0; i < names.size(); i++) {
        std::string path = dir + "/" + names[i];
        if (luaL_loadfile(L, path.c_str()) != LUA_OK) { ReportTop(L, names[i].c_str()); continue; }

        lua_newtable(L);                                // env
        lua_newtable(L);                                // meta
        lua_pushglobaltable(L);
        lua_setfield(L, -2, "__index");
        lua_setmetatable(L, -2);
        lua_pushstring(L, names[i].c_str());
        lua_setfield(L, -2, "MODULE_NAME");
        lua_pushvalue(L, -1);
        int envRef = luaL_ref(L, LUA_REGISTRYINDEX);    // env-in surəti
        if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);   // chunk-in _ENV-i

        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            ReportTop(L, names[i].c_str());
            luaL_unref(L, LUA_REGISTRYINDEX, envRef);
            continue;
        }
        Module m;
        m.name = names[i];
        m.env = envRef;
        m.renderDead = false;
        s_modules.push_back(m);
        FLog("[Lua] modul yuklendi: %s", names[i].c_str());
    }
}

bool DispatchCommand(lua_State* L, const std::string& cmd, const std::string& args) {
    Guard lk;
    for (size_t i = 0; i < s_modules.size(); i++) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s_modules[i].env);
        lua_pushstring(L, "onCommand");
        lua_rawget(L, -2);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 2); continue; }
        lua_pushstring(L, cmd.c_str());
        lua_pushstring(L, args.c_str());
        if (lua_pcall(L, 2, 1, 0) != LUA_OK) { ReportTop(L, s_modules[i].name.c_str()); lua_pop(L, 1); continue; }
        bool handled = lua_toboolean(L, -1) != 0;
        lua_pop(L, 2);                                  // netice + env
        if (handled) return true;
    }
    return false;
}

void DispatchRender(lua_State* L) {
    Guard lk;
    FlushErrors();
    for (size_t i = 0; i < s_modules.size(); i++) {
        if (s_modules[i].renderDead) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, s_modules[i].env);
        lua_pushstring(L, "onRender");
        lua_rawget(L, -2);
        if (!lua_isfunction(L, -1)) { lua_pop(L, 2); continue; }
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            ReportTop(L, s_modules[i].name.c_str());
            s_modules[i].renderDead = true;             // xeta chati doldurmasin
        }
        lua_pop(L, 1);                                  // env
    }
}

// =====================================================================
//  Qeydiyyat / Shutdown
// =====================================================================
void Register(lua_State* L) {
    Guard lk;
    s_main = L;
    s_errq.clear();

    lua_register(L, "findSymbol", l_findSymbol);
    lua_register(L, "findSymbols", l_findSymbols);
    lua_register(L, "symbolInfo", l_symbolInfo);

    lua_register(L, "hook", l_hook);
    lua_register(L, "unhook", l_unhook);
    lua_register(L, "argf", l_argf);
    lua_register(L, "setArg", l_setArg);
    lua_register(L, "setArgf", l_setArgf);

    lua_register(L, "javaContext", l_javaContext);
    lua_register(L, "javaClass", l_javaClass);
    lua_register(L, "javaNew", l_javaNew);
    lua_register(L, "javaCall", l_javaCall);
    lua_register(L, "javaCallStatic", l_javaCallStatic);
    lua_register(L, "javaGetField", l_javaGetField);
    lua_register(L, "javaGetStaticField", l_javaGetStaticField);
    lua_register(L, "javaSetField", l_javaSetField);
    lua_register(L, "javaSetStaticField", l_javaSetStaticField);
    lua_register(L, "javaRelease", l_javaRelease);

    lua_register(L, "saveData", l_saveData);
    lua_register(L, "loadData", l_loadData);
    lua_register(L, "deleteData", l_deleteData);

    lua_register(L, "readHex", l_readHex);
    lua_register(L, "patchCode", l_patchCode);
    lua_register(L, "bitsToFloat", l_bitsToFloat);
    lua_register(L, "floatToBits", l_floatToBits);
}

void Shutdown(lua_State* L) {
    Guard lk;
    UnloadModules(L);
    for (int i = 0; i < LB_MAX_HOOKS; i++) UnhookSlot(i, L);
    JavaShutdown();
    s_errq.clear();
    s_main = nullptr;
}

}   // namespace LuaBridge

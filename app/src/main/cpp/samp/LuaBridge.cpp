#include "LuaBridge.h"
#include "vendor/lua/lua.hpp"
#include "vendor/armhook/patch.h"   // shadowhook sarmalayicisi (layihede artiq var)
#include <jni.h>
#include <dirent.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <elf.h>

extern JavaVM* javaVM;
extern char* g_pszStorage;
void FLog(const char* fmt, ...);

std::recursive_mutex g_luaMutex;
static lua_State* g_L = nullptr;   // hook geri-cagirislari ucun saxlanir

// =====================================================================
//  1) ELF SIMVOL CƏDVƏLİ: .so faylini diskden oxuyub ad -> unvan tapir
// =====================================================================
struct SymEntry { std::string name; uint64_t value; };

static bool ReadWholeFile(const char* path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(sz);
    size_t r = fread(out.data(), 1, sz, f);
    fclose(f);
    return r == (size_t)sz;
}

// so faylinin diskdeki yolunu /proc/self/maps-den tapir (adi ile axtarir)
static std::string FindSoPath(const char* soName) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return "";
    char line[1024];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, soName)) {
            char* p = strchr(line, '/');
            if (p) {
                std::string path(p);
                while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
                result = path;
                break;
            }
        }
    }
    fclose(f);
    return result;
}

// so-nun /proc/self/maps-deki ilk (icra olunan ve ya ilk) yuklenme unvanini tapir
static uint64_t FindLoadBase(const char* soName) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[1024];
    uint64_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, soName)) {
            base = strtoull(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return base;
}

// so-nun .symtab/.strtab (yoxdursa .dynsym/.dynstr) cedvelini chixarir
static bool ParseSymbols64(const std::vector<uint8_t>& buf, std::vector<SymEntry>& out) {
    if (buf.size() < sizeof(Elf64_Ehdr)) return false;
    auto* eh = (const Elf64_Ehdr*)buf.data();
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0) return false;
    if (eh->e_ident[EI_CLASS] != ELFCLASS64) return false;

    auto* sh = (const Elf64_Shdr*)(buf.data() + eh->e_shoff);
    const char* shstr = (const char*)(buf.data() + sh[eh->e_shstrndx].sh_offset);

    const Elf64_Shdr *symtab = nullptr, *strtab = nullptr;
    const Elf64_Shdr *dynsym = nullptr, *dynstr = nullptr;

    for (int i = 0; i < eh->e_shnum; i++) {
        const char* nm = shstr + sh[i].sh_name;
        if (sh[i].sh_type == SHT_SYMTAB) symtab = &sh[i];
        else if (sh[i].sh_type == SHT_DYNSYM) dynsym = &sh[i];
        else if (!strcmp(nm, ".strtab")) strtab = &sh[i];
        else if (!strcmp(nm, ".dynstr")) dynstr = &sh[i];
    }

    const Elf64_Shdr* useSym = symtab ? symtab : dynsym;
    const Elf64_Shdr* useStr = symtab ? strtab : dynstr;
    if (!useSym || !useStr) return false;

    size_t count = useSym->sh_size / sizeof(Elf64_Sym);
    auto* syms = (const Elf64_Sym*)(buf.data() + useSym->sh_offset);
    const char* strs = (const char*)(buf.data() + useStr->sh_offset);

    out.reserve(count);
    for (size_t i = 0; i < count; i++) {
        if (syms[i].st_name == 0 || syms[i].st_value == 0) continue;
        out.push_back({ strs + syms[i].st_name, syms[i].st_value });
    }
    return true;
}

// her so ucun simvol siyahisini bir defe cixarib yaddasda saxlayir
static std::unordered_map<std::string, std::vector<SymEntry>> g_symCache;
static std::unordered_map<std::string, uint64_t> g_baseCache;

static uintptr_t ResolveSymbol(const char* soName, const char* symName) {
    std::string key = soName;
    auto itSyms = g_symCache.find(key);
    if (itSyms == g_symCache.end()) {
        std::string path = FindSoPath(soName);
        if (path.empty()) { FLog("[Bridge] so tapilmadi: %s", soName); return 0; }

        std::vector<uint8_t> buf;
        if (!ReadWholeFile(path.c_str(), buf)) { FLog("[Bridge] oxuna bilmedi: %s", path.c_str()); return 0; }

        std::vector<SymEntry> syms;
        if (!ParseSymbols64(buf, syms)) { FLog("[Bridge] ELF simvol cedveli yoxdur: %s", soName); return 0; }

        g_baseCache[key] = FindLoadBase(soName);
        itSyms = g_symCache.emplace(key, std::move(syms)).first;
        FLog("[Bridge] %s: %zu simvol yuklendi", soName, itSyms->second.size());
    }

    uint64_t base = g_baseCache[key];
    if (!base) return 0;

    for (auto& s : itSyms->second) {
        if (s.name == symName) return (uintptr_t)(base + s.value);
    }
    return 0;
}

static int l_findSymbol(lua_State* S) {
    const char* so = luaL_checkstring(S, 1);
    const char* name = luaL_checkstring(S, 2);
    uintptr_t addr = ResolveSymbol(so, name);
    if (!addr) return 0;
    lua_pushinteger(S, (lua_Integer)addr);
    return 1;
}

// =====================================================================
//  2) UMUMI HOOK: oncedan tikilmis bir nece imza wrapper-i
//     Lua: hookSig("void_ptr", addr_or_symbolname, function(args...) ... end)
//     Geri cagirisdan "true" qaytarsa original funksiya da cagirilir (davam),
//     "false" qaytarsa original funksiya ATLANIR (deyisdirilir).
// =====================================================================
struct HookSlot {
    void* orig = nullptr;
    int luaRef = LUA_NOREF;
    bool active = false;
};

// imza basina 1 statik slot (eyni imzadan birden cox hook lazimdirsa bu boyude)
static HookSlot g_slot_void_ptr;
static HookSlot g_slot_void_ptr_f32;
static HookSlot g_slot_void_ptr_i32;
static HookSlot g_slot_i32_ptr_i32;

static void CallLuaHook(int ref, std::initializer_list<lua_Number> nums, std::initializer_list<lua_Integer> ints) {
    std::lock_guard<std::recursive_mutex> lk(g_luaMutex);
    if (!g_L || ref == LUA_NOREF) return;
    lua_rawgeti(g_L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(g_L, -1)) { lua_pop(g_L, 1); return; }
    int n = 0;
    for (auto v : nums) { lua_pushnumber(g_L, v); n++; }
    for (auto v : ints) { lua_pushinteger(g_L, v); n++; }
    if (lua_pcall(g_L, n, 0, 0) != LUA_OK) {
        FLog("[Bridge] hook xeta: %s", lua_tostring(g_L, -1));
        lua_pop(g_L, 1);
    }
}

// --- void f(void* a1) ---
typedef void (*Fn_void_ptr)(void*);
static void Hooked_void_ptr(void* a1) {
    CallLuaHook(g_slot_void_ptr.luaRef, {}, { (lua_Integer)(uintptr_t)a1 });
    if (g_slot_void_ptr.orig) ((Fn_void_ptr)g_slot_void_ptr.orig)(a1);
}
// --- void f(void* a1, float a2) ---
typedef void (*Fn_void_ptr_f32)(void*, float);
static void Hooked_void_ptr_f32(void* a1, float a2) {
    CallLuaHook(g_slot_void_ptr_f32.luaRef, { a2 }, { (lua_Integer)(uintptr_t)a1 });
    if (g_slot_void_ptr_f32.orig) ((Fn_void_ptr_f32)g_slot_void_ptr_f32.orig)(a1, a2);
}
// --- void f(void* a1, int a2) ---
typedef void (*Fn_void_ptr_i32)(void*, int);
static void Hooked_void_ptr_i32(void* a1, int a2) {
    CallLuaHook(g_slot_void_ptr_i32.luaRef, {}, { (lua_Integer)(uintptr_t)a1, (lua_Integer)a2 });
    if (g_slot_void_ptr_i32.orig) ((Fn_void_ptr_i32)g_slot_void_ptr_i32.orig)(a1, a2);
}
// --- int f(void* a1, int a2) ---
typedef int (*Fn_i32_ptr_i32)(void*, int);
static int Hooked_i32_ptr_i32(void* a1, int a2) {
    CallLuaHook(g_slot_i32_ptr_i32.luaRef, {}, { (lua_Integer)(uintptr_t)a1, (lua_Integer)a2 });
    if (g_slot_i32_ptr_i32.orig) return ((Fn_i32_ptr_i32)g_slot_i32_ptr_i32.orig)(a1, a2);
    return 0;
}

static bool InstallHook(const char* sig, uintptr_t target, HookSlot& slot, void* hookedFn) {
    if (slot.active) { FLog("[Bridge] '%s' artiq hook olunub (bir slotda 1 hook)", sig); return false; }
    void* orig = nullptr;
    // CHook::InlineHook neticeni qaytarmir, ona gore shadowhook-u birbasa cagiririq
    int rc = shadowhook_hook_func_addr((void*)target, hookedFn, &orig);
    if (rc != 0) {
        FLog("[Bridge] hook ugursuz (rc=%d): %s @ 0x%lx", rc, sig, (unsigned long)target);
        return false;
    }
    slot.orig = orig;
    slot.active = true;
    return true;
}

// hookSig(sig, addrOrSoAndName..., luaFunction)
// istifade: hookSig("void_ptr", addr, function(self) ... end)
static int l_hookSig(lua_State* S) {
    const char* sig = luaL_checkstring(S, 1);
    uintptr_t target = (uintptr_t)luaL_checkinteger(S, 2);
    luaL_checktype(S, 3, LUA_TFUNCTION);
    lua_pushvalue(S, 3);
    int ref = luaL_ref(S, LUA_REGISTRYINDEX);

    bool ok = false;
    if (!strcmp(sig, "void_ptr"))       { g_slot_void_ptr.luaRef = ref;       ok = InstallHook(sig, target, g_slot_void_ptr, (void*)Hooked_void_ptr); }
    else if (!strcmp(sig, "void_ptr_f32")) { g_slot_void_ptr_f32.luaRef = ref;  ok = InstallHook(sig, target, g_slot_void_ptr_f32, (void*)Hooked_void_ptr_f32); }
    else if (!strcmp(sig, "void_ptr_i32")) { g_slot_void_ptr_i32.luaRef = ref;  ok = InstallHook(sig, target, g_slot_void_ptr_i32, (void*)Hooked_void_ptr_i32); }
    else if (!strcmp(sig, "i32_ptr_i32"))  { g_slot_i32_ptr_i32.luaRef = ref;   ok = InstallHook(sig, target, g_slot_i32_ptr_i32, (void*)Hooked_i32_ptr_i32); }
    else { luaL_unref(S, LUA_REGISTRYINDEX, ref); return luaL_error(S, "hookSig: bilinmeyen imza '%s'", sig); }

    lua_pushboolean(S, ok);
    return 1;
}

// =====================================================================
//  3) JAVA KORPUSU: statik metodu ad/imza ile cagirir (String arqument/qayidis)
//     javaCallStatic("com/samp/mobile/game/SAMP", "someMethod", "(Ljava/lang/String;)V", "arg")
// =====================================================================
static int l_javaCallStatic(lua_State* S) {
    const char* cls = luaL_checkstring(S, 1);
    const char* method = luaL_checkstring(S, 2);
    const char* sig = luaL_checkstring(S, 3);
    const char* arg = luaL_optstring(S, 4, nullptr);

    JNIEnv* env = nullptr;
    if (!javaVM || javaVM->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (javaVM) javaVM->AttachCurrentThread(&env, nullptr);
    }
    if (!env) return luaL_error(S, "javaCallStatic: JNIEnv yoxdur");

    jclass c = env->FindClass(cls);
    if (!c) { env->ExceptionClear(); return luaL_error(S, "javaCallStatic: sinif tapilmadi: %s", cls); }

    bool isVoid = strstr(sig, ")V") != nullptr;
    bool wantsString = strstr(sig, "Ljava/lang/String;") != nullptr;

    if (isVoid) {
        jmethodID m = env->GetStaticMethodID(c, method, sig);
        if (!m) { env->ExceptionClear(); return luaL_error(S, "javaCallStatic: metod tapilmadi: %s", method); }
        if (wantsString && arg) {
            jstring js = env->NewStringUTF(arg);
            env->CallStaticVoidMethod(c, m, js);
            env->DeleteLocalRef(js);
        } else {
            env->CallStaticVoidMethod(c, m);
        }
        return 0;
    } else {
        // yalniz String qayidis desteklenir (umumi hal kifayet edir)
        jmethodID m = env->GetStaticMethodID(c, method, sig);
        if (!m) { env->ExceptionClear(); return luaL_error(S, "javaCallStatic: metod tapilmadi: %s", method); }
        jobject r;
        if (wantsString && arg) {
            jstring js = env->NewStringUTF(arg);
            r = env->CallStaticObjectMethod(c, m, js);
            env->DeleteLocalRef(js);
        } else {
            r = env->CallStaticObjectMethod(c, m);
        }
        if (!r) return 0;
        const char* chars = env->GetStringUTFChars((jstring)r, nullptr);
        lua_pushstring(S, chars);
        env->ReleaseStringUTFChars((jstring)r, chars);
        env->DeleteLocalRef(r);
        return 1;
    }
}

// =====================================================================
//  4) SAXLAMA: files/lua_data.txt icinde sade acar=deyer (her setirde biri)
// =====================================================================
static std::string DataPath() {
    return std::string(g_pszStorage) + "/lua_data.txt";
}

static std::unordered_map<std::string, std::string> g_kv;
static bool g_kvLoaded = false;

static void LoadKV() {
    if (g_kvLoaded) return;
    g_kvLoaded = true;
    std::ifstream f(DataPath());
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t p = line.find('=');
        if (p == std::string::npos) continue;
        g_kv[line.substr(0, p)] = line.substr(p + 1);
    }
}

static void SaveKV() {
    std::ofstream f(DataPath(), std::ios::trunc);
    if (!f.is_open()) return;
    for (auto& kv : g_kv) f << kv.first << "=" << kv.second << "\n";
}

static int l_saveData(lua_State* S) {
    LoadKV();
    const char* k = luaL_checkstring(S, 1);
    const char* v = luaL_checkstring(S, 2);
    std::string val(v);
    for (auto& ch : val) if (ch == '\n') ch = ' ';   // setir qirilmasin
    g_kv[k] = val;
    SaveKV();
    return 0;
}
static int l_loadData(lua_State* S) {
    LoadKV();
    const char* k = luaL_checkstring(S, 1);
    auto it = g_kv.find(k);
    if (it == g_kv.end()) return 0;
    lua_pushstring(S, it->second.c_str());
    return 1;
}

// =====================================================================
//  5) MODUL YUKLEYICI: files/lua/*.lua elifba sirasi ile yuklenir
// =====================================================================
void LuaBridge::LoadModules(lua_State* L) {
    std::string dir = std::string(g_pszStorage) + "/lua";
    DIR* d = opendir(dir.c_str());
    if (!d) { FLog("[Bridge] lua/ qovlugu yoxdur (optional): %s", dir.c_str()); return; }

    std::vector<std::string> files;
    dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string name = e->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".lua") files.push_back(name);
    }
    closedir(d);
    std::sort(files.begin(), files.end());

    for (auto& f : files) {
        std::string full = dir + "/" + f;
        if (luaL_dofile(L, full.c_str()) != LUA_OK) {
            FLog("[Bridge] modul xeta (%s): %s", f.c_str(), lua_tostring(L, -1));
            lua_pop(L, 1);
        } else {
            FLog("[Bridge] modul yuklendi: %s", f.c_str());
        }
    }
}

// =====================================================================
void LuaBridge::Register(lua_State* L) {
    g_L = L;
    lua_register(L, "findSymbol", l_findSymbol);
    lua_register(L, "hookSig", l_hookSig);
    lua_register(L, "javaCallStatic", l_javaCallStatic);
    lua_register(L, "saveData", l_saveData);
    lua_register(L, "loadData", l_loadData);
}

void LuaBridge::Shutdown(lua_State* L) {
    // Qeyd: hook-lar unhook olunmur (shadowhook sarmalayicisinda unhook yoxdursa).
    // /luareload zamani kohne luaRef-ler etibarsizlasir; hook cagiranda state
    // g_L yenilenir, amma kohne funksiya gostericileri qaliq qala biler.
    // Bu, bilinen mehdudiyyetdir: hook qoyulan funksiyalari tez-tez reload etme.
    g_L = nullptr;
}

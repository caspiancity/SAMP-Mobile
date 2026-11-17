# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v8.py -- layihenin kokunde isle:  python3 apply_v8.py     (evvel apply_v7 isleyib)
#   1) plate.png-ni RenderWare-e (RtPNGImageRead) QOYMADAN oxuyur: oz PNG dekoderi (inflate daxil) -> RW-in fayl sistemi problemi bitir
#   2) fayl yolu: bir nece namized qovluq yoxlanir (g_pszStorage/SAMP, /storage/emulated/0/Android/data/<paket>/files/SAMP ve s.)
#   3) plate.ttf de eyni yolla tapilir
#   4) Java: assetleri bir nece ada baxaraq hem getExternalFilesDir hem getFilesDir altina kopyalayir
#   5) tenzimle kamerasi: yer altina / masinin icine dusmesin (bucaq limitleri)
# Geri qaytarmaq: her faylin yaninda *.bakv8
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
        print("[XETA]  %s: anker tapilmadi -> %s (fayla toxunulmadi)" % (name, e)); return
    if not os.path.exists(path + ".bakv8"):
        shutil.copy(path, path + ".bakv8")
    wr(path, n, crlf)
    print("[OK]    %s" % name)

def once(s, old, new, what):
    if s.count(old) != 1:
        raise ValueError("%s (%d tapildi)" % (what, s.count(old)))
    return s.replace(old, new, 1)

def sub1(s, pattern, repl, what, flags=0):
    pat = re.compile(pattern, flags)
    n = len(pat.findall(s))
    if n != 1:
        raise ValueError("%s (%d tapildi)" % (what, n))
    return pat.sub(lambda m: repl(m) if callable(repl) else repl, s, count=1)

# =============================================================== 1. AzPlate.cpp
INCLUDES8 = r'''#include <map>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <android/log.h>   // PLATE_V8
extern char* g_pszStorage;'''

PATHS_AND_PNG8 = r'''    // ------------------------------------------------------------ PLATE_V8: fayl yolu
    static std::string PkgName()
    {
        static std::string pk;
        if (!pk.empty()) return pk;
        FILE* f = fopen("/proc/self/cmdline", "rb");
        if (f)
        {
            char b[256];
            size_t n = fread(b, 1, sizeof(b) - 1, f);
            fclose(f);
            b[n] = 0;
            pk = b;
        }
        size_t c = pk.find(':');
        if (c != std::string::npos) pk.erase(c);
        if (pk.empty()) pk = "com.samp.mobile";
        return pk;
    }

    static std::vector<std::string> Bases()
    {
        std::vector<std::string> v;
        std::vector<std::string> raw;
        std::string st = g_pszStorage ? g_pszStorage : "";
        if (!st.empty()) { raw.push_back(st + "/SAMP"); raw.push_back(st); }
        const std::string pk = PkgName();
        raw.push_back("/storage/emulated/0/Android/data/" + pk + "/files/SAMP");
        raw.push_back("/sdcard/Android/data/" + pk + "/files/SAMP");
        raw.push_back("/data/user/0/" + pk + "/files/SAMP");
        raw.push_back("/data/data/" + pk + "/files/SAMP");
        for (size_t i = 0; i < raw.size(); ++i)
        {
            std::string s = raw[i];
            while (s.size() > 1 && s[s.size() - 1] == '/') s.erase(s.size() - 1);
            s += "/";
            bool dup = false;
            for (size_t k = 0; k < v.size(); ++k) if (v[k] == s) dup = true;
            if (!dup) v.push_back(s);
        }
        return v;
    }

    // rel ("plate/plate.png") -> movcud faylin yolu; yoxdursa yazila bilen qovluq + rel
    static std::string P(const char* rel)
    {
        const std::vector<std::string> b = Bases();
        const char* sl = strrchr(rel, '/');
        const std::string bn = sl ? std::string(sl + 1) : std::string(rel);
        for (size_t i = 0; i < b.size(); ++i)
        {
            std::string a = b[i] + rel;
            if (access(a.c_str(), R_OK) == 0) return a;
            a = b[i] + bn;
            if (access(a.c_str(), R_OK) == 0) return a;
        }
        for (size_t i = 0; i < b.size(); ++i)
            if (access(b[i].c_str(), W_OK) == 0) return b[i] + rel;
        return b.empty() ? std::string(rel) : b[0] + rel;
    }

    // ------------------------------------------------------------ PLATE_V8: oz PNG dekoderi (RW-den asili deyil)
    struct PngInf  { const uint8_t* src; size_t len, pos; uint32_t tag; int bits; bool err; };
    struct PngHuff { uint16_t cnt[16]; uint16_t sym[288]; };

    static int PBit(PngInf& s)
    {
        if (s.bits == 0)
        {
            if (s.pos >= s.len) { s.err = true; return 0; }
            s.tag = s.src[s.pos++];
            s.bits = 8;
        }
        const int b = (int)(s.tag & 1u);
        s.tag >>= 1;
        s.bits--;
        return b;
    }

    static uint32_t PBits(PngInf& s, int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= (uint32_t)PBit(s) << i;
        return v;
    }

    static void PBuild(PngHuff& h, const uint8_t* lens, int n)
    {
        uint16_t offs[16];
        memset(h.cnt, 0, sizeof(h.cnt));
        memset(h.sym, 0, sizeof(h.sym));
        for (int i = 0; i < n; ++i) h.cnt[lens[i] & 15]++;
        h.cnt[0] = 0;
        uint16_t sum = 0;
        for (int i = 0; i < 16; ++i) { offs[i] = sum; sum = (uint16_t)(sum + h.cnt[i]); }
        for (int i = 0; i < n; ++i) if (lens[i]) h.sym[offs[lens[i] & 15]++] = (uint16_t)i;
    }

    static int PDecode(PngInf& s, const PngHuff& h)
    {
        int sum = 0, cur = 0, len = 0;
        do
        {
            cur = 2 * cur + PBit(s);
            ++len;
            if (len > 15 || s.err) { s.err = true; return 0; }
            sum += h.cnt[len];
            cur -= h.cnt[len];
        } while (cur >= 0);
        return h.sym[sum + cur];
    }

    static bool PInflate(const uint8_t* src, size_t len, std::vector<uint8_t>& out)
    {
        static const uint16_t lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        static const uint8_t  lext[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        static const uint16_t dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        static const uint8_t  dext[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
        static const uint8_t  order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

        PngInf s;
        s.src = src; s.len = len; s.pos = 0; s.tag = 0; s.bits = 0; s.err = false;
        bool fin = false;
        while (!fin && !s.err)
        {
            fin = PBit(s) != 0;
            const uint32_t type = PBits(s, 2);
            if (type == 0)
            {
                s.bits = 0;
                if (s.pos + 4 > s.len) return false;
                const size_t l = (size_t)s.src[s.pos] | ((size_t)s.src[s.pos + 1] << 8);
                s.pos += 4;
                if (s.pos + l > s.len) return false;
                out.insert(out.end(), s.src + s.pos, s.src + s.pos + l);
                s.pos += l;
                continue;
            }
            if (type == 3) return false;

            PngHuff lt, dt;
            if (type == 1)
            {
                uint8_t l[288];
                for (int i = 0; i < 144; ++i) l[i] = 8;
                for (int i = 144; i < 256; ++i) l[i] = 9;
                for (int i = 256; i < 280; ++i) l[i] = 7;
                for (int i = 280; i < 288; ++i) l[i] = 8;
                PBuild(lt, l, 288);
                uint8_t d[30];
                for (int i = 0; i < 30; ++i) d[i] = 5;
                PBuild(dt, d, 30);
            }
            else
            {
                const int hlit = (int)PBits(s, 5) + 257;
                const int hdist = (int)PBits(s, 5) + 1;
                const int hclen = (int)PBits(s, 4) + 4;
                uint8_t cl[19];
                memset(cl, 0, sizeof(cl));
                for (int i = 0; i < hclen; ++i) cl[order[i]] = (uint8_t)PBits(s, 3);
                PngHuff ch;
                PBuild(ch, cl, 19);
                uint8_t ll[320];
                memset(ll, 0, sizeof(ll));
                int num = 0;
                const int tot = hlit + hdist;
                while (num < tot && !s.err)
                {
                    const int sym = PDecode(s, ch);
                    if (s.err) break;
                    if (sym < 16) { ll[num++] = (uint8_t)sym; continue; }
                    int prev = 0, rep = 0;
                    if (sym == 16)
                    {
                        if (num == 0) return false;
                        prev = ll[num - 1];
                        rep = 3 + (int)PBits(s, 2);
                    }
                    else if (sym == 17) rep = 3 + (int)PBits(s, 3);
                    else                rep = 11 + (int)PBits(s, 7);
                    if (num + rep > tot) return false;
                    while (rep--) ll[num++] = (uint8_t)prev;
                }
                if (s.err) return false;
                PBuild(lt, ll, hlit);
                PBuild(dt, ll + hlit, hdist);
            }

            while (!s.err)
            {
                int sym = PDecode(s, lt);
                if (s.err) break;
                if (sym < 256) { out.push_back((uint8_t)sym); continue; }
                if (sym == 256) break;
                sym -= 257;
                if (sym >= 29) return false;
                const int length = lbase[sym] + (int)PBits(s, lext[sym]);
                const int ds = PDecode(s, dt);
                if (s.err || ds >= 30) return false;
                const size_t dist = (size_t)dbase[ds] + PBits(s, dext[ds]);
                if (dist > out.size()) return false;
                size_t from = out.size() - dist;
                for (int i = 0; i < length; ++i) { const uint8_t b = out[from + i]; out.push_back(b); }
            }
        }
        return !s.err;
    }

    static uint32_t BE32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }

    static bool DecodePng(const std::vector<uint8_t>& d, int& W, int& H, std::vector<uint8_t>& rgba, std::string& err)
    {
        static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        if (d.size() < 33 || memcmp(&d[0], sig, 8) != 0) { err = "PNG deyil (basliq sehvdir)"; return false; }
        size_t p = 8;
        int depth = 0, ct = -1, il = 0;
        W = H = 0;
        std::vector<uint8_t> idat, pal, trns;
        while (p + 12 <= d.size())
        {
            const size_t len = (size_t)BE32(&d[p]);
            if (p + 12 + len > d.size()) break;
            const uint8_t* ty = &d[p + 4];
            const uint8_t* cd = &d[p + 8];
            if (!memcmp(ty, "IHDR", 4) && len >= 13) { W = (int)BE32(cd); H = (int)BE32(cd + 4); depth = cd[8]; ct = cd[9]; il = cd[12]; }
            else if (!memcmp(ty, "PLTE", 4)) pal.assign(cd, cd + len);
            else if (!memcmp(ty, "tRNS", 4)) trns.assign(cd, cd + len);
            else if (!memcmp(ty, "IDAT", 4)) idat.insert(idat.end(), cd, cd + len);
            else if (!memcmp(ty, "IEND", 4)) break;
            p += 12 + len;
        }
        if (W < 2 || H < 2 || W > 8192 || H > 8192) { err = "PNG olcusu yararsizdir"; return false; }
        if (il != 0) { err = "interlaced PNG desteklenmir (Interlace sondur)"; return false; }
        int ch = 0;
        switch (ct) { case 0: ch = 1; break; case 2: ch = 3; break; case 3: ch = 1; break; case 4: ch = 2; break; case 6: ch = 4; break; default: break; }
        if (!ch) { err = "PNG reng tipi taninmir"; return false; }
        if (ct == 3) { if (depth != 1 && depth != 2 && depth != 4 && depth != 8) { err = "PNG bit derinliyi desteklenmir"; return false; } }
        else if (depth != 8 && depth != 16) { err = "PNG bit derinliyi desteklenmir (8/16 lazimdir)"; return false; }
        if (idat.size() < 3) { err = "PNG-de IDAT yoxdur"; return false; }

        const size_t stride = ((size_t)W * ch * depth + 7) / 8;
        const int bpp = std::max(1, ch * depth / 8);
        std::vector<uint8_t> raw;
        raw.reserve((stride + 1) * (size_t)H);
        if (!PInflate(&idat[2], idat.size() - 2, raw)) { err = "PNG sixisdirma xetasi (zlib)"; return false; }
        if (raw.size() < (stride + 1) * (size_t)H) { err = "PNG melumati natamamdir"; return false; }

        std::vector<uint8_t> img(stride * (size_t)H);
        for (int y = 0; y < H; ++y)
        {
            const int ft = raw[(size_t)y * (stride + 1)];
            const uint8_t* in = &raw[(size_t)y * (stride + 1) + 1];
            uint8_t* o = &img[(size_t)y * stride];
            const uint8_t* up = y ? &img[(size_t)(y - 1) * stride] : nullptr;
            for (size_t x = 0; x < stride; ++x)
            {
                const int a = (x >= (size_t)bpp) ? o[x - bpp] : 0;
                const int b = up ? up[x] : 0;
                const int c = (up && x >= (size_t)bpp) ? up[x - bpp] : 0;
                int v = in[x];
                switch (ft)
                {
                    case 0: break;
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) >> 1; break;
                    case 4:
                    {
                        const int pp = a + b - c;
                        const int pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
                        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                        break;
                    }
                    default: err = "PNG filter xetasi"; return false;
                }
                o[x] = (uint8_t)v;
            }
        }

        rgba.assign((size_t)W * H * 4, 255);
        for (int y = 0; y < H; ++y)
        {
            const uint8_t* row = &img[(size_t)y * stride];
            for (int x = 0; x < W; ++x)
            {
                uint8_t r = 0, g = 0, b = 0, a = 255;
                if (ct == 6)      { const uint8_t* q = row + (size_t)x * (depth == 8 ? 4 : 8); const int st = depth == 8 ? 1 : 2; r = q[0]; g = q[st]; b = q[2 * st]; a = q[3 * st]; }
                else if (ct == 2) { const uint8_t* q = row + (size_t)x * (depth == 8 ? 3 : 6); const int st = depth == 8 ? 1 : 2; r = q[0]; g = q[st]; b = q[2 * st]; }
                else if (ct == 0) { r = g = b = row[(size_t)x * (depth == 8 ? 1 : 2)]; }
                else if (ct == 4) { const uint8_t* q = row + (size_t)x * (depth == 8 ? 2 : 4); const int st = depth == 8 ? 1 : 2; r = g = b = q[0]; a = q[st]; }
                else
                {
                    int idx;
                    if (depth == 8) idx = row[x];
                    else { const int bp = x * depth; idx = (row[bp >> 3] >> (8 - depth - (bp & 7))) & ((1 << depth) - 1); }
                    if ((size_t)idx * 3 + 2 < pal.size()) { r = pal[idx * 3]; g = pal[idx * 3 + 1]; b = pal[idx * 3 + 2]; }
                    if ((size_t)idx < trns.size()) a = trns[idx];
                }
                uint8_t* o = &rgba[((size_t)y * W + x) * 4];
                o[0] = r; o[1] = g; o[2] = b; o[3] = a;
            }
        }
        return true;
    }
'''

BASETEX8 = r'''    static RwTexture* BaseTex()
    {
        if (g_baseTex) return g_baseTex;
        static time_t lastTry = 0;
        const time_t now = time(nullptr);
        if (g_baseTried && now - lastTry < 3) return nullptr;     // PLATE_V8: ugursuz olsa 3 san-den bir yeniden cehd (fayli sonradan qoysan isleyir)
        g_baseTried = true;
        lastTry = now;
        EnsureCfg();

        if (!RwRasterLock || !RwRasterUnlock) { g_err = "RW funksiyalari yoxdur"; return nullptr; }

        const std::string path = P("plate/plate.png");
        std::vector<uint8_t> file;
        {
            FILE* f = fopen(path.c_str(), "rb");
            if (!f)
            {
                g_err = "plate.png yoxdur: " + path;
                __android_log_print(ANDROID_LOG_WARN, "SAMPPlate", "%s", g_err.c_str());
                const std::vector<std::string> b = Bases();
                for (size_t i = 0; i < b.size(); ++i)
                    __android_log_print(ANDROID_LOG_WARN, "SAMPPlate", "  baxilan qovluq: %s", b[i].c_str());
                return nullptr;
            }
            uint8_t buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0) file.insert(file.end(), buf, buf + n);
            fclose(f);
        }

        int iw = 0, ih = 0;
        std::vector<uint8_t> src;
        std::string perr;
        if (!DecodePng(file, iw, ih, src, perr))
        {
            g_err = "plate.png oxunmadi: " + perr;
            __android_log_print(ANDROID_LOG_WARN, "SAMPPlate", "%s (%s)", g_err.c_str(), path.c_str());
            return nullptr;
        }

        const int TW = 1024, TH = 256;
        RwRaster* ras = RwRasterCreate(TW, TH, 32, rwRASTERFORMAT8888 | rwRASTERTYPETEXTURE);
        if (!ras) { g_err = "raster yaradilmadi"; return nullptr; }
        uint8_t* px = RwRasterLock(ras, 0, rwRASTERLOCKWRITE);
        if (!px)  { g_err = "raster kilidlenmedi"; return nullptr; }

        const int stride = (ras->stride > 0) ? (int)ras->stride : TW * 4;
        const int istr = iw * 4;
        const uint8_t* sp = &src[0];
        for (int y = 0; y < TH; ++y)
        {
            float fy = ((float)y + 0.5f) * (float)ih / (float)TH - 0.5f;
            if (fy < 0.0f) fy = 0.0f;
            int y0 = (int)fy; if (y0 > ih - 2) y0 = ih - 2;
            const float ty = fy - (float)y0;
            for (int x = 0; x < TW; ++x)
            {
                float fx = ((float)x + 0.5f) * (float)iw / (float)TW - 0.5f;
                if (fx < 0.0f) fx = 0.0f;
                int x0 = (int)fx; if (x0 > iw - 2) x0 = iw - 2;
                const float tx = fx - (float)x0;
                float ch[4];
                for (int k = 0; k < 4; ++k)
                {
                    const float a = sp[y0 * istr + x0 * 4 + k];
                    const float b = sp[y0 * istr + (x0 + 1) * 4 + k];
                    const float c = sp[(y0 + 1) * istr + x0 * 4 + k];
                    const float d = sp[(y0 + 1) * istr + (x0 + 1) * 4 + k];
                    ch[k] = (a * (1.0f - tx) + b * tx) * (1.0f - ty) + (c * (1.0f - tx) + d * tx) * ty;
                }
                uint8_t* o = px + y * stride + x * 4;
                o[0] = (uint8_t)(g_swapRB ? ch[2] : ch[0]);
                o[1] = (uint8_t)ch[1];
                o[2] = (uint8_t)(g_swapRB ? ch[0] : ch[2]);
                o[3] = (uint8_t)ch[3];
            }
        }
        RwRasterUnlock(ras);

        g_baseTex = RwTextureCreate(ras);
        if (!g_baseTex) g_err = "tekstur yaradilmadi";
        else
        {
            g_err.clear();
            __android_log_print(ANDROID_LOG_INFO, "SAMPPlate", "plate.png yuklendi: %s (%dx%d)", path.c_str(), iw, ih);
        }
        return g_baseTex;
    }

'''

def p_cpp8(s):
    s = once(s, "#include <map>\n#include <cmath>\n#include <cstdint>\nextern char* g_pszStorage;", INCLUDES8, "include bloku")
    old_p = '''    static std::string P(const char* rel)
    {
        return std::string(g_pszStorage ? g_pszStorage : "") + "/SAMP/" + rel;
    }
'''
    s = once(s, old_p, PATHS_AND_PNG8, "P() funksiyasi")
    a = s.find("    static RwTexture* BaseTex()")
    b = s.find("    // ImGui font atlasinin raster-i")
    if a < 0 or b < 0 or b < a:
        raise ValueError("BaseTex() blok sinirlari")
    s = s[:a] + BASETEX8 + s[b:]
    s = s.rstrip() + "\n\n// PLATE_V8: imguiwrapper.cpp ucun (plate.ttf yolu)\nstd::string PlateFindFile(const char* rel) { return PlateImpl::P(rel); }\n"
    return s

# =============================================================== 2. imguiwrapper.cpp: ttf yolu
def p_wrapper8(s):
    return sub1(
        s,
        r'if \(g_pszStorage\)\s*\{\s*const std::string plateP = std::string\(g_pszStorage\) \+ "/SAMP/plate/plate\.ttf";',
        'if (true)   // PLATE_V8\n\t\t{\n\t\t\textern std::string PlateFindFile(const char*);\n\t\t\tconst std::string plateP = PlateFindFile("plate/plate.ttf");',
        "plate.ttf yolu")

# =============================================================== 3. Java
JAVA8 = r'''    // PLATE_V8: plate.png / plate.ttf -> <externalFiles>/SAMP/plate/ (+ <files>/SAMP/plate/). Asset yoxdursa movcud fayla toxunmur.
    private void copyPlateAssets() {
        final String[] pngNames = { "plate/plate.png", "Plate/plate.png", "plate/Plate.png", "plate.png" };
        final String[] ttfNames = { "Fonts/plate.ttf", "fonts/plate.ttf", "plate/plate.ttf", "plate.ttf" };
        java.util.ArrayList<java.io.File> dirs = new java.util.ArrayList<java.io.File>();
        try {
            java.io.File b = getExternalFilesDir(null);
            if (b != null) dirs.add(new java.io.File(b, "SAMP/plate"));
        } catch (Throwable t) { }
        try { dirs.add(new java.io.File(getFilesDir(), "SAMP/plate")); } catch (Throwable t) { }
        for (java.io.File d : dirs) {
            try { d.mkdirs(); } catch (Throwable t) { }
            boolean a = copyFirstAsset(pngNames, new java.io.File(d, "plate.png"));
            boolean b = copyFirstAsset(ttfNames, new java.io.File(d, "plate.ttf"));
            android.util.Log.i("SAMPPlate", d.getPath() + " png=" + a + " ttf=" + b);
        }
    }

    private boolean copyFirstAsset(String[] names, java.io.File out) {
        for (String name : names) {
            java.io.InputStream in = null;
            try {
                in = getAssets().open(name);
                java.io.File tmp = new java.io.File(out.getPath() + ".tmp");
                java.io.FileOutputStream os = new java.io.FileOutputStream(tmp);
                byte[] buf = new byte[16384];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                os.close();
                if (out.exists()) out.delete();
                if (tmp.renameTo(out)) return true;
            } catch (Throwable t) {
                // bu adla asset yoxdur -> novbetisi
            } finally {
                try { if (in != null) in.close(); } catch (Throwable t) { }
            }
        }
        return false;
    }

'''

def p_java8(s):
    i = s.find("    // PLATE_V7: assets/Fonts/plate.ttf")
    if i >= 0:
        j = s.find("    private native void initializeSAMP();", i)
        if j < 0:
            raise ValueError("initializeSAMP elani (v7 bloku sonrasi)")
        s = s[:i] + JAVA8 + s[j:]
    else:
        m = re.search(r'\n([ \t]*)(?:(?:private|public|protected)\s+)?(?:static\s+)?native\s+void\s+initializeSAMP\s*\(\s*\)\s*;', s)
        if not m:
            raise ValueError("native initializeSAMP elani")
        s = s[:m.start() + 1] + JAVA8 + s[m.start() + 1:]
    if "copyPlateAssets();" not in s:
        m = re.search(r'\n([ \t]*)initializeSAMP\(\);', s)
        if not m:
            raise ValueError("initializeSAMP() cagirisi")
        s = s[:m.start() + 1] + m.group(1) + "copyPlateAssets();   // PLATE_V8\n" + s[m.start() + 1:]
    return s

# =============================================================== 4. gui.cpp: kamera limitleri
def p_gui8(s):
    s = once(s, "s_phi   = Clamp(s_phi, -1.2f, 1.2f);", "s_phi   = Clamp(s_phi, -0.95f, 0.95f);   // PLATE_V8", "phi limiti")
    s = once(s, "s_theta = Clamp(s_theta, -0.3f, 1.0f);", "s_theta = Clamp(s_theta, 0.0f, 0.9f);", "theta limiti")
    return s

# =============================================================== isle
if __name__ == "__main__":
    step("v8: AzPlate.cpp (yol + oz PNG dekoderi)", os.path.join(CPP, "game/AzPlate.cpp"), "PLATE_V8", p_cpp8)
    step("v8: plate.ttf yolu (imguiwrapper)",       os.path.join(CPP, "gui/imguiwrapper.cpp"), "PLATE_V8", p_wrapper8)
    step("v8: Java asset kopyalama",                JAVA, "PLATE_V8", p_java8)
    step("v8: tenzimle kamera limitleri (gui.cpp)", os.path.join(CPP, "gui/gui.cpp"), "PLATE_V8", p_gui8)
    print("\nBitdi. Geri qaytarmaq: her faylin yaninda *.bakv8")
    print("Yoxlama: adb logcat -s SAMPPlate   (hansi qovluqlara baxdigini ve yuklenme neticesini yazir)")

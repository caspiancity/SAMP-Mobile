# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v7.py -- layihenin kokunde isle:  python3 apply_v7.py     (evvel apply_v4, v5, v6 isleyib)
#   NOMRE (plaka) sistemi: PNG + plate.ttf, modele gore plates.json, /mm -> "Nomre" sehifesi,
#   "Tenzimle" rejimi (kamera plakaya yaxinlasir, sag-sol / irelli-geri / yuxari-asagi / donme / olcu, yadda saxla).
# Geri qaytarmaq: her faylin yaninda *.bakv7
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
    if not os.path.exists(path + ".bakv7"):
        shutil.copy(path, path + ".bakv7")
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

# =============================================================== 1. AzPlate.h
PLATE_H = r'''
// ===== PLATE_V7: PNG nomre lovhesi (3D) =====
struct PlateCfg { float pos[3]; float rot[3]; float w; };

namespace Plate
{
    bool  Normalize(const std::string& in, std::string& out);         // "10aa123" -> "10 AA 123"
    bool  Set(CVehicleGTA* veh, const std::string& text);             // nomreni teyin et
    void  Remove(CVehicleGTA* veh);
    bool  Get(CVehicleGTA* veh, std::string& text);
    void  SetSides(CVehicleGTA* veh, bool front, bool rear);          // qabaq / arxa plaka acigi
    void  GetSides(CVehicleGTA* veh, bool& front, bool& rear);
    void  Render(CVehicleGTA* veh);                                   // CVehicle render hook-undan
    PlateCfg GetCfg(CVehicleGTA* veh, bool front);                    // plates.json (yoxdursa avtomatik)
    void  SetCfg(CVehicleGTA* veh, bool front, const PlateCfg& c);    // yaddasda (modele gore)
    void  ResetCfg(CVehicleGTA* veh, bool front);
    bool  HasCustom(CVehicleGTA* veh, bool front);
    bool  SaveCfg();                                                  // plates.json-a yaz
    bool  WorldPose(CVehicleGTA* veh, bool front, float pos[3], float n[3], float up[3]);
    const char* LastError();
    void* BaseRaster();                                               // RwRaster* (onizleme), yoxdursa nullptr
}
'''

def p_h(s):
    if "namespace AzPlate" not in s:
        raise ValueError("namespace AzPlate")
    return s.rstrip() + "\n" + PLATE_H

# =============================================================== 2. AzPlate.cpp (modul)
PLATE_CPP = r'''// ===== PLATE_V7: PNG nomre lovhesi (3D, PNG + plate.ttf) =====
#include <map>
#include <cmath>
#include <cstdint>
extern char* g_pszStorage;
extern RwImage*  (*RtPNGImageRead)(const RwChar* imageName);
extern RwUInt8*  (*RwRasterLock)(RwRaster* raster, RwUInt8 level, RwInt32 lockMode);
extern RwRaster* (*RwRasterUnlock)(RwRaster* raster);
extern RwBool    (*RwImageDestroy)(RwImage* image);

ImFont* g_pPlateFont = nullptr;   // imguiwrapper.cpp doldurur (assets/Fonts/plate.ttf)

namespace PlateImpl
{
    // ------------------------------------------------------------ mini JSON
    struct JV
    {
        int t = 0;   // 0 yox, 1 eded, 2 setir, 3 massiv, 4 obyekt, 5 bool
        double n = 0.0;
        std::string s;
        std::vector<JV> a;
        std::vector<std::pair<std::string, JV> > o;
    };

    static void Ws(const char*& p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p; }

    static bool Parse(const char*& p, JV& v, int depth)
    {
        if (depth > 16) return false;
        Ws(p);
        if (*p == '{')
        {
            ++p; v.t = 4; Ws(p);
            if (*p == '}') { ++p; return true; }
            while (true)
            {
                Ws(p);
                if (*p != '"') return false;
                JV k;
                if (!Parse(p, k, depth + 1)) return false;
                Ws(p);
                if (*p != ':') return false;
                ++p;
                JV val;
                if (!Parse(p, val, depth + 1)) return false;
                v.o.push_back(std::make_pair(k.s, val));
                Ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[')
        {
            ++p; v.t = 3; Ws(p);
            if (*p == ']') { ++p; return true; }
            while (true)
            {
                JV e;
                if (!Parse(p, e, depth + 1)) return false;
                v.a.push_back(e);
                Ws(p);
                if (*p == ',') { ++p; continue; }
                if (*p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"')
        {
            ++p; v.t = 2;
            while (*p && *p != '"')
            {
                if (*p == '\\' && p[1]) { ++p; v.s.push_back(*p == 'n' ? '\n' : (*p == 't' ? '\t' : *p)); }
                else v.s.push_back(*p);
                ++p;
            }
            if (*p != '"') return false;
            ++p;
            return true;
        }
        if (!strncmp(p, "true", 4))  { v.t = 5; v.n = 1.0; p += 4; return true; }
        if (!strncmp(p, "false", 5)) { v.t = 5; v.n = 0.0; p += 5; return true; }
        if (!strncmp(p, "null", 4))  { p += 4; return true; }
        char* end = nullptr;
        v.n = strtod(p, &end);
        if (end == p) return false;
        v.t = 1;
        p = end;
        return true;
    }

    static const JV* Find(const JV& o, const char* key)
    {
        for (size_t i = 0; i < o.o.size(); ++i) if (o.o[i].first == key) return &o.o[i].second;
        return nullptr;
    }

    // ------------------------------------------------------------ vezyet
    struct ModelCfg { bool hasF = false, hasR = false; PlateCfg f, r; std::string name; };

    static std::map<int, ModelCfg> g_models;
    static float g_textBox[4] = { 0.181f, 0.11f, 0.966f, 0.806f };   // plaka sekli daxilinde yazi sahesi (0..1)
    static bool  g_swapRB   = false;
    static bool  g_cfgLoaded = false;
    static float g_aspect   = 1235.0f / 273.0f;                      // plaka eni / hundurluyu
    static std::string g_err;

    struct Entry { uint32_t ct = 0; std::string text; bool front = true, rear = true; };
    static std::map<CVehicleGTA*, Entry> g_ent;
    static std::mutex g_mtx;

    static std::string P(const char* rel)
    {
        return std::string(g_pszStorage ? g_pszStorage : "") + "/SAMP/" + rel;
    }

    static void ReadVec(const JV* a, float* out, int n)
    {
        if (!a || a->t != 3) return;
        for (int i = 0; i < n && i < (int)a->a.size(); ++i) out[i] = (float)a->a[i].n;
    }

    static void ReadSide(const JV* o, PlateCfg& c)
    {
        if (!o || o->t != 4) return;
        ReadVec(Find(*o, "pos"), c.pos, 3);
        ReadVec(Find(*o, "rot"), c.rot, 3);
        const JV* w = Find(*o, "w");
        if (w && w->t == 1 && w->n > 0.05) c.w = (float)w->n;
    }

    static void EnsureCfg()
    {
        if (g_cfgLoaded) return;
        g_cfgLoaded = true;
        FILE* f = fopen(P("plates.json").c_str(), "rb");
        if (!f) return;
        std::string txt;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) txt.append(buf, n);
        fclose(f);

        JV root;
        const char* p = txt.c_str();
        if (!Parse(p, root, 0) || root.t != 4) { g_err = "plates.json oxunmadi (JSON xetasi)"; return; }

        ReadVec(Find(root, "text_box"), g_textBox, 4);
        const JV* sw = Find(root, "swap_rb");
        if (sw && sw->t == 5) g_swapRB = sw->n != 0.0;
        const JV* ms = Find(root, "models");
        if (ms && ms->t == 4)
        {
            for (size_t i = 0; i < ms->o.size(); ++i)
            {
                const int id = atoi(ms->o[i].first.c_str());
                const JV& m = ms->o[i].second;
                if (m.t != 4) continue;
                ModelCfg mc;
                const JV* nm = Find(m, "name");
                if (nm && nm->t == 2) mc.name = nm->s;
                const JV* fr = Find(m, "front");
                const JV* rr = Find(m, "rear");
                if (fr) { mc.f = PlateCfg(); mc.f.w = 0.52f; ReadSide(fr, mc.f); mc.hasF = true; }
                if (rr) { mc.r = PlateCfg(); mc.r.w = 0.52f; ReadSide(rr, mc.r); mc.hasR = true; }
                g_models[id] = mc;
            }
        }
    }

    static PlateCfg AutoCfg(CVehicleGTA* veh, bool front)
    {
        PlateCfg c;
        memset(&c, 0, sizeof(c));
        c.w = 0.52f;
        float fy = 2.0f, fz = -0.15f, ry = -2.1f, rz = -0.1f;
        auto* mi = CModelInfo::GetVehicleModelInfo(veh->m_nModelIndex);
        if (mi && mi->m_pVehicleStruct)
        {
            const CVector& d0 = mi->m_pVehicleStruct->m_avDummyPos[0];   // on isiqlar
            const CVector& d1 = mi->m_pVehicleStruct->m_avDummyPos[1];   // arxa isiqlar
            if (fabsf(d0.y) > 0.5f) { fy = d0.y + 0.03f; fz = d0.z - 0.30f; }
            if (fabsf(d1.y) > 0.5f) { ry = d1.y - 0.03f; rz = d1.z - 0.22f; }
        }
        if (front) { c.pos[1] = fy; c.pos[2] = fz; c.rot[2] = 180.0f; }
        else       { c.pos[1] = ry; c.pos[2] = rz; c.rot[2] = 0.0f; }
        return c;
    }

    // ------------------------------------------------------------ riyaziyyat
    struct V3 { float x, y, z; };

    static V3 Rot(V3 p, const float rot[3])
    {
        const float d2r = 0.0174532925f;
        const float rx = rot[0] * d2r, ry = rot[1] * d2r, rz = rot[2] * d2r;
        float c = cosf(ry), s = sinf(ry);
        V3 a = { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };            // Y (yan egilme)
        c = cosf(rx); s = sinf(rx);
        V3 b = { a.x, a.y * c - a.z * s, a.y * s + a.z * c };             // X (on-arxa egilme)
        c = cosf(rz); s = sinf(rz);
        V3 d = { b.x * c - b.y * s, b.x * s + b.y * c, b.z };             // Z (donme)
        return d;
    }

    static V3 Dir(const RwMatrix& m, V3 l)
    {
        V3 r = { m.right.x * l.x + m.up.x * l.y + m.at.x * l.z,
                 m.right.y * l.x + m.up.y * l.y + m.at.y * l.z,
                 m.right.z * l.x + m.up.z * l.y + m.at.z * l.z };
        return r;
    }

    static V3 World(const RwMatrix& m, V3 l)
    {
        V3 d = Dir(m, l);
        V3 r = { m.pos.x + d.x, m.pos.y + d.y, m.pos.z + d.z };
        return r;
    }

    // ------------------------------------------------------------ tekstur
    static RwTexture* g_baseTex = nullptr;
    static bool g_baseTried = false;

    static RwTexture* BaseTex()
    {
        if (g_baseTex || g_baseTried) return g_baseTex;
        g_baseTried = true;
        EnsureCfg();

        if (!g_pszStorage)                    { g_err = "storage yolu yoxdur"; return nullptr; }
        if (!RtPNGImageRead || !RwRasterLock) { g_err = "RW funksiyalari yoxdur"; return nullptr; }

        const std::string path = P("plate/plate.png");
        RwImage* img = RtPNGImageRead(path.c_str());
        if (!img) { g_err = "plate.png oxunmadi: " + path; return nullptr; }
        if (img->depth != 32 || !img->cpPixels || img->width < 2 || img->height < 2)
        {
            g_err = "plate.png 32-bit (RGBA) deyil";
            if (RwImageDestroy) RwImageDestroy(img);
            return nullptr;
        }

        const int TW = 1024, TH = 256;
        RwRaster* ras = RwRasterCreate(TW, TH, 32, rwRASTERFORMAT8888 | rwRASTERTYPETEXTURE);
        if (!ras) { g_err = "raster yaradilmadi"; if (RwImageDestroy) RwImageDestroy(img); return nullptr; }
        uint8_t* px = RwRasterLock(ras, 0, rwRASTERLOCKWRITE);
        if (!px)  { g_err = "raster kilidlenmedi"; if (RwImageDestroy) RwImageDestroy(img); return nullptr; }

        const int stride = (ras->stride > 0) ? (int)ras->stride : TW * 4;
        const int iw = img->width, ih = img->height;
        const int istr = (img->stride > 0) ? img->stride : iw * 4;
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
                    const float a = img->cpPixels[y0 * istr + x0 * 4 + k];
                    const float b = img->cpPixels[y0 * istr + (x0 + 1) * 4 + k];
                    const float c = img->cpPixels[(y0 + 1) * istr + x0 * 4 + k];
                    const float d = img->cpPixels[(y0 + 1) * istr + (x0 + 1) * 4 + k];
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
        if (RwImageDestroy) RwImageDestroy(img);

        g_baseTex = RwTextureCreate(ras);
        if (!g_baseTex) g_err = "tekstur yaradilmadi";
        else g_err.clear();
        return g_baseTex;
    }

    // ImGui font atlasinin raster-i (yazi glifleri 3D-de bu teksturdan cizilir)
    static void* AtlasRaster()
    {
        ImFontAtlas* at = ImGui::GetIO().Fonts;
        void* id = (void*)at->TexID;
        if (!id) return nullptr;
        RwRaster* r = (RwRaster*)id;
        if ((int)RwRasterGetWidth(r) == at->TexWidth && (int)RwRasterGetHeight(r) == at->TexHeight) return r;
        RwTexture* t = (RwTexture*)id;
        if (t->raster && (int)RwRasterGetWidth(t->raster) == at->TexWidth && (int)RwRasterGetHeight(t->raster) == at->TexHeight) return t->raster;
        return r;
    }

    struct Vtx { float x, y, z, nx, ny, nz; uint32_t color; float u, v; };

    static void DrawTris(const Vtx* v, int nv, const uint16_t* idx, int ni, void* raster)
    {
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);
        if (RwIm3DTransform((RwIm3DVertex*)v, (RwUInt32)nv, nullptr, 1 /* rwIM3D_VERTEXUV */))
        {
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST, (RwImVertexIndex*)idx, ni);
            RwIm3DEnd();
        }
    }

    static void DrawSide(CVehicleGTA* veh, const RwMatrix& m, bool front, const std::string& text, RwTexture* base, ImFont* font, void* atlas)
    {
        const PlateCfg c = Plate::GetCfg(veh, front);
        const float W = c.w, H = c.w / g_aspect;

        auto toWorld = [&](float x, float y, float z) -> V3 {
            V3 q = { x, y, z };
            V3 r = Rot(q, c.rot);
            V3 l = { r.x + c.pos[0], r.y + c.pos[1], r.z + c.pos[2] };
            return World(m, l);
        };

        // 1) plaka sekli
        {
            Vtx v[4];
            const float cx[4] = { -W * 0.5f,  W * 0.5f, W * 0.5f, -W * 0.5f };
            const float cz[4] = {  H * 0.5f,  H * 0.5f, -H * 0.5f, -H * 0.5f };
            const float uu[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
            const float vv[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            for (int i = 0; i < 4; ++i)
            {
                const V3 w = toWorld(cx[i], 0.0f, cz[i]);
                Vtx t = { w.x, w.y, w.z, 0, 0, 0, 0xFFFFFFFFu, uu[i], vv[i] };
                v[i] = t;
            }
            static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
            DrawTris(v, 4, idx, 6, base->raster);
        }

        // 2) yazi (font atlasindan glif kvadlari)
        if (!atlas || !font || text.empty()) return;
        const ImFontGlyph* g8 = font->FindGlyph((ImWchar)'8');
        if (!g8) return;

        float totalAdv = 0.0f;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const ImFontGlyph* g = font->FindGlyph((ImWchar)(unsigned char)text[i]);
            if (g) totalAdv += g->AdvanceX;
        }
        const float capTop = g8->Y0, capBot = g8->Y1;
        const float capH = capBot - capTop;
        if (totalAdv < 1.0f || capH < 1.0f) return;

        const float boxW = (g_textBox[2] - g_textBox[0]) * W;
        const float boxH = (g_textBox[3] - g_textBox[1]) * H;
        float mpp = (boxW * 0.94f) / totalAdv;               // metr / font pikseli
        const float mh = (boxH * 0.88f) / capH;
        if (mh < mpp) mpp = mh;
        const float ccx = ((g_textBox[0] + g_textBox[2]) * 0.5f - 0.5f) * W;
        const float ccz = (0.5f - (g_textBox[1] + g_textBox[3]) * 0.5f) * H;
        const float capMid = (capTop + capBot) * 0.5f;

        Vtx tv[4 * 16];
        uint16_t ti[6 * 16];
        int nv = 0, ni = 0;
        float pen = -totalAdv * 0.5f;
        for (size_t i = 0; i < text.size() && nv + 4 <= 4 * 16; ++i)
        {
            const ImFontGlyph* g = font->FindGlyph((ImWchar)(unsigned char)text[i]);
            if (!g) continue;
            if (text[i] != ' ')
            {
                const float x0 = ccx + (pen + g->X0) * mpp, x1 = ccx + (pen + g->X1) * mpp;
                const float z0 = ccz - (g->Y0 - capMid) * mpp, z1 = ccz - (g->Y1 - capMid) * mpp;
                const float gx[4] = { x0, x1, x1, x0 };
                const float gz[4] = { z0, z0, z1, z1 };
                const float gu[4] = { g->U0, g->U1, g->U1, g->U0 };
                const float gv[4] = { g->V0, g->V0, g->V1, g->V1 };
                for (int k = 0; k < 4; ++k)
                {
                    const V3 w = toWorld(gx[k], -0.003f, gz[k]);      // plakanin ustunde (z-fight olmasin)
                    Vtx t = { w.x, w.y, w.z, 0, 0, 0, 0xFF0F0F0Fu, gu[k], gv[k] };
                    tv[nv + k] = t;
                }
                ti[ni++] = (uint16_t)nv;       ti[ni++] = (uint16_t)(nv + 1); ti[ni++] = (uint16_t)(nv + 2);
                ti[ni++] = (uint16_t)nv;       ti[ni++] = (uint16_t)(nv + 2); ti[ni++] = (uint16_t)(nv + 3);
                nv += 4;
            }
            pen += g->AdvanceX;
        }
        if (nv > 0) DrawTris(tv, nv, ti, ni, atlas);
    }
}

namespace Plate
{
    bool Normalize(const std::string& in, std::string& out)
    {
        std::string n;
        if (!AzPlate::Normalize(in, n)) return false;     // "10-AA-123"
        out = n.substr(0, 2) + " " + n.substr(3, 2) + " " + n.substr(6, 3);
        return true;
    }

    bool Set(CVehicleGTA* veh, const std::string& text)
    {
        if (!veh) return false;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        PlateImpl::Entry& e = PlateImpl::g_ent[veh];
        if (e.ct != (uint32_t)veh->m_nCreationTime) { e = PlateImpl::Entry(); e.ct = (uint32_t)veh->m_nCreationTime; }
        e.text = text;
        return true;
    }

    void Remove(CVehicleGTA* veh)
    {
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        PlateImpl::g_ent.erase(veh);
    }

    bool Get(CVehicleGTA* veh, std::string& text)
    {
        if (!veh) return false;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
        if (it == PlateImpl::g_ent.end() || it->second.ct != (uint32_t)veh->m_nCreationTime) return false;
        text = it->second.text;
        return true;
    }

    void SetSides(CVehicleGTA* veh, bool front, bool rear)
    {
        if (!veh) return;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
        if (it == PlateImpl::g_ent.end() || it->second.ct != (uint32_t)veh->m_nCreationTime) return;
        it->second.front = front;
        it->second.rear = rear;
    }

    void GetSides(CVehicleGTA* veh, bool& front, bool& rear)
    {
        front = rear = true;
        if (!veh) return;
        std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
        std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
        if (it == PlateImpl::g_ent.end() || it->second.ct != (uint32_t)veh->m_nCreationTime) return;
        front = it->second.front;
        rear = it->second.rear;
    }

    PlateCfg GetCfg(CVehicleGTA* veh, bool front)
    {
        PlateImpl::EnsureCfg();
        std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.find((int)veh->m_nModelIndex);
        if (it != PlateImpl::g_models.end())
        {
            if (front && it->second.hasF) return it->second.f;
            if (!front && it->second.hasR) return it->second.r;
        }
        return PlateImpl::AutoCfg(veh, front);
    }

    void SetCfg(CVehicleGTA* veh, bool front, const PlateCfg& c)
    {
        PlateImpl::EnsureCfg();
        PlateImpl::ModelCfg& m = PlateImpl::g_models[(int)veh->m_nModelIndex];
        if (front) { m.f = c; m.hasF = true; }
        else       { m.r = c; m.hasR = true; }
    }

    void ResetCfg(CVehicleGTA* veh, bool front)
    {
        PlateImpl::EnsureCfg();
        std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.find((int)veh->m_nModelIndex);
        if (it == PlateImpl::g_models.end()) return;
        if (front) it->second.hasF = false; else it->second.hasR = false;
    }

    bool HasCustom(CVehicleGTA* veh, bool front)
    {
        PlateImpl::EnsureCfg();
        std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.find((int)veh->m_nModelIndex);
        if (it == PlateImpl::g_models.end()) return false;
        return front ? it->second.hasF : it->second.hasR;
    }

    bool SaveCfg()
    {
        PlateImpl::EnsureCfg();
        FILE* f = fopen(PlateImpl::P("plates.json").c_str(), "wb");
        if (!f) { PlateImpl::g_err = "plates.json yazilmadi"; return false; }
        fprintf(f, "{\n  \"_readme\": \"pos = masin merkezine nezeren metr (x sag, y irelli, z yuxari). rot = derece (x, y, z). rot=[0,0,0] plaka arxaya baxir; qabaq ucun z=180. w = plaka eni (metr).\",\n");
        fprintf(f, "  \"plate_image\": \"plate.png\",\n  \"swap_rb\": %s,\n", PlateImpl::g_swapRB ? "true" : "false");
        fprintf(f, "  \"text_box\": [%.3f, %.3f, %.3f, %.3f],\n  \"models\": {\n",
                PlateImpl::g_textBox[0], PlateImpl::g_textBox[1], PlateImpl::g_textBox[2], PlateImpl::g_textBox[3]);
        bool first = true;
        for (std::map<int, PlateImpl::ModelCfg>::iterator it = PlateImpl::g_models.begin(); it != PlateImpl::g_models.end(); ++it)
        {
            const PlateImpl::ModelCfg& m = it->second;
            if (!m.hasF && !m.hasR) continue;
            fprintf(f, "%s    \"%d\": {", first ? "" : ",\n", it->first);
            first = false;
            bool sep = false;
            if (!m.name.empty()) { fprintf(f, " \"name\": \"%s\"", m.name.c_str()); sep = true; }
            const PlateCfg* cs[2] = { &m.f, &m.r };
            const bool has[2] = { m.hasF, m.hasR };
            const char* nm[2] = { "front", "rear" };
            for (int k = 0; k < 2; ++k)
            {
                if (!has[k]) continue;
                fprintf(f, "%s\n      \"%s\": { \"pos\": [%.4f, %.4f, %.4f], \"rot\": [%.2f, %.2f, %.2f], \"w\": %.4f }",
                        sep ? "," : "", nm[k], cs[k]->pos[0], cs[k]->pos[1], cs[k]->pos[2],
                        cs[k]->rot[0], cs[k]->rot[1], cs[k]->rot[2], cs[k]->w);
                sep = true;
            }
            fprintf(f, "\n    }");
        }
        fprintf(f, "\n  }\n}\n");
        fclose(f);
        return true;
    }

    bool WorldPose(CVehicleGTA* veh, bool front, float pos[3], float n[3], float up[3])
    {
        if (!veh) return false;
        const PlateCfg c = GetCfg(veh, front);
        RwMatrix m;
        veh->GetMatrix(&m);
        PlateImpl::V3 lp = { c.pos[0], c.pos[1], c.pos[2] };
        const PlateImpl::V3 wp = PlateImpl::World(m, lp);
        const PlateImpl::V3 nl = { 0.0f, -1.0f, 0.0f }, ul = { 0.0f, 0.0f, 1.0f };
        const PlateImpl::V3 nw = PlateImpl::Dir(m, PlateImpl::Rot(nl, c.rot));
        const PlateImpl::V3 uw = PlateImpl::Dir(m, PlateImpl::Rot(ul, c.rot));
        pos[0] = wp.x; pos[1] = wp.y; pos[2] = wp.z;
        n[0] = nw.x;   n[1] = nw.y;   n[2] = nw.z;
        up[0] = uw.x;  up[1] = uw.y;  up[2] = uw.z;
        return true;
    }

    const char* LastError() { return PlateImpl::g_err.c_str(); }

    void* BaseRaster()
    {
        RwTexture* t = PlateImpl::BaseTex();
        return t ? (void*)t->raster : nullptr;
    }

    void Render(CVehicleGTA* veh)
    {
        if (!veh) return;
        PlateImpl::Entry e;
        {
            std::lock_guard<std::mutex> lk(PlateImpl::g_mtx);
            if (PlateImpl::g_ent.empty()) return;
            std::map<CVehicleGTA*, PlateImpl::Entry>::iterator it = PlateImpl::g_ent.find(veh);
            if (it == PlateImpl::g_ent.end()) return;
            if (it->second.ct != (uint32_t)veh->m_nCreationTime) { PlateImpl::g_ent.erase(it); return; }
            e = it->second;
        }
        if (!e.front && !e.rear) return;

        RwTexture* base = PlateImpl::BaseTex();
        if (!base || !base->raster) return;

        ImFont* font = g_pPlateFont ? g_pPlateFont : ImGui::GetFont();
        void* atlas = PlateImpl::AtlasRaster();

        RwMatrix m;
        veh->GetMatrix(&m);

        RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERLINEAR));
        RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
        RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(TRUE));
        RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
        RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
        RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDINVSRCALPHA));
        RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));

        if (e.front) PlateImpl::DrawSide(veh, m, true,  e.text, base, font, atlas);
        if (e.rear)  PlateImpl::DrawSide(veh, m, false, e.text, base, font, atlas);

        RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLBACK));
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(NULL));
    }
}
'''

def p_cpp(s):
    if "namespace AzPlate" not in s:
        raise ValueError("namespace AzPlate")
    return s.rstrip() + "\n\n#include <cstdlib>\n" + PLATE_CPP + "\n"

# =============================================================== 3. render hook
def p_hook(s):
    old = "void RenderDriverAndPassengers_hook(CVehicleGTA *thiz)\n{\n    thiz->RenderDriverAndPassengers();\n}"
    new = "void RenderDriverAndPassengers_hook(CVehicleGTA *thiz)\n{\n    thiz->RenderDriverAndPassengers();\n    Plate::Render(thiz);   // PLATE_V7: PNG nomre lovhesi\n}"
    return once(s, old, new, "RenderDriverAndPassengers_hook")

# =============================================================== 4. plate.ttf -> ImGui atlasi
PLATE_FONT = r'''	// PLATEFONT_V7: DIN 1451 (assets/Fonts/plate.ttf -> <storage>/SAMP/plate/plate.ttf) nomre lovhesi ucun
	{
		extern ImFont* g_pPlateFont;
		extern char* g_pszStorage;
		g_pPlateFont = nullptr;
		if (g_pszStorage)
		{
			const std::string plateP = std::string(g_pszStorage) + "/SAMP/plate/plate.ttf";
			if (access(plateP.c_str(), R_OK) == 0)
			{
				static const ImWchar plateRanges[] = { 0x0020, 0x0020, 0x0030, 0x0039, 0x0041, 0x005A, 0 };
				ImFontConfig pc;
				pc.PixelSnapH = true;
				g_pPlateFont = io.Fonts->AddFontFromFileTTF(plateP.c_str(), 128.0f, &pc, plateRanges);
				if (g_pPlateFont) Log::addParameter("Plate font: %s", plateP.c_str());
			}
		}
	}

'''

def p_wrapper(s):
    i = s.find("\t// EMOJI_V5: vektor emoji glifleri\n\t{")
    if i >= 0:
        return s[:i] + PLATE_FONT + s[i:]
    pat = re.compile(r'\n([ \t]*)createFontTexture\(\);\s*\n\s*m_renderer = new ImGuiRenderer\(ImGui::GetBackgroundDrawList\(\), font\);')
    ms = list(pat.finditer(s))
    if len(ms) != 1:
        raise ValueError("createFontTexture + m_renderer (%d)" % len(ms))
    m = ms[0]
    return s[:m.start()] + "\n" + PLATE_FONT + s[m.start() + 1:]

# =============================================================== 5. Java: assetleri storage-a kopyala
JAVA_COPY = r'''    // PLATE_V7: assets/Fonts/plate.ttf ve assets/plate/plate.png -> <storage>/SAMP/plate/
    private void copyPlateAssets() {
        try {
            File base = getExternalFilesDir(null);
            if (base == null) base = getFilesDir();
            File dir = new File(base, "SAMP/plate");
            if (!dir.exists()) dir.mkdirs();
            copyAssetTo("Fonts/plate.ttf", new File(dir, "plate.ttf"));
            copyAssetTo("plate/plate.png", new File(dir, "plate.png"));
        } catch (Throwable t) {
            Log.e(TAG, "copyPlateAssets: " + t);
        }
    }

    private void copyAssetTo(String name, File out) {
        try {
            java.io.InputStream in = getAssets().open(name);
            java.io.FileOutputStream os = new java.io.FileOutputStream(out);
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            os.close();
            in.close();
        } catch (Throwable t) {
            Log.w(TAG, "asset tapilmadi: " + name);
        }
    }

'''

def p_java(s):
    s = once(s, "    private native void initializeSAMP();", JAVA_COPY + "    private native void initializeSAMP();", "initializeSAMP")
    s = once(s, "        try {\n            initializeSAMP();", "        copyPlateAssets();   // PLATE_V7\n        try {\n            initializeSAMP();", "onCreate initializeSAMP")
    return s

# =============================================================== 6. gui.cpp: nomre sehifesi + yerlesdirme rejimi
PLATE_UI = r'''// ===== PLATEADJ_V7: nomre sehifesi + yerlesdirme rejimi =====
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
                s_phi   = Clamp(s_phi, -1.2f, 1.2f);
                s_theta = Clamp(s_theta, -0.3f, 1.0f);
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

'''

PLATE_TAB = r'''    else if (s_tab == 4)                                    // NOMRE (PLATEADJ_V7)
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
'''

def p_gui(s):
    s = once(s, "\nvoid RenderHandlingDialog()\n{\n    using namespace mm;\n",
             "\n" + PLATE_UI + "void RenderHandlingDialog()\n{\n    using namespace mm;\n", "RenderHandlingDialog basi")
    s = once(s, "if (!want && Garage::g_on) Garage::End();   // GARAGE_V5",
             "if (!want && Garage::g_on) Garage::End();   // GARAGE_V5\n    if (!want && PlateAdj::g_on) PlateAdj::End();   // PLATEADJ_V7", "Garage::End")
    s = sub1(s, r'(Garage::Render\(pVeh, dt\);\n\s*return;\n\s*\}\n)',
             lambda m: m.group(1) + "    if (want && PlateAdj::g_on) {   // PLATEADJ_V7\n        s_hndRectValid = false;\n        PlateAdj::Render(pVeh, dt);\n        return;\n    }\n",
             "Garage::Render hook")
    s = once(s, 'static const char* kTabs[5] = { "Maşın", "Handling", "Far", "Tuning", "Spidometr" };',
             'static const char* kTabs[6] = { "Maşın", "Handling", "Far", "Tuning", "Nömrə", "Spidometr" };', "kTabs")
    s = once(s, "const float tw = seg.w / 5.0f;", "const float tw = seg.w / 6.0f;", "tw")
    s = sub1(s, r'for \(int i = 0; i < 5; \+\+i\) \{\n(\s*)const Rc ti\{ seg\.x \+ i \* tw',
             lambda m: "for (int i = 0; i < 6; ++i) {\n" + m.group(1) + "const Rc ti{ seg.x + i * tw", "tab dovru")
    m = re.search(r'\n    else[ \t]+// SPIDOMETR\n', s)
    if not m:
        raise ValueError("SPIDOMETR sehifesi")
    return s[:m.start() + 1] + PLATE_TAB + s[m.start() + 1:]

# =============================================================== isle
if __name__ == "__main__":
    step("NOMRE: AzPlate.h",              os.path.join(CPP, "game/AzPlate.h"),   "PLATE_V7", p_h)
    step("NOMRE: AzPlate.cpp (modul)",    os.path.join(CPP, "game/AzPlate.cpp"), "PLATE_V7", p_cpp)
    step("NOMRE: render hook",            os.path.join(CPP, "game/Entity/CVehicleGTA.cpp"), "PLATE_V7", p_hook)
    step("NOMRE: plate.ttf (imguiwrapper)", os.path.join(CPP, "gui/imguiwrapper.cpp"), "PLATEFONT_V7", p_wrapper)
    step("NOMRE: Java (assetleri kopyala)", JAVA, "PLATE_V7", p_java)
    step("NOMRE: /mm sehifesi + tenzimle (gui.cpp)", os.path.join(CPP, "gui/gui.cpp"), "PLATEADJ_V7", p_gui)
    print("\nBitdi. Geri qaytarmaq: her faylin yaninda *.bakv7")
    print("Fayllar: app/src/main/assets/Fonts/plate.ttf  ve  app/src/main/assets/plate/plate.png")

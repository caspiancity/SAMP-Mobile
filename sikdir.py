# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v10.py -- layihenin kokunde isle:  python3 apply_v10.py     (evvel apply_v8 isleyib; v9 mecburi deyil)
#   plate.png ISTENILEN formatda ola biler (JPEG / WebP / GIF / BMP / PNG): native terefde oxunur.
#   PNG -> oz dekoderimiz.  Diger -> Android sistem dekoderi (AImageDecoder, dlopen, Android 11+).
# Geri qaytarmaq: AzPlate.cpp.bakv10
import os, shutil

ROOT = os.getcwd()
CPP  = os.path.join(ROOT, "app/src/main/cpp/samp")

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
    if not os.path.exists(path + ".bakv10"):
        shutil.copy(path, path + ".bakv10")
    wr(path, n, crlf)
    print("[OK]    %s" % name)

def once(s, old, new, what):
    if s.count(old) != 1:
        raise ValueError("%s (%d tapildi)" % (what, s.count(old)))
    return s.replace(old, new, 1)

DECODE_ANY = r'''    // ------------------------------------------------------------ PLATE_V10: istenilen format (sistem dekoderi)
    typedef int      (*fnCreateBuf)(const void*, size_t, void**);
    typedef const void* (*fnHdr)(void*);
    typedef int32_t  (*fnWH)(const void*);
    typedef int      (*fnSetFmt)(void*, int32_t);
    typedef size_t   (*fnStride)(void*);
    typedef int      (*fnDecode)(void*, void*, size_t, size_t);
    typedef void     (*fnDel)(void*);

    static bool DecodeSystem(const std::vector<uint8_t>& d, int& W, int& H, std::vector<uint8_t>& rgba, std::string& err)
    {
        void* lib = dlopen("libjnigraphics.so", RTLD_NOW);
        if (!lib) { err = "libjnigraphics.so yoxdur"; return false; }
        fnCreateBuf cr  = (fnCreateBuf)dlsym(lib, "AImageDecoder_createFromBuffer");
        fnHdr       hdr = (fnHdr)      dlsym(lib, "AImageDecoder_getHeaderInfo");
        fnWH        gw  = (fnWH)       dlsym(lib, "AImageDecoderHeaderInfo_getWidth");
        fnWH        gh  = (fnWH)       dlsym(lib, "AImageDecoderHeaderInfo_getHeight");
        fnSetFmt    sf  = (fnSetFmt)   dlsym(lib, "AImageDecoder_setAndroidBitmapFormat");
        fnStride    gs  = (fnStride)   dlsym(lib, "AImageDecoder_getMinimumStride");
        fnDecode    dc  = (fnDecode)   dlsym(lib, "AImageDecoder_decodeImage");
        fnDel       del = (fnDel)      dlsym(lib, "AImageDecoder_delete");
        if (!cr || !hdr || !gw || !gh || !sf || !gs || !dc || !del)
        {
            err = "sistem sekil dekoderi yoxdur (Android 11+ lazimdir)";
            return false;
        }
        void* dec = nullptr;
        const int rc = cr(&d[0], d.size(), &dec);
        if (rc != 0 || !dec)
        {
            char b[96];
            snprintf(b, sizeof(b), "sistem dekoderi formati tanimadi (kod %d)", rc);
            err = b;
            return false;
        }
        const void* hi = hdr(dec);
        W = gw(hi);
        H = gh(hi);
        if (W < 2 || H < 2 || W > 8192 || H > 8192) { del(dec); err = "sekil olcusu yararsizdir"; return false; }
        if (sf(dec, 1 /* ANDROID_BITMAP_FORMAT_RGBA_8888 */) != 0) { del(dec); err = "RGBA formati secilmedi"; return false; }
        const size_t stride = gs(dec);
        std::vector<uint8_t> tmp(stride * (size_t)H);
        const int rc2 = dc(dec, &tmp[0], stride, tmp.size());
        del(dec);
        if (rc2 != 0)
        {
            char b[96];
            snprintf(b, sizeof(b), "sistem dekoderi oxuya bilmedi (kod %d)", rc2);
            err = b;
            return false;
        }
        rgba.assign((size_t)W * H * 4, 255);
        for (int y = 0; y < H; ++y) memcpy(&rgba[(size_t)y * W * 4], &tmp[(size_t)y * stride], (size_t)W * 4);
        return true;
    }

    static bool DecodeAny(const std::vector<uint8_t>& d, int& W, int& H, std::vector<uint8_t>& rgba, std::string& err)
    {
        static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
        if (d.empty()) { err = "fayl bosdur"; return false; }
        std::string e1, e2;
        if (d.size() >= 8 && !memcmp(&d[0], sig, 8) && DecodePng(d, W, H, rgba, e1)) return true;
        if (DecodeSystem(d, W, H, rgba, e2)) return true;
        err = e1.empty() ? e2 : (e1 + " / " + e2);
        return false;
    }

'''

def p_cpp10(s):
    s = once(s, "#include <android/log.h>   // PLATE_V8", "#include <android/log.h>   // PLATE_V8\n#include <dlfcn.h>", "include")
    s = once(s, "    static RwTexture* BaseTex()\n", DECODE_ANY + "    static RwTexture* BaseTex()\n", "BaseTex() basi")
    s = once(s, "if (!DecodePng(file, iw, ih, src, perr))", "if (!DecodeAny(file, iw, ih, src, perr))   // PLATE_V10", "DecodePng cagirisi")
    s = s.replace('g_err = "plate.png oxunmadi: " + perr;', 'g_err = "plate.png oxunmadi: " + perr;', 1)
    return s

if __name__ == "__main__":
    step("v10: istenilen sekil formati (AzPlate.cpp)", os.path.join(CPP, "game/AzPlate.cpp"), "PLATE_V10", p_cpp10)
    print("\nBitdi. Geri qaytarmaq: AzPlate.cpp.bakv10")
    print("Yoxlama: adb logcat -s SAMPPlate")

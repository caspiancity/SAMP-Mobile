# -*- coding: utf-8 -*-
#!/usr/bin/env python3
# apply_v9.py -- layihenin kokunde isle:  python3 apply_v9.py     (evvel apply_v8 isleyib)
#   1) Java: plate.png real PNG deyilse (JPEG/WebP/BMP/GIF adini .png qoyulub) BitmapFactory ile oxuyub real PNG kimi yeniden yazir
#   2) Native: xeta mesajinda fayl olcusu, ilk baytlar ve format tapilir
# Geri qaytarmaq: her faylin yaninda *.bakv9
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
    if not os.path.exists(path + ".bakv9"):
        shutil.copy(path, path + ".bakv9")
    wr(path, n, crlf)
    print("[OK]    %s" % name)

def once(s, old, new, what):
    if s.count(old) != 1:
        raise ValueError("%s (%d tapildi)" % (what, s.count(old)))
    return s.replace(old, new, 1)

# =============================================================== Native diaqnostika
DIAG = r'''if (d.size() < 33 || memcmp(&d[0], sig, 8) != 0)
        {
            // PLATE_V9: diaqnostika
            const unsigned n = (unsigned)d.size();
            const unsigned b0 = n > 0 ? d[0] : 0, b1 = n > 1 ? d[1] : 0, b2 = n > 2 ? d[2] : 0, b3 = n > 3 ? d[3] : 0;
            const char* fmt = "namelum format";
            if (n == 0) fmt = "fayl bosdur";
            else if (b0 == 0xFF && b1 == 0xD8) fmt = "bu JPEG-dir";
            else if (b0 == 'R' && b1 == 'I' && b2 == 'F') fmt = "bu WebP-dir";
            else if (b0 == 'G' && b1 == 'I' && b2 == 'F') fmt = "bu GIF-dir";
            else if (b0 == 'B' && b1 == 'M') fmt = "bu BMP-dir";
            else if (b0 == '<' || b0 == 'v' || b0 == 'V') fmt = "bu metn/HTML/LFS fayldir";
            else if (n < 33) fmt = "fayl cox kicikdir";
            char hb[160];
            snprintf(hb, sizeof(hb), "PNG deyil (%s): %u bayt, ilk baytlar %02X %02X %02X %02X", fmt, n, b0, b1, b2, b3);
            err = hb;
            return false;
        }'''

def p_cpp9(s):
    old = 'if (d.size() < 33 || memcmp(&d[0], sig, 8) != 0) { err = "PNG deyil (basliq sehvdir)"; return false; }'
    return once(s, old, DIAG, "PNG basliq yoxlamasi")

# =============================================================== Java: real PNG-e cevir
JAVA_FIX = r'''    // PLATE_V9: plate.png real PNG deyilse (JPEG/WebP/BMP/GIF) oxuyub real PNG kimi yeniden yazir
    private void fixPlatePng(java.io.File f) {
        try {
            if (f == null || !f.exists()) return;
            byte[] h = new byte[8];
            java.io.FileInputStream in = new java.io.FileInputStream(f);
            int n = in.read(h);
            in.close();
            final int[] sig = { 137, 80, 78, 71, 13, 10, 26, 10 };
            boolean ok = (n == 8);
            for (int i = 0; ok && i < 8; i++) if ((h[i] & 0xFF) != sig[i]) ok = false;
            if (ok) return;
            android.graphics.Bitmap bmp = android.graphics.BitmapFactory.decodeFile(f.getPath());
            if (bmp == null) {
                android.util.Log.w("SAMPPlate", "plate.png sekil kimi oxunmadi: " + f.getPath() + " (" + f.length() + " bayt)");
                return;
            }
            java.io.File tmp = new java.io.File(f.getPath() + ".fix");
            java.io.FileOutputStream os = new java.io.FileOutputStream(tmp);
            bmp.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, os);
            os.close();
            if (f.delete() | true) tmp.renameTo(f);
            android.util.Log.i("SAMPPlate", "plate.png real PNG-e cevrildi: " + f.getPath() + " " + bmp.getWidth() + "x" + bmp.getHeight());
        } catch (Throwable t) {
            android.util.Log.w("SAMPPlate", "fixPlatePng: " + t);
        }
    }

'''

def p_java9(s):
    s = once(s, "    private boolean copyFirstAsset(", JAVA_FIX + "    private boolean copyFirstAsset(", "copyFirstAsset")
    s = once(s,
             '            boolean a = copyFirstAsset(pngNames, new java.io.File(d, "plate.png"));\n',
             '            boolean a = copyFirstAsset(pngNames, new java.io.File(d, "plate.png"));\n            fixPlatePng(new java.io.File(d, "plate.png"));   // PLATE_V9\n',
             "copyPlateAssets png satiri")
    return s

if __name__ == "__main__":
    step("v9: native diaqnostika (AzPlate.cpp)", os.path.join(CPP, "game/AzPlate.cpp"), "PLATE_V9", p_cpp9)
    step("v9: Java PNG duzeldici",              JAVA, "PLATE_V9", p_java9)
    print("\nBitdi. Geri qaytarmaq: *.bakv9")
    print("Yoxlama: adb logcat -s SAMPPlate")

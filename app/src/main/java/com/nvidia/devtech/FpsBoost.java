package com.nvidia.devtech;

import android.app.Activity;
import android.os.Build;
import android.view.Display;
import android.view.Window;
import android.view.WindowManager;

/** Ekranin yuksek yenileme tezliyini isteyir (90 Hz hedef). 60 Hz ekranda hec ne deyismir. */
public final class FpsBoost {
    private static final float TARGET_HZ = 90.0f;

    private FpsBoost() {}

    /** C++ terefe secilen yenilenme tezliyini verir (avtomatik uzaqliq tenzimi hedef FPS kimi istifade edir). */
    private static native void nativeSetRefresh(float hz);

    public static void apply(Activity act) {
        try {
            if (Build.VERSION.SDK_INT < 23 || act == null) return;
            Window w = act.getWindow();
            Display d = w.getWindowManager().getDefaultDisplay();
            Display.Mode cur = d.getMode();
            Display.Mode best = cur;
            for (Display.Mode m : d.getSupportedModes()) {
                if (m.getPhysicalWidth() != cur.getPhysicalWidth()
                        || m.getPhysicalHeight() != cur.getPhysicalHeight()) continue;
                float dm = Math.abs(m.getRefreshRate() - TARGET_HZ);
                float db = Math.abs(best.getRefreshRate() - TARGET_HZ);
                if (dm < db || (dm == db && m.getRefreshRate() > best.getRefreshRate())) best = m;
            }
            WindowManager.LayoutParams lp = w.getAttributes();
            lp.preferredDisplayModeId = best.getModeId();
            w.setAttributes(lp);
            try {
                nativeSetRefresh(best.getRefreshRate());
            } catch (Throwable ignored) {
                // native kitabxana hele yuklenmeyib: hedef FPS settings.ini-den (PerfTargetFPS) ve ya 60 goturulur
            }
        } catch (Throwable ignored) {
        }
    }
}

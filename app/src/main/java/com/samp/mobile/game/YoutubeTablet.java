package com.samp.mobile.game;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.TimeInterpolator;
import android.animation.ValueAnimator;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Outline;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.view.Window;
import android.view.WindowManager;
import android.view.animation.AccelerateInterpolator;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.OvershootInterpolator;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.webkit.CookieManager;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * /yt - oyunun ustunde iOS usulunda "planset" YouTube penceresi.
 *
 * DUZELISLER (kohne versiya ile muqayise):
 *  1) MINI REJIMDE MAHNI DAYANMIR:
 *     - WebView-nin layout olcusu mini-ye kecende HECH deyismir (kohne kod 480x270-e deyisirdi,
 *       bu resize YouTube-u dayandirirdi). Yalniz scale + klip istifade olunur.
 *     - Gizlilik API-lari (visibility/blur/pagehide/hasFocus) tam saxtalashdirilir.
 *     - Mini rejimde "istenilen veziyyet" (wantPlay) izlenir: audio focus ve ya basqa sebebe gore
 *       pauza olsa, avtomatik davam etdirilir. Yalniz istifadeci ozu pauza etse dayanir.
 *     - WebView.onResume()/resumeTimers() cagrilir.
 *  2) MENTIQ SEHVLERI:
 *     - Animasiya yarimciq dayananda (toxunus) pencere sonuna "tullanirdi" -> indi cari veziyyet qalir.
 *     - Kicik ekranda min olcu ekrandan boyuk ola bilirdi -> clamp duzeldildi.
 *     - Baglama animasiyasi zamani yeni /yt komandasi itirdi -> indi novbeye alinir.
 *     - about:blank tarixce girisi qalirdi (Geri duymesi bosh sehifeye aparirdi) -> tarixce temizlenir.
 *     - Ekran olcusu deyisende pencere ekrandan cixa bilirdi -> avtomatik refit.
 *     - Maximize rejiminde surukleme/movqe qarisirdi -> maximize-da surukleme bagli.
 *     - Mini-den qayidanda WebView resize edilirdi -> indi resize animasiyadan EVVEL bir defe olur.
 *     - Geri/Ireli duymeleri SPA navigasiyada yenilenmirdi -> doUpdateVisitedHistory.
 *     - Yukleme xetti 100%-de ilisib qalirdi -> hamar animasiya + solma.
 *  3) iOS ANIMASIYALARI: real "spring" (sonuk salinim) interpolyator, acilis/baglanma, maximize,
 *     mini kecidi (layout-driven crossfade), surukleme + atma (fling) ile kenara yapisma,
 *     duymelerde press-scale, suruklemede "lift" (kolge + seffaflig).
 *  4) YUKSEK FPS: pencere acilanda ekranin en yuksek yenilenme tezliyi (90/120Hz) secilir,
 *     baglananda evvelki rejim berpa olunur. FLIP texnikasi ile WebView her kadrda yeniden olculmur.
 */
public final class YoutubeTablet {

    private static final String HOME = "https://m.youtube.com/";

    private static final int C_BEZEL = 0xFF0B0D12;
    private static final int C_INNER = 0xFF14161C;
    private static final int C_BAR   = 0xFF1A1D25;
    private static final int C_FIELD = 0xFF262A35;
    private static final int C_TXT   = 0xFFE8EAF0;
    private static final int C_HINT  = 0xFF8B92A3;
    private static final int C_ACC   = 0xFFE53935;
    private static final int C_ICON  = 0xFFD5D9E3;

    private static final int MINI_W = 316, MINI_H = 85;   // dp (kart)
    private static final int PREV_W = 112, PREV_H = 63;   // dp (mini-de video penceresi)
    private static final int CHROME_H = 72;               // dp (bashliq 26 + panel 44 + xett 2)
    private static final int PAD = 5;                     // dp (kart padding)

    private static final int MATCH = ViewGroup.LayoutParams.MATCH_PARENT;

    /** Sehife "gizli/fokussuz" olanda YouTube oynatmani dayandirmasin */
    private static final String KEEPALIVE =
            "(function(){try{if(window.__ytk)return;window.__ytk=1;"
          + "function d(o,k,v){try{Object.defineProperty(o,k,{get:v,configurable:true});}catch(e){}}"
          + "d(document,'hidden',function(){return false;});"
          + "d(document,'webkitHidden',function(){return false;});"
          + "d(document,'visibilityState',function(){return 'visible';});"
          + "d(document,'webkitVisibilityState',function(){return 'visible';});"
          + "try{document.hasFocus=function(){return true;};}catch(e){}"
          + "var s=function(e){if(e.target===window||e.target===document)e.stopImmediatePropagation();};"
          + "['visibilitychange','webkitvisibilitychange','pagehide','freeze','blur'].forEach(function(n){"
          + "window.addEventListener(n,s,true);document.addEventListener(n,s,true);});"
          + "}catch(e){}})();";

    private static final String STATE_JS =
            "(function(){var v=document.querySelector('video');return v?(v.ended?'e':(v.paused?'0':'1')):'x';})()";
    private static final String PLAY_JS =
            "(function(){var v=document.querySelector('video');if(v){var p=v.play();if(p&&p.catch)p.catch(function(){});}})()";
    private static final String PAUSE_JS =
            "(function(){var v=document.querySelector('video');if(v)v.pause();})()";
    private static final String PAUSE_ALL_JS =
            "(function(){var a=document.querySelectorAll('video,audio');for(var i=0;i<a.length;i++){try{a[i].pause();}catch(e){}}})()";

    // ------------------------------------------------------------------ statik giris
    private static YoutubeTablet sInst;

    /** /yt [axtaris]  ->  bos: ac/bagla; metn: ac ve axtar */
    public static void command(Activity act, String arg) {
        if (act == null || act.isFinishing()) return;
        if (sInst == null || sInst.act != act) {
            if (sInst != null) sInst.destroy();
            sInst = new YoutubeTablet(act);
        }
        sInst.onCommand(arg == null ? "" : arg.trim());
    }

    public static void hide() {
        if (sInst != null) sInst.close();
    }

    public static void release() {
        if (sInst != null) { sInst.destroy(); sInst = null; }
    }

    // ------------------------------------------------------------------ vezyyet
    private final Activity act;
    private final float dp;
    private final SharedPreferences prefs;
    private final Handler ui = new Handler(Looper.getMainLooper());

    private FrameLayout card, inner, webHolder, fsHolder;
    private LinearLayout chrome, miniGroup, topBar;
    private View handle, previewOverlay;
    private Line line;
    private EditText search;
    private WebView web;
    private Ic icBack, icFwd, icMax, icPlay, icGrip;
    private TextView miniTitle;

    private boolean built, attached, closing, mini, maximized, playing, wantPlay, busy, histReset, fpsBoost;
    private int oldSoftMode = -1, savedMode;
    private String pending;
    private String lastTitle = "";
    private long toggleAt;

    private int nx, ny, nw, nh;          // normal pencere (px)
    private int mx = -1, my = -1;        // mini movqe (px)
    private int webW0, webH0;            // mini rejimde WebView-nin SABIT layout olcusu (px)
    private float holderRadius;          // webHolder kunc radiusu (px)

    private View customView;
    private WebChromeClient.CustomViewCallback customCb;
    private ValueAnimator rectAnim, modeAnim, openAnim;

    private YoutubeTablet(Activity act) {
        this.act = act;
        this.dp = act.getResources().getDisplayMetrics().density;
        this.prefs = act.getSharedPreferences("yt_tablet", Context.MODE_PRIVATE);
        nx = prefs.getInt("nx", 0);
        ny = prefs.getInt("ny", 0);
        nw = prefs.getInt("nw", 0);
        nh = prefs.getInt("nh", 0);
        mx = prefs.getInt("mx", -1);
        my = prefs.getInt("my", -1);
    }

    private int dpx(float v) { return Math.round(v * dp); }
    private static int clamp(int v, int a, int b) { return v < a ? a : (v > b ? Math.max(a, b) : v); }
    private static float clamp01(float v) { return v < 0f ? 0f : (v > 1f ? 1f : v); }

    private View root() { return act.findViewById(android.R.id.content); }

    private int screenW() {
        View r = root();
        if (r != null && r.getWidth() > 0) return r.getWidth();
        return act.getResources().getDisplayMetrics().widthPixels;
    }

    private int screenH() {
        View r = root();
        if (r != null && r.getHeight() > 0) return r.getHeight();
        return act.getResources().getDisplayMetrics().heightPixels;
    }

    private int curW() { return card.getLayoutParams() != null ? card.getLayoutParams().width : nw; }
    private int curH() { return card.getLayoutParams() != null ? card.getLayoutParams().height : nh; }

    // ------------------------------------------------------------------ iOS spring
    /** Sonuk salinimli (underdamped) yay. zeta: 1=salinimsiz, 0.7=nezerceare sekme */
    private static final class Spring implements TimeInterpolator {
        private final double z, w, wd, T;
        Spring(float zeta, float responseSec, float durationSec) {
            z = zeta;
            w = 2.0 * Math.PI / responseSec;
            wd = w * Math.sqrt(1.0 - z * z);
            T = durationSec;
        }
        @Override public float getInterpolation(float x) {
            if (x <= 0f) return 0f;
            if (x >= 1f) return 1f;
            double t = x * T;
            double e = Math.exp(-z * w * t);
            return (float) (1.0 - e * (Math.cos(wd * t) + (z * w / wd) * Math.sin(wd * t)));
        }
    }

    // ------------------------------------------------------------------ yuksek FPS
    @SuppressWarnings("deprecation")
    private void boostFps(boolean on) {
        if (Build.VERSION.SDK_INT < 23) return;
        try {
            Window w = act.getWindow();
            WindowManager.LayoutParams a = w.getAttributes();
            if (on) {
                if (fpsBoost) return;
                Display d = act.getWindowManager().getDefaultDisplay();
                Display.Mode cur = d.getMode(), best = cur;
                for (Display.Mode m : d.getSupportedModes()) {
                    if (m.getPhysicalWidth() == cur.getPhysicalWidth()
                            && m.getPhysicalHeight() == cur.getPhysicalHeight()
                            && m.getRefreshRate() > best.getRefreshRate() + 1f) best = m;
                }
                if (best.getModeId() == cur.getModeId()) return;
                savedMode = a.preferredDisplayModeId;
                a.preferredDisplayModeId = best.getModeId();
                w.setAttributes(a);
                fpsBoost = true;
            } else {
                if (!fpsBoost) return;
                a.preferredDisplayModeId = savedMode;
                w.setAttributes(a);
                fpsBoost = false;
            }
        } catch (Throwable ignored) { }
    }

    // ------------------------------------------------------------------ komanda
    private void onCommand(String arg) {
        if (closing) { pending = arg; return; }          // baglanma bitende yeniden acilacaq
        if (attached) {
            if (arg.isEmpty()) { close(); return; }
            if (mini) exitMini();
            go(arg);
            return;
        }
        open(arg);
    }

    private final View.OnLayoutChangeListener rootLayout = new View.OnLayoutChangeListener() {
        @Override public void onLayoutChange(View v, int l, int t, int r, int b,
                                             int oldL, int oldT, int oldR, int oldB) {
            if (!attached || closing) return;
            if (r - l == oldR - oldL && b - t == oldB - oldT) return;
            refit();
        }
    };

    private void open(String arg) {
        if (!built) build();

        web.onResume();
        web.resumeTimers();
        ensureRect();
        resetViewsToNormal();

        card.setAlpha(0f);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(nw, nh, Gravity.TOP | Gravity.LEFT);
        act.addContentView(card, lp);
        card.setTranslationX(nx);
        card.setTranslationY(ny);
        card.setTranslationZ(0f);
        attached = true;

        View r = root();
        if (r != null) r.addOnLayoutChangeListener(rootLayout);

        oldSoftMode = act.getWindow().getAttributes().softInputMode;
        act.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_UNCHANGED);

        boostFps(true);

        // iOS "pop" acilisi
        card.setPivotX(nw / 2f);
        card.setPivotY(nh / 2f);
        card.setScaleX(0.86f);
        card.setScaleY(0.86f);
        final ValueAnimator a = ValueAnimator.ofFloat(0f, 1f);
        openAnim = a;
        a.setDuration(520);
        a.setInterpolator(new Spring(0.80f, 0.40f, 0.52f));
        a.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator an) {
                float f = (Float) an.getAnimatedValue();
                float s = 0.86f + 0.14f * f;
                card.setScaleX(s);
                card.setScaleY(s);
                card.setAlpha(clamp01(f * 3f));
            }
        });
        a.addListener(new AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(Animator an) {
                card.setScaleX(1f);
                card.setScaleY(1f);
                card.setAlpha(1f);
                if (openAnim == a) openAnim = null;
            }
        });
        a.start();

        histReset = true;
        if (arg.isEmpty()) web.loadUrl(HOME); else go(arg);
    }

    private void close() {
        if (!attached || closing) return;
        closing = true;
        pending = null;
        wantPlay = false;
        ui.removeCallbacks(poll);
        cancelAnims();
        exitFullscreenVideo();
        hideIme();
        web.evaluateJavascript(PAUSE_ALL_JS, null);
        web.loadUrl("about:blank");           // oynatmani tam dayandir

        card.animate().cancel();
        card.setPivotX(curW() / 2f);
        card.setPivotY(curH() / 2f);
        card.animate().alpha(0f).scaleX(0.92f).scaleY(0.92f).translationZ(0f).setDuration(190)
                .setInterpolator(new AccelerateInterpolator(1.4f))
                .setListener(new AnimatorListenerAdapter() {
                    @Override public void onAnimationEnd(Animator a) {
                        card.animate().setListener(null);
                        detach();
                    }
                }).start();
    }

    private void detach() {
        View r = root();
        if (r != null) r.removeOnLayoutChangeListener(rootLayout);
        if (card.getParent() instanceof ViewGroup) ((ViewGroup) card.getParent()).removeView(card);
        attached = false;
        closing = false;
        card.setScaleX(1f);
        card.setScaleY(1f);
        card.setAlpha(1f);
        resetViewsToNormal();
        if (web != null) web.onPause();
        if (oldSoftMode != -1) act.getWindow().setSoftInputMode(oldSoftMode);
        boostFps(false);
        savePrefs();

        String p = pending;
        pending = null;
        if (p != null) open(p);
    }

    private void destroy() {
        ui.removeCallbacksAndMessages(null);
        cancelAnims();
        View r = root();
        if (r != null) r.removeOnLayoutChangeListener(rootLayout);
        if (card != null) card.animate().cancel();
        if (card != null && card.getParent() instanceof ViewGroup) ((ViewGroup) card.getParent()).removeView(card);
        if (web != null) {
            web.evaluateJavascript(PAUSE_ALL_JS, null);
            if (web.getParent() instanceof ViewGroup) ((ViewGroup) web.getParent()).removeView(web);
            web.destroy();
            web = null;
        }
        boostFps(false);
        attached = false;
        closing = false;
        busy = false;
        built = false;
    }

    // ------------------------------------------------------------------ axtaris
    private void go(String input) {
        String t = input.trim();
        if (t.isEmpty()) return;
        String url;
        if (t.startsWith("http://") || t.startsWith("https://")) url = t;
        else if (t.contains("youtu.be/") || t.contains("youtube.com/")) url = "https://" + t;
        else url = "https://m.youtube.com/results?search_query=" + Uri.encode(t);
        web.loadUrl(url);
    }

    private void hideIme() {
        InputMethodManager imm = (InputMethodManager) act.getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null && search != null) imm.hideSoftInputFromWindow(search.getWindowToken(), 0);
        if (search != null) search.clearFocus();
    }

    private void refreshNav() {
        if (web == null) return;
        float b = web.canGoBack() ? 1f : 0.35f, f = web.canGoForward() ? 1f : 0.35f;
        if (icBack.getAlpha() != b) icBack.animate().alpha(b).setDuration(150).start();
        if (icFwd.getAlpha() != f) icFwd.animate().alpha(f).setDuration(150).start();
    }

    // ------------------------------------------------------------------ olcu / movqe
    private void fitNormal() {
        final int sw = screenW(), sh = screenH();
        final int minW = Math.min(dpx(420), sw), minH = Math.min(dpx(240), sh);
        nw = clamp(nw, minW, sw);
        nh = clamp(nh, minH, sh);
        nx = clamp(nx, 0, sw - nw);
        ny = clamp(ny, 0, sh - nh);
    }

    private void ensureRect() {
        final int sw = screenW(), sh = screenH();
        if (nw <= 0 || nh <= 0) {
            nw = (int) Math.min(sw * 0.66f, dpx(760));
            nh = (int) Math.min(sh * 0.80f, nw * 0.62f);
            nx = (sw - nw) / 2;
            ny = (sh - nh) / 2;
        }
        fitNormal();
        maximized = false;
        icMax.setType(Ic.MAX);
    }

    /** ekran olcusu deyisende pencereni yeniden yerleshdir */
    private void refit() {
        if (busy || card == null) return;
        cancelRectAnim();
        final int sw = screenW(), sh = screenH(), m = dpx(10);
        if (mini) {
            mx = clamp(mx, 0, sw - curW());
            my = clamp(my, 0, sh - curH());
            applyRect(mx, my, curW(), curH());
        } else if (maximized) {
            applyRect(m, m, sw - 2 * m, sh - 2 * m);
        } else {
            fitNormal();
            applyRect(nx, ny, nw, nh);
        }
    }

    private void rememberNormal() {
        if (mini || maximized || card == null || busy) return;
        nx = Math.round(card.getTranslationX());
        ny = Math.round(card.getTranslationY());
        nw = curW();
        nh = curH();
    }

    private void setLayoutSize(int w, int h) {
        ViewGroup.LayoutParams lp = card.getLayoutParams();
        if (lp != null && (lp.width != w || lp.height != h)) {
            lp.width = w;
            lp.height = h;
            card.setLayoutParams(lp);
        }
    }

    /** ani qoyma (suruklemede / olcu deyisende) */
    private void applyRect(int x, int y, int w, int h) {
        setLayoutSize(w, h);
        card.setPivotX(0f);
        card.setPivotY(0f);
        card.setScaleX(1f);
        card.setScaleY(1f);
        card.setTranslationX(x);
        card.setTranslationY(y);
    }

    private void cancelRectAnim() {
        if (openAnim != null && openAnim.isRunning()) openAnim.cancel();
        openAnim = null;
        if (rectAnim != null && rectAnim.isRunning()) rectAnim.cancel();   // cari veziyyeti "bake" edir
        rectAnim = null;
    }

    private void cancelAnims() {
        cancelRectAnim();
        if (modeAnim != null && modeAnim.isRunning()) modeAnim.cancel();
        modeAnim = null;
    }

    /** animasiya yarimciq dayananda cari gorunen veziyyeti real layout-a cevir (tullanma olmasin) */
    private void bakeVisual() {
        int w = Math.max(1, Math.round(curW() * card.getScaleX()));
        int h = Math.max(1, Math.round(curH() * card.getScaleY()));
        applyRect(Math.round(card.getTranslationX()), Math.round(card.getTranslationY()), w, h);
    }

    private void animateRect(int x, int y, int w, int h) { animateRect(x, y, w, h, 0.82f); }

    /**
     * Axici kecid (FLIP + spring): yeni olcu bir defe teyin olunur, sonra GPU ile
     * kohne cerceveden yeniye scale+translate animasiyasi gedir.
     * WebView her kadrda yeniden olculmur -> yuksek FPS.
     */
    private void animateRect(final int x, final int y, final int w, final int h, float zeta) {
        cancelRectAnim();

        final float vx = card.getTranslationX(), vy = card.getTranslationY();
        final float vw = curW() * card.getScaleX(), vh = curH() * card.getScaleY();

        setLayoutSize(w, h);
        card.setPivotX(0f);
        card.setPivotY(0f);
        final float sx0 = vw / w, sy0 = vh / h;
        card.setScaleX(sx0);
        card.setScaleY(sy0);
        card.setTranslationX(vx);
        card.setTranslationY(vy);

        final ValueAnimator a = ValueAnimator.ofFloat(0f, 1f);
        rectAnim = a;
        a.setDuration(480);
        a.setInterpolator(new Spring(zeta, 0.40f, 0.48f));
        a.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator an) {
                float f = (Float) an.getAnimatedValue();
                card.setScaleX(sx0 + (1f - sx0) * f);
                card.setScaleY(sy0 + (1f - sy0) * f);
                card.setTranslationX(vx + (x - vx) * f);
                card.setTranslationY(vy + (y - vy) * f);
            }
        });
        a.addListener(new AnimatorListenerAdapter() {
            boolean cancelled;
            @Override public void onAnimationCancel(Animator an) {
                cancelled = true;
                bakeVisual();
            }
            @Override public void onAnimationEnd(Animator an) {
                if (cancelled) return;
                card.setScaleX(1f);
                card.setScaleY(1f);
                card.setTranslationX(x);
                card.setTranslationY(y);
                if (rectAnim == a) rectAnim = null;
                savePrefs();
            }
        });
        a.start();
    }

    private void setPos(float x, float y) {
        int px = clamp(Math.round(x), 0, screenW() - curW());
        int py = clamp(Math.round(y), 0, screenH() - curH());
        card.setTranslationX(px);
        card.setTranslationY(py);
        if (mini) { mx = px; my = py; } else if (!maximized) { nx = px; ny = py; }
    }

    private void savePrefs() {
        if (card == null) return;
        if (!mini && !maximized && attached && !busy) rememberNormal();
        SharedPreferences.Editor e = prefs.edit();
        e.putInt("nx", nx).putInt("ny", ny).putInt("nw", nw).putInt("nh", nh);
        e.putInt("mx", mx).putInt("my", my);
        e.apply();
    }

    private void toggleMaximize() {
        if (mini || busy || closing) return;
        final int m = dpx(10);
        if (!maximized) {
            cancelRectAnim();
            rememberNormal();
            maximized = true;
            icMax.setType(Ic.RESTORE);
            animateRect(m, m, screenW() - 2 * m, screenH() - 2 * m);
        } else {
            maximized = false;
            icMax.setType(Ic.MAX);
            fitNormal();
            animateRect(nx, ny, nw, nh);
        }
    }

    // ------------------------------------------------------------------ mini pleyer
    private float miniScale() {
        return dpx(PREV_W) / (float) Math.max(1, webW0);
    }

    /**
     * Rejimin STABIL (animasiyasiz) veziyyeti. VACIB: WebView hec vaxt detach olunmur,
     * valideynleri GONE edilmir ve mini-de LAYOUT OLCUSU DEYISMIR (yalniz scale).
     */
    private void applyMode(boolean m) {
        FrameLayout.LayoutParams hp;
        if (m) {
            hp = new FrameLayout.LayoutParams(dpx(PREV_W), dpx(PREV_H), Gravity.TOP | Gravity.LEFT);
            hp.leftMargin = dpx(6);
            hp.topMargin = dpx(6);
            FrameLayout.LayoutParams wl = new FrameLayout.LayoutParams(
                    Math.max(1, webW0), Math.max(1, webH0), Gravity.TOP | Gravity.LEFT);
            web.setLayoutParams(wl);
            web.setPivotX(0f);
            web.setPivotY(0f);
            float sc = miniScale();
            web.setScaleX(sc);
            web.setScaleY(sc);
            holderRadius = dpx(8);
        } else {
            hp = new FrameLayout.LayoutParams(MATCH, MATCH);
            hp.topMargin = dpx(CHROME_H);
            web.setLayoutParams(new FrameLayout.LayoutParams(MATCH, MATCH));
            web.setScaleX(1f);
            web.setScaleY(1f);
            holderRadius = 0f;
        }
        webHolder.setLayoutParams(hp);
        webHolder.invalidateOutline();

        chrome.setAlpha(1f);
        miniGroup.setAlpha(1f);
        icGrip.setAlpha(1f);
        chrome.setVisibility(m ? View.GONE : View.VISIBLE);
        miniGroup.setVisibility(m ? View.VISIBLE : View.GONE);
        previewOverlay.setVisibility(m ? View.VISIBLE : View.GONE);
        icGrip.setVisibility(m ? View.GONE : View.VISIBLE);
    }

    /**
     * Normal <-> mini kecidi: kart olcusu, webHolder ve scale HER KADRDA hesablanir (spring),
     * WebView-nin ozunun layout olcusu sabit qalir -> mahni dayanmir, FPS yuksek qalir.
     */
    private void transitionMode(final boolean toMini, final int tx, final int ty, final int tw, final int th) {
        if (modeAnim != null && modeAnim.isRunning()) modeAnim.cancel();
        busy = true;

        final int pad = dpx(PAD) * 2, chromeH = dpx(CHROME_H), m6 = dpx(6);
        final float sx = card.getTranslationX(), sy = card.getTranslationY();
        final int sw0 = curW(), sh0 = curH();
        card.setScaleX(1f);
        card.setScaleY(1f);

        // WebView-ni sabit olcuye kecir (cari olcu ile eynidir -> real resize yoxdur)
        web.setLayoutParams(new FrameLayout.LayoutParams(
                Math.max(1, webW0), Math.max(1, webH0), Gravity.TOP | Gravity.LEFT));
        web.setPivotX(0f);
        web.setPivotY(0f);

        final float hl0, ht0, hw0, hh0, hl1, ht1, hw1, hh1;
        if (toMini) {
            hl0 = 0; ht0 = chromeH; hw0 = sw0 - pad; hh0 = sh0 - pad - chromeH;
            hl1 = m6; ht1 = m6; hw1 = dpx(PREV_W); hh1 = dpx(PREV_H);
        } else {
            hl0 = m6; ht0 = m6; hw0 = dpx(PREV_W); hh0 = dpx(PREV_H);
            hl1 = 0; ht1 = chromeH; hw1 = tw - pad; hh1 = th - pad - chromeH;
        }
        final float rad0 = toMini ? 0f : dpx(8), rad1 = toMini ? dpx(8) : 0f;

        chrome.setVisibility(View.VISIBLE);
        miniGroup.setVisibility(View.VISIBLE);
        previewOverlay.setVisibility(View.GONE);
        icGrip.setVisibility(View.VISIBLE);

        final ValueAnimator a = ValueAnimator.ofFloat(0f, 1f);
        modeAnim = a;
        a.setDuration(520);
        a.setInterpolator(new Spring(0.84f, 0.42f, 0.52f));
        a.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator an) {
                float f = (Float) an.getAnimatedValue();
                int w = Math.max(dpx(60), Math.round(sw0 + (tw - sw0) * f));
                int h = Math.max(dpx(40), Math.round(sh0 + (th - sh0) * f));
                setLayoutSize(w, h);
                card.setTranslationX(sx + (tx - sx) * f);
                card.setTranslationY(sy + (ty - sy) * f);

                float hw = Math.max(1f, hw0 + (hw1 - hw0) * f);
                float hh = Math.max(1f, hh0 + (hh1 - hh0) * f);
                FrameLayout.LayoutParams hp = (FrameLayout.LayoutParams) webHolder.getLayoutParams();
                hp.width = Math.round(hw);
                hp.height = Math.round(hh);
                hp.leftMargin = Math.round(hl0 + (hl1 - hl0) * f);
                hp.topMargin = Math.round(ht0 + (ht1 - ht0) * f);
                webHolder.setLayoutParams(hp);

                float s = hw / Math.max(1, webW0);
                web.setScaleX(s);
                web.setScaleY(s);

                holderRadius = rad0 + (rad1 - rad0) * clamp01(f);
                webHolder.invalidateOutline();

                float c = clamp01(f);
                float outA = 1f - clamp01(c * 2.5f);
                float inA = clamp01((c - 0.4f) * 2.5f);
                chrome.setAlpha(toMini ? outA : inA);
                icGrip.setAlpha(toMini ? outA : inA);
                miniGroup.setAlpha(toMini ? inA : outA);
            }
        });
        a.addListener(new AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(Animator an) {
                if (modeAnim == a) modeAnim = null;
                if (web == null || card == null) { busy = false; return; }
                busy = false;
                setLayoutSize(tw, th);
                card.setTranslationX(tx);
                card.setTranslationY(ty);
                applyMode(toMini);
                savePrefs();
            }
        });
        a.start();
    }

    private static String clean(String s) {
        return s == null ? "x" : s.replace("\"", "");
    }

    private void enterMini() {
        if (mini || !attached || closing || busy) return;
        exitFullscreenVideo();
        hideIme();
        cancelRectAnim();
        rememberNormal();

        webW0 = web.getWidth();
        webH0 = web.getHeight();
        if (webW0 <= 0 || webH0 <= 0) {
            webW0 = Math.max(1, curW() - 2 * dpx(PAD));
            webH0 = Math.max(1, curH() - 2 * dpx(PAD) - dpx(CHROME_H));
        }

        // hazirki oynatma veziyyetini oyren -> mini-de ona gore "davam etdir" qoruyucusu
        web.evaluateJavascript(KEEPALIVE, null);
        web.evaluateJavascript(STATE_JS, new ValueCallback<String>() {
            @Override public void onReceiveValue(String s) {
                String st = clean(s);
                if (st.equals("1")) { wantPlay = true; playing = true; }
                else if (st.equals("0")) { wantPlay = false; playing = false; }
                icPlay.setType(playing ? Ic.PAUSE : Ic.PLAY);
            }
        });
        web.onResume();
        web.resumeTimers();

        final int sw = screenW(), sh = screenH();
        if (mx < 0 || my < 0) { mx = sw - dpx(MINI_W) - dpx(12); my = dpx(12); }
        mx = clamp(mx, 0, sw - dpx(MINI_W));
        my = clamp(my, 0, sh - dpx(MINI_H));

        String t = web.getTitle();
        if (t != null) { lastTitle = t.replace(" - YouTube", ""); miniTitle.setText(lastTitle); }

        mini = true;
        transitionMode(true, mx, my, dpx(MINI_W), dpx(MINI_H));

        ui.removeCallbacks(poll);
        ui.postDelayed(poll, 400);
    }

    private void exitMini() {
        if (!mini || busy || closing) return;
        ui.removeCallbacks(poll);
        cancelRectAnim();
        mx = Math.round(card.getTranslationX());
        my = Math.round(card.getTranslationY());

        final int sw = screenW(), sh = screenH(), m = dpx(10);
        final int tx, ty, tw, th;
        if (maximized) {
            tx = m; ty = m; tw = sw - 2 * m; th = sh - 2 * m;
        } else {
            fitNormal();
            tx = nx; ty = ny; tw = nw; th = nh;
        }
        // yeni normal web olcusu - animasiyadan EVVEL bir defe (mini-de gorunmur)
        webW0 = Math.max(1, tw - 2 * dpx(PAD));
        webH0 = Math.max(1, th - 2 * dpx(PAD) - dpx(CHROME_H));

        mini = false;
        transitionMode(false, tx, ty, tw, th);
    }

    /** mini pleyeri en yaxin kenara yapishdir (atma sureti nezere alinir) */
    private void snapMini(float vx, float vy) {
        final int sw = screenW(), sh = screenH(), m = dpx(10);
        float px = card.getTranslationX() + vx * 0.12f;
        float py = card.getTranslationY() + vy * 0.12f;
        float cx = px + curW() / 2f;
        int tx = cx < sw / 2f ? m : sw - curW() - m;
        int ty = clamp(Math.round(py), m, sh - curH() - m);
        mx = tx;
        my = ty;
        animateRect(tx, ty, curW(), curH(), 0.70f);
    }

    private void resetViewsToNormal() {
        if (!built) return;
        ui.removeCallbacks(poll);
        if (modeAnim != null && modeAnim.isRunning()) modeAnim.cancel();
        busy = false;
        mini = false;
        maximized = false;
        applyMode(false);
        icMax.setType(Ic.MAX);
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!attached || !mini || web == null || closing) return;
            web.evaluateJavascript(STATE_JS, new ValueCallback<String>() {
                @Override public void onReceiveValue(String s) {
                    if (!attached || !mini || web == null) return;
                    if (SystemClock.uptimeMillis() - toggleAt < 500) return;   // istifadeci toxunusu ile yarish
                    String st = clean(s);
                    if (st.equals("1")) {
                        playing = true;
                        wantPlay = true;
                    } else if (st.equals("0")) {
                        if (wantPlay) {
                            // istifadeci pauza etmeyib, sistem/audio focus dayandirib -> davam et
                            web.evaluateJavascript(PLAY_JS, null);
                            playing = true;
                        } else {
                            playing = false;
                        }
                    } else {
                        playing = false;    // 'e' (bitib) ve ya 'x' (video yoxdur): zorla oynatma
                    }
                    icPlay.setType(playing ? Ic.PAUSE : Ic.PLAY);
                }
            });
            web.evaluateJavascript(KEEPALIVE, null);
            String t = web.getTitle();
            if (t != null) {
                String c = t.replace(" - YouTube", "");
                if (!c.equals(lastTitle)) { lastTitle = c; miniTitle.setText(c); }
            }
            ui.postDelayed(this, 700);
        }
    };

    private void togglePlay() {
        boolean target = !playing;
        wantPlay = target;
        playing = target;
        toggleAt = SystemClock.uptimeMillis();
        web.evaluateJavascript(target ? PLAY_JS : PAUSE_JS, null);
        icPlay.setType(target ? Ic.PAUSE : Ic.PLAY);
    }

    private void exitFullscreenVideo() {
        if (customView == null) return;
        fsHolder.removeView(customView);
        fsHolder.setVisibility(View.GONE);
        web.setVisibility(View.VISIBLE);
        if (customCb != null) customCb.onCustomViewHidden();
        customView = null;
        customCb = null;
    }

    // ------------------------------------------------------------------ toxunus: surukleme
    private void lift(boolean on) {
        card.animate().cancel();
        card.animate().alpha(on ? 0.94f : 1f).translationZ(on ? dpx(10) : 0f)
                .setDuration(on ? 120 : 220).setInterpolator(new DecelerateInterpolator())
                .setListener(null).start();
    }

    @SuppressLint("ClickableViewAccessibility")
    private void attachDrag(View v, final Runnable onTap, final Runnable onDoubleTap) {
        v.setOnTouchListener(new View.OnTouchListener() {
            float sx, sy, stx, sty, lx, ly, vx, vy;
            long lt, lastTap;
            boolean moved, active;

            @Override public boolean onTouch(View view, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        active = !(closing || busy);
                        if (!active) return true;
                        cancelRectAnim();
                        sx = e.getRawX(); sy = e.getRawY();
                        lx = sx; ly = sy; lt = e.getEventTime();
                        vx = 0; vy = 0;
                        stx = card.getTranslationX(); sty = card.getTranslationY();
                        moved = false;
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        if (!active) return true;
                        float rx = e.getRawX(), ry = e.getRawY();
                        float dx = rx - sx, dy = ry - sy;
                        if (!moved && Math.hypot(dx, dy) > dpx(6)) {
                            if (!mini && maximized) { active = false; return true; }   // maximize-da surukleme yoxdur
                            moved = true;
                            lift(true);
                        }
                        if (moved) {
                            long dt = e.getEventTime() - lt;
                            if (dt >= 8) {
                                vx = vx * 0.4f + ((rx - lx) / dt * 1000f) * 0.6f;
                                vy = vy * 0.4f + ((ry - ly) / dt * 1000f) * 0.6f;
                                lx = rx; ly = ry; lt = e.getEventTime();
                            }
                            setPos(stx + dx, sty + dy);
                        }
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL: {
                        if (!active) return true;
                        boolean up = e.getActionMasked() == MotionEvent.ACTION_UP;
                        if (moved) {
                            lift(false);
                            if (e.getEventTime() - lt > 80) { vx = 0; vy = 0; }
                            if (mini) {
                                snapMini(vx, vy);
                            } else {
                                int px = clamp(Math.round(card.getTranslationX() + vx * 0.08f), 0, screenW() - curW());
                                int py = clamp(Math.round(card.getTranslationY() + vy * 0.08f), 0, screenH() - curH());
                                nx = px; ny = py;
                                animateRect(px, py, curW(), curH(), 0.88f);
                            }
                        } else if (up) {
                            long now = SystemClock.uptimeMillis();
                            if (onDoubleTap != null && now - lastTap < 320) { lastTap = 0; onDoubleTap.run(); }
                            else { lastTap = now; if (onTap != null) onTap.run(); }
                        }
                        return true;
                    }
                }
                return false;
            }
        });
    }

    // ------------------------------------------------------------------ UI qurulumu
    @SuppressLint({"SetJavaScriptEnabled", "ClickableViewAccessibility"})
    private void build() {
        // ---- korpus ----
        card = new FrameLayout(act);
        GradientDrawable bezel = new GradientDrawable();
        bezel.setColor(C_BEZEL);
        bezel.setCornerRadius(dpx(22));
        bezel.setStroke(Math.max(1, dpx(1)), 0xFF2A2E38);
        card.setBackground(bezel);
        card.setPadding(dpx(PAD), dpx(PAD), dpx(PAD), dpx(PAD));
        card.setElevation(dpx(18));
        card.setClickable(true);
        card.setFocusable(true);
        card.setFocusableInTouchMode(true);

        inner = new FrameLayout(act);
        inner.setBackgroundColor(C_INNER);
        inner.setOutlineProvider(new ViewOutlineProvider() {
            @Override public void getOutline(View v, Outline o) { o.setRoundRect(0, 0, v.getWidth(), v.getHeight(), dpx(17)); }
        });
        inner.setClipToOutline(true);
        card.addView(inner, new FrameLayout.LayoutParams(MATCH, MATCH));

        // ================= mini panel (en altda; webHolder onun ustunde preview kimi gorunur) =================
        miniGroup = new LinearLayout(act);
        miniGroup.setOrientation(LinearLayout.HORIZONTAL);
        miniGroup.setGravity(Gravity.CENTER_VERTICAL);
        miniGroup.setBackgroundColor(C_BAR);
        miniGroup.setPadding(dpx(6 + PREV_W + 10), dpx(4), dpx(6), dpx(4));
        miniGroup.setVisibility(View.GONE);
        inner.addView(miniGroup, new FrameLayout.LayoutParams(MATCH, MATCH));
        attachDrag(miniGroup, null, null);

        LinearLayout col = new LinearLayout(act);
        col.setOrientation(LinearLayout.VERTICAL);
        miniGroup.addView(col, new LinearLayout.LayoutParams(0, MATCH, 1f));

        miniTitle = new TextView(act);
        miniTitle.setTextColor(C_TXT);
        miniTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        miniTitle.setSingleLine(true);
        miniTitle.setEllipsize(TextUtils.TruncateAt.END);
        miniTitle.setGravity(Gravity.CENTER_VERTICAL);
        miniTitle.setText("YouTube");
        col.addView(miniTitle, new LinearLayout.LayoutParams(MATCH, 0, 1f));

        LinearLayout ctr = new LinearLayout(act);
        ctr.setOrientation(LinearLayout.HORIZONTAL);
        ctr.setGravity(Gravity.CENTER_VERTICAL);
        col.addView(ctr, new LinearLayout.LayoutParams(MATCH, dpx(38)));

        icPlay = new Ic(act, Ic.PAUSE, dp);
        Ic icExpand = new Ic(act, Ic.EXPAND, dp);
        Ic icMiniClose = new Ic(act, Ic.CLOSE, dp);
        ctr.addView(icPlay, new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        ctr.addView(icExpand, new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        ctr.addView(icMiniClose, new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        icPlay.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { togglePlay(); } });
        icExpand.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { exitMini(); } });
        icMiniClose.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { close(); } });

        // ================= veb sahe (normalda butun sahe, mini-de sol-ust preview) =================
        webHolder = new FrameLayout(act);
        webHolder.setBackgroundColor(0xFF0F0F0F);
        webHolder.setOutlineProvider(new ViewOutlineProvider() {
            @Override public void getOutline(View v, Outline o) {
                o.setRoundRect(0, 0, v.getWidth(), v.getHeight(), holderRadius);
            }
        });
        webHolder.setClipToOutline(true);
        FrameLayout.LayoutParams whp = new FrameLayout.LayoutParams(MATCH, MATCH);
        whp.topMargin = dpx(CHROME_H);
        inner.addView(webHolder, whp);

        web = new WebView(act);
        web.setBackgroundColor(0xFF0F0F0F);
        web.setOverScrollMode(View.OVER_SCROLL_NEVER);
        web.setLayerType(View.LAYER_TYPE_HARDWARE, null);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setAllowFileAccess(false);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_COMPATIBILITY_MODE);
        s.setUserAgentString(s.getUserAgentString().replace("; wv", ""));
        CookieManager cm = CookieManager.getInstance();
        cm.setAcceptCookie(true);
        cm.setAcceptThirdPartyCookies(web, true);

        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, WebResourceRequest r) {
                String u = r.getUrl().toString();
                return u.startsWith("intent:") || u.startsWith("vnd.youtube") || u.startsWith("market:") || u.startsWith("youtube:");
            }
            @Override public void onPageStarted(WebView v, String url, Bitmap fav) {
                line.set(5);
                v.evaluateJavascript(KEEPALIVE, null);
            }
            @Override public void onPageFinished(WebView v, String url) {
                line.set(100);
                if (histReset && url != null && !url.startsWith("about:")) {
                    histReset = false;
                    v.clearHistory();             // about:blank girisi geri duymesine dusmesin
                }
                refreshNav();
                v.evaluateJavascript(KEEPALIVE, null);
            }
            @Override public void doUpdateVisitedHistory(WebView v, String url, boolean isReload) {
                refreshNav();
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override public void onProgressChanged(WebView v, int p) { line.set(p); }
            @Override public Bitmap getDefaultVideoPoster() { return Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888); }
            @Override public void onShowCustomView(View view, CustomViewCallback cb) {
                if (customView != null) { cb.onCustomViewHidden(); return; }
                customView = view;
                customCb = cb;
                fsHolder.addView(view, new FrameLayout.LayoutParams(MATCH, MATCH));
                fsHolder.setVisibility(View.VISIBLE);
                web.setVisibility(View.INVISIBLE);
            }
            @Override public void onHideCustomView() { exitFullscreenVideo(); }
        });
        web.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent e) {
                if (search.hasFocus()) hideIme();
                return false;
            }
        });
        webHolder.addView(web, new FrameLayout.LayoutParams(MATCH, MATCH));

        fsHolder = new FrameLayout(act);
        fsHolder.setBackgroundColor(Color.BLACK);
        fsHolder.setVisibility(View.GONE);
        webHolder.addView(fsHolder, new FrameLayout.LayoutParams(MATCH, MATCH));

        // ================= normal rejim: ust hisse (bashliq + panel + xett) =================
        chrome = new LinearLayout(act);
        chrome.setOrientation(LinearLayout.VERTICAL);
        chrome.setBackgroundColor(C_BAR);
        inner.addView(chrome, new FrameLayout.LayoutParams(MATCH, dpx(CHROME_H), Gravity.TOP));

        handle = new View(act) {
            final Paint bg = new Paint(Paint.ANTI_ALIAS_FLAG), pill = new Paint(Paint.ANTI_ALIAS_FLAG), tx = new Paint(Paint.ANTI_ALIAS_FLAG);
            final RectF r = new RectF();
            { bg.setColor(C_BAR); pill.setColor(0xFF4A4F5C); tx.setColor(C_HINT); tx.setTextSize(dpx(11)); tx.setTypeface(Typeface.DEFAULT_BOLD); }
            @Override protected void onDraw(Canvas c) {
                float w = getWidth(), h = getHeight();
                c.drawRect(0, 0, w, h, bg);
                r.set(w / 2f - dpx(18), h / 2f - dpx(2), w / 2f + dpx(18), h / 2f + dpx(2));
                c.drawRoundRect(r, dpx(2), dpx(2), pill);
                c.drawText("YouTube", dpx(14), h / 2f + dpx(4), tx);
            }
        };
        chrome.addView(handle, new LinearLayout.LayoutParams(MATCH, dpx(26)));
        attachDrag(handle, null, new Runnable() { @Override public void run() { toggleMaximize(); } });

        topBar = new LinearLayout(act);
        topBar.setOrientation(LinearLayout.HORIZONTAL);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        topBar.setBackgroundColor(C_BAR);
        topBar.setPadding(dpx(4), 0, dpx(4), dpx(2));
        chrome.addView(topBar, new LinearLayout.LayoutParams(MATCH, dpx(44)));

        icBack = new Ic(act, Ic.BACK, dp);
        icFwd  = new Ic(act, Ic.FWD, dp);
        Ic icHome = new Ic(act, Ic.HOME, dp);
        topBar.addView(icBack, new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icFwd,  new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icHome, new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        icBack.setAlpha(0.35f);
        icFwd.setAlpha(0.35f);

        search = new EditText(act);
        search.setSingleLine(true);
        search.setHint("YouTube-da axtar");
        search.setHintTextColor(C_HINT);
        search.setTextColor(C_TXT);
        search.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        search.setPadding(dpx(14), 0, dpx(14), 0);
        search.setImeOptions(EditorInfo.IME_ACTION_SEARCH | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        search.setInputType(android.text.InputType.TYPE_CLASS_TEXT);
        GradientDrawable fieldBg = new GradientDrawable();
        fieldBg.setColor(C_FIELD);
        fieldBg.setCornerRadius(dpx(17));
        search.setBackground(fieldBg);
        search.setOnEditorActionListener(new TextView.OnEditorActionListener() {
            @Override public boolean onEditorAction(TextView v, int actionId, android.view.KeyEvent ev) {
                if (actionId == EditorInfo.IME_ACTION_SEARCH || actionId == EditorInfo.IME_ACTION_DONE
                        || (ev != null && ev.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER)) {
                    go(search.getText().toString());
                    hideIme();
                    return true;
                }
                return false;
            }
        });
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(0, dpx(34), 1f);
        slp.setMargins(dpx(4), 0, dpx(2), 0);
        topBar.addView(search, slp);

        Ic icGo   = new Ic(act, Ic.SEARCH, dp);
        Ic icNote = new Ic(act, Ic.NOTE, dp);
        icMax     = new Ic(act, Ic.MAX, dp);
        Ic icClose = new Ic(act, Ic.CLOSE, dp);
        topBar.addView(icGo,    new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icNote,  new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icMax,   new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icClose, new LinearLayout.LayoutParams(dpx(38), dpx(38)));

        icBack.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { if (web.canGoBack()) web.goBack(); } });
        icFwd.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { if (web.canGoForward()) web.goForward(); } });
        icHome.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { web.loadUrl(HOME); } });
        icGo.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { go(search.getText().toString()); hideIme(); } });
        icNote.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { enterMini(); } });
        icMax.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { toggleMaximize(); } });
        icClose.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { close(); } });

        line = new Line(act);
        chrome.addView(line, new LinearLayout.LayoutParams(MATCH, dpx(2)));

        // ================= mini: preview ustunde toxunus qati (tap = geri boyut, surukle = movqe) =================
        previewOverlay = new View(act);
        previewOverlay.setVisibility(View.GONE);
        FrameLayout.LayoutParams pop = new FrameLayout.LayoutParams(dpx(PREV_W), dpx(PREV_H), Gravity.TOP | Gravity.LEFT);
        pop.leftMargin = dpx(6);
        pop.topMargin = dpx(6);
        inner.addView(previewOverlay, pop);
        attachDrag(previewOverlay, new Runnable() { @Override public void run() { exitMini(); } }, null);

        // ================= olcu kunci =================
        icGrip = new Ic(act, Ic.GRIP, dp);
        FrameLayout.LayoutParams glp = new FrameLayout.LayoutParams(dpx(34), dpx(34), Gravity.BOTTOM | Gravity.RIGHT);
        card.addView(icGrip, glp);
        icGrip.setOnTouchListener(new View.OnTouchListener() {
            float sx, sy; int sw0, sh0; boolean active;
            @Override public boolean onTouch(View v, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        active = !(closing || busy || mini);
                        if (!active) return true;
                        cancelRectAnim();
                        sx = e.getRawX(); sy = e.getRawY(); sw0 = curW(); sh0 = curH();
                        if (card.getParent() != null) card.getParent().requestDisallowInterceptTouchEvent(true);
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        if (!active) return true;
                        int tx = Math.round(card.getTranslationX()), ty = Math.round(card.getTranslationY());
                        int minW = Math.min(dpx(420), screenW() - tx), minH = Math.min(dpx(240), screenH() - ty);
                        int w = clamp(Math.round(sw0 + (e.getRawX() - sx)), minW, screenW() - tx);
                        int h = clamp(Math.round(sh0 + (e.getRawY() - sy)), minH, screenH() - ty);
                        maximized = false;
                        icMax.setType(Ic.MAX);
                        applyRect(tx, ty, w, h);
                        nw = w; nh = h; nx = tx; ny = ty;
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        if (active) savePrefs();
                        active = false;
                        return true;
                }
                return false;
            }
        });

        built = true;
        applyMode(false);
    }

    // ------------------------------------------------------------------ yardimci View-lar
    /** hamar yukleme xetti (iOS usulu: yumshaq irelileme + solma) */
    private static final class Line extends View {
        private final Paint p = new Paint();
        private float shown, target;
        private final Runnable step = new Runnable() {
            @Override public void run() {
                shown += (target - shown) * 0.2f;
                if (Math.abs(target - shown) < 0.4f) shown = target;
                invalidate();
                if (shown < target) {
                    postOnAnimation(this);
                } else if (shown >= 100f) {
                    animate().alpha(0f).setDuration(280).withEndAction(new Runnable() {
                        @Override public void run() { shown = 0f; target = 0f; invalidate(); }
                    }).start();
                }
            }
        };

        Line(Context c) { super(c); p.setColor(C_ACC); }

        void set(int v) {
            animate().cancel();
            setAlpha(1f);
            if (v <= 0) { shown = 0f; target = 0f; invalidate(); return; }
            if (shown >= 100f) shown = 0f;
            target = Math.min(100f, v);
            removeCallbacks(step);
            postOnAnimation(step);
        }

        @Override protected void onDraw(Canvas c) {
            if (shown <= 0f) return;
            c.drawRect(0, 0, getWidth() * shown / 100f, getHeight(), p);
        }
    }

    /** vektor ikonlar (resurs lazim deyil) + iOS press-scale */
    private static final class Ic extends View {
        static final int BACK = 0, FWD = 1, HOME = 2, SEARCH = 3, NOTE = 4, MAX = 5, RESTORE = 6,
                         CLOSE = 7, PLAY = 8, PAUSE = 9, EXPAND = 10, GRIP = 11;

        private int type;
        private final float dp;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();

        Ic(Context c, int type, float dp) {
            super(c);
            this.type = type;
            this.dp = dp;
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeJoin(Paint.Join.ROUND);
            if (type != GRIP) {
                TypedValue tv = new TypedValue();
                if (c.getTheme().resolveAttribute(android.R.attr.selectableItemBackgroundBorderless, tv, true))
                    setBackgroundResource(tv.resourceId);
            }
            setClickable(true);
        }

        void setType(int t) {
            if (type == t) return;
            type = t;
            invalidate();
            if (isAttachedToWindow()) {
                setScaleX(0.6f);
                setScaleY(0.6f);
                animate().scaleX(1f).scaleY(1f).setDuration(260)
                        .setInterpolator(new OvershootInterpolator(2.2f)).start();
            }
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    animate().scaleX(0.82f).scaleY(0.82f).setDuration(90)
                            .setInterpolator(new DecelerateInterpolator()).start();
                    break;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    animate().scaleX(1f).scaleY(1f).setDuration(280)
                            .setInterpolator(new OvershootInterpolator(2.5f)).start();
                    break;
            }
            return super.onTouchEvent(e);
        }

        @Override protected void onDraw(Canvas c) {
            final float w = getWidth(), h = getHeight(), cx = w / 2f, cy = h / 2f, u = Math.min(w, h) / 24f;
            p.setColor(C_ICON);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(2f * dp);
            path.reset();
            switch (type) {
                case BACK:
                    path.moveTo(cx + 3 * u, cy - 6 * u); path.lineTo(cx - 3 * u, cy); path.lineTo(cx + 3 * u, cy + 6 * u);
                    c.drawPath(path, p);
                    break;
                case FWD:
                    path.moveTo(cx - 3 * u, cy - 6 * u); path.lineTo(cx + 3 * u, cy); path.lineTo(cx - 3 * u, cy + 6 * u);
                    c.drawPath(path, p);
                    break;
                case HOME:
                    path.moveTo(cx - 7 * u, cy); path.lineTo(cx, cy - 6.5f * u); path.lineTo(cx + 7 * u, cy);
                    c.drawPath(path, p);
                    c.drawRect(cx - 4.5f * u, cy, cx + 4.5f * u, cy + 6.5f * u, p);
                    break;
                case SEARCH:
                    c.drawCircle(cx - 1.5f * u, cy - 1.5f * u, 5.5f * u, p);
                    c.drawLine(cx + 2.5f * u, cy + 2.5f * u, cx + 7 * u, cy + 7 * u, p);
                    break;
                case NOTE:
                    p.setStyle(Paint.Style.FILL);
                    c.drawCircle(cx - 3.5f * u, cy + 5 * u, 3f * u, p);
                    p.setStyle(Paint.Style.STROKE);
                    path.moveTo(cx - 0.6f * u, cy + 5 * u); path.lineTo(cx - 0.6f * u, cy - 6 * u); path.lineTo(cx + 5.5f * u, cy - 3.5f * u);
                    c.drawPath(path, p);
                    break;
                case MAX:
                    c.drawRoundRect(cx - 5.5f * u, cy - 5.5f * u, cx + 5.5f * u, cy + 5.5f * u, 1.5f * u, 1.5f * u, p);
                    break;
                case RESTORE:
                    c.drawRoundRect(cx - 6f * u, cy - 2f * u, cx + 2f * u, cy + 6f * u, 1.2f * u, 1.2f * u, p);
                    path.moveTo(cx - 2f * u, cy - 2f * u); path.lineTo(cx - 2f * u, cy - 6f * u);
                    path.lineTo(cx + 6f * u, cy - 6f * u); path.lineTo(cx + 6f * u, cy + 2f * u); path.lineTo(cx + 2f * u, cy + 2f * u);
                    c.drawPath(path, p);
                    break;
                case CLOSE:
                    c.drawLine(cx - 5 * u, cy - 5 * u, cx + 5 * u, cy + 5 * u, p);
                    c.drawLine(cx - 5 * u, cy + 5 * u, cx + 5 * u, cy - 5 * u, p);
                    break;
                case PLAY:
                    p.setStyle(Paint.Style.FILL);
                    path.moveTo(cx - 4 * u, cy - 6.5f * u); path.lineTo(cx + 6.5f * u, cy); path.lineTo(cx - 4 * u, cy + 6.5f * u); path.close();
                    c.drawPath(path, p);
                    break;
                case PAUSE:
                    p.setStyle(Paint.Style.FILL);
                    c.drawRoundRect(cx - 5 * u, cy - 6 * u, cx - 1.5f * u, cy + 6 * u, u, u, p);
                    c.drawRoundRect(cx + 1.5f * u, cy - 6 * u, cx + 5 * u, cy + 6 * u, u, u, p);
                    break;
                case EXPAND:
                    c.drawLine(cx - 5 * u, cy + 5 * u, cx + 5 * u, cy - 5 * u, p);
                    path.moveTo(cx + 0.5f * u, cy - 5 * u); path.lineTo(cx + 5 * u, cy - 5 * u); path.lineTo(cx + 5 * u, cy - 0.5f * u);
                    c.drawPath(path, p);
                    break;
                case GRIP:
                    p.setColor(0xFF6B7184);
                    p.setStrokeWidth(1.6f * dp);
                    c.drawLine(w - 4 * u, h - 14 * u, w - 14 * u, h - 4 * u, p);
                    c.drawLine(w - 4 * u, h - 9 * u, w - 9 * u, h - 4 * u, p);
                    break;
            }
        }
    }
}

// ===== END OF YoutubeTablet.java =====
package com.samp.mobile.game;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
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
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.view.WindowManager;
import android.view.animation.AccelerateInterpolator;
import android.view.animation.DecelerateInterpolator;
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
 * /yt  -  oyunun ustunde "planset" seklinde YouTube penceresi.
 *  - bashliqdan surukle, sag-alt kuncden olcunu deyis, cut toxunus = boyut/geri
 *  - axtaris sahesi, geri / ireli / ana sehife
 *  - mp3 duymesi: kicik mini-pleyere kecir (video kucuk gorunur, ses davam edir)
 *  - X: baglayir ve oynatmani dayandirir
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

    private static final int MINI_W = 316, MINI_H = 85;          // dp
    private static final int PREV_W = 112, PREV_H = 63;          // dp
    private static final int VIRT_W = 480, VIRT_H = 270;         // dp (mini rejimde sehife bu olcude qurulur)

    private static final int MATCH = ViewGroup.LayoutParams.MATCH_PARENT;

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

    private FrameLayout card, inner, previewHolder, webHolder, fsHolder;
    private LinearLayout normalGroup, miniGroup, topBar;
    private View handle, previewOverlay;
    private Line line;
    private EditText search;
    private WebView web;
    private Ic icBack, icFwd, icMax, icPlay, icGrip;
    private TextView miniTitle;

    private boolean built, attached, closing, mini, maximized, playing;
    private int oldSoftMode = -1;

    private int nx, ny, nw, nh;          // normal pencere (px)
    private int mx = -1, my = -1;        // mini movqe (px)
    private int pmx, pmy, pmw, pmh;      // boyutmeden evvel

    private View customView;
    private WebChromeClient.CustomViewCallback customCb;
    private ValueAnimator rectAnim;

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

    private int screenW() {
        if (card != null && card.getParent() instanceof View && ((View) card.getParent()).getWidth() > 0)
            return ((View) card.getParent()).getWidth();
        return act.getResources().getDisplayMetrics().widthPixels;
    }

    private int screenH() {
        if (card != null && card.getParent() instanceof View && ((View) card.getParent()).getHeight() > 0)
            return ((View) card.getParent()).getHeight();
        return act.getResources().getDisplayMetrics().heightPixels;
    }

    private int curW() { return card.getLayoutParams() != null ? card.getLayoutParams().width : nw; }
    private int curH() { return card.getLayoutParams() != null ? card.getLayoutParams().height : nh; }

    // ------------------------------------------------------------------ komanda
    private void onCommand(String arg) {
        if (closing) return;
        if (attached) {
            if (arg.isEmpty()) { close(); return; }
            if (mini) exitMini();
            go(arg);
            return;
        }
        open(arg);
    }

    private void open(String arg) {
        if (!built) build();

        ensureRect();
        resetViewsToNormal();

        card.setAlpha(0f);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(nw, nh, Gravity.TOP | Gravity.LEFT);
        act.addContentView(card, lp);
        card.setTranslationX(nx);
        card.setTranslationY(ny);
        attached = true;

        oldSoftMode = act.getWindow().getAttributes().softInputMode;
        act.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_NOTHING
                | WindowManager.LayoutParams.SOFT_INPUT_STATE_UNCHANGED);

        card.setPivotX(nw / 2f);
        card.setPivotY(nh / 2f);
        card.setScaleX(0.94f);
        card.setScaleY(0.94f);
        card.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(200)
                .setInterpolator(new DecelerateInterpolator(1.6f)).setListener(null).start();

        if (arg.isEmpty()) web.loadUrl(HOME); else go(arg);
    }

    private void close() {
        if (!attached || closing) return;
        closing = true;
        ui.removeCallbacks(poll);
        exitFullscreenVideo();
        hideIme();
        if (rectAnim != null) rectAnim.cancel();
        web.loadUrl("about:blank");           // oynatmani dayandir

        card.animate().alpha(0f).scaleX(0.96f).scaleY(0.96f).setDuration(150)
                .setInterpolator(new AccelerateInterpolator())
                .setListener(new AnimatorListenerAdapter() {
                    @Override public void onAnimationEnd(Animator a) {
                        card.animate().setListener(null);
                        detach();
                    }
                }).start();
    }

    private void detach() {
        if (card.getParent() instanceof ViewGroup) ((ViewGroup) card.getParent()).removeView(card);
        attached = false;
        closing = false;
        resetViewsToNormal();
        if (oldSoftMode != -1) act.getWindow().setSoftInputMode(oldSoftMode);
        savePrefs();
    }

    private void destroy() {
        ui.removeCallbacksAndMessages(null);
        if (card != null && card.getParent() instanceof ViewGroup) ((ViewGroup) card.getParent()).removeView(card);
        if (web != null) {
            if (web.getParent() instanceof ViewGroup) ((ViewGroup) web.getParent()).removeView(web);
            web.destroy();
            web = null;
        }
        attached = false;
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
        icBack.setAlpha(web.canGoBack() ? 1f : 0.35f);
        icFwd.setAlpha(web.canGoForward() ? 1f : 0.35f);
    }

    // ------------------------------------------------------------------ olcu / movqe
    private void ensureRect() {
        final int sw = screenW(), sh = screenH();
        if (nw <= 0 || nh <= 0) {
            nw = (int) Math.min(sw * 0.66f, dpx(760));
            nh = (int) Math.min(sh * 0.80f, nw * 0.62f);
            nx = (sw - nw) / 2;
            ny = (sh - nh) / 2;
        }
        nw = clamp(nw, dpx(420), sw);
        nh = clamp(nh, dpx(240), sh);
        nx = clamp(nx, 0, sw - nw);
        ny = clamp(ny, 0, sh - nh);
        maximized = false;
        icMax.setType(Ic.MAX);
    }

    private void rememberNormal() {
        if (mini || maximized || card == null) return;
        nx = Math.round(card.getTranslationX());
        ny = Math.round(card.getTranslationY());
        nw = curW();
        nh = curH();
    }

    private void applyRect(int x, int y, int w, int h) {
        ViewGroup.LayoutParams lp = card.getLayoutParams();
        if (lp != null) {
            lp.width = w;
            lp.height = h;
            card.setLayoutParams(lp);
        }
        card.setTranslationX(x);
        card.setTranslationY(y);
        card.setPivotX(w / 2f);
        card.setPivotY(h / 2f);
    }

    private void animateRect(final int x, final int y, final int w, final int h) {
        if (rectAnim != null) rectAnim.cancel();
        final float x0 = card.getTranslationX(), y0 = card.getTranslationY();
        final int w0 = curW(), h0 = curH();
        rectAnim = ValueAnimator.ofFloat(0f, 1f);
        rectAnim.setDuration(240);
        rectAnim.setInterpolator(new DecelerateInterpolator(1.8f));
        rectAnim.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator a) {
                float f = (Float) a.getAnimatedValue();
                applyRect(Math.round(x0 + (x - x0) * f), Math.round(y0 + (y - y0) * f),
                          Math.round(w0 + (w - w0) * f), Math.round(h0 + (h - h0) * f));
            }
        });
        rectAnim.addListener(new AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(Animator a) { savePrefs(); }
        });
        rectAnim.start();
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
        if (!mini && !maximized && attached) rememberNormal();
        SharedPreferences.Editor e = prefs.edit();
        e.putInt("nx", nx).putInt("ny", ny).putInt("nw", nw).putInt("nh", nh);
        e.putInt("mx", mx).putInt("my", my);
        e.apply();
    }

    private void toggleMaximize() {
        if (mini) return;
        if (!maximized) {
            rememberNormal();
            pmx = nx; pmy = ny; pmw = nw; pmh = nh;
            final int m = dpx(10);
            maximized = true;
            icMax.setType(Ic.RESTORE);
            animateRect(m, m, screenW() - 2 * m, screenH() - 2 * m);
        } else {
            maximized = false;
            icMax.setType(Ic.MAX);
            nx = pmx; ny = pmy; nw = pmw; nh = pmh;
            animateRect(nx, ny, nw, nh);
        }
    }

    // ------------------------------------------------------------------ mini pleyer
    private void enterMini() {
        if (mini || !attached) return;
        exitFullscreenVideo();
        hideIme();
        rememberNormal();
        mini = true;

        webHolder.removeView(web);
        previewHolder.addView(web, 0, new FrameLayout.LayoutParams(dpx(VIRT_W), dpx(VIRT_H)));
        web.setPivotX(0f);
        web.setPivotY(0f);
        final float sc = dpx(PREV_W) / (float) dpx(VIRT_W);
        web.setScaleX(sc);
        web.setScaleY(sc);

        normalGroup.setVisibility(View.GONE);
        miniGroup.setVisibility(View.VISIBLE);
        icGrip.setVisibility(View.GONE);

        if (mx < 0 || my < 0) { mx = screenW() - dpx(MINI_W) - dpx(14); my = dpx(14); }
        mx = clamp(mx, 0, screenW() - dpx(MINI_W));
        my = clamp(my, 0, screenH() - dpx(MINI_H));
        animateRect(mx, my, dpx(MINI_W), dpx(MINI_H));

        icPlay.setType(Ic.PAUSE);
        ui.removeCallbacks(poll);
        ui.post(poll);
    }

    private void exitMini() {
        if (!mini) return;
        ui.removeCallbacks(poll);
        mini = false;
        mx = Math.round(card.getTranslationX());
        my = Math.round(card.getTranslationY());
        restoreWebToNormal();

        normalGroup.setVisibility(View.VISIBLE);
        miniGroup.setVisibility(View.GONE);
        icGrip.setVisibility(View.VISIBLE);

        ensureRectKeepMax();
        animateRect(maximized ? dpx(10) : nx, maximized ? dpx(10) : ny,
                    maximized ? screenW() - 2 * dpx(10) : nw, maximized ? screenH() - 2 * dpx(10) : nh);
    }

    private void ensureRectKeepMax() {
        final boolean wasMax = maximized;
        final int sw = screenW(), sh = screenH();
        nw = clamp(nw, dpx(420), sw);
        nh = clamp(nh, dpx(240), sh);
        nx = clamp(nx, 0, sw - nw);
        ny = clamp(ny, 0, sh - nh);
        maximized = wasMax;
    }

    private void restoreWebToNormal() {
        if (web.getParent() instanceof ViewGroup) ((ViewGroup) web.getParent()).removeView(web);
        webHolder.addView(web, 0, new FrameLayout.LayoutParams(MATCH, MATCH));
        web.setScaleX(1f);
        web.setScaleY(1f);
    }

    private void resetViewsToNormal() {
        if (!built) return;
        ui.removeCallbacks(poll);
        if (mini) {
            mini = false;
            restoreWebToNormal();
        }
        normalGroup.setVisibility(View.VISIBLE);
        miniGroup.setVisibility(View.GONE);
        icGrip.setVisibility(View.VISIBLE);
        maximized = false;
        icMax.setType(Ic.MAX);
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!attached || !mini || web == null) return;
            web.evaluateJavascript(
                    "(function(){var v=document.querySelector('video');return v?(v.paused?'0':'1'):'x';})()",
                    new ValueCallback<String>() {
                        @Override public void onReceiveValue(String s) {
                            playing = s != null && s.contains("1");
                            icPlay.setType(playing ? Ic.PAUSE : Ic.PLAY);
                        }
                    });
            String t = web.getTitle();
            if (t != null) miniTitle.setText(t.replace(" - YouTube", ""));
            ui.postDelayed(this, 700);
        }
    };

    private void togglePlay() {
        web.evaluateJavascript(
                "(function(){var v=document.querySelector('video');if(v){if(v.paused)v.play();else v.pause();}})()", null);
        playing = !playing;
        icPlay.setType(playing ? Ic.PAUSE : Ic.PLAY);
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
    @SuppressLint("ClickableViewAccessibility")
    private void attachDrag(View v, final Runnable onTap, final Runnable onDoubleTap) {
        v.setOnTouchListener(new View.OnTouchListener() {
            float sx, sy, stx, sty;
            boolean moved;
            long lastTap;

            @Override public boolean onTouch(View view, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        sx = e.getRawX(); sy = e.getRawY();
                        stx = card.getTranslationX(); sty = card.getTranslationY();
                        moved = false;
                        if (rectAnim != null) rectAnim.cancel();
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        float dx = e.getRawX() - sx, dy = e.getRawY() - sy;
                        if (!moved && Math.hypot(dx, dy) > dpx(6)) moved = true;
                        if (moved) setPos(stx + dx, sty + dy);
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                        if (!moved) {
                            long now = SystemClock.uptimeMillis();
                            if (onDoubleTap != null && now - lastTap < 320) { lastTap = 0; onDoubleTap.run(); }
                            else { lastTap = now; if (onTap != null) onTap.run(); }
                        } else {
                            savePrefs();
                        }
                        return true;
                    case MotionEvent.ACTION_CANCEL:
                        return true;
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
        card.setPadding(dpx(5), dpx(5), dpx(5), dpx(5));
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

        // ================= normal rejim =================
        normalGroup = new LinearLayout(act);
        normalGroup.setOrientation(LinearLayout.VERTICAL);
        inner.addView(normalGroup, new FrameLayout.LayoutParams(MATCH, MATCH));

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
        normalGroup.addView(handle, new LinearLayout.LayoutParams(MATCH, dpx(26)));
        attachDrag(handle, null, new Runnable() { @Override public void run() { toggleMaximize(); } });

        // ---- ust panel ----
        topBar = new LinearLayout(act);
        topBar.setOrientation(LinearLayout.HORIZONTAL);
        topBar.setGravity(Gravity.CENTER_VERTICAL);
        topBar.setBackgroundColor(C_BAR);
        topBar.setPadding(dpx(4), 0, dpx(4), dpx(2));
        normalGroup.addView(topBar, new LinearLayout.LayoutParams(MATCH, dpx(44)));

        icBack = new Ic(act, Ic.BACK, dp);
        icFwd  = new Ic(act, Ic.FWD, dp);
        Ic icHome = new Ic(act, Ic.HOME, dp);
        topBar.addView(icBack, new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icFwd,  new LinearLayout.LayoutParams(dpx(38), dpx(38)));
        topBar.addView(icHome, new LinearLayout.LayoutParams(dpx(38), dpx(38)));

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
        normalGroup.addView(line, new LinearLayout.LayoutParams(MATCH, dpx(2)));

        // ---- veb sahe ----
        webHolder = new FrameLayout(act);
        webHolder.setBackgroundColor(0xFF0F0F0F);
        normalGroup.addView(webHolder, new LinearLayout.LayoutParams(MATCH, 0, 1f));

        web = new WebView(act);
        web.setBackgroundColor(0xFF0F0F0F);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
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
            @Override public void onPageStarted(WebView v, String url, Bitmap fav) { line.set(5); }
            @Override public void onPageFinished(WebView v, String url) { line.set(0); refreshNav(); }
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

        // ================= mini rejim =================
        miniGroup = new LinearLayout(act);
        miniGroup.setOrientation(LinearLayout.HORIZONTAL);
        miniGroup.setGravity(Gravity.CENTER_VERTICAL);
        miniGroup.setBackgroundColor(C_BAR);
        miniGroup.setPadding(dpx(6), dpx(6), dpx(6), dpx(6));
        miniGroup.setVisibility(View.GONE);
        inner.addView(miniGroup, new FrameLayout.LayoutParams(MATCH, MATCH));
        attachDrag(miniGroup, null, null);

        previewHolder = new FrameLayout(act);
        previewHolder.setBackgroundColor(Color.BLACK);
        previewHolder.setOutlineProvider(new ViewOutlineProvider() {
            @Override public void getOutline(View v, Outline o) { o.setRoundRect(0, 0, v.getWidth(), v.getHeight(), dpx(8)); }
        });
        previewHolder.setClipToOutline(true);
        miniGroup.addView(previewHolder, new LinearLayout.LayoutParams(dpx(PREV_W), dpx(PREV_H)));

        previewOverlay = new View(act);
        previewHolder.addView(previewOverlay, new FrameLayout.LayoutParams(MATCH, MATCH));
        attachDrag(previewOverlay, new Runnable() { @Override public void run() { exitMini(); } }, null);

        LinearLayout col = new LinearLayout(act);
        col.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(0, MATCH, 1f);
        clp.leftMargin = dpx(10);
        miniGroup.addView(col, clp);

        miniTitle = new TextView(act);
        miniTitle.setTextColor(C_TXT);
        miniTitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        miniTitle.setSingleLine(true);
        miniTitle.setEllipsize(TextUtils.TruncateAt.END);
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

        // ================= olcu kunci =================
        icGrip = new Ic(act, Ic.GRIP, dp);
        FrameLayout.LayoutParams glp = new FrameLayout.LayoutParams(dpx(34), dpx(34), Gravity.BOTTOM | Gravity.RIGHT);
        card.addView(icGrip, glp);
        icGrip.setOnTouchListener(new View.OnTouchListener() {
            float sx, sy; int sw0, sh0;
            @Override public boolean onTouch(View v, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        sx = e.getRawX(); sy = e.getRawY(); sw0 = curW(); sh0 = curH();
                        if (rectAnim != null) rectAnim.cancel();
                        card.getParent().requestDisallowInterceptTouchEvent(true);
                        return true;
                    case MotionEvent.ACTION_MOVE: {
                        int tx = Math.round(card.getTranslationX()), ty = Math.round(card.getTranslationY());
                        int w = clamp(Math.round(sw0 + (e.getRawX() - sx)), dpx(420), screenW() - tx);
                        int h = clamp(Math.round(sh0 + (e.getRawY() - sy)), dpx(240), screenH() - ty);
                        maximized = false;
                        icMax.setType(Ic.MAX);
                        applyRect(tx, ty, w, h);
                        nw = w; nh = h; nx = tx; ny = ty;
                        return true;
                    }
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        savePrefs();
                        return true;
                }
                return false;
            }
        });

        built = true;
    }

    // ------------------------------------------------------------------ yardimci View-lar
    /** nazik yukleme xetti */
    private static final class Line extends View {
        private final Paint p = new Paint();
        private int pct;
        Line(Context c) { super(c); p.setColor(C_ACC); }
        void set(int v) { pct = v; invalidate(); }
        @Override protected void onDraw(Canvas c) {
            if (pct <= 0 || pct >= 100) return;
            c.drawRect(0, 0, getWidth() * pct / 100f, getHeight(), p);
        }
    }

    /** vektor ikonlar (resurs lazim deyil) */
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

        void setType(int t) { if (type != t) { type = t; invalidate(); } }

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

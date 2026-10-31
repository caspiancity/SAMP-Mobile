package com.samp.mobile.launcher;

import android.animation.ValueAnimator;
import android.app.Activity;
import android.app.ActivityManager;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.StatFs;
import android.text.Editable;
import android.text.InputType;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.text.format.Formatter;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.LinearInterpolator;
import android.view.animation.OvershootInterpolator;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import com.samp.mobile.game.SAMP;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * LOBI: oyun acilanda ilk bu ekran gorunur (LAUNCHER).
 *  - oyunci adi (settings.ini -> [client] name)
 *  - data yoxlanisi (data yoxdursa oyun BASLAMIR, crash olmur)
 *  - ayarlar (settings.ini), aletler, haqqinda
 *  - BASLA -> oyun OFFLINE rejimde basladilir (SAMP activity, extra "offline_mode"=true)
 *
 *  QEYD: Onlayn rejim və server seçimi UI-dən çıxarılıb (onlayn tərəf hazırda inkişaf
 *  mərhələsindədir). Oyun local/offline rejimdə başladılır. /tp, /m, /ayarlar komandaları,
 *  NPC spawn məntiqi və musiqi slotlarının faktiki çalınması bu fayldan kənarda, oyun
 *  tərəfində (SAMP.java / native tərəf) işlənməlidir — bax: handoff qeydləri.
 *
 * Hec bir XML/resurs lazim deyil, butun UI proqramla qurulur.
 */
public class LobbyActivity extends Activity {

    // ============================== sabitler (lazim olsa deyis) ==============================
    /** Data qovluğunun minimum həcmi. 0 = həcm yoxlanmasın. */
    private static final long MIN_DATA_BYTES = 200L * 1024 * 1024;
    /** Data qovluğunda (getExternalFilesDir) mütləq olmalı fayllar. */
    private static final String[] REQUIRED_FILES = { "SAMP/main.scm" };
    private static final String NAME_REGEX = "^[A-Za-z0-9_\\[\\]\\$=@\\(\\)\\.]{3,20}$";
    private static final String[] CHANGELOG = {
            "Offline rejim: local serverdə NPC-lərlə oynama sistemi",
            "/tp sistemi — fərqli nöqtələrə teleport",
            "/m sistemi — menyulu maşın seçimi",
            "/ayarlar — silah, skin, godmode, star və s. idarəetməsi",
            "Arxa fon musiqisi: 3 slot, səs səviyyəsi tənzimlənə bilən",
            "Daha sürətli, daha axıcı animasiyalı interfeys",
    };

    private static final int C_PANEL = 0xFF14161C, C_CARD = 0xFF1A1D25, C_FIELD = 0xFF232733,
            C_TXT = 0xFFE8EAF0, C_HINT = 0xFF8B92A3, C_ACC = 0xFF4E8EEB, C_OK = 0xFF4CAF7A, C_BAD = 0xFFE5534B;

    private static final int MATCH = ViewGroup.LayoutParams.MATCH_PARENT, WRAP = ViewGroup.LayoutParams.WRAP_CONTENT;

    private static final String DEFAULT_INI =
            "[client]\nname = \nmode = offline\n\n"
          + "[debug]\ndebug = true\nonline = false\n\n"
          + "[offline]\nnpc = true\n\n"
          + "[music]\nslot1 = \nslot1vol = 40\nslot2 = \nslot2vol = 40\nslot3 = \nslot3vol = 40\n\n"
          + "[gui]\nandroidkeyboard = false\nautoaim = true\nfps = 90\nFPSLimit = 90\nChatMaxMessages = 10\n"
          + "Font = arial.ttf\nFontSize = 30.000000\nFontOutline = 2\nChatPosX = 325.000000\nChatPosY = 25.000000\n"
          + "ChatSizeX = 1150.000000\nChatSizeY = 220.000000\nHealthBarWidth = 60.000000\nHealthBarHeight = 10.000000\n"
          + "MicrophoneSize = 150.000000\nMicrophoneX = 1450.000000\nMicrophoneY = 600.000000\n";

    // ============================== veziyyet ==============================
    private float dp;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private IniFile ini;
    private DataStatus data;                // null = yoxlanilir
    private boolean starting;
    private int animIdx;                    // kart giris animasiyasi ucun sayğac

    private FrameLayout pageHost;
    private final View[] pages = new View[5];
    private final NavItem[] navs = new NavItem[5];
    private int cur = -1;
    private TextView banner;
    private Runnable bannerHide;

    // yenilenen elementler
    private EditText nameField;
    private TextView nameHelp;
    private StartBtn startBtn;
    private TextView homeDataTxt, homeModeTxt, homePerfTxt;
    private View homeDataDot, sideDot;
    private TextView sideDataTxt;
    private TextView dataStatusTxt, dataPathTxt, dataFilesTxt;

    private int dp(float v) { return Math.round(v * dp); }

    // ============================== yasam dovru ==============================
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        dp = getResources().getDisplayMetrics().density;

        ini = new IniFile(iniFile());
        ensureIni();

        FrameLayout root = new FrameLayout(this);
        root.addView(new Backdrop(this), new FrameLayout.LayoutParams(MATCH, MATCH));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.HORIZONTAL);
        root.addView(content, new FrameLayout.LayoutParams(MATCH, MATCH));

        content.addView(buildSidebar(), new LinearLayout.LayoutParams(dp(210), MATCH));

        pageHost = new FrameLayout(this);
        content.addView(pageHost, new LinearLayout.LayoutParams(0, MATCH, 1f));

        banner = new TextView(this);
        banner.setTextColor(C_TXT);
        banner.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        banner.setPadding(dp(18), dp(11), dp(18), dp(11));
        banner.setBackground(rr(0xF2262A35, 12, 0x33FFFFFF));
        banner.setAlpha(0f);
        FrameLayout.LayoutParams blp = new FrameLayout.LayoutParams(WRAP, WRAP, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        blp.bottomMargin = dp(22);
        root.addView(banner, blp);

        setContentView(root);

        showPage(0);
        scanData();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                  | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        starting = false;
        if (startBtn != null) startBtn.setBusy(false);
        scanData();
    }

    // ============================== fayl / ini ==============================
    private File filesDir() {
        File d = getExternalFilesDir(null);
        return d != null ? d : getFilesDir();
    }

    private File iniFile() { return new File(filesDir(), "SAMP/settings.ini"); }

    private void ensureIni() {
        File f = iniFile();
        if (f.exists() && f.length() > 0) return;
        try {
            File p = f.getParentFile();
            if (p != null && !p.exists()) p.mkdirs();
            Writer w = new OutputStreamWriter(new FileOutputStream(f), "UTF-8");
            w.write(DEFAULT_INI);
            w.close();
            ini.load();
        } catch (IOException ignored) { }
    }

    private void iniSet(String sec, String key, String val) {
        ini.set(sec, key, val);
        ini.save();
        refreshHome();
    }

    private static String f6(float v) { return String.format(Locale.US, "%.6f", v); }

    /** Sade INI: sirani, serhleri, sirasini saxlayir. */
    static final class IniFile {
        private final File file;
        private final List<String> lines = new ArrayList<>();

        IniFile(File f) { file = f; load(); }

        void load() {
            lines.clear();
            if (!file.exists()) return;
            try {
                BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(file), "UTF-8"));
                String l;
                while ((l = br.readLine()) != null) lines.add(l);
                br.close();
            } catch (IOException ignored) { }
        }

        private static String keyOf(String line) {
            int eq = line.indexOf('=');
            return eq < 0 ? null : line.substring(0, eq).trim();
        }

        private static String sectionOf(String line) {
            String t = line.trim();
            return (t.startsWith("[") && t.endsWith("]")) ? t.substring(1, t.length() - 1).trim() : null;
        }

        String get(String sec, String key, String def) {
            String cur = "";
            for (String l : lines) {
                String s = sectionOf(l);
                if (s != null) { cur = s; continue; }
                if (!cur.equalsIgnoreCase(sec)) continue;
                String k = keyOf(l);
                if (k != null && k.equalsIgnoreCase(key)) return l.substring(l.indexOf('=') + 1).trim();
            }
            return def;
        }

        void set(String sec, String key, String val) {
            String cur = "";
            int lastInSec = -1, secStart = -1;
            for (int i = 0; i < lines.size(); i++) {
                String l = lines.get(i);
                String s = sectionOf(l);
                if (s != null) { cur = s; if (cur.equalsIgnoreCase(sec)) { secStart = i; lastInSec = i; } continue; }
                if (!cur.equalsIgnoreCase(sec)) continue;
                lastInSec = i;
                String k = keyOf(l);
                if (k != null && k.equalsIgnoreCase(key)) { lines.set(i, k + " = " + val); return; }
            }
            if (secStart >= 0) {
                int pos = lastInSec + 1;
                while (pos > secStart + 1 && lines.get(pos - 1).trim().isEmpty()) pos--;
                lines.add(pos, key + " = " + val);
            } else {
                if (!lines.isEmpty() && !lines.get(lines.size() - 1).trim().isEmpty()) lines.add("");
                lines.add("[" + sec + "]");
                lines.add(key + " = " + val);
            }
        }

        boolean save() {
            try {
                File p = file.getParentFile();
                if (p != null && !p.exists()) p.mkdirs();
                Writer w = new OutputStreamWriter(new FileOutputStream(file), "UTF-8");
                for (String l : lines) { w.write(l); w.write("\n"); }
                w.close();
                return true;
            } catch (IOException e) {
                return false;
            }
        }
    }

    // ============================== data yoxlanisi ==============================
    static final class DataStatus {
        boolean ok, mainOk;
        long bytes;
        String reason = "";
        List<String> missing = new ArrayList<>();
    }

    private static long folderSize(File f) {
        if (f == null || !f.exists()) return 0;
        if (f.isFile()) return f.length();
        File[] list = f.listFiles();
        if (list == null) return 0;
        long s = 0;
        for (File c : list) s += folderSize(c);
        return s;
    }

    private DataStatus checkData() {
        DataStatus st = new DataStatus();
        File dir = filesDir();
        for (String rel : REQUIRED_FILES) {
            if (!new File(dir, rel).exists()) st.missing.add(rel);
        }
        st.mainOk = st.missing.isEmpty();
        st.bytes = folderSize(dir);
        boolean sizeOk = MIN_DATA_BYTES <= 0 || st.bytes >= MIN_DATA_BYTES;
        st.ok = st.mainOk && sizeOk;
        if (!st.mainOk) st.reason = "Çatışmayan fayl: " + st.missing.get(0);
        else if (!sizeOk) st.reason = "Data həcmi çox azdır";
        return st;
    }

    private void scanData() {
        data = null;
        applyData();
        new Thread(new Runnable() {
            @Override public void run() {
                final DataStatus s = checkData();
                runOnUiThread(new Runnable() { @Override public void run() { data = s; applyData(); } });
            }
        }).start();
    }

    private void applyData() {
        boolean scanning = data == null;
        int col = scanning ? C_HINT : (data.ok ? C_OK : C_BAD);
        String sz = scanning ? "" : Formatter.formatShortFileSize(this, data.bytes);
        String head = scanning ? "Yoxlanılır..." : (data.ok ? "Hazırdır" : "Data tapılmadı");

        if (sideDot != null) sideDot.setBackground(oval(col));
        if (sideDataTxt != null) sideDataTxt.setText(scanning ? "Data yoxlanılır" : (data.ok ? "Hazırdır" : "Data yoxdur"));
        if (homeDataDot != null) homeDataDot.setBackground(oval(col));
        if (homeDataTxt != null) homeDataTxt.setText(scanning ? head : (data.ok ? head + " · " + sz : head + " · " + data.reason));
        if (dataStatusTxt != null) { dataStatusTxt.setText(scanning ? head : (data.ok ? head + " · " + sz : head)); dataStatusTxt.setTextColor(col); }
        if (dataPathTxt != null) dataPathTxt.setText(filesDir().getAbsolutePath());
        if (dataFilesTxt != null) dataFilesTxt.setText(filesReport());
        if (startBtn != null) {
            if (scanning) startBtn.setState(false, "Yoxlanılır...");
            else if (!data.ok) startBtn.setState(false, "DATA YOXDUR");
            else startBtn.setState(true, "OFFLİNE BAŞLA");
        }
    }

    private String filesReport() {
        if (data == null) return "Yoxlanılır...";
        StringBuilder sb = new StringBuilder();
        for (String rel : REQUIRED_FILES) {
            boolean ok = !data.missing.contains(rel);
            sb.append(ok ? "✓  " : "✗  ").append(rel).append(ok ? "" : "   (yoxdur)").append('\n');
        }
        boolean szOk = MIN_DATA_BYTES <= 0 || data.bytes >= MIN_DATA_BYTES;
        sb.append(szOk ? "✓  " : "✗  ").append("Həcm: ").append(Formatter.formatShortFileSize(this, data.bytes));
        if (MIN_DATA_BYTES > 0) sb.append("  (minimum ").append(Formatter.formatShortFileSize(this, MIN_DATA_BYTES)).append(")");
        return sb.toString();
    }

    // ============================== baslat ==============================
    private void onStartPressed() {
        if (starting || data == null) return;
        String name = nameField.getText().toString().trim();
        if (!name.matches(NAME_REGEX)) {
            nameHelp.setText("Ad 3-20 simvol olmalıdır: A-Z a-z 0-9 _ [ ] $ = @ ( ) .");
            nameHelp.setTextColor(C_BAD);
            shake(nameField);
            return;
        }
        iniSet("client", "name", name);
        iniSet("client", "mode", "offline");

        if (!data.ok) {
            banner("Data tapılmadı. Data bölməsinə bax.");
            showPage(1);
            return;
        }

        starting = true;
        startBtn.setBusy(true);
        ui.postDelayed(new Runnable() {
            @Override public void run() {
                try {
                    Intent it = new Intent(LobbyActivity.this, SAMP.class);
                    it.putExtra("offline_mode", true);
                    startActivity(it);
                    overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out);
                    finish();
                } catch (Throwable t) {
                    starting = false;
                    startBtn.setBusy(false);
                    banner("Oyunu başlatmaq alınmadı: " + t.getClass().getSimpleName());
                }
            }
        }, 650);
    }

    private void refreshHome() {
        if (homeModeTxt != null) {
            homeModeTxt.setText("Offline · Local server");
        }
        if (homePerfTxt != null) {
            homePerfTxt.setText("FPS limit: " + ini.get("gui", "FPSLimit", "90") + "   ·   Şrift: " + ini.get("gui", "FontSize", "30").replaceAll("\\.0+$", ""));
        }
    }

    // ============================== naviqasiya ==============================
    private static final String[] NAV_TITLES = { "Başla", "Data", "Ayarlar", "Alətlər", "Haqqında" };

    private View buildSidebar() {
        LinearLayout sb = new LinearLayout(this);
        sb.setOrientation(LinearLayout.VERTICAL);
        sb.setBackgroundColor(0x99101218);
        sb.setPadding(dp(16), dp(22), dp(12), dp(16));

        LinearLayout brand = new LinearLayout(this);
        brand.setOrientation(LinearLayout.HORIZONTAL);
        brand.setGravity(Gravity.CENTER_VERTICAL);
        View bar = new View(this);
        bar.setBackground(rr(C_ACC, 2, 0));
        brand.addView(bar, new LinearLayout.LayoutParams(dp(4), dp(34)));
        LinearLayout bt = new LinearLayout(this);
        bt.setOrientation(LinearLayout.VERTICAL);
        bt.setPadding(dp(10), 0, 0, 0);
        bt.addView(text("APACHE ONLINE", 20, C_TXT, true));
        bt.addView(text("Mobile · v" + versionName() + " · Offline", 12, C_HINT, false));
        brand.addView(bt);
        sb.addView(brand, new LinearLayout.LayoutParams(MATCH, WRAP));

        View gap = new View(this);
        sb.addView(gap, new LinearLayout.LayoutParams(MATCH, dp(22)));

        for (int i = 0; i < 5; i++) {
            final int idx = i;
            navs[i] = new NavItem(this, i, NAV_TITLES[i], dp);
            navs[i].setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { showPage(idx); } });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(MATCH, dp(46));
            lp.bottomMargin = dp(4);
            sb.addView(navs[i], lp);
        }

        View fill = new View(this);
        sb.addView(fill, new LinearLayout.LayoutParams(MATCH, 0, 1f));

        LinearLayout chip = new LinearLayout(this);
        chip.setOrientation(LinearLayout.HORIZONTAL);
        chip.setGravity(Gravity.CENTER_VERTICAL);
        chip.setBackground(rr(0x1FFFFFFF, 10, 0));
        chip.setPadding(dp(12), dp(10), dp(12), dp(10));
        sideDot = new View(this);
        sideDot.setBackground(oval(C_HINT));
        chip.addView(sideDot, new LinearLayout.LayoutParams(dp(9), dp(9)));
        sideDataTxt = text("Data yoxlanılır", 13, C_TXT, false);
        sideDataTxt.setPadding(dp(9), 0, 0, 0);
        chip.addView(sideDataTxt);
        sb.addView(chip, new LinearLayout.LayoutParams(MATCH, WRAP));
        return sb;
    }

    private void showPage(int idx) {
        if (idx == cur) return;
        View next = pages[idx];
        if (next == null) { next = buildPage(idx); pages[idx] = next; }
        if (next.getParent() != null) ((ViewGroup) next.getParent()).removeView(next);
        pageHost.addView(next, new FrameLayout.LayoutParams(MATCH, MATCH));
        next.setAlpha(0f);
        next.setTranslationX(dp(26));
        next.setScaleX(0.985f);
        next.setScaleY(0.985f);
        next.animate().alpha(1f).translationX(0f).scaleX(1f).scaleY(1f).setDuration(260)
                .setInterpolator(new DecelerateInterpolator(1.8f)).start();

        if (cur >= 0) {
            final View old = pages[cur];
            old.animate().alpha(0f).translationX(-dp(10)).setDuration(140).withEndAction(new Runnable() {
                @Override public void run() { if (old.getParent() == pageHost && old != pages[cur]) pageHost.removeView(old); }
            }).start();
        }
        cur = idx;
        for (int i = 0; i < 5; i++) navs[i].setSelectedAnimated(i == idx);
        applyData();
        refreshHome();
        hideIme();
    }

    private View buildPage(int idx) {
        switch (idx) {
            case 0: return buildHome();
            case 1: return buildData();
            case 2: return buildSettings();
            case 3: return buildTools();
            default: return buildAbout();
        }
    }

    // ============================== kart giris animasiyasi ==============================
    private void animIn(View v) {
        final int i = animIdx++;
        v.setAlpha(0f);
        v.setTranslationY(dp(16));
        v.animate().alpha(1f).translationY(0f).setStartDelay(Math.min(i, 6) * 45L).setDuration(280)
                .setInterpolator(new DecelerateInterpolator(1.6f)).start();
    }

    // ============================== SEHIFE: BASLA ==============================
    private View buildHome() {
        animIdx = 0;
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(dp(26), dp(24), dp(26), dp(22));

        LinearLayout left = new LinearLayout(this);
        left.setOrientation(LinearLayout.VERTICAL);
        left.setGravity(Gravity.CENTER_VERTICAL);
        left.setPadding(0, 0, dp(22), 0);

        left.addView(text("Xoş gəldin", 32, C_TXT, true));
        TextView sub = text("Sadəcə ad qoy və offline rejimdə başla!", 15, C_HINT, false);
        sub.setPadding(0, dp(4), 0, dp(22));
        left.addView(sub);

        left.addView(label("OYUNÇU ADI"));
        nameField = field("Ad (3-20 simvol)");
        nameField.setText(ini.get("client", "name", ""));
        nameField.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        nameField.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void afterTextChanged(Editable e) {
                String n = e.toString().trim();
                if (n.isEmpty()) { nameHelp.setText("Ad lazımdır"); nameHelp.setTextColor(C_HINT); }
                else if (n.matches(NAME_REGEX)) { nameHelp.setText("Ad uyğundur"); nameHelp.setTextColor(C_OK); }
                else { nameHelp.setText("3-20 simvol: A-Z a-z 0-9 _ [ ] $ = @ ( ) ."); nameHelp.setTextColor(C_BAD); }
            }
        });
        left.addView(nameField, new LinearLayout.LayoutParams(MATCH, dp(48)));
        nameHelp = text(nameField.getText().length() == 0 ? "Ad lazımdır" : "", 12, C_HINT, false);
        nameHelp.setPadding(dp(4), dp(6), 0, dp(20));
        left.addView(nameHelp);

        startBtn = new StartBtn(this, dp);
        startBtn.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { onStartPressed(); } });
        left.addView(startBtn, new LinearLayout.LayoutParams(MATCH, dp(58)));
        row.addView(left, new LinearLayout.LayoutParams(0, MATCH, 1.25f));

        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        right.setGravity(Gravity.CENTER_VERTICAL);

        LinearLayout c1 = card();
        c1.addView(label("DATA"));
        LinearLayout r1 = new LinearLayout(this);
        r1.setGravity(Gravity.CENTER_VERTICAL);
        homeDataDot = new View(this);
        homeDataDot.setBackground(oval(C_HINT));
        r1.addView(homeDataDot, new LinearLayout.LayoutParams(dp(10), dp(10)));
        homeDataTxt = text("Yoxlanılır...", 14, C_TXT, false);
        homeDataTxt.setPadding(dp(10), 0, 0, 0);
        r1.addView(homeDataTxt);
        c1.addView(r1);
        right.addView(c1, cardLp());
        animIn(c1);

        LinearLayout c2 = card();
        c2.addView(label("REJİM"));
        homeModeTxt = text("", 14, C_TXT, false);
        c2.addView(homeModeTxt);
        right.addView(c2, cardLp());
        animIn(c2);

        LinearLayout c3 = card();
        c3.addView(label("PERFORMANS"));
        homePerfTxt = text("", 14, C_TXT, false);
        c3.addView(homePerfTxt);
        right.addView(c3, cardLp());
        animIn(c3);

        row.addView(right, new LinearLayout.LayoutParams(0, MATCH, 1f));

        applyData();
        refreshHome();
        return row;
    }

    // ============================== SEHIFE: DATA ==============================
    private View buildData() {
        animIdx = 0;
        LinearLayout col = pageColumn("Data", "Oyun faylları və yoxlanış");

        LinearLayout c = card();
        dataStatusTxt = text("Yoxlanılır...", 20, C_TXT, true);
        c.addView(dataStatusTxt);
        dataPathTxt = text("", 12, C_HINT, false);
        dataPathTxt.setPadding(0, dp(6), 0, dp(14));
        c.addView(dataPathTxt);

        LinearLayout btns = new LinearLayout(this);
        btns.setOrientation(LinearLayout.HORIZONTAL);
        btns.addView(button("Yenidən yoxla", false, new View.OnClickListener() {
            @Override public void onClick(View v) { scanData(); banner("Data yoxlanılır..."); }
        }), btnLp());
        btns.addView(button("Yolu kopyala", false, new View.OnClickListener() {
            @Override public void onClick(View v) { copy(filesDir().getAbsolutePath()); banner("Yol kopyalandı"); }
        }), btnLp());
        c.addView(btns);
        col.addView(c, cardLp());
        animIn(c);

        LinearLayout c2 = card();
        c2.addView(label("YOXLANIŞ"));
        dataFilesTxt = text("", 13, C_TXT, false);
        dataFilesTxt.setTypeface(Typeface.MONOSPACE);
        dataFilesTxt.setLineSpacing(dp(3), 1f);
        c2.addView(dataFilesTxt);
        col.addView(c2, cardLp());
        animIn(c2);

        applyData();
        return scroll(col);
    }

    // ============================== SEHIFE: AYARLAR ==============================
    private View buildSettings() {
        animIdx = 0;
        LinearLayout col = pageColumn("Ayarlar", "Dəyişikliklər avtomatik saxlanılır və növbəti oyun açılışında tətbiq olunur");

        // --- qrafika ---
        LinearLayout g1 = card();
        g1.addView(label("QRAFİKA"));
        g1.addView(text("FPS limiti", 14, C_TXT, false));
        final int[] fpsVals = { 30, 60, 90, 120, 144 };
        int curFps = parseInt(ini.get("gui", "FPSLimit", "90"), 90);
        final TextView[] chips = new TextView[fpsVals.length];
        LinearLayout chipRow = new LinearLayout(this);
        chipRow.setPadding(0, dp(10), 0, 0);
        for (int i = 0; i < fpsVals.length; i++) {
            final int v = fpsVals[i];
            final int ci = i;
            chips[i] = chip(String.valueOf(v), v == curFps);
            chips[i].setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View x) {
                    for (int k = 0; k < chips.length; k++) styleChip(chips[k], k == ci);
                    iniSet("gui", "FPSLimit", String.valueOf(v));
                }
            });
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(58), dp(38));
            lp.rightMargin = dp(8);
            chipRow.addView(chips[i], lp);
        }
        g1.addView(chipRow);
        col.addView(g1, cardLp());
        animIn(g1);

        // --- interfeys ---
        LinearLayout g2 = card();
        g2.addView(label("İNTERFEYS"));
        addSlider(g2, "Şrift ölçüsü", "FontSize", 20, 45, true, "");
        addSlider(g2, "Şrift konturu", "FontOutline", 0, 4, false, "");
        addSlider(g2, "Çat mesaj sayı", "ChatMaxMessages", 5, 30, false, "");
        addSlider(g2, "Can zolağı eni", "HealthBarWidth", 30, 120, true, "");
        addSlider(g2, "Can zolağı hündürlüyü", "HealthBarHeight", 5, 20, true, "");
        addSlider(g2, "Mikrofon ölçüsü", "MicrophoneSize", 100, 250, true, "");
        col.addView(g2, cardLp());
        animIn(g2);

        // --- nezaret ---
        LinearLayout g3 = card();
        g3.addView(label("NƏZARƏT"));
        addSwitch(g3, "Android klaviaturası", "gui", "androidkeyboard");
        addSwitch(g3, "Avto-hədəf", "gui", "autoaim");
        col.addView(g3, cardLp());
        animIn(g3);

        // --- offline ---
        LinearLayout g4 = card();
        g4.addView(label("OFFLİNE REJİM"));
        TextView offInfo = text("Onlayn rejim hazırda inkişaf mərhələsindədir və bağlıdır. Oyun yalnız local serverdə, NPC-lərlə offline rejimdə başlayır.", 13, C_HINT, false);
        offInfo.setPadding(0, 0, 0, dp(10));
        g4.addView(offInfo);
        addSwitch(g4, "NPC-lər aktiv", "offline", "npc");
        col.addView(g4, cardLp());
        animIn(g4);

        // --- musiqi ---
        LinearLayout g5 = card();
        g5.addView(label("ARXA FON MUSİQİSİ  (3 slot)"));
        TextView musInfo = text("Hər slota mahnının fayl yolunu yaz. Səs səviyyəsi aşağı saxlanılır ki, oyunu batırmasın.", 13, C_HINT, false);
        musInfo.setPadding(0, 0, 0, dp(10));
        g5.addView(musInfo);
        addMusicSlot(g5, "Slot 1", "slot1", "slot1vol");
        addMusicSlot(g5, "Slot 2", "slot2", "slot2vol");
        addMusicSlot(g5, "Slot 3", "slot3", "slot3vol");
        col.addView(g5, cardLp());
        animIn(g5);

        // --- sifirla ---
        LinearLayout g6 = card();
        g6.addView(label("SIFIRLA"));
        g6.addView(button("İnterfeys və qrafikanı standarta qaytar", false, new View.OnClickListener() {
            @Override public void onClick(View v) { resetGui(); banner("Standart dəyərlər qaytarıldı"); pages[2] = null; rebuildPage(2); }
        }), btnLp());
        col.addView(g6, cardLp());
        animIn(g6);

        return scroll(col);
    }

    private void addSlider(LinearLayout parent, String title, final String key, float min, float max, final boolean decimal, String unit) {
        float cur = parseFloat(ini.get("gui", key, String.valueOf(min)), min);
        LinearLayout head = new LinearLayout(this);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(0, dp(10), 0, 0);
        head.addView(text(title, 14, C_TXT, false), new LinearLayout.LayoutParams(0, WRAP, 1f));
        final TextView val = text(fmtVal(cur, decimal), 14, C_ACC, true);
        head.addView(val);
        parent.addView(head);
        Sl sl = new Sl(this, dp, min, max, cur);
        sl.setListener(new Sl.Listener() {
            @Override public void onValue(float v, boolean fin) {
                val.setText(fmtVal(v, decimal));
                if (fin) iniSet("gui", key, decimal ? f6(v) : String.valueOf(Math.round(v)));
            }
        });
        parent.addView(sl, new LinearLayout.LayoutParams(MATCH, dp(34)));
    }

    private void addSwitch(LinearLayout parent, String title, final String sec, final String key) {
        LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(10), 0, dp(4));
        row.addView(text(title, 14, C_TXT, false), new LinearLayout.LayoutParams(0, WRAP, 1f));
        boolean on = "true".equalsIgnoreCase(ini.get(sec, key, "false")) || "1".equals(ini.get(sec, key, "0"));
        final Sw sw = new Sw(this, dp, on);
        sw.setListener(new Sw.Listener() { @Override public void onChange(boolean c) { iniSet(sec, key, c ? "true" : "false"); } });
        row.addView(sw, new LinearLayout.LayoutParams(dp(52), dp(30)));
        parent.addView(row);
    }

    /** Musiqi slotu: fayl yolu sahəsi + həcm slayderi. Faktiki çalma oyun tərəfində edilməlidir. */
    private void addMusicSlot(LinearLayout parent, String title, final String pathKey, final String volKey) {
        LinearLayout wrap = new LinearLayout(this);
        wrap.setOrientation(LinearLayout.VERTICAL);
        wrap.setPadding(0, dp(10), 0, dp(4));
        wrap.addView(text(title, 13, C_HINT, true));

        final EditText path = field("Mahnı faylının yolu (məs: Music/slot1.mp3)");
        path.setText(ini.get("music", pathKey, ""));
        path.setSingleLine(true);
        LinearLayout.LayoutParams plp = new LinearLayout.LayoutParams(MATCH, dp(44));
        plp.topMargin = dp(6);
        wrap.addView(path, plp);
        path.setOnFocusChangeListener(new View.OnFocusChangeListener() {
            @Override public void onFocusChange(View v, boolean hasFocus) {
                if (!hasFocus) iniSet("music", pathKey, path.getText().toString().trim());
            }
        });

        LinearLayout volRow = new LinearLayout(this);
        volRow.setGravity(Gravity.CENTER_VERTICAL);
        volRow.setPadding(0, dp(6), 0, 0);
        volRow.addView(text("Səs", 12, C_HINT, false), new LinearLayout.LayoutParams(dp(34), WRAP));
        float curVol = parseFloat(ini.get("music", volKey, "40"), 40);
        final TextView volVal = text(Math.round(curVol) + "%", 12, C_ACC, true);
        Sl vs = new Sl(this, dp, 0, 100, curVol);
        vs.setListener(new Sl.Listener() {
            @Override public void onValue(float v, boolean fin) {
                volVal.setText(Math.round(v) + "%");
                if (fin) iniSet("music", volKey, String.valueOf(Math.round(v)));
            }
        });
        volRow.addView(vs, new LinearLayout.LayoutParams(0, dp(30), 1f));
        LinearLayout.LayoutParams vvlp = new LinearLayout.LayoutParams(dp(42), WRAP);
        vvlp.leftMargin = dp(8);
        volRow.addView(volVal, vvlp);
        wrap.addView(volRow);

        parent.addView(wrap);
    }

    private void resetGui() {
        IniFile def = new IniFile(new File("/nonexistent"));
        for (String l : DEFAULT_INI.split("\n")) def.lines.add(l);
        String[] keys = { "androidkeyboard", "autoaim", "FPSLimit", "ChatMaxMessages", "Font", "FontSize", "FontOutline",
                "ChatPosX", "ChatPosY", "ChatSizeX", "ChatSizeY", "HealthBarWidth", "HealthBarHeight", "MicrophoneSize", "MicrophoneX", "MicrophoneY" };
        for (String k : keys) ini.set("gui", k, def.get("gui", k, ""));
        ini.save();
        refreshHome();
    }

    private void rebuildPage(int idx) {
        if (cur != idx) return;
        View old = pageHost.getChildAt(pageHost.getChildCount() - 1);
        View fresh = buildPage(idx);
        pages[idx] = fresh;
        pageHost.removeView(old);
        pageHost.addView(fresh, new FrameLayout.LayoutParams(MATCH, MATCH));
    }

    // ============================== SEHIFE: ALETLER ==============================
    // Yalnız əsl faydası olan alətlər saxlanılıb; onlayn/server ilə bağlı olanlar çıxarılıb.
    private View buildTools() {
        animIdx = 0;
        LinearLayout col = pageColumn("Alətlər", "Faydalı sistem funksiyaları");

        addTool(col, "Cihaz məlumatı", "Model, Android, RAM, yaddaş", "Kopyala", new Runnable() {
            @Override public void run() { String s = deviceInfo(); copy(s); banner(s.replace('\n', ' ').length() > 90 ? "Cihaz məlumatı kopyalandı" : s); }
        });
        addTool(col, "Keşi təmizlə", "Müvəqqəti faylları silir (data silinmir)", "Təmizlə", new Runnable() {
            @Override public void run() {
                long before = folderSize(getCacheDir()) + folderSize(getExternalCacheDir());
                deleteContents(getCacheDir());
                deleteContents(getExternalCacheDir());
                banner("Təmizləndi: " + Formatter.formatShortFileSize(LobbyActivity.this, before));
            }
        });
        addTool(col, "Ayarların ehtiyat nüsxəsi", "settings.ini → settings.ini.bak", "Saxla", new Runnable() {
            @Override public void run() {
                banner(copyFile(iniFile(), new File(iniFile().getParentFile(), "settings.ini.bak")) ? "Ehtiyat nüsxə saxlanıldı" : "Alınmadı");
            }
        });
        addTool(col, "Ehtiyat nüsxəni bərpa et", "settings.ini.bak → settings.ini", "Bərpa et", new Runnable() {
            @Override public void run() {
                File bak = new File(iniFile().getParentFile(), "settings.ini.bak");
                if (!bak.exists()) { banner("Ehtiyat nüsxə tapılmadı"); return; }
                boolean ok = copyFile(bak, iniFile());
                ini.load();
                if (nameField != null) nameField.setText(ini.get("client", "name", ""));
                pages[2] = null;
                rebuildPage(2);
                refreshHome();
                banner(ok ? "Ayarlar bərpa olundu" : "Bərpa alınmadı");
            }
        });
        return scroll(col);
    }

    private void addTool(LinearLayout parent, String title, String desc, String action, final Runnable run) {
        LinearLayout c = card();
        c.setOrientation(LinearLayout.HORIZONTAL);
        c.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout t = new LinearLayout(this);
        t.setOrientation(LinearLayout.VERTICAL);
        t.addView(text(title, 15, C_TXT, true));
        TextView d = text(desc, 12, C_HINT, false);
        d.setPadding(0, dp(2), 0, 0);
        t.addView(d);
        c.addView(t, new LinearLayout.LayoutParams(0, WRAP, 1f));
        c.addView(button(action, false, new View.OnClickListener() { @Override public void onClick(View v) { run.run(); } }),
                new LinearLayout.LayoutParams(WRAP, dp(40)));
        parent.addView(c, cardLp());
        animIn(c);
    }

    private String deviceInfo() {
        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        if (am != null) am.getMemoryInfo(mi);
        long free = 0;
        try { StatFs s = new StatFs(filesDir().getAbsolutePath()); free = s.getAvailableBytes(); } catch (Throwable ignored) { }
        String abi = Build.SUPPORTED_ABIS != null && Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?";
        return Build.MANUFACTURER + " " + Build.MODEL + "\nAndroid " + Build.VERSION.RELEASE + " (API " + Build.VERSION.SDK_INT + ")\nABI: " + abi
                + "\nRAM: " + Formatter.formatShortFileSize(this, mi.totalMem) + "\nBoş yaddaş: " + Formatter.formatShortFileSize(this, free)
                + "\nVersiya: " + versionName();
    }

    // ============================== SEHIFE: HAQQINDA ==============================
    private View buildAbout() {
        animIdx = 0;
        LinearLayout col = pageColumn("Haqqında", "Versiya və dəyişikliklər");
        LinearLayout c = card();
        c.addView(text("APACHE ONLINE", 22, C_TXT, true));
        TextView v = text("Versiya " + versionName() + "   ·   " + getPackageName(), 13, C_HINT, false);
        v.setPadding(0, dp(4), 0, 0);
        c.addView(v);
        col.addView(c, cardLp());
        animIn(c);

        LinearLayout c2 = card();
        c2.addView(label("YENİLİKLƏR"));
        for (String s : CHANGELOG) {
            TextView t = text("•  " + s, 14, C_TXT, false);
            t.setPadding(0, dp(4), 0, dp(4));
            c2.addView(t);
        }
        col.addView(c2, cardLp());
        animIn(c2);
        return scroll(col);
    }

    // ============================== yardimci ==============================
    private String versionName() {
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            return pi.versionName == null ? "?" : pi.versionName;
        } catch (Throwable t) {
            return "?";
        }
    }

    private static int parseInt(String s, int d) { try { return Integer.parseInt(s.trim()); } catch (Exception e) { return d; } }
    private static float parseFloat(String s, float d) { try { return Float.parseFloat(s.trim()); } catch (Exception e) { return d; } }
    private static String fmtVal(float v, boolean dec) { return dec ? String.valueOf(Math.round(v)) : String.valueOf(Math.round(v)); }

    private void copy(String s) {
        ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("lobby", s));
    }

    private static boolean copyFile(File a, File b) {
        try {
            InputStream in = new FileInputStream(a);
            OutputStream out = new FileOutputStream(b);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            in.close();
            out.close();
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    private static void deleteContents(File dir) {
        if (dir == null || !dir.exists()) return;
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (f.isDirectory()) deleteContents(f);
            f.delete();
        }
    }

    private void hideIme() {
        android.view.inputmethod.InputMethodManager imm = (android.view.inputmethod.InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        View f = getCurrentFocus();
        if (imm != null && f != null) { imm.hideSoftInputFromWindow(f.getWindowToken(), 0); f.clearFocus(); }
    }

    private void banner(String msg) {
        banner.setText(msg);
        banner.animate().cancel();
        banner.setTranslationY(dp(14));
        banner.animate().alpha(1f).translationY(0f).setDuration(180).setInterpolator(new DecelerateInterpolator()).start();
        if (bannerHide != null) ui.removeCallbacks(bannerHide);
        bannerHide = new Runnable() { @Override public void run() { banner.animate().alpha(0f).translationY(dp(10)).setDuration(220).start(); } };
        ui.postDelayed(bannerHide, 2600);
    }

    private void shake(View v) {
        v.animate().cancel();
        ValueAnimator a = ValueAnimator.ofFloat(0f, 1f);
        a.setDuration(320);
        final View vv = v;
        a.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
            @Override public void onAnimationUpdate(ValueAnimator an) {
                float f = (Float) an.getAnimatedValue();
                vv.setTranslationX((float) (Math.sin(f * Math.PI * 6) * dp(8) * (1f - f)));
            }
        });
        a.start();
    }

    // ---- UI fabrikasi ----
    private GradientDrawable rr(int color, float radiusDp, int stroke) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(color);
        g.setCornerRadius(dp(radiusDp));
        if (stroke != 0) g.setStroke(Math.max(1, dp(1)), stroke);
        return g;
    }

    private GradientDrawable oval(int color) {
        GradientDrawable g = new GradientDrawable();
        g.setShape(GradientDrawable.OVAL);
        g.setColor(color);
        return g;
    }

    private TextView text(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private TextView label(String s) {
        TextView t = text(s, 11, C_HINT, true);
        t.setLetterSpacing(0.12f);
        t.setPadding(0, 0, 0, dp(8));
        return t;
    }

    private LinearLayout card() {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackground(rr(0xCC1A1D25, 14, 0x1AFFFFFF));
        c.setPadding(dp(18), dp(16), dp(18), dp(16));
        return c;
    }

    private LinearLayout.LayoutParams cardLp() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(MATCH, WRAP);
        lp.bottomMargin = dp(12);
        return lp;
    }

    private LinearLayout.LayoutParams btnLp() {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(WRAP, dp(42));
        lp.rightMargin = dp(10);
        return lp;
    }

    private EditText field(String hint) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setHintTextColor(C_HINT);
        e.setTextColor(C_TXT);
        e.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        e.setSingleLine(true);
        e.setPadding(dp(14), 0, dp(14), 0);
        e.setBackground(rr(C_FIELD, 12, 0x14FFFFFF));
        e.setImeOptions(EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        return e;
    }

    private TextView button(String s, boolean primary, View.OnClickListener l) {
        final TextView b = text(s, 14, primary ? Color.WHITE : C_TXT, true);
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(18), 0, dp(18), 0);
        b.setBackground(primary ? rr(C_ACC, 11, 0) : rr(0x14FFFFFF, 11, 0x22FFFFFF));
        b.setOnClickListener(l);
        b.setOnTouchListener(new View.OnTouchListener() {
            @Override public boolean onTouch(View v, MotionEvent e) {
                int a = e.getActionMasked();
                if (a == MotionEvent.ACTION_DOWN) v.animate().scaleX(0.96f).scaleY(0.96f).setDuration(80).start();
                else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) v.animate().scaleX(1f).scaleY(1f).setDuration(120).start();
                return false;
            }
        });
        return b;
    }

    private TextView chip(String s, boolean sel) {
        TextView t = text(s, 14, C_TXT, true);
        t.setGravity(Gravity.CENTER);
        styleChip(t, sel);
        return t;
    }

    private void styleChip(TextView t, boolean sel) {
        t.setBackground(sel ? rr(C_ACC, 10, 0) : rr(0x14FFFFFF, 10, 0x22FFFFFF));
        t.setTextColor(sel ? Color.WHITE : C_TXT);
    }

    private LinearLayout pageColumn(String title, String sub) {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(dp(26), dp(22), dp(26), dp(22));
        col.addView(text(title, 26, C_TXT, true));
        TextView s = text(sub, 13, C_HINT, false);
        s.setPadding(0, dp(3), 0, dp(16));
        col.addView(s);
        return col;
    }

    private View scroll(View child) {
        ScrollView sv = new ScrollView(this);
        sv.setVerticalScrollBarEnabled(false);
        sv.setOverScrollMode(View.OVER_SCROLL_NEVER);
        sv.addView(child, new FrameLayout.LayoutParams(MATCH, WRAP));
        return sv;
    }

    // ============================== ozel View-lar ==============================
    /** yavas hereketli yumsaq arxa fon */
    private static final class Backdrop extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final ValueAnimator anim = ValueAnimator.ofFloat(0f, 1f);
        private float t;
        private LinearGradient lg;
        private int lw, lh;

        Backdrop(Context c) {
            super(c);
            anim.setDuration(45000);
            anim.setRepeatCount(ValueAnimator.INFINITE);
            anim.setInterpolator(new LinearInterpolator());
            anim.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
                @Override public void onAnimationUpdate(ValueAnimator a) { t = (Float) a.getAnimatedValue(); invalidate(); }
            });
        }

        @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); anim.start(); }
        @Override protected void onDetachedFromWindow() { anim.cancel(); super.onDetachedFromWindow(); }

        @Override protected void onDraw(Canvas c) {
            final int w = getWidth(), h = getHeight();
            if (lg == null || lw != w || lh != h) {
                lg = new LinearGradient(0, 0, 0, h, 0xFF0B0D12, 0xFF11141B, Shader.TileMode.CLAMP);
                lw = w; lh = h;
            }
            p.setShader(lg);
            c.drawRect(0, 0, w, h, p);
            final float a = t * (float) (Math.PI * 2);
            blob(c, w * (0.22f + 0.07f * (float) Math.sin(a)), h * (0.28f + 0.10f * (float) Math.cos(a)), h * 0.95f, 0x264E8EEB);
            blob(c, w * (0.82f + 0.06f * (float) Math.cos(a)), h * (0.80f + 0.08f * (float) Math.sin(a)), h * 0.85f, 0x1C3F6FD0);
            blob(c, w * (0.55f + 0.10f * (float) Math.sin(a * 2f)), h * (0.10f + 0.05f * (float) Math.cos(a)), h * 0.55f, 0x143A8F9E);
            p.setShader(null);
        }

        private void blob(Canvas c, float x, float y, float r, int col) {
            p.setShader(new RadialGradient(x, y, r, col, 0x00000000, Shader.TileMode.CLAMP));
            c.drawCircle(x, y, r, p);
        }
    }

    /** yan menyu elementi */
    private static final class NavItem extends View {
        private final int type;
        private final String label;
        private final float dp;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final RectF r = new RectF();
        private float sel;
        private ValueAnimator anim;

        NavItem(Context c, int type, String label, float dp) {
            super(c);
            this.type = type; this.label = label; this.dp = dp;
            setClickable(true);
        }

        void setSelectedAnimated(boolean on) {
            if (anim != null) anim.cancel();
            anim = ValueAnimator.ofFloat(sel, on ? 1f : 0f);
            anim.setDuration(200);
            anim.setInterpolator(new DecelerateInterpolator());
            anim.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
                @Override public void onAnimationUpdate(ValueAnimator a) { sel = (Float) a.getAnimatedValue(); invalidate(); }
            });
            anim.start();
        }

        @Override protected void onDraw(Canvas c) {
            final float w = getWidth(), h = getHeight();
            if (sel > 0.01f) {
                p.setStyle(Paint.Style.FILL);
                p.setColor(Color.argb((int) (34 * sel), 78, 142, 235));
                r.set(0, 0, w, h);
                c.drawRoundRect(r, 11 * dp, 11 * dp, p);
                p.setColor(Color.argb((int) (255 * sel), 78, 142, 235));
                r.set(0, h * 0.28f, 3 * dp, h * 0.72f);
                c.drawRoundRect(r, 2 * dp, 2 * dp, p);
            }
            final int col = blend(0xFF8B92A3, 0xFFE8EAF0, sel);
            p.setColor(col);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(1.9f * dp);
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeJoin(Paint.Join.ROUND);
            final float cx = 24 * dp, cy = h / 2f, u = dp;
            path.reset();
            switch (type) {
                case 0: // play
                    p.setStyle(Paint.Style.FILL);
                    path.moveTo(cx - 4 * u, cy - 7 * u); path.lineTo(cx + 7 * u, cy); path.lineTo(cx - 4 * u, cy + 7 * u); path.close();
                    c.drawPath(path, p);
                    break;
                case 1: // data / disk
                    c.drawRoundRect(cx - 8 * u, cy - 7 * u, cx + 8 * u, cy + 7 * u, 3 * u, 3 * u, p);
                    c.drawLine(cx - 8 * u, cy + 1 * u, cx + 8 * u, cy + 1 * u, p);
                    c.drawLine(cx - 4 * u, cy + 4.5f * u, cx - 1 * u, cy + 4.5f * u, p);
                    break;
                case 2: // slayderler
                    c.drawLine(cx - 8 * u, cy - 4 * u, cx + 8 * u, cy - 4 * u, p);
                    c.drawLine(cx - 8 * u, cy + 4 * u, cx + 8 * u, cy + 4 * u, p);
                    p.setStyle(Paint.Style.FILL);
                    c.drawCircle(cx - 2 * u, cy - 4 * u, 2.6f * u, p);
                    c.drawCircle(cx + 3 * u, cy + 4 * u, 2.6f * u, p);
                    break;
                case 3: // aletler (4 kvadrat)
                    c.drawRoundRect(cx - 8 * u, cy - 8 * u, cx - 1.5f * u, cy - 1.5f * u, 2 * u, 2 * u, p);
                    c.drawRoundRect(cx + 1.5f * u, cy - 8 * u, cx + 8 * u, cy - 1.5f * u, 2 * u, 2 * u, p);
                    c.drawRoundRect(cx - 8 * u, cy + 1.5f * u, cx - 1.5f * u, cy + 8 * u, 2 * u, 2 * u, p);
                    c.drawRoundRect(cx + 1.5f * u, cy + 1.5f * u, cx + 8 * u, cy + 8 * u, 2 * u, 2 * u, p);
                    break;
                default: // info
                    c.drawCircle(cx, cy, 8.5f * u, p);
                    c.drawLine(cx, cy - 0.5f * u, cx, cy + 4 * u, p);
                    p.setStyle(Paint.Style.FILL);
                    c.drawCircle(cx, cy - 4 * u, 1.2f * u, p);
                    break;
            }
            p.setStyle(Paint.Style.FILL);
            p.setTextSize(15 * dp);
            p.setTypeface(sel > 0.5f ? Typeface.DEFAULT_BOLD : Typeface.DEFAULT);
            c.drawText(label, 46 * dp, cy + 5.2f * dp, p);
        }

        private static int blend(int a, int b, float t) {
            return Color.rgb((int) (Color.red(a) + (Color.red(b) - Color.red(a)) * t),
                             (int) (Color.green(a) + (Color.green(b) - Color.green(a)) * t),
                             (int) (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * t));
        }
    }

    /** boyuk OFFLİNE BAŞLA duymesi (yumsaq isiq zolagi animasiyasi ile) */
    private static final class StartBtn extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private final float dp;
        private final ValueAnimator shimmer = ValueAnimator.ofFloat(0f, 1f);
        private float sh, press, busyT;
        private boolean enabledLook = true, busy;
        private String label = "OFFLİNE BAŞLA";

        StartBtn(Context c, float dp) {
            super(c);
            this.dp = dp;
            setClickable(true);
            shimmer.setDuration(2200);
            shimmer.setRepeatCount(ValueAnimator.INFINITE);
            shimmer.setInterpolator(new LinearInterpolator());
            shimmer.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
                @Override public void onAnimationUpdate(ValueAnimator a) {
                    sh = (Float) a.getAnimatedValue();
                    if (busy) busyT += 0.12f;
                    invalidate();
                }
            });
        }

        void setState(boolean ok, String text) { enabledLook = ok; label = text; invalidate(); }

        void setBusy(boolean b) { busy = b; if (b) label = "Yüklənir..."; else if (enabledLook) label = "OFFLİNE BAŞLA"; invalidate(); }

        @Override protected void onAttachedToWindow() { super.onAttachedToWindow(); shimmer.start(); }
        @Override protected void onDetachedFromWindow() { shimmer.cancel(); super.onDetachedFromWindow(); }

        @Override public boolean onTouchEvent(MotionEvent e) {
            int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN) { animate().scaleX(0.975f).scaleY(0.975f).setDuration(90).start(); }
            else if (a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) { animate().scaleX(1f).scaleY(1f).setDuration(140).start(); }
            return super.onTouchEvent(e);
        }

        @Override protected void onDraw(Canvas c) {
            final float w = getWidth(), h = getHeight(), rad = 14 * dp;
            r.set(0, 0, w, h);
            p.setStyle(Paint.Style.FILL);
            p.setShader(null);
            p.setColor(enabledLook ? 0xFF4E8EEB : 0xFF2A2F3B);
            c.drawRoundRect(r, rad, rad, p);

            if (enabledLook && !busy) {
                final float x = -w * 0.3f + (w * 1.6f) * sh;
                p.setShader(new LinearGradient(x, 0, x + w * 0.28f, 0, 0x00FFFFFF, 0x33FFFFFF, Shader.TileMode.CLAMP));
                c.save();
                c.clipPath(roundPath(w, h, rad));
                c.drawRect(x, 0, x + w * 0.28f, h, p);
                c.restore();
                p.setShader(null);
            }

            p.setColor(enabledLook ? Color.WHITE : 0xFF8B92A3);
            p.setTextSize(18 * dp);
            p.setTypeface(Typeface.DEFAULT_BOLD);
            p.setLetterSpacing(0.14f);
            final float tw = p.measureText(label);
            float tx = (w - tw) / 2f;
            if (busy) {
                tx += 10 * dp;
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(2.4f * dp);
                p.setStrokeCap(Paint.Cap.ROUND);
                r.set(tx - 30 * dp, h / 2f - 8 * dp, tx - 14 * dp, h / 2f + 8 * dp);
                c.drawArc(r, busyT * 57f, 250f, false, p);
                p.setStyle(Paint.Style.FILL);
            }
            c.drawText(label, tx, h / 2f + 6.5f * dp, p);
            p.setLetterSpacing(0f);
        }

        private final Path rp = new Path();
        private Path roundPath(float w, float h, float rad) {
            rp.reset();
            rp.addRoundRect(new RectF(0, 0, w, h), rad, rad, Path.Direction.CW);
            return rp;
        }
    }

    /** acar */
    private static final class Sw extends View {
        interface Listener { void onChange(boolean checked); }
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private final float dp;
        private boolean on;
        private float t;
        private Listener l;

        Sw(Context c, float dp, boolean on) {
            super(c);
            this.dp = dp; this.on = on; this.t = on ? 1f : 0f;
            setClickable(true);
            setOnClickListener(new OnClickListener() {
                @Override public void onClick(View v) {
                    Sw.this.on = !Sw.this.on;
                    ValueAnimator a = ValueAnimator.ofFloat(t, Sw.this.on ? 1f : 0f);
                    a.setDuration(180);
                    a.addUpdateListener(new ValueAnimator.AnimatorUpdateListener() {
                        @Override public void onAnimationUpdate(ValueAnimator an) { t = (Float) an.getAnimatedValue(); invalidate(); }
                    });
                    a.start();
                    if (l != null) l.onChange(Sw.this.on);
                }
            });
        }

        void setListener(Listener l) { this.l = l; }

        @Override protected void onDraw(Canvas c) {
            final float w = getWidth(), h = getHeight();
            r.set(0, 0, w, h);
            p.setStyle(Paint.Style.FILL);
            p.setColor(Color.rgb((int) (58 + (78 - 58) * t), (int) (64 + (142 - 64) * t), (int) (80 + (235 - 80) * t)));
            c.drawRoundRect(r, h / 2f, h / 2f, p);
            p.setColor(0xFFF2F4F8);
            c.drawCircle(h / 2f + t * (w - h), h / 2f, h / 2f - 3.5f * dp, p);
        }
    }

    /** slayder */
    private static final class Sl extends View {
        interface Listener { void onValue(float v, boolean fin); }
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private final float dp, min, max;
        private float val;
        private Listener l;

        Sl(Context c, float dp, float min, float max, float val) {
            super(c);
            this.dp = dp; this.min = min; this.max = max;
            this.val = Math.max(min, Math.min(max, val));
        }

        void setListener(Listener l) { this.l = l; }

        @Override public boolean onTouchEvent(MotionEvent e) {
            final float pad = 14 * dp;
            final float f = Math.max(0f, Math.min(1f, (e.getX() - pad) / Math.max(1f, getWidth() - 2 * pad)));
            final int a = e.getActionMasked();
            if (a == MotionEvent.ACTION_DOWN) getParent().requestDisallowInterceptTouchEvent(true);
            if (a == MotionEvent.ACTION_DOWN || a == MotionEvent.ACTION_MOVE || a == MotionEvent.ACTION_UP) {
                val = Math.round(min + f * (max - min));
                invalidate();
                if (l != null) l.onValue(val, a == MotionEvent.ACTION_UP);
                return true;
            }
            return a == MotionEvent.ACTION_CANCEL;
        }

        @Override protected void onDraw(Canvas c) {
            final float w = getWidth(), h = getHeight(), pad = 14 * dp;
            final float f = (val - min) / (max - min);
            final float x = pad + f * (w - 2 * pad), cy = h / 2f;
            p.setStyle(Paint.Style.FILL);
            p.setColor(0x22FFFFFF);
            r.set(pad, cy - 3 * dp, w - pad, cy + 3 * dp);
            c.drawRoundRect(r, 3 * dp, 3 * dp, p);
            p.setColor(0xFF4E8EEB);
            r.set(pad, cy - 3 * dp, x, cy + 3 * dp);
            c.drawRoundRect(r, 3 * dp, 3 * dp, p);
            p.setColor(0x55000000);
            c.drawCircle(x, cy + dp, 10.5f * dp, p);
            p.setColor(0xFFF2F4F8);
            c.drawCircle(x, cy, 10 * dp, p);
        }
    }
}

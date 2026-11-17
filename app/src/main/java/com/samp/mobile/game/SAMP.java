package com.samp.mobile.game;

import android.app.Activity;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;

import com.google.firebase.crashlytics.FirebaseCrashlytics;
import com.joom.paranoid.Obfuscate;
import com.samp.mobile.game.ui.AttachEdit;
import com.samp.mobile.game.ui.CustomKeyboard;
import com.samp.mobile.game.ui.LoadingScreen;
import com.samp.mobile.game.ui.dialog.DialogManager;
import com.samp.mobile.launcher.util.SharedPreferenceCore;
import com.samp.mobile.launcher.util.SignatureChecker;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.io.UnsupportedEncodingException;
import java.nio.charset.StandardCharsets;
import java.util.List;

@Obfuscate
public class SAMP extends GTASA implements CustomKeyboard.InputListener, HeightProvider.HeightListener {
    private static final String TAG = "SAMP";
    private static SAMP instance;

    private CustomKeyboard mKeyboard;
    private DialogManager mDialog;
    private HeightProvider mHeightProvider;

    private AttachEdit mAttachEdit;
    private LoadingScreen mLoadingScreen;

    /** Arxa fon musiqisi: 3 slot, settings.ini-nin [music] bölməsindən oxunur. */
    private final MediaPlayer[] mMusicPlayers = new MediaPlayer[3];

    public native void sendDialogResponse(int i, int i2, int i3, byte[] str);

    public static SAMP getInstance() {
        return instance;
    }

    private void showTab()
    {

    }

    private void hideTab()
    {

    }

    private void setTab(int id, String name, int score, int ping)
    {

    }

    private void clearTab()
    {

    }

    private void showLoadingScreen()
    {

    }

    private void hideLoadingScreen()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mLoadingScreen.hide();
            }
        });
    }

    public void setPauseState(boolean pause) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (pause) {
                    mDialog.hideWithoutReset();
                    mAttachEdit.hideWithoutReset();
                }
                else {
                    if(mDialog.isShow)
                        mDialog.showWithOldContent();
                    if(mAttachEdit.isShow)
                        mAttachEdit.showWithoutReset();
                }
            }
        });
    }

    public void exitGame(){
        FirebaseCrashlytics.getInstance().setCrashlyticsCollectionEnabled(false);

        finishAndRemoveTask();
        System.exit(0);
    }

    public void showDialog(int dialogId, int dialogTypeId, byte[] bArr, byte[] bArr2, byte[] bArr3, byte[] bArr4) {
        final String caption = new String(bArr, StandardCharsets.UTF_8);
        final String content = new String(bArr2, StandardCharsets.UTF_8);
        final String leftBtnText = new String(bArr3, StandardCharsets.UTF_8);
        final String rightBtnText = new String(bArr4, StandardCharsets.UTF_8);
        runOnUiThread(() -> { this.mDialog.show(dialogId, dialogTypeId, caption, content, leftBtnText, rightBtnText); });
    }

    /** /yt komandasi (C++ -> JNI -> buraya). Imza: ([B)V */
    public void ytCommand(final byte[] data) {
        String s = "";
        try {
            if (data != null) s = new String(data, "UTF-8");
        } catch (Exception e) {
            s = "";
        }
        final String arg = s;
        final Activity a = this;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    YoutubeTablet.command(a, arg);
                } catch (Throwable t) {
                    Log.e(TAG, "ytCommand error: " + t);
                }
            }
        });
    }

    // ============================== ARXA FON MUSİQİSİ ==============================

    private File musicIniFile() {
        File d = getExternalFilesDir(null);
        File base = d != null ? d : getFilesDir();
        return new File(base, "SAMP/settings.ini");
    }

    /** settings.ini-ni sade oxuyur (LobbyActivity.IniFile-a bencer, serbest, bu fayl ozu yazmir). */
    private String iniGet(List<String> lines, String sec, String key, String def) {
        String cur = "";
        for (String l : lines) {
            String t = l.trim();
            if (t.startsWith("[") && t.endsWith("]")) { cur = t.substring(1, t.length() - 1).trim(); continue; }
            if (!cur.equalsIgnoreCase(sec)) continue;
            int eq = l.indexOf('=');
            if (eq < 0) continue;
            String k = l.substring(0, eq).trim();
            if (k.equalsIgnoreCase(key)) return l.substring(eq + 1).trim();
        }
        return def;
    }

    private void startBackgroundMusic() {
        stopBackgroundMusic();
        try {
            File ini = musicIniFile();
            if (!ini.exists()) return;

            java.util.List<String> lines = new java.util.ArrayList<>();
            BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(ini), "UTF-8"));
            String l;
            while ((l = br.readLine()) != null) lines.add(l);
            br.close();

            File base = getExternalFilesDir(null);
            if (base == null) base = getFilesDir();

            String[] slotKeys = { "slot1", "slot2", "slot3" };
            String[] volKeys  = { "slot1vol", "slot2vol", "slot3vol" };

            for (int i = 0; i < 3; i++) {
                String rel = iniGet(lines, "music", slotKeys[i], "");
                if (rel == null || rel.trim().isEmpty()) continue;

                File f = new File(base, rel.trim());
                if (!f.exists()) {
                    Log.w(TAG, "music slot " + (i + 1) + " not found: " + f.getAbsolutePath());
                    continue;
                }

                int volPct = parseIntSafe(iniGet(lines, "music", volKeys[i], "40"), 40);
                float vol = Math.max(0f, Math.min(100f, (float) volPct)) / 100f;

                try {
                    MediaPlayer mp = new MediaPlayer();
                    mp.setDataSource(f.getAbsolutePath());
                    mp.setLooping(true);
                    mp.setVolume(vol, vol);
                    mp.prepare();
                    mp.start();
                    mMusicPlayers[i] = mp;
                } catch (Throwable t) {
                    Log.e(TAG, "music slot " + (i + 1) + " play error: " + t);
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "startBackgroundMusic error: " + t);
        }
    }

    private void stopBackgroundMusic() {
        for (int i = 0; i < mMusicPlayers.length; i++) {
            if (mMusicPlayers[i] != null) {
                try {
                    if (mMusicPlayers[i].isPlaying()) mMusicPlayers[i].stop();
                    mMusicPlayers[i].release();
                } catch (Throwable ignored) { }
                mMusicPlayers[i] = null;
            }
        }
    }

    private static int parseIntSafe(String s, int def) {
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return def; }
    }

    // AZENC_V5: Az herfleri windows-1251-de olmadigi ucun '?' olurdu -> bir baytliq kodlar (chat.cpp ChatDecode geri cevirir)
    private static String azEnc(String s) {
        if (s == null) return "";
        String[] from = { "\u0259", "\u018F", "\u011F", "\u011E", "\u0131", "\u0130", "\u015F", "\u015E", "\u00E7", "\u00C7", "\u00F6", "\u00D6", "\u00FC", "\u00DC" };
        char[] to = { 1, 2, 3, 4, 5, 6, 7, 8, 14, 15, 16, 17, 18, 19 };
        for (int i = 0; i < from.length; i++) s = s.replace(from[i], String.valueOf(to[i]));
        return s;
    }

    private native void onInputEnd(byte[] str);
    @Override
    public void OnInputEnd(String str)
    {
        byte[] toReturn = null;
        try
        {
            toReturn = azEnc(str).getBytes("windows-1251");
        }
        catch(UnsupportedEncodingException e)
        {

        }

        try {
            onInputEnd(toReturn);
        }
        catch (UnsatisfiedLinkError e5) {
            Log.e(TAG, e5.getMessage());
        }
    }

    private void showKeyboard()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Log.d("AXL", "showKeyboard()");
                mKeyboard.ShowInputLayout();
            }
        });
    }

    private void hideKeyboard()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mKeyboard.HideInputLayout();
            }
        });
    }

    private void showEditObject()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mAttachEdit.show();
            }
        });
    }

    private void hideEditObject()
    {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                mAttachEdit.hide();
            }
        });
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        Log.i(TAG, "**** onCreate");
        super.onCreate(savedInstanceState);

        //if(!SignatureChecker.isSignatureValid(this, getPackageName()))
        //{
            //Toast.makeText(this, "Use original launcher! No remake", Toast.LENGTH_LONG).show();
            //return;
        //}

        //mHeightProvider = new HeightProvider(this);

        mKeyboard = new CustomKeyboard(this);

        mDialog = new DialogManager(this);

        mAttachEdit = new AttachEdit(this);

        mLoadingScreen = new LoadingScreen(this);

        instance = this;

        copyPlateAssets();   // PLATE_V7
        try {
            initializeSAMP();
        } catch (UnsatisfiedLinkError e5) {
            Log.e(TAG, e5.getMessage());
        }

        startBackgroundMusic();
    }

    // REQ_HND_PICK: menyudan handling.cfg fayl secici (native -> pickHandlingFile)
    private static final int REQ_HND_PICK = 0x4801;
    private String mHndDest = null;
    private int mHndSlot = 0;

    public void pickHandlingFile(final int slot, final String dest) {
        mHndSlot = slot;
        mHndDest = dest;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    android.content.Intent it = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
                    it.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                    it.setType("*/*");
                    startActivityForResult(it, REQ_HND_PICK);
                } catch (Throwable t) {
                    Log.e(TAG, "pickHandlingFile: " + t);
                    Toast.makeText(SAMP.this, "Fayl seçici açılmadı", Toast.LENGTH_SHORT).show();
                }
            }
        });
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_HND_PICK || resultCode != RESULT_OK || data == null || data.getData() == null || mHndDest == null) return;
        try {
            java.io.InputStream in = getContentResolver().openInputStream(data.getData());
            if (in == null) return;
            File out = new File(mHndDest);
            File parent = out.getParentFile();
            if (parent != null && !parent.exists()) parent.mkdirs();
            java.io.FileOutputStream os = new java.io.FileOutputStream(out);
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            os.close();
            in.close();
            Toast.makeText(this, "Handling faylı slot " + mHndSlot + "-ə əlavə olundu", Toast.LENGTH_SHORT).show();
        } catch (Throwable t) {
            Log.e(TAG, "handling copy: " + t);
            Toast.makeText(this, "Fayl oxunmadı", Toast.LENGTH_SHORT).show();
        }
    }

    // PLATE_V8: plate.png / plate.ttf -> <externalFiles>/SAMP/plate/ (+ <files>/SAMP/plate/). Asset yoxdursa movcud fayla toxunmur.
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
            fixPlatePng(new java.io.File(d, "plate.png"));   // PLATE_V9
            boolean b = copyFirstAsset(ttfNames, new java.io.File(d, "plate.ttf"));
            android.util.Log.i("SAMPPlate", d.getPath() + " png=" + a + " ttf=" + b);
        }
    }

    // PLATE_V9: plate.png real PNG deyilse (JPEG/WebP/BMP/GIF) oxuyub real PNG kimi yeniden yazir
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

    private native void initializeSAMP();

    @Override
    public void onStart() {
        Log.i(TAG, "**** onStart");
        super.onStart();
    }

    @Override
    public void onRestart() {
        Log.i(TAG, "**** onRestart");
        super.onRestart();
    }

    @Override
    public void onResume() {
        Log.i(TAG, "**** onResume");
        super.onResume();
        //mHeightProvider.init(view);
    }

    public native void onEventBackPressed();

    @Override
    public void onBackPressed() {
        super.onBackPressed();
        onEventBackPressed();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if(keyCode == KeyEvent.KEYCODE_BACK)
        {
            onEventBackPressed();
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public void onPause() {
        Log.i(TAG, "**** onPause");
        super.onPause();
    }

    @Override
    public void onStop() {
        Log.i(TAG, "**** onStop");
        super.onStop();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "**** onDestroy");
        stopBackgroundMusic();
        try {
            YoutubeTablet.release();
        } catch (Throwable t) {
            Log.e(TAG, "YoutubeTablet.release error: " + t);
        }
        super.onDestroy();
    }

    @Override
    public void onHeightChanged(int orientation, int height) {
        //mKeyboard.onHeightChanged(height);
        //mDialog.onHeightChanged(height);
    }
}

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

    private native void onInputEnd(byte[] str);
    @Override
    public void OnInputEnd(String str)
    {
        byte[] toReturn = null;
        try
        {
            toReturn = str.getBytes("windows-1251");
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

        try {
            initializeSAMP();
        } catch (UnsatisfiedLinkError e5) {
            Log.e(TAG, e5.getMessage());
        }

        startBackgroundMusic();
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

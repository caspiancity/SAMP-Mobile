package com.samp.mobile.game;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;

import java.nio.ByteBuffer;

/**
 * Azerbaycan nomre nisani (12-AB-345) cizir. Netice: RGBA bayt massivi (512x128).
 * Font: app/src/main/assets/fonts/plate.ttf  (yoxdursa sistem qalin fontu istifade olunur)
 */
public final class PlateGen {
    public static final int W = 512, H = 128;

    private static Typeface sFace;

    private static Typeface face(Context c) {
        if (sFace == null) {
            try {
                sFace = Typeface.createFromAsset(c.getAssets(), "fonts/plate.ttf");
            } catch (Throwable t) {
                sFace = Typeface.create(Typeface.DEFAULT_BOLD, Typeface.BOLD);
            }
        }
        return sFace;
    }

    public static byte[] render(Context ctx, String text) {
        if (text == null) text = "";
        text = text.trim().toUpperCase();
        if (text.length() > 12) text = text.substring(0, 12);

        Bitmap bmp = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(bmp);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

        // --- fon (ag, yungul gradient) ---
        RectF outer = new RectF(0, 0, W, H);
        p.setStyle(Paint.Style.FILL);
        p.setShader(new LinearGradient(0, 0, 0, H, 0xFFFFFFFF, 0xFFE4E6EA, Shader.TileMode.CLAMP));
        c.drawRoundRect(outer, 16, 16, p);
        p.setShader(null);

        // --- qara cerceve ---
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(5f);
        p.setColor(0xFF111111);
        c.drawRoundRect(new RectF(4, 4, W - 4, H - 4), 13, 13, p);

        // --- sol hisse: bayraq + AZ ---
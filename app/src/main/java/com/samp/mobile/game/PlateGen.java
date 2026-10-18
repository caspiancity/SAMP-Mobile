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
        final float fx = 18, fy = 16, fw = 74, fh = 44;
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF00B5E2);
        c.drawRect(fx, fy, fx + fw, fy + fh / 3f, p);
        p.setColor(0xFFEF3340);
        c.drawRect(fx, fy + fh / 3f, fx + fw, fy + 2 * fh / 3f, p);
        p.setColor(0xFF509E2F);
        c.drawRect(fx, fy + 2 * fh / 3f, fx + fw, fy + fh, p);
        // ay-ulduz
        final float mx = fx + fw / 2f, my = fy + fh / 2f;
        p.setColor(0xFFFFFFFF);
        c.drawCircle(mx - 2, my, 7f, p);
        p.setColor(0xFFEF3340);
        c.drawCircle(mx + 0.5f, my, 5.6f, p);
        p.setColor(0xFFFFFFFF);
        Path star = new Path();
        for (int i = 0; i < 10; i++) {
            double a = -Math.PI / 2 + i * Math.PI / 5;
            float r = (i % 2 == 0) ? 3.6f : 1.5f;
            float sx = (float) (mx + 6 + Math.cos(a) * r), sy = (float) (my + Math.sin(a) * r);
            if (i == 0) star.moveTo(sx, sy); else star.lineTo(sx, sy);
        }
        star.close();
        c.drawPath(star, p);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(1.5f);
        p.setColor(0xFF555555);
        c.drawRect(fx, fy, fx + fw, fy + fh, p);

        // AZ qutusu
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(2.5f);
        p.setColor(0xFF222222);
        RectF az = new RectF(fx, 70, fx + fw, 112);
        c.drawRoundRect(az, 8, 8, p);
        p.setStyle(Paint.Style.FILL);
        p.setColor(0xFF111111);
        p.setTypeface(Typeface.create(Typeface.DEFAULT_BOLD, Typeface.BOLD));
        p.setTextSize(34f);
        p.setTextAlign(Paint.Align.CENTER);
        c.drawText("AZ", az.centerX(), az.centerY() + 12f, p);

        // --- esas metn ---
        final float tx0 = 108, tx1 = W - 18;
        p.setTypeface(face(ctx));
        p.setTextAlign(Paint.Align.CENTER);
        p.setColor(0xFF0B0B0B);
        float size = 92f;
        p.setTextSize(size);
        float tw = p.measureText(text);
        float maxW = tx1 - tx0;
        if (tw > maxW) {
            size = size * maxW / tw;
            p.setTextSize(size);
        }
        Paint.FontMetrics fm = p.getFontMetrics();
        float baseline = H / 2f - (fm.ascent + fm.descent) / 2f;
        c.drawText(text, (tx0 + tx1) / 2f, baseline, p);

        ByteBuffer buf = ByteBuffer.allocate(W * H * 4);
        bmp.copyPixelsToBuffer(buf);
        bmp.recycle();
        return buf.array();   // RGBA (ARGB_8888 yaddas duzumu)
    }
}

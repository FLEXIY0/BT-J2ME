package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.game.Sprite;

final class NokiaGraphics implements DirectGraphics {
    private final Graphics g;
    private int alpha = 0xFF;

    NokiaGraphics(Graphics g) {
        this.g = g;
    }

    public void setARGBColor(int argb) {
        alpha = argb >>> 24;
        g.setColor(argb & 0xFFFFFF);
    }

    public int getAlphaComponent() { return alpha; }
    public int getNativePixelFormat() { return TYPE_INT_8888_ARGB; }

    /* поворот против часовой стрелки + отражения Nokia → трансформации MIDP 2 */
    private static int toTransform(int m) {
        boolean fh = (m & FLIP_HORIZONTAL) != 0, fv = (m & FLIP_VERTICAL) != 0;
        int rot = m & 0x1FFF;
        int t;
        switch (rot) {
        case ROTATE_90: t = Sprite.TRANS_ROT270; break;
        case ROTATE_180: t = Sprite.TRANS_ROT180; break;
        case ROTATE_270: t = Sprite.TRANS_ROT90; break;
        default: t = Sprite.TRANS_NONE; break;
        }
        if (fh && fv)
            t = t == Sprite.TRANS_NONE ? Sprite.TRANS_ROT180 : t == Sprite.TRANS_ROT180 ? Sprite.TRANS_NONE
                    : t == Sprite.TRANS_ROT90 ? Sprite.TRANS_ROT270 : Sprite.TRANS_ROT90;
        else if (fh)
            t = t == Sprite.TRANS_NONE ? Sprite.TRANS_MIRROR : t == Sprite.TRANS_ROT180 ? Sprite.TRANS_MIRROR_ROT180
                    : t == Sprite.TRANS_ROT90 ? Sprite.TRANS_MIRROR_ROT270 : Sprite.TRANS_MIRROR_ROT90;
        else if (fv)
            t = t == Sprite.TRANS_NONE ? Sprite.TRANS_MIRROR_ROT180 : t == Sprite.TRANS_ROT180 ? Sprite.TRANS_MIRROR
                    : t == Sprite.TRANS_ROT90 ? Sprite.TRANS_MIRROR_ROT90 : Sprite.TRANS_MIRROR_ROT270;
        return t;
    }

    public void drawImage(Image img, int x, int y, int anchor, int manipulation) {
        if (anchor == 0)
            anchor = Graphics.TOP | Graphics.LEFT;
        g.drawRegion(img, 0, 0, img.getWidth(), img.getHeight(), toTransform(manipulation), x, y, anchor);
    }

    public void drawTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argb) {
        setARGBColor(argb);
        g.drawLine(x1, y1, x2, y2);
        g.drawLine(x2, y2, x3, y3);
        g.drawLine(x3, y3, x1, y1);
    }

    public void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argb) {
        setARGBColor(argb);
        g.fillTriangle(x1, y1, x2, y2, x3, y3);
    }

    public void drawPolygon(int[] xs, int xo, int[] ys, int yo, int n, int argb) {
        setARGBColor(argb);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            g.drawLine(xs[xo + i], ys[yo + i], xs[xo + j], ys[yo + j]);
        }
    }

    public void fillPolygon(int[] xs, int xo, int[] ys, int yo, int n, int argb) {
        setARGBColor(argb);
        for (int i = 1; i + 1 < n; i++)
            g.fillTriangle(xs[xo], ys[yo], xs[xo + i], ys[yo + i], xs[xo + i + 1], ys[yo + i + 1]);
    }

    private static int toArgb(int v, int format) {
        switch (format) {
        case TYPE_USHORT_4444_ARGB:
            return ((v >> 12 & 15) * 17) << 24 | ((v >> 8 & 15) * 17) << 16 | ((v >> 4 & 15) * 17) << 8 | (v & 15) * 17;
        case TYPE_USHORT_444_RGB:
            return 0xFF000000 | ((v >> 8 & 15) * 17) << 16 | ((v >> 4 & 15) * 17) << 8 | (v & 15) * 17;
        case TYPE_USHORT_565_RGB:
            return 0xFF000000 | ((v >> 11 & 31) * 255 / 31) << 16 | ((v >> 5 & 63) * 255 / 63) << 8 | (v & 31) * 255 / 31;
        case TYPE_USHORT_555_RGB:
            return 0xFF000000 | ((v >> 10 & 31) * 255 / 31) << 16 | ((v >> 5 & 31) * 255 / 31) << 8 | (v & 31) * 255 / 31;
        case TYPE_USHORT_1555_ARGB:
            return ((v & 0x8000) != 0 ? 0xFF000000 : 0) | ((v >> 10 & 31) * 255 / 31) << 16 | ((v >> 5 & 31) * 255 / 31) << 8 | (v & 31) * 255 / 31;
        case TYPE_INT_888_RGB:
            return 0xFF000000 | v;
        default:
            return v;
        }
    }

    public void drawPixels(int[] px, boolean transparency, int off, int scan, int x, int y, int w, int h, int manipulation, int format) {
        int[] rgb = new int[w * h];
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int v = toArgb(px[off + j * scan + i], format);
                rgb[j * w + i] = transparency ? v : (v | 0xFF000000);
            }
        drawRgb(rgb, x, y, w, h, manipulation);
    }

    public void drawPixels(short[] px, boolean transparency, int off, int scan, int x, int y, int w, int h, int manipulation, int format) {
        int[] rgb = new int[w * h];
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int v = toArgb(px[off + j * scan + i] & 0xFFFF, format);
                rgb[j * w + i] = transparency ? v : (v | 0xFF000000);
            }
        drawRgb(rgb, x, y, w, h, manipulation);
    }

    public void drawPixels(byte[] px, byte[] mask, int off, int scan, int x, int y, int w, int h, int manipulation, int format) {
        int[] rgb = new int[w * h];
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int bit = off * 8 + j * scan + i;
                boolean on = (px[bit >> 3] & (0x80 >> (bit & 7))) != 0;
                boolean opaque = mask == null || (mask[bit >> 3] & (0x80 >> (bit & 7))) != 0;
                rgb[j * w + i] = opaque ? (on ? 0xFF000000 : 0xFFFFFFFF) : 0;
            }
        drawRgb(rgb, x, y, w, h, manipulation);
    }

    private void drawRgb(int[] rgb, int x, int y, int w, int h, int manipulation) {
        if (manipulation == 0) {
            g.drawRGB(rgb, 0, w, x, y, w, h, true);
            return;
        }
        Image img = Image.createRGBImage(rgb, w, h, true);
        drawImage(img, x, y, Graphics.TOP | Graphics.LEFT, manipulation);
    }

    public void getPixels(int[] px, int off, int scan, int x, int y, int w, int h, int format) {
    }

    public void getPixels(short[] px, int off, int scan, int x, int y, int w, int h, int format) {
    }

    public void getPixels(byte[] px, byte[] mask, int off, int scan, int x, int y, int w, int h, int format) {
    }
}

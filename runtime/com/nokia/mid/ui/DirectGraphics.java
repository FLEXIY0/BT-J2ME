package com.nokia.mid.ui;

import javax.microedition.lcdui.Image;

public interface DirectGraphics {
    int FLIP_HORIZONTAL = 0x2000, FLIP_VERTICAL = 0x4000, ROTATE_90 = 90, ROTATE_180 = 180, ROTATE_270 = 270;
    int TYPE_BYTE_1_GRAY = 1, TYPE_BYTE_1_GRAY_VERTICAL = -1, TYPE_BYTE_2_GRAY = 2, TYPE_BYTE_4_GRAY = 4,
            TYPE_BYTE_8_GRAY = 8, TYPE_BYTE_332_RGB = 332, TYPE_USHORT_4444_ARGB = 4444, TYPE_USHORT_444_RGB = 444,
            TYPE_USHORT_555_RGB = 555, TYPE_USHORT_1555_ARGB = 1555, TYPE_USHORT_565_RGB = 565,
            TYPE_INT_888_RGB = 888, TYPE_INT_8888_ARGB = 8888;

    void setARGBColor(int argb);
    void drawImage(Image img, int x, int y, int anchor, int manipulation);
    void drawTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argb);
    void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argb);
    void drawPolygon(int[] xs, int xo, int[] ys, int yo, int n, int argb);
    void fillPolygon(int[] xs, int xo, int[] ys, int yo, int n, int argb);
    void drawPixels(int[] pixels, boolean transparency, int off, int scan, int x, int y, int w, int h, int manipulation, int format);
    void drawPixels(short[] pixels, boolean transparency, int off, int scan, int x, int y, int w, int h, int manipulation, int format);
    void drawPixels(byte[] pixels, byte[] mask, int off, int scan, int x, int y, int w, int h, int manipulation, int format);
    void getPixels(int[] pixels, int off, int scan, int x, int y, int w, int h, int format);
    void getPixels(short[] pixels, int off, int scan, int x, int y, int w, int h, int format);
    void getPixels(byte[] pixels, byte[] mask, int off, int scan, int x, int y, int w, int h, int format);
    int getNativePixelFormat();
    int getAlphaComponent();
}

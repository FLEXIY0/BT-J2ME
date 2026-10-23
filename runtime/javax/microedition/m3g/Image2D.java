package javax.microedition.m3g;

import javax.microedition.lcdui.Image;

/* Картинка хранится сразу в 0xAARRGGBB — так её рисует натив. */
public class Image2D extends Object3D {
    public static final int ALPHA = 96, LUMINANCE = 97, LUMINANCE_ALPHA = 98, RGB = 99, RGBA = 100;

    final int format, w, h;
    final int[] px;
    boolean mutable;

    public Image2D(int format, int w, int h) {
        this(format, w, h, true);
    }

    Image2D(int format, int w, int h, boolean mutable) {
        if (format < ALPHA || format > RGBA || w <= 0 || h <= 0)
            throw new IllegalArgumentException();
        this.format = format;
        this.w = w;
        this.h = h;
        this.mutable = mutable;
        px = new int[w * h];
        for (int i = 0; i < px.length; i++)
            px[i] = 0xFFFFFFFF;
    }

    public Image2D(int format, int w, int h, byte[] image) {
        this(format, w, h, false);
        unpack(image, 0, 0, 0, w, h);
    }

    public Image2D(int format, int w, int h, byte[] image, byte[] palette) {
        this(format, w, h, false);
        int bpp = bytes(format);
        for (int i = 0; i < w * h; i++) {
            int k = (image[i] & 0xFF) * bpp;
            px[i] = k + bpp <= palette.length ? texel(palette, k) : 0xFFFFFFFF;
        }
    }

    public Image2D(int format, Object image) {
        this(format, ((Image) image).getWidth(), ((Image) image).getHeight(), false);
        Image img = (Image) image;
        img.getRGB(px, 0, w, 0, 0, w, h);
        for (int i = 0; i < px.length; i++)
            px[i] = convert(px[i]);
    }

    /* ARGB из lcdui → формат этой картинки. */
    private int convert(int c) {
        int a = c >>> 24, r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255, l = (r * 77 + g * 150 + b * 29) >> 8;
        switch (format) {
        case ALPHA: return (a << 24) | 0xFFFFFF;
        case LUMINANCE: return 0xFF000000 | (l << 16) | (l << 8) | l;
        case LUMINANCE_ALPHA: return (a << 24) | (l << 16) | (l << 8) | l;
        case RGB: return c | 0xFF000000;
        default: return c;
        }
    }

    static int bytes(int f) {
        return f == RGBA ? 4 : f == RGB ? 3 : f == LUMINANCE_ALPHA ? 2 : 1;
    }

    private int texel(byte[] d, int k) {
        switch (format) {
        case ALPHA: return ((d[k] & 255) << 24) | 0xFFFFFF;
        case LUMINANCE: { int l = d[k] & 255; return 0xFF000000 | (l << 16) | (l << 8) | l; }
        case LUMINANCE_ALPHA: { int l = d[k] & 255; return ((d[k + 1] & 255) << 24) | (l << 16) | (l << 8) | l; }
        case RGB: return 0xFF000000 | ((d[k] & 255) << 16) | ((d[k + 1] & 255) << 8) | (d[k + 2] & 255);
        default: return ((d[k + 3] & 255) << 24) | ((d[k] & 255) << 16) | ((d[k + 1] & 255) << 8) | (d[k + 2] & 255);
        }
    }

    private void unpack(byte[] d, int off, int x, int y, int cw, int ch) {
        int bpp = bytes(format);
        if (d.length < off + cw * ch * bpp)
            throw new IllegalArgumentException();
        for (int j = 0; j < ch; j++)
            for (int i = 0; i < cw; i++)
                px[(y + j) * w + x + i] = texel(d, off + (j * cw + i) * bpp);
    }

    public void set(int x, int y, int cw, int ch, byte[] image) {
        if (!mutable)
            throw new IllegalStateException();
        if (x < 0 || y < 0 || cw <= 0 || ch <= 0 || x + cw > w || y + ch > h)
            throw new IllegalArgumentException();
        unpack(image, 0, x, y, cw, ch);
    }

    public boolean isMutable() { return mutable; }
    public int getFormat() { return format; }
    public int getWidth() { return w; }
    public int getHeight() { return h; }
}

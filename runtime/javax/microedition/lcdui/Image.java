package javax.microedition.lcdui;

import java.io.IOException;
import java.io.InputStream;

public class Image {
    int width, height;
    int[] pixels;   /* 0xAARRGGBB */
    boolean mutable;

    Image(int w, int h, int[] px, boolean mutable) {
        width = w;
        height = h;
        pixels = px;
        this.mutable = mutable;
    }

    public static Image createImage(int w, int h) {
        if (w <= 0 || h <= 0)
            throw new IllegalArgumentException();
        int[] px = new int[w * h];
        for (int i = 0; i < px.length; i++)
            px[i] = 0xFFFFFFFF;
        return new Image(w, h, px, true);
    }

    public static Image createImage(Image src) {
        if (!src.mutable)
            return src;
        int[] px = new int[src.pixels.length];
        System.arraycopy(src.pixels, 0, px, 0, px.length);
        return new Image(src.width, src.height, px, false);
    }

    public static Image createImage(String name) throws IOException {
        if (name == null)
            throw new NullPointerException();
        InputStream in = Image.class.getResourceAsStream(name.startsWith("/") ? name : "/" + name);
        if (in == null)
            throw new IOException("нет ресурса " + name);
        return createImage(in);
    }

    public static Image createImage(InputStream in) throws IOException {
        if (in == null)
            throw new NullPointerException();
        java.io.ByteArrayOutputStream bo = new java.io.ByteArrayOutputStream();
        byte[] b = new byte[1024];
        int n;
        while ((n = in.read(b, 0, b.length)) > 0)
            bo.write(b, 0, n);
        byte[] data = bo.toByteArray();
        try {
            return createImage(data, 0, data.length);
        } catch (IllegalArgumentException e) {
            throw new IOException("плохая картинка");
        }
    }

    public static Image createImage(byte[] data, int off, int len) {
        if (data == null)
            throw new NullPointerException();
        if (off < 0 || len < 0 || off + len > data.length)
            throw new ArrayIndexOutOfBoundsException();
        int[] wh = new int[2];
        int[] px = btvm.Native.decodeImage(data, off, len, wh);
        if (px == null)
            throw new IllegalArgumentException("плохая картинка");
        return new Image(wh[0], wh[1], px, false);
    }

    public static Image createImage(Image src, int x, int y, int w, int h, int transform) {
        if (src == null)
            throw new NullPointerException();
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > src.width || y + h > src.height || transform < 0 || transform > 7)
            throw new IllegalArgumentException();
        int dw = transform >= 4 ? h : w, dh = transform >= 4 ? w : h;
        Image dst = new Image(dw, dh, new int[dw * dh], true);
        Graphics g = dst.getGraphics();
        for (int i = 0; i < dst.pixels.length; i++)
            dst.pixels[i] = 0;
        g.drawRegion(src, x, y, w, h, transform, 0, 0, Graphics.TOP | Graphics.LEFT);
        dst.mutable = false;
        return dst;
    }

    public static Image createRGBImage(int[] rgb, int w, int h, boolean alpha) {
        if (rgb == null)
            throw new NullPointerException();
        if (w <= 0 || h <= 0)
            throw new IllegalArgumentException();
        if (w * h > rgb.length)
            throw new ArrayIndexOutOfBoundsException();
        int[] px = new int[w * h];
        for (int i = 0; i < px.length; i++)
            px[i] = alpha ? rgb[i] : (rgb[i] | 0xFF000000);
        return new Image(w, h, px, false);
    }

    public Graphics getGraphics() {
        if (!mutable)
            throw new IllegalStateException();
        return new Graphics(pixels, width, height);
    }

    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public boolean isMutable() { return mutable; }

    public void getRGB(int[] out, int off, int scan, int x, int y, int w, int h) {
        if (x < 0 || y < 0 || x + w > width || y + h > height)
            throw new IllegalArgumentException();
        for (int j = 0; j < h; j++)
            System.arraycopy(pixels, (y + j) * width + x, out, off + j * scan, w);
    }
}

package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public class DirectUtils {
    private DirectUtils() {
    }

    public static DirectGraphics getDirectGraphics(Graphics g) {
        return new NokiaGraphics(g);
    }

    public static Image createImage(byte[] data, int off, int len) {
        return Image.createImage(data, off, len);
    }

    public static Image createImage(int w, int h, int argb) {
        int[] px = new int[w * h];
        for (int i = 0; i < px.length; i++)
            px[i] = argb;
        Image img = Image.createImage(w, h);
        img.getGraphics().drawRGB(px, 0, w, 0, 0, w, h, false);
        if ((argb >>> 24) != 0xFF) {
            int[] dst = javax.microedition.lcdui.GameCanvasAccess.pixels(img);
            for (int i = 0; i < dst.length; i++)
                dst[i] = argb;
        }
        return img;
    }
}

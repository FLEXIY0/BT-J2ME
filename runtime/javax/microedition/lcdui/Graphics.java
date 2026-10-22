package javax.microedition.lcdui;

/* Рисование выполняют нативы (src/midp.c): они читают поля buf/bw/bh/cx/cy/cw/ch/color/stroke.
 * Перенос координат (translate) и якоря считаются здесь, на Java. */
public class Graphics {
    public static final int HCENTER = 1, VCENTER = 2, LEFT = 4, RIGHT = 8, TOP = 16, BOTTOM = 32, BASELINE = 64;
    public static final int SOLID = 0, DOTTED = 1;

    int[] buf;
    int bw, bh;
    int tx, ty;
    int cx, cy, cw, ch;
    int color;
    int stroke;
    Font font;

    Graphics(int[] buf, int w, int h) {
        this.buf = buf;
        bw = w;
        bh = h;
        reset();
    }

    void reset() {
        tx = ty = 0;
        cx = cy = 0;
        cw = bw;
        ch = bh;
        color = 0xFF000000;
        stroke = SOLID;
        font = Font.getDefaultFont();
    }

    public void translate(int x, int y) {
        tx += x;
        ty += y;
    }

    public int getTranslateX() { return tx; }
    public int getTranslateY() { return ty; }

    public int getColor() { return color & 0xFFFFFF; }
    public int getRedComponent() { return (color >> 16) & 0xFF; }
    public int getGreenComponent() { return (color >> 8) & 0xFF; }
    public int getBlueComponent() { return color & 0xFF; }

    public int getGrayScale() {
        return (getRedComponent() * 30 + getGreenComponent() * 59 + getBlueComponent() * 11) / 100;
    }

    public void setColor(int rgb) {
        color = 0xFF000000 | rgb;
    }

    public void setColor(int r, int g, int b) {
        if (((r | g | b) & ~0xFF) != 0)
            throw new IllegalArgumentException();
        color = 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    public void setGrayScale(int v) {
        setColor(v, v, v);
    }

    public Font getFont() { return font; }

    public void setFont(Font f) {
        font = f == null ? Font.getDefaultFont() : f;
    }

    public int getClipX() { return cx - tx; }
    public int getClipY() { return cy - ty; }
    public int getClipWidth() { return cw; }
    public int getClipHeight() { return ch; }

    public void clipRect(int x, int y, int w, int h) {
        int x1 = Math.max(cx, x + tx), y1 = Math.max(cy, y + ty);
        int x2 = Math.min(cx + cw, x + tx + w), y2 = Math.min(cy + ch, y + ty + h);
        cx = x1;
        cy = y1;
        cw = Math.max(0, x2 - x1);
        ch = Math.max(0, y2 - y1);
    }

    public void setClip(int x, int y, int w, int h) {
        int x1 = Math.max(0, x + tx), y1 = Math.max(0, y + ty);
        int x2 = Math.min(bw, x + tx + w), y2 = Math.min(bh, y + ty + h);
        cx = x1;
        cy = y1;
        cw = Math.max(0, x2 - x1);
        ch = Math.max(0, y2 - y1);
    }

    public void setStrokeStyle(int s) {
        if (s != SOLID && s != DOTTED)
            throw new IllegalArgumentException();
        stroke = s;
    }

    public int getStrokeStyle() { return stroke; }

    public int getDisplayColor(int c) { return c & 0xFFFFFF; }

    private native void fill0(int x, int y, int w, int h);
    private native void line0(int x1, int y1, int x2, int y2);
    private native void arc0(int x, int y, int w, int h, int start, int arc, boolean fill);
    private native void roundRect0(int x, int y, int w, int h, int aw, int ah, boolean fill);
    private native void triangle0(int x1, int y1, int x2, int y2, int x3, int y3);
    private native void image0(int[] src, int sw, int sh, int sx, int sy, int w, int h, int transform, int dx, int dy);
    private native void rgb0(int[] rgb, int off, int scan, int x, int y, int w, int h, boolean alpha);
    private native void text0(String s, int x, int y, int fontId);
    private native void copy0(int sx, int sy, int w, int h, int dx, int dy);

    public void fillRect(int x, int y, int w, int h) {
        if (w > 0 && h > 0)
            fill0(x + tx, y + ty, w, h);
    }

    public void drawRect(int x, int y, int w, int h) {
        if (w < 0 || h < 0)
            return;
        if (stroke == DOTTED) {
            drawLine(x, y, x + w, y);
            drawLine(x, y + h, x + w, y + h);
            drawLine(x, y, x, y + h);
            drawLine(x + w, y, x + w, y + h);
            return;
        }
        x += tx;
        y += ty;
        fill0(x, y, w + 1, 1);
        fill0(x, y + h, w + 1, 1);
        fill0(x, y + 1, 1, h - 1);
        fill0(x + w, y + 1, 1, h - 1);
    }

    public void drawLine(int x1, int y1, int x2, int y2) {
        line0(x1 + tx, y1 + ty, x2 + tx, y2 + ty);
    }

    public void drawArc(int x, int y, int w, int h, int start, int arc) {
        if (w >= 0 && h >= 0)
            arc0(x + tx, y + ty, w, h, start, arc, false);
    }

    public void fillArc(int x, int y, int w, int h, int start, int arc) {
        if (w > 0 && h > 0)
            arc0(x + tx, y + ty, w, h, start, arc, true);
    }

    public void drawRoundRect(int x, int y, int w, int h, int aw, int ah) {
        if (w >= 0 && h >= 0)
            roundRect0(x + tx, y + ty, w, h, aw, ah, false);
    }

    public void fillRoundRect(int x, int y, int w, int h, int aw, int ah) {
        if (w > 0 && h > 0)
            roundRect0(x + tx, y + ty, w, h, aw, ah, true);
    }

    public void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3) {
        triangle0(x1 + tx, y1 + ty, x2 + tx, y2 + ty, x3 + tx, y3 + ty);
    }

    private static void checkAnchor(int a, boolean text) {
        int h = a & (LEFT | HCENTER | RIGHT), v = a & (TOP | (text ? BASELINE : VCENTER) | BOTTOM);
        if (a != 0 && ((h != 0 && h != LEFT && h != HCENTER && h != RIGHT) || (v != 0 && v != TOP && v != BOTTOM
                && v != (text ? BASELINE : VCENTER)) || (a & ~(LEFT | HCENTER | RIGHT | TOP | BOTTOM | (text ? BASELINE : VCENTER))) != 0))
            throw new IllegalArgumentException("anchor");
    }

    public void drawString(String s, int x, int y, int anchor) {
        if (s == null)
            throw new NullPointerException();
        checkAnchor(anchor, true);
        Font f = font;
        if ((anchor & HCENTER) != 0)
            x -= f.stringWidth(s) / 2;
        else if ((anchor & RIGHT) != 0)
            x -= f.stringWidth(s);
        if ((anchor & BOTTOM) != 0)
            y -= f.getHeight();
        else if ((anchor & BASELINE) != 0)
            y -= f.getBaselinePosition();
        text0(s, x + tx, y + ty, f.id);
    }

    public void drawSubstring(String s, int off, int len, int x, int y, int anchor) {
        if (off < 0 || len < 0 || off + len > s.length())
            throw new StringIndexOutOfBoundsException();
        drawString(s.substring(off, off + len), x, y, anchor);
    }

    public void drawChar(char c, int x, int y, int anchor) {
        drawString(String.valueOf(c), x, y, anchor);
    }

    public void drawChars(char[] data, int off, int len, int x, int y, int anchor) {
        drawString(new String(data, off, len), x, y, anchor);
    }

    public void drawImage(Image img, int x, int y, int anchor) {
        if (img == null)
            throw new NullPointerException();
        checkAnchor(anchor, false);
        if ((anchor & HCENTER) != 0)
            x -= img.width / 2;
        else if ((anchor & RIGHT) != 0)
            x -= img.width;
        if ((anchor & VCENTER) != 0)
            y -= img.height / 2;
        else if ((anchor & BOTTOM) != 0)
            y -= img.height;
        image0(img.pixels, img.width, img.height, 0, 0, img.width, img.height, 0, x + tx, y + ty);
    }

    public void drawRegion(Image src, int xs, int ys, int w, int h, int transform, int xd, int yd, int anchor) {
        if (src == null)
            throw new NullPointerException();
        if (xs < 0 || ys < 0 || w < 0 || h < 0 || xs + w > src.width || ys + h > src.height || transform < 0 || transform > 7)
            throw new IllegalArgumentException();
        checkAnchor(anchor, false);
        int dw = transform >= 4 ? h : w, dh = transform >= 4 ? w : h;
        if ((anchor & HCENTER) != 0)
            xd -= dw / 2;
        else if ((anchor & RIGHT) != 0)
            xd -= dw;
        if ((anchor & VCENTER) != 0)
            yd -= dh / 2;
        else if ((anchor & BOTTOM) != 0)
            yd -= dh;
        image0(src.pixels, src.width, src.height, xs, ys, w, h, transform, xd + tx, yd + ty);
    }

    public void drawRGB(int[] rgb, int off, int scan, int x, int y, int w, int h, boolean alpha) {
        if (rgb == null)
            throw new NullPointerException();
        if (w <= 0 || h <= 0)
            return;
        rgb0(rgb, off, scan, x + tx, y + ty, w, h, alpha);
    }

    public void copyArea(int xs, int ys, int w, int h, int xd, int yd, int anchor) {
        if ((anchor & HCENTER) != 0)
            xd -= w / 2;
        else if ((anchor & RIGHT) != 0)
            xd -= w;
        if ((anchor & VCENTER) != 0)
            yd -= h / 2;
        else if ((anchor & BOTTOM) != 0)
            yd -= h;
        copy0(xs + tx, ys + ty, w, h, xd + tx, yd + ty);
    }
}

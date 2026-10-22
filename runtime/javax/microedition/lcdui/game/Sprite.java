package javax.microedition.lcdui.game;

import javax.microedition.lcdui.GameCanvasAccess;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public class Sprite extends Layer {
    public static final int TRANS_NONE = 0, TRANS_ROT90 = 5, TRANS_ROT180 = 3, TRANS_ROT270 = 6, TRANS_MIRROR = 2,
            TRANS_MIRROR_ROT90 = 7, TRANS_MIRROR_ROT180 = 1, TRANS_MIRROR_ROT270 = 4;

    private Image image;
    private int frameW, frameH, rawFrames;
    private int[] sequence;
    private int seqIndex;
    private int refX, refY;
    private int transform;
    private int colX, colY, colW, colH;

    public Sprite(Image img) {
        this(img, img.getWidth(), img.getHeight());
    }

    public Sprite(Image img, int fw, int fh) {
        super(fw, fh);
        setImage(img, fw, fh);
    }

    public Sprite(Sprite s) {
        super(s.width, s.height);
        image = s.image;
        frameW = s.frameW;
        frameH = s.frameH;
        rawFrames = s.rawFrames;
        sequence = s.sequence;
        seqIndex = s.seqIndex;
        refX = s.refX;
        refY = s.refY;
        transform = s.transform;
        colX = s.colX;
        colY = s.colY;
        colW = s.colW;
        colH = s.colH;
        x = s.x;
        y = s.y;
        visible = s.visible;
    }

    public void setImage(Image img, int fw, int fh) {
        if (img == null)
            throw new NullPointerException();
        if (fw <= 0 || fh <= 0 || img.getWidth() % fw != 0 || img.getHeight() % fh != 0)
            throw new IllegalArgumentException();
        int old = rawFrames;
        image = img;
        frameW = fw;
        frameH = fh;
        rawFrames = (img.getWidth() / fw) * (img.getHeight() / fh);
        if (sequence == null || rawFrames < old) {
            sequence = null;
            seqIndex = 0;
        }
        colX = colY = 0;
        colW = fw;
        colH = fh;
        boolean swap = transform >= 4;
        setSize(swap ? fh : fw, swap ? fw : fh);
    }

    public void defineReferencePixel(int x, int y) {
        refX = x;
        refY = y;
    }

    public void setRefPixelPosition(int rx, int ry) {
        int[] p = transformPoint(refX, refY);
        x = rx - p[0];
        y = ry - p[1];
    }

    public int getRefPixelX() { return x + transformPoint(refX, refY)[0]; }
    public int getRefPixelY() { return y + transformPoint(refX, refY)[1]; }

    private int[] transformPoint(int px, int py) {
        int w = frameW, h = frameH, rx, ry;
        switch (transform) {
        case TRANS_MIRROR_ROT180: rx = px; ry = h - 1 - py; break;
        case TRANS_MIRROR: rx = w - 1 - px; ry = py; break;
        case TRANS_ROT180: rx = w - 1 - px; ry = h - 1 - py; break;
        case TRANS_MIRROR_ROT270: rx = py; ry = px; break;
        case TRANS_ROT90: rx = h - 1 - py; ry = px; break;
        case TRANS_ROT270: rx = py; ry = w - 1 - px; break;
        case TRANS_MIRROR_ROT90: rx = h - 1 - py; ry = w - 1 - px; break;
        default: rx = px; ry = py; break;
        }
        return new int[] { rx, ry };
    }

    public void setFrame(int i) {
        if (i < 0 || i >= getFrameSequenceLength())
            throw new IndexOutOfBoundsException();
        seqIndex = i;
    }

    public final int getFrame() { return seqIndex; }
    public int getRawFrameCount() { return rawFrames; }
    public int getFrameSequenceLength() { return sequence != null ? sequence.length : rawFrames; }

    public void nextFrame() {
        seqIndex = (seqIndex + 1) % getFrameSequenceLength();
    }

    public void prevFrame() {
        seqIndex = (seqIndex + getFrameSequenceLength() - 1) % getFrameSequenceLength();
    }

    public void setFrameSequence(int[] seq) {
        if (seq == null) {
            sequence = null;
            seqIndex = 0;
            return;
        }
        if (seq.length == 0)
            throw new IllegalArgumentException();
        for (int i = 0; i < seq.length; i++)
            if (seq[i] < 0 || seq[i] >= rawFrames)
                throw new ArrayIndexOutOfBoundsException();
        sequence = new int[seq.length];
        System.arraycopy(seq, 0, sequence, 0, seq.length);
        seqIndex = 0;
    }

    public void setTransform(int t) {
        if (t < 0 || t > 7)
            throw new IllegalArgumentException();
        int rx = getRefPixelX(), ry = getRefPixelY();
        transform = t;
        boolean swap = t >= 4;
        setSize(swap ? frameH : frameW, swap ? frameW : frameH);
        setRefPixelPosition(rx, ry);
    }

    public void defineCollisionRectangle(int x, int y, int w, int h) {
        if (w < 0 || h < 0)
            throw new IllegalArgumentException();
        colX = x;
        colY = y;
        colW = w;
        colH = h;
    }

    private int frameIndex() {
        return sequence != null ? sequence[seqIndex] : seqIndex;
    }

    public final void paint(Graphics g) {
        if (!visible)
            return;
        int cols = image.getWidth() / frameW, f = frameIndex();
        g.drawRegion(image, (f % cols) * frameW, (f / cols) * frameH, frameW, frameH, transform, x, y,
                Graphics.TOP | Graphics.LEFT);
    }

    /* прямоугольник столкновений в координатах экрана с учётом поворота */
    private int[] colRect() {
        int[] a = transformPoint(colX, colY), b = transformPoint(colX + colW - 1, colY + colH - 1);
        int x1 = Math.min(a[0], b[0]), y1 = Math.min(a[1], b[1]);
        return new int[] { x + x1, y + y1, Math.abs(a[0] - b[0]) + 1, Math.abs(a[1] - b[1]) + 1 };
    }

    private static boolean overlap(int[] r, int x, int y, int w, int h) {
        return r[0] < x + w && x < r[0] + r[2] && r[1] < y + h && y < r[1] + r[3];
    }

    private boolean opaqueAt(int sx, int sy) {
        int lx = sx - x, ly = sy - y;
        int w = frameW, h = frameH, px, py;
        switch (transform) {
        case TRANS_MIRROR_ROT180: px = lx; py = h - 1 - ly; break;
        case TRANS_MIRROR: px = w - 1 - lx; py = ly; break;
        case TRANS_ROT180: px = w - 1 - lx; py = h - 1 - ly; break;
        case TRANS_MIRROR_ROT270: px = ly; py = lx; break;
        case TRANS_ROT90: px = ly; py = h - 1 - lx; break;
        case TRANS_ROT270: px = w - 1 - ly; py = lx; break;
        case TRANS_MIRROR_ROT90: px = w - 1 - ly; py = h - 1 - lx; break;
        default: px = lx; py = ly; break;
        }
        if (px < 0 || py < 0 || px >= w || py >= h)
            return false;
        int cols = image.getWidth() / frameW, f = frameIndex();
        int ix = (f % cols) * frameW + px, iy = (f / cols) * frameH + py;
        return (GameCanvasAccess.pixels(image)[iy * image.getWidth() + ix] >>> 24) != 0;
    }

    public final boolean collidesWith(Sprite s, boolean pixelLevel) {
        if (!visible || !s.visible)
            return false;
        int[] a = colRect(), b = s.colRect();
        if (!overlap(a, b[0], b[1], b[2], b[3]))
            return false;
        if (!pixelLevel)
            return true;
        int x1 = Math.max(a[0], b[0]), y1 = Math.max(a[1], b[1]);
        int x2 = Math.min(a[0] + a[2], b[0] + b[2]), y2 = Math.min(a[1] + a[3], b[1] + b[3]);
        for (int yy = y1; yy < y2; yy++)
            for (int xx = x1; xx < x2; xx++)
                if (opaqueAt(xx, yy) && s.opaqueAt(xx, yy))
                    return true;
        return false;
    }

    public final boolean collidesWith(TiledLayer t, boolean pixelLevel) {
        if (!visible || !t.visible)
            return false;
        int[] a = colRect();
        int c0 = Math.max(0, (a[0] - t.x) / t.cellW), c1 = Math.min(t.cols - 1, (a[0] + a[2] - 1 - t.x) / t.cellW);
        int r0 = Math.max(0, (a[1] - t.y) / t.cellH), r1 = Math.min(t.rows - 1, (a[1] + a[3] - 1 - t.y) / t.cellH);
        if (a[0] + a[2] <= t.x || a[1] + a[3] <= t.y)
            return false;
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++)
                if (t.getCell(c, r) != 0) {
                    if (!pixelLevel)
                        return true;
                    int cx = t.x + c * t.cellW, cy = t.y + r * t.cellH;
                    for (int yy = Math.max(cy, a[1]); yy < Math.min(cy + t.cellH, a[1] + a[3]); yy++)
                        for (int xx = Math.max(cx, a[0]); xx < Math.min(cx + t.cellW, a[0] + a[2]); xx++)
                            if (opaqueAt(xx, yy) && t.opaqueAt(xx, yy))
                                return true;
                }
        return false;
    }

    public final boolean collidesWith(Image img, int ix, int iy, boolean pixelLevel) {
        if (!visible)
            return false;
        int[] a = colRect();
        if (!overlap(a, ix, iy, img.getWidth(), img.getHeight()))
            return false;
        if (!pixelLevel)
            return true;
        int[] px = GameCanvasAccess.pixels(img);
        int x1 = Math.max(a[0], ix), y1 = Math.max(a[1], iy);
        int x2 = Math.min(a[0] + a[2], ix + img.getWidth()), y2 = Math.min(a[1] + a[3], iy + img.getHeight());
        for (int yy = y1; yy < y2; yy++)
            for (int xx = x1; xx < x2; xx++)
                if (opaqueAt(xx, yy) && (px[(yy - iy) * img.getWidth() + (xx - ix)] >>> 24) != 0)
                    return true;
        return false;
    }
}

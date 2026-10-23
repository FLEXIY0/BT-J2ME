package javax.microedition.m3g;

import java.util.Hashtable;
import java.util.Vector;
import javax.microedition.lcdui.Graphics;

/* Рисует спрайты прямо в Graphics: проекция центра спрайта камерой, размер — по масштабу
 * узла, смешивание — по CompositingMode. Глубины нет: порядок — слой, потом дальние раньше. */
public class Graphics3D {
    public static final int ANTIALIAS = 2, DITHER = 4, TRUE_COLOR = 8, OVERWRITE = 16;

    private static Graphics3D instance;
    private Graphics target;
    private int vx, vy, vw, vh;
    private float depthNear = 0, depthFar = 1;
    private Camera camera;
    private final Transform camTransform = new Transform();
    private final Vector lights = new Vector();

    public static Graphics3D getInstance() {
        if (instance == null)
            instance = new Graphics3D();
        return instance;
    }

    public static Hashtable getProperties() {
        Hashtable h = new Hashtable();
        h.put("supportAntialiasing", Boolean.FALSE);
        h.put("supportTrueColor", Boolean.TRUE);
        h.put("supportDithering", Boolean.FALSE);
        h.put("supportMipmapping", Boolean.FALSE);
        h.put("supportPerspectiveCorrection", Boolean.FALSE);
        h.put("supportLocalCameraLighting", Boolean.FALSE);
        h.put("maxLights", new Integer(8));
        h.put("maxViewportWidth", new Integer(1024));
        h.put("maxViewportHeight", new Integer(1024));
        h.put("maxViewportDimension", new Integer(1024));
        h.put("maxTextureDimension", new Integer(1024));
        h.put("maxSpriteCropDimension", new Integer(1024));
        h.put("maxTransformsPerVertex", new Integer(4));
        h.put("numTextureUnits", new Integer(2));
        return h;
    }

    public void bindTarget(Object t) { bindTarget(t, true, 0); }

    public void bindTarget(Object t, boolean depth, int hints) {
        if (t == null)
            throw new NullPointerException();
        if (target != null)
            throw new IllegalStateException("цель уже привязана");
        if (!(t instanceof Graphics))
            throw new IllegalArgumentException("рисовать можно только в Graphics");
        target = (Graphics) t;
        vx = target.getTranslateX() + target.getClipX();
        vy = target.getTranslateY() + target.getClipY();
        vw = target.getClipWidth();
        vh = target.getClipHeight();
    }

    public Object getTarget() { return target; }

    public void releaseTarget() { target = null; }

    public void setViewport(int x, int y, int w, int h) {
        if (w <= 0 || h <= 0)
            throw new IllegalArgumentException();
        vx = (target != null ? target.getTranslateX() : 0) + x;
        vy = (target != null ? target.getTranslateY() : 0) + y;
        vw = w;
        vh = h;
    }

    public int getViewportX() { return vx - (target != null ? target.getTranslateX() : 0); }
    public int getViewportY() { return vy - (target != null ? target.getTranslateY() : 0); }
    public int getViewportWidth() { return vw; }
    public int getViewportHeight() { return vh; }

    public void setDepthRange(float n, float f) { depthNear = n; depthFar = f; }
    public float getDepthRangeNear() { return depthNear; }
    public float getDepthRangeFar() { return depthFar; }
    public boolean isDepthBufferEnabled() { return true; }
    public int getHints() { return 0; }

    public void setCamera(Camera c, Transform t) {
        camera = c;
        if (t == null)
            camTransform.setIdentity();
        else
            camTransform.set(t);
    }

    public Camera getCamera(Transform t) {
        if (t != null)
            t.set(camTransform);
        return camera;
    }

    public int addLight(Object light, Transform t) {
        lights.addElement(light);
        return lights.size() - 1;
    }

    public void setLight(int i, Object light, Transform t) {}
    public void resetLights() { lights.removeAllElements(); }
    public int getLightCount() { return lights.size(); }

    public void clear(Background b) {
        check();
        clearWith(b);
    }

    private void check() {
        if (target == null)
            throw new IllegalStateException("нет цели: сначала bindTarget");
    }

    private void clearWith(Background b) {
        if (b == null) {
            fill0(target, vx, vy, vw, vh, 0xFF000000);
            return;
        }
        if (!b.colorClear)
            return;
        fill0(target, vx, vy, vw, vh, b.color | 0xFF000000);
        if (b.image != null)
            sprite0(target, b.image.px, b.image.w, b.image.h, b.cropX, b.cropY, b.cropW, b.cropH,
                    vx, vy, vw, vh, CompositingMode.ALPHA, 255, 0);
    }

    public void render(World w) {
        check();
        if (w == null)
            throw new NullPointerException();
        Camera c = w.camera;
        if (c == null)
            throw new IllegalStateException("у мира нет камеры");
        Transform ct = new Transform();
        c.worldTransform(ct);
        clearWith(w.background);
        draw(w, c, ct);
    }

    public void render(Node n, Transform t) {
        check();
        if (camera == null)
            throw new IllegalStateException("нет камеры: setCamera");
        draw(n, camera, camTransform);
    }

    private void draw(Node root, Camera c, Transform camWorld) {
        Transform view = new Transform(camWorld);
        view.invert();
        Vector items = new Vector();
        collect(root, items);
        int n = items.size();
        Sprite3D[] sp = new Sprite3D[n];
        float[] z = new float[n], cx = new float[n], cy = new float[n], ww = new float[n], hh = new float[n];
        float[] v = new float[8];
        Transform m = new Transform();
        int count = 0;
        for (int i = 0; i < n; i++) {
            Sprite3D s = (Sprite3D) items.elementAt(i);
            s.worldTransform(m);
            Transform mv = new Transform(view);
            mv.postMultiply(m);
            v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 1;
            v[4] = 1; v[5] = 1; v[6] = 0; v[7] = 1;
            mv.transform(v);
            float d = -v[2];
            if (d < c.near || d > c.far)
                continue;
            /* размер: проекция единичного квадрата (масштабированный) или сама картинка */
            float sx = Math.abs(v[4] - v[0]), sy = Math.abs(v[5] - v[1]);
            float px, py, pw, ph;
            if (c.type == Camera.PERSPECTIVE) {
                float k = (float) (vh / 2 / Math.tan(Math.toRadians(c.fovy / 2)) / d);
                px = vx + vw / 2f + v[0] * k;
                py = vy + vh / 2f - v[1] * k;
                pw = sx * k;
                ph = sy * k;
            } else {
                float hv = c.type == Camera.PARALLEL ? c.fovy : 2, wv = c.type == Camera.PARALLEL ? c.fovy * c.aspect : 2;
                px = vx + vw * (0.5f + v[0] / wv);
                py = vy + vh * (0.5f - v[1] / hv);
                pw = sx * vw / wv;
                ph = sy * vh / hv;
            }
            if (!s.scaled) {
                pw = Math.abs(s.cropW);
                ph = Math.abs(s.cropH);
            }
            sp[count] = s;
            z[count] = d;
            cx[count] = px;
            cy[count] = py;
            ww[count] = pw;
            hh[count] = ph;
            count++;
        }
        /* слой по возрастанию, внутри слоя — сначала дальние */
        for (int i = 1; i < count; i++)
            for (int j = i; j > 0 && after(sp[j - 1], z[j - 1], sp[j], z[j]); j--) {
                Sprite3D ts = sp[j]; sp[j] = sp[j - 1]; sp[j - 1] = ts;
                float t = z[j]; z[j] = z[j - 1]; z[j - 1] = t;
                t = cx[j]; cx[j] = cx[j - 1]; cx[j - 1] = t;
                t = cy[j]; cy[j] = cy[j - 1]; cy[j - 1] = t;
                t = ww[j]; ww[j] = ww[j - 1]; ww[j - 1] = t;
                t = hh[j]; hh[j] = hh[j - 1]; hh[j - 1] = t;
            }
        for (int i = 0; i < count; i++) {
            Sprite3D s = sp[i];
            CompositingMode cm = s.appearance.compositing;
            int blend = cm != null ? cm.blending : CompositingMode.REPLACE;
            int thr = cm != null ? (int) (cm.threshold * 255) : 0;
            if (cm != null && !cm.colorWrite)
                continue;
            int alpha = (int) (alphaOf(s) * 255 + 0.5f);
            int x0 = Math.round(cx[i] - ww[i] / 2), y0 = Math.round(cy[i] - hh[i] / 2);
            int x1 = Math.round(cx[i] + ww[i] / 2), y1 = Math.round(cy[i] + hh[i] / 2);
            if (x1 > x0 && y1 > y0)
                sprite0(target, s.image.px, s.image.w, s.image.h, s.cropX, s.cropY, s.cropW, s.cropH,
                        x0, y0, x1 - x0, y1 - y0, blend, alpha, thr);
        }
    }

    private static boolean after(Sprite3D a, float za, Sprite3D b, float zb) {
        int la = a.appearance.layer, lb = b.appearance.layer;
        return la != lb ? la > lb : za < zb;
    }

    private static float alphaOf(Node n) {
        float a = 1;
        for (; n != null; n = n.parent)
            a *= n.alpha;
        return a;
    }

    private static void collect(Node n, Vector out) {
        if (!n.rendering)
            return;
        if (n instanceof Sprite3D) {
            if (((Sprite3D) n).appearance != null)
                out.addElement(n);
        } else if (n instanceof Group) {
            Group g = (Group) n;
            for (int i = 0; i < g.children.size(); i++)
                collect((Node) g.children.elementAt(i), out);
        }
    }

    private static native void fill0(Graphics g, int x, int y, int w, int h, int argb);
    private static native void sprite0(Graphics g, int[] px, int iw, int ih, int cx, int cy, int cw, int ch,
                                       int dx, int dy, int dw, int dh, int blend, int alpha, int threshold);
}

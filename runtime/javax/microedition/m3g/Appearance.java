package javax.microedition.m3g;

/* Для спрайтов важны только слой и смешивание; остальное хранится, чтобы игры не падали. */
public class Appearance extends Object3D {
    int layer;
    CompositingMode compositing;
    Object material, polygon, fog;
    final Object[] textures = new Object[2];

    public void setLayer(int l) {
        if (l < -63 || l > 63)
            throw new IllegalArgumentException();
        layer = l;
    }

    public int getLayer() { return layer; }
    public void setCompositingMode(CompositingMode c) { compositing = c; }
    public CompositingMode getCompositingMode() { return compositing; }
}

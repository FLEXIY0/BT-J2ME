package javax.microedition.lcdui.game;

import java.util.Vector;

import javax.microedition.lcdui.Graphics;

public class LayerManager {
    private final Vector layers = new Vector();
    private int vx, vy, vw = Integer.MAX_VALUE, vh = Integer.MAX_VALUE;

    public LayerManager() {
    }

    public void append(Layer l) {
        if (l == null)
            throw new NullPointerException();
        layers.removeElement(l);
        layers.addElement(l);
    }

    public void insert(Layer l, int index) {
        if (l == null)
            throw new NullPointerException();
        layers.removeElement(l);
        if (index < 0 || index > layers.size())
            throw new IndexOutOfBoundsException();
        layers.insertElementAt(l, index);
    }

    public void remove(Layer l) {
        if (l == null)
            throw new NullPointerException();
        layers.removeElement(l);
    }

    public Layer getLayerAt(int i) {
        return (Layer) layers.elementAt(i);
    }

    public int getSize() {
        return layers.size();
    }

    public void setViewWindow(int x, int y, int w, int h) {
        if (w < 0 || h < 0)
            throw new IllegalArgumentException();
        vx = x;
        vy = y;
        vw = w;
        vh = h;
    }

    public void paint(Graphics g, int x, int y) {
        int cx = g.getClipX(), cy = g.getClipY(), cw = g.getClipWidth(), ch = g.getClipHeight();
        g.clipRect(x, y, vw, vh);
        g.translate(x - vx, y - vy);
        for (int i = layers.size() - 1; i >= 0; i--) {
            Layer l = (Layer) layers.elementAt(i);
            if (l.isVisible())
                l.paint(g);
        }
        g.translate(vx - x, vy - y);
        g.setClip(cx, cy, cw, ch);
    }
}

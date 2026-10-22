package javax.microedition.lcdui;

/* Элемент формы, который рисует сама игра. */
public abstract class CustomItem extends Item {
    protected static final int TRAVERSE_HORIZONTAL = 1, TRAVERSE_VERTICAL = 2, KEY_PRESS = 4, KEY_RELEASE = 8,
            KEY_REPEAT = 0x10, POINTER_PRESS = 0x20, POINTER_RELEASE = 0x40, POINTER_DRAG = 0x80, NONE = 0;

    protected CustomItem(String label) {
        super(label);
    }

    public int getGameAction(int keyCode) { return Canvas.gameAction(keyCode); }
    protected final int getInteractionModes() { return KEY_PRESS | KEY_RELEASE | POINTER_PRESS; }
    protected abstract int getMinContentWidth();
    protected abstract int getMinContentHeight();
    protected abstract int getPrefContentWidth(int height);
    protected abstract int getPrefContentHeight(int width);
    protected abstract void paint(Graphics g, int w, int h);
    protected void keyPressed(int keyCode) {}
    protected void keyReleased(int keyCode) {}
    protected void keyRepeated(int keyCode) {}
    protected void pointerPressed(int x, int y) {}
    protected void pointerReleased(int x, int y) {}
    protected void pointerDragged(int x, int y) {}
    protected void showNotify() {}
    protected void hideNotify() {}
    protected void sizeChanged(int w, int h) {}
    protected boolean traverse(int dir, int vw, int vh, int[] rect) { return false; }
    protected void traverseOut() {}
    protected final void invalidate() { Display.requestRepaint(); }
    protected final void repaint() { Display.requestRepaint(); }
    protected final void repaint(int x, int y, int w, int h) { Display.requestRepaint(); }

    boolean focusable() { return true; }

    int height(int w) {
        return labelHeight() + getPrefContentHeight(w);
    }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        int h = getPrefContentHeight(w);
        int ox = g.getTranslateX(), oy = g.getTranslateY();
        int cx = g.getClipX(), cy = g.getClipY(), cw = g.getClipWidth(), ch = g.getClipHeight();
        g.translate(x, y);
        g.clipRect(0, 0, w, h);
        try {
            paint(g, w, h);
        } finally {
            g.translate(ox - g.getTranslateX(), oy - g.getTranslateY());
            g.setClip(cx, cy, cw, ch);
        }
    }

    boolean key(int code) {
        keyPressed(code);
        return Canvas.gameAction(code) != Canvas.UP && Canvas.gameAction(code) != Canvas.DOWN;
    }

    void pointer(int x, int y) {
        pointerPressed(x, y - labelHeight());
    }
}

package javax.microedition.lcdui;

public abstract class Canvas extends Displayable {
    public static final int UP = 1, DOWN = 6, LEFT = 2, RIGHT = 5, FIRE = 8;
    public static final int GAME_A = 9, GAME_B = 10, GAME_C = 11, GAME_D = 12;
    public static final int KEY_NUM0 = 48, KEY_NUM1 = 49, KEY_NUM2 = 50, KEY_NUM3 = 51, KEY_NUM4 = 52,
            KEY_NUM5 = 53, KEY_NUM6 = 54, KEY_NUM7 = 55, KEY_NUM8 = 56, KEY_NUM9 = 57, KEY_STAR = 42, KEY_POUND = 35;

    boolean fullScreen;
    boolean suppressGameKeys;

    protected Canvas() {
    }

    public boolean isDoubleBuffered() { return true; }
    public boolean hasPointerEvents() { return true; }
    public boolean hasPointerMotionEvents() { return true; }
    public boolean hasRepeatEvents() { return true; }

    static int gameAction(int keyCode) {
        switch (keyCode) {
        case -1: case KEY_NUM2: return UP;
        case -2: case KEY_NUM8: return DOWN;
        case -3: case KEY_NUM4: return LEFT;
        case -4: case KEY_NUM6: return RIGHT;
        case -5: case KEY_NUM5: return FIRE;
        case KEY_NUM1: return GAME_A;
        case KEY_NUM3: return GAME_B;
        case KEY_NUM7: return GAME_C;
        case KEY_NUM9: return GAME_D;
        default: return 0;
        }
    }

    public int getGameAction(int keyCode) {
        return gameAction(keyCode);
    }

    public int getKeyCode(int action) {
        switch (action) {
        case UP: return -1;
        case DOWN: return -2;
        case LEFT: return -3;
        case RIGHT: return -4;
        case FIRE: return -5;
        case GAME_A: return KEY_NUM1;
        case GAME_B: return KEY_NUM3;
        case GAME_C: return KEY_NUM7;
        case GAME_D: return KEY_NUM9;
        default: throw new IllegalArgumentException();
        }
    }

    public String getKeyName(int keyCode) {
        switch (keyCode) {
        case -1: return "Up";
        case -2: return "Down";
        case -3: return "Left";
        case -4: return "Right";
        case -5: return "Select";
        case -6: return "Soft1";
        case -7: return "Soft2";
        case KEY_STAR: return "*";
        case KEY_POUND: return "#";
        default:
            if (keyCode >= KEY_NUM0 && keyCode <= KEY_NUM9)
                return String.valueOf((char) keyCode);
            throw new IllegalArgumentException();
        }
    }

    protected void keyPressed(int keyCode) {}
    protected void keyReleased(int keyCode) {}
    protected void keyRepeated(int keyCode) {}
    protected void pointerPressed(int x, int y) {}
    protected void pointerReleased(int x, int y) {}
    protected void pointerDragged(int x, int y) {}
    protected void showNotify() {}
    protected void hideNotify() {}

    protected abstract void paint(Graphics g);

    public final void repaint(int x, int y, int w, int h) {
        Display.requestRepaint();
    }

    public final void repaint() {
        Display.requestRepaint();
    }

    public final void serviceRepaints() {
        Display.serviceRepaints(this);
    }

    public void setFullScreenMode(boolean mode) {
        if (fullScreen != mode) {
            fullScreen = mode;
            sizeChanged(getWidth(), getHeight());
            Display.requestRepaint();
        }
    }

    public int getWidth() {
        return Display.W;
    }

    public int getHeight() {
        return fullScreen ? Display.H : Display.H - Theme.SOFTBAR_H;
    }

    boolean hasSoftBar() {
        return !fullScreen;
    }

    void paintInternal(Graphics g) {
        g.reset();
        g.setClip(0, 0, getWidth(), getHeight());
        paint(g);
        if (!fullScreen)
            paintSoftBar(g);
    }

    private boolean gameKey(int code) {
        return suppressGameKeys && gameAction(code) != 0;
    }

    void keyDown(int code, boolean printable) {
        if (!gameKey(code))
            keyPressed(code);
    }

    void keyUp(int code) {
        if (!gameKey(code))
            keyReleased(code);
    }

    void keyRepeat(int code) {
        if (!gameKey(code))
            keyRepeated(code);
    }

    void pointerDown(int x, int y) { pointerPressed(x, y); }
    void pointerUp(int x, int y) { pointerReleased(x, y); }
    void pointerDrag(int x, int y) { pointerDragged(x, y); }

    void showNotifyInternal() {
        super.showNotifyInternal();
        showNotify();
    }

    void hideNotifyInternal() {
        super.hideNotifyInternal();
        hideNotify();
    }
}

package javax.microedition.lcdui;

public class Gauge extends Item {
    public static final int INDEFINITE = -1, CONTINUOUS_IDLE = 0, INCREMENTAL_IDLE = 1, CONTINUOUS_RUNNING = 2,
            INCREMENTAL_UPDATING = 3;

    private final boolean interactive;
    private int max, value;

    public Gauge(String label, boolean interactive, int maxValue, int initialValue) {
        super(label);
        this.interactive = interactive;
        max = maxValue;
        setValue(initialValue);
    }

    public void setValue(int v) {
        value = max > 0 ? Math.max(0, Math.min(max, v)) : v;
        Display.requestRepaint();
    }

    public int getValue() { return value; }

    public void setMaxValue(int m) {
        max = m;
        setValue(value);
    }

    public int getMaxValue() { return max; }
    public boolean isInteractive() { return interactive; }

    boolean focusable() { return interactive || super.focusable(); }

    int height(int w) { return labelHeight() + 10; }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        g.setColor(focused ? Theme.ACCENT : Theme.DIVIDER);
        g.drawRect(x, y, w - 1, 9);
        g.setColor(Theme.ACCENT);
        int fill = max > 0 ? (w - 4) * value / max : (int) (System.currentTimeMillis() / 50 % (w - 4));
        g.fillRect(x + 2, y + 2, max > 0 ? fill : 8, 6);
    }

    boolean key(int code) {
        if (!interactive)
            return super.key(code);
        int a = Canvas.gameAction(code);
        if (a == Canvas.LEFT || a == Canvas.RIGHT) {
            setValue(value + (a == Canvas.LEFT ? -1 : 1));
            notifyStateChanged();
            return true;
        }
        return super.key(code);
    }
}

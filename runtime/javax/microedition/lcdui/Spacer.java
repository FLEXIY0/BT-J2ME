package javax.microedition.lcdui;

public class Spacer extends Item {
    private int minW, minH;

    public Spacer(int minWidth, int minHeight) {
        super(null);
        setMinimumSize(minWidth, minHeight);
    }

    public void setMinimumSize(int w, int h) {
        if (w < 0 || h < 0)
            throw new IllegalArgumentException();
        minW = w;
        minH = h;
    }

    public void addCommand(Command c) { throw new IllegalStateException(); }
    public void setDefaultCommand(Command c) { throw new IllegalStateException(); }
    public void setLabel(String s) { throw new IllegalStateException(); }

    int height(int w) { return minH; }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
    }
}

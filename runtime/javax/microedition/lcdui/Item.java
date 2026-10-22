package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Item {
    public static final int LAYOUT_DEFAULT = 0, LAYOUT_LEFT = 1, LAYOUT_RIGHT = 2, LAYOUT_CENTER = 3,
            LAYOUT_TOP = 0x10, LAYOUT_BOTTOM = 0x20, LAYOUT_VCENTER = 0x30, LAYOUT_NEWLINE_BEFORE = 0x100,
            LAYOUT_NEWLINE_AFTER = 0x200, LAYOUT_SHRINK = 0x400, LAYOUT_EXPAND = 0x800, LAYOUT_VSHRINK = 0x1000,
            LAYOUT_VEXPAND = 0x2000, LAYOUT_2 = 0x4000;
    public static final int PLAIN = 0, HYPERLINK = 1, BUTTON = 2;

    String label;
    int layout;
    Screen owner;
    final Vector itemCommands = new Vector();
    ItemCommandListener itemListener;
    Command defaultCommand;
    int prefW = -1, prefH = -1;

    Item(String label) {
        this.label = label;
    }

    public String getLabel() { return label; }

    public void setLabel(String s) {
        label = s;
        Display.requestRepaint();
    }

    public int getLayout() { return layout; }
    public void setLayout(int l) { layout = l; }

    public void addCommand(Command c) {
        if (c == null)
            throw new NullPointerException();
        if (!itemCommands.contains(c))
            itemCommands.addElement(c);
    }

    public void removeCommand(Command c) {
        itemCommands.removeElement(c);
        if (c == defaultCommand)
            defaultCommand = null;
    }

    public void setItemCommandListener(ItemCommandListener l) { itemListener = l; }

    public void setDefaultCommand(Command c) {
        defaultCommand = c;
        if (c != null)
            addCommand(c);
    }

    public int getPreferredWidth() { return prefW > 0 ? prefW : Display.W - 8; }
    public int getPreferredHeight() { return prefH > 0 ? prefH : height(Display.W - 8); }
    public int getMinimumWidth() { return 16; }
    public int getMinimumHeight() { return 8; }

    public void setPreferredSize(int w, int h) {
        prefW = w;
        prefH = h;
    }

    public void notifyStateChanged() {
        if (owner instanceof Form)
            ((Form) owner).itemChanged(this);
    }

    int labelHeight() {
        return label != null && label.length() > 0 ? Theme.SMALL.getHeight() + 2 : 0;
    }

    int paintLabel(Graphics g, int x, int y) {
        if (label == null || label.length() == 0)
            return y;
        g.setFont(Theme.SMALL);
        g.setColor(Theme.SUB);
        g.drawString(label, x, y, Graphics.TOP | Graphics.LEFT);
        return y + Theme.SMALL.getHeight() + 2;
    }

    abstract int height(int w);

    abstract void paint(Graphics g, int x, int y, int w, boolean focused);

    boolean focusable() {
        return !itemCommands.isEmpty();
    }

    /* true — клавиша обработана элементом */
    boolean key(int code) {
        if (Canvas.gameAction(code) == Canvas.FIRE && itemListener != null && !itemCommands.isEmpty()) {
            Command c = defaultCommand != null ? defaultCommand : (Command) itemCommands.elementAt(0);
            try {
                itemListener.commandAction(c, this);
            } catch (Throwable t) {
                Display.report(t);
            }
            return true;
        }
        return false;
    }

    void charTyped(char c) {
    }

    void pointer(int x, int y) {
        key(-5);
    }

    /* курсор внутри элемента: для прокрутки формы к видимой части */
    int cursorY(int w) {
        return 0;
    }
}

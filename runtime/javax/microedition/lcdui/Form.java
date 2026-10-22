package javax.microedition.lcdui;

import java.util.Vector;

public class Form extends Screen {
    final Vector items = new Vector();
    private ItemStateListener stateListener;
    private int focus = -1;

    public Form(String title) {
        this.title = title;
    }

    public Form(String title, Item[] list) {
        this(title);
        if (list != null)
            for (int i = 0; i < list.length; i++)
                append(list[i]);
    }

    public int append(Item item) {
        if (item == null)
            throw new NullPointerException();
        if (item.owner != null)
            throw new IllegalStateException();
        item.owner = this;
        items.addElement(item);
        if (focus < 0 && item.focusable())
            focus = items.size() - 1;
        Display.requestRepaint();
        return items.size() - 1;
    }

    public int append(String s) {
        return append(new StringItem(null, s));
    }

    public int append(Image img) {
        return append(new ImageItem(null, img, Item.LAYOUT_DEFAULT, null));
    }

    public void insert(int i, Item item) {
        if (i < 0 || i > items.size())
            throw new IndexOutOfBoundsException();
        if (item.owner != null)
            throw new IllegalStateException();
        item.owner = this;
        items.insertElementAt(item, i);
        if (focus >= i)
            focus++;
        Display.requestRepaint();
    }

    public void delete(int i) {
        Item it = (Item) items.elementAt(i);
        it.owner = null;
        items.removeElementAt(i);
        if (focus == i)
            focus = nextFocusable(i - 1, 1);
        else if (focus > i)
            focus--;
        Display.requestRepaint();
    }

    public void deleteAll() {
        for (int i = 0; i < items.size(); i++)
            ((Item) items.elementAt(i)).owner = null;
        items.removeAllElements();
        focus = -1;
        scroll = 0;
        Display.requestRepaint();
    }

    public void set(int i, Item item) {
        Item old = (Item) items.elementAt(i);
        if (item.owner != null)
            throw new IllegalStateException();
        old.owner = null;
        item.owner = this;
        items.setElementAt(item, i);
        Display.requestRepaint();
    }

    public Item get(int i) {
        return (Item) items.elementAt(i);
    }

    public int size() {
        return items.size();
    }

    public void setItemStateListener(ItemStateListener l) {
        stateListener = l;
    }

    void itemChanged(Item item) {
        if (stateListener != null) {
            try {
                stateListener.itemStateChanged(item);
            } catch (Throwable t) {
                Display.report(t);
            }
        }
    }

    void focusItem(Item item) {
        int i = items.indexOf(item);
        if (i >= 0 && item.focusable())
            focus = i;
    }

    private int nextFocusable(int from, int dir) {
        for (int i = from + dir; i >= 0 && i < items.size(); i += dir)
            if (((Item) items.elementAt(i)).focusable())
                return i;
        return -1;
    }

    private int itemTop(int idx) {
        int y = 4, w = Display.W - 8;
        for (int i = 0; i < idx; i++)
            y += ((Item) items.elementAt(i)).height(w) + 6;
        return y;
    }

    void paintContent(Graphics g, int w, int h) {
        int y = 4 - scroll, iw = w - 8;
        for (int i = 0; i < items.size(); i++) {
            Item it = (Item) items.elementAt(i);
            int ih = it.height(iw);
            if (y + ih >= 0 && y < h)
                it.paint(g, 4, y, iw, i == focus);
            y += ih + 6;
        }
    }

    private void revealFocus() {
        if (focus < 0)
            return;
        Item it = (Item) items.elementAt(focus);
        int top = itemTop(focus), w = Display.W - 8;
        int h = it.height(w);
        if (h > contentHeight()) {
            int cy = top + it.cursorY(w);
            reveal(cy, Theme.MEDIUM.getHeight() + 6);
        } else {
            reveal(top, h);
        }
    }

    void keyDown(int code, boolean printable) {
        Item it = focus >= 0 ? (Item) items.elementAt(focus) : null;
        if (it instanceof TextField && printable)
            return;   /* буквы и цифры придут символами (charTyped) */
        if (it != null && it.key(code)) {
            revealFocus();
            Display.requestRepaint();
            return;
        }
        int a = Canvas.gameAction(code);
        if (a == Canvas.DOWN || a == Canvas.UP) {
            int dir = a == Canvas.DOWN ? 1 : -1;
            int n = nextFocusable(focus, dir);
            if (n >= 0) {
                focus = n;
                Item ni = (Item) items.elementAt(n);
                if (ni instanceof ChoiceGroup)
                    ((ChoiceGroup) ni).enterFrom(dir < 0);
                revealFocus();
            } else {
                int total = itemTop(items.size());
                scroll = Math.max(0, Math.min(Math.max(0, total - contentHeight()), scroll + dir * 24));
            }
            Display.requestRepaint();
        }
    }

    void keyRepeat(int code) {
        int a = Canvas.gameAction(code);
        if (a == Canvas.UP || a == Canvas.DOWN || code == -8)
            keyDown(code, false);
    }

    void charTyped(char c) {
        if (focus >= 0)
            ((Item) items.elementAt(focus)).charTyped(c);
    }

    void contentPointer(int x, int y) {
        int top = 4, w = Display.W - 8;
        for (int i = 0; i < items.size(); i++) {
            Item it = (Item) items.elementAt(i);
            int h = it.height(w);
            if (y >= top && y < top + h) {
                if (it.focusable()) {
                    focus = i;
                    it.pointer(x - 4, y - top);
                }
                Display.requestRepaint();
                return;
            }
            top += h + 6;
        }
    }
}

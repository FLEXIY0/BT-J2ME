package javax.microedition.lcdui;

public class ChoiceGroup extends Item implements Choice {
    private final ChoiceModel m;

    public ChoiceGroup(String label, int type) {
        this(label, type, new String[0], null);
    }

    public ChoiceGroup(String label, int type, String[] elems, Image[] imgs) {
        super(label);
        if (type != EXCLUSIVE && type != MULTIPLE && type != POPUP)
            throw new IllegalArgumentException();
        m = new ChoiceModel(type);
        for (int i = 0; i < elems.length; i++)
            m.append(elems[i], imgs != null ? imgs[i] : null);
    }

    public int size() { return m.size(); }
    public String getString(int i) { m.check(i); return (String) m.strings.elementAt(i); }
    public Image getImage(int i) { m.check(i); return (Image) m.images.elementAt(i); }
    public int append(String s, Image img) { int r = m.append(s, img); Display.requestRepaint(); return r; }
    public void insert(int i, String s, Image img) { m.insert(i, s, img); Display.requestRepaint(); }
    public void delete(int i) { m.delete(i); Display.requestRepaint(); }
    public void deleteAll() { m.deleteAll(); Display.requestRepaint(); }
    public void set(int i, String s, Image img) { m.set(i, s, img); Display.requestRepaint(); }
    public boolean isSelected(int i) { return m.isSelected(i); }
    public int getSelectedIndex() { return m.selectedIndex(); }
    public int getSelectedFlags(boolean[] f) { return m.getFlags(f); }
    public void setSelectedIndex(int i, boolean s) { m.setSelected(i, s); Display.requestRepaint(); }
    public void setSelectedFlags(boolean[] f) { m.setFlags(f); Display.requestRepaint(); }
    public void setFitPolicy(int p) { m.fit = p; }
    public int getFitPolicy() { return m.fit; }
    public void setFont(int i, Font f) {}
    public Font getFont(int i) { return Theme.MEDIUM; }

    boolean focusable() { return m.size() > 0; }

    int height(int w) {
        if (m.type == POPUP)
            return labelHeight() + Theme.MEDIUM.getHeight() + 8;
        return labelHeight() + m.height();
    }

    int cursorY(int w) {
        return labelHeight() + (m.type == POPUP ? 0 : m.rowTop(m.cursor));
    }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        if (m.type == POPUP) {
            int h = Theme.MEDIUM.getHeight() + 6;
            g.setColor(Theme.FIELD);
            g.fillRect(x, y, w, h);
            g.setColor(focused ? Theme.ACCENT : Theme.DIVIDER);
            g.drawRect(x, y, w - 1, h - 1);
            int sel = m.selectedIndex();
            g.setFont(Theme.MEDIUM);
            g.setColor(Theme.TEXT);
            if (sel >= 0)
                g.drawString((String) m.strings.elementAt(sel), x + 14, y + 3, Graphics.TOP | Graphics.LEFT);
            g.setColor(focused ? Theme.ACCENT : Theme.SUB);
            g.fillTriangle(x + 4, y + h / 2, x + 9, y + 3, x + 9, y + h - 4);
            g.fillTriangle(x + w - 5, y + h / 2, x + w - 10, y + 3, x + w - 10, y + h - 4);
            return;
        }
        m.paint(g, x, y, w, focused, true);
    }

    boolean key(int code) {
        int a = Canvas.gameAction(code);
        if (m.size() == 0)
            return super.key(code);
        if (m.type == POPUP) {
            if (a == Canvas.LEFT || a == Canvas.RIGHT || a == Canvas.FIRE) {
                int sel = Math.max(0, m.selectedIndex());
                sel = (sel + (a == Canvas.LEFT ? m.size() - 1 : 1)) % m.size();
                m.setSelected(sel, true);
                notifyStateChanged();
                Display.requestRepaint();
                return true;
            }
            return false;
        }
        if (a == Canvas.UP && m.cursor > 0) {
            m.cursor--;
            Display.requestRepaint();
            return true;
        }
        if (a == Canvas.DOWN && m.cursor < m.size() - 1) {
            m.cursor++;
            Display.requestRepaint();
            return true;
        }
        if (a == Canvas.FIRE) {
            m.setSelected(m.cursor, m.type == MULTIPLE ? !m.isSelected(m.cursor) : true);
            notifyStateChanged();
            Display.requestRepaint();
            return true;
        }
        return false;
    }

    void enterFrom(boolean below) {
        if (m.type != POPUP)
            m.cursor = below ? m.size() - 1 : 0;
    }

    void pointer(int x, int y) {
        int r = m.type == POPUP ? -1 : m.rowAt(y - labelHeight());
        if (r >= 0) {
            m.cursor = r;
            key(-5);
        } else if (m.type == POPUP) {
            key(-5);
        }
    }
}

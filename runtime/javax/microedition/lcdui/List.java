package javax.microedition.lcdui;

import java.util.Vector;

public class List extends Screen implements Choice {
    public static final Command SELECT_COMMAND = new Command("", Command.SCREEN, 0);

    private final ChoiceModel m;
    private Command selectCommand = SELECT_COMMAND;

    public List(String title, int type) {
        this(title, type, new String[0], null);
    }

    public List(String title, int type, String[] elems, Image[] imgs) {
        if (type != IMPLICIT && type != EXCLUSIVE && type != MULTIPLE)
            throw new IllegalArgumentException();
        this.title = title;
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
    public void deleteAll() { m.deleteAll(); scroll = 0; Display.requestRepaint(); }
    public void set(int i, String s, Image img) { m.set(i, s, img); Display.requestRepaint(); }
    public boolean isSelected(int i) { return m.type == IMPLICIT ? i == m.cursor : m.isSelected(i); }
    public int getSelectedIndex() { return m.type == IMPLICIT ? (m.size() > 0 ? m.cursor : -1) : m.selectedIndex(); }

    public int getSelectedFlags(boolean[] f) {
        if (m.type == IMPLICIT) {
            for (int i = 0; i < m.size(); i++)
                f[i] = i == m.cursor;
            return m.size() > 0 ? 1 : 0;
        }
        return m.getFlags(f);
    }

    public void setSelectedIndex(int i, boolean s) {
        if (m.type == IMPLICIT) {
            m.check(i);
            if (s)
                m.cursor = i;
        } else {
            m.setSelected(i, s);
            if (s)
                m.cursor = i;
        }
        Display.requestRepaint();
    }

    public void setSelectedFlags(boolean[] f) {
        m.setFlags(f);
        if (m.type == IMPLICIT)
            for (int i = 0; i < f.length && i < m.size(); i++)
                if (f[i]) {
                    m.cursor = i;
                    break;
                }
        Display.requestRepaint();
    }

    public void setFitPolicy(int p) { m.fit = p; }
    public int getFitPolicy() { return m.fit; }
    public void setFont(int i, Font f) {}
    public Font getFont(int i) { return Theme.MEDIUM; }

    public void setSelectCommand(Command c) {
        if (m.type != IMPLICIT)
            return;
        if (c != null && c != SELECT_COMMAND)
            addCommand(c);
        selectCommand = c;
        layoutSoft();
    }

    public void removeCommand(Command c) {
        if (c == selectCommand)
            selectCommand = null;
        super.removeCommand(c);
    }

    Vector visibleCommands() {
        if (selectCommand == null || selectCommand == SELECT_COMMAND || !commands.contains(selectCommand))
            return commands;
        Vector v = new Vector();
        for (int i = 0; i < commands.size(); i++)
            if (commands.elementAt(i) != selectCommand)
                v.addElement(commands.elementAt(i));
        return v;
    }

    void paintContent(Graphics g, int w, int h) {
        m.paint(g, 0, -scroll, w, true, m.type != IMPLICIT);
    }

    private void moveCursor(int d) {
        if (m.size() == 0)
            return;
        m.cursor = (m.cursor + d + m.size()) % m.size();
        reveal(m.rowTop(m.cursor), m.rowHeight(m.cursor));
        Display.requestRepaint();
    }

    private void activate() {
        if (m.size() == 0)
            return;
        if (m.type == IMPLICIT) {
            if (selectCommand != null)
                fireCommand(selectCommand);
        } else {
            m.setSelected(m.cursor, m.type == MULTIPLE ? !m.isSelected(m.cursor) : true);
            Display.requestRepaint();
        }
    }

    void keyDown(int code, boolean printable) {
        int a = Canvas.gameAction(code);
        if (a == Canvas.UP)
            moveCursor(-1);
        else if (a == Canvas.DOWN)
            moveCursor(1);
        else if (a == Canvas.FIRE)
            activate();
    }

    void keyRepeat(int code) {
        int a = Canvas.gameAction(code);
        if (a == Canvas.UP || a == Canvas.DOWN)
            keyDown(code, false);
    }

    void contentPointer(int x, int y) {
        int r = m.rowAt(y);
        if (r < 0)
            return;
        if (r == m.cursor || m.type != IMPLICIT) {
            m.cursor = r;
            activate();
        } else {
            m.cursor = r;
        }
        Display.requestRepaint();
    }
}

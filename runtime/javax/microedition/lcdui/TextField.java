package javax.microedition.lcdui;

import java.util.Vector;

public class TextField extends Item {
    public static final int ANY = 0, EMAILADDR = 1, NUMERIC = 2, PHONENUMBER = 3, URL = 4, DECIMAL = 5;
    public static final int PASSWORD = 0x10000, UNEDITABLE = 0x20000, SENSITIVE = 0x40000, NON_PREDICTIVE = 0x80000,
            INITIAL_CAPS_WORD = 0x100000, INITIAL_CAPS_SENTENCE = 0x200000, CONSTRAINT_MASK = 0xFFFF;

    final StringBuffer text = new StringBuffer();
    int maxSize;
    int constraints;

    public TextField(String label, String text, int maxSize, int constraints) {
        super(label);
        if (maxSize <= 0)
            throw new IllegalArgumentException();
        this.maxSize = maxSize;
        setConstraints(constraints);
        setString(text);
    }

    public String getString() { return text.toString(); }

    public void setString(String s) {
        text.setLength(0);
        if (s != null) {
            if (s.length() > maxSize)
                throw new IllegalArgumentException();
            text.append(s);
        }
        Display.requestRepaint();
    }

    public int getChars(char[] data) {
        text.getChars(0, text.length(), data, 0);
        return text.length();
    }

    public void setChars(char[] data, int off, int len) {
        setString(data == null ? null : new String(data, off, len));
    }

    public void insert(String s, int pos) {
        if (text.length() + s.length() > maxSize)
            throw new IllegalArgumentException();
        text.insert(Math.max(0, Math.min(pos, text.length())), s);
        Display.requestRepaint();
    }

    public void insert(char[] data, int off, int len, int pos) {
        insert(new String(data, off, len), pos);
    }

    public void delete(int off, int len) {
        text.delete(off, off + len);
        Display.requestRepaint();
    }

    public int getMaxSize() { return maxSize; }

    public int setMaxSize(int m) {
        if (m <= 0)
            throw new IllegalArgumentException();
        maxSize = m;
        if (text.length() > m)
            text.setLength(m);
        return m;
    }

    public int size() { return text.length(); }
    public int getCaretPosition() { return text.length(); }

    public void setConstraints(int c) {
        if ((c & CONSTRAINT_MASK) > DECIMAL)
            throw new IllegalArgumentException();
        constraints = c;
    }

    public int getConstraints() { return constraints; }
    public void setInitialInputMode(String mode) {}

    boolean focusable() { return true; }

    static boolean allowed(int constraints, char c, String cur) {
        switch (constraints & CONSTRAINT_MASK) {
        case NUMERIC: return Character.isDigit(c) || (c == '-' && cur.length() == 0);
        case DECIMAL: return Character.isDigit(c) || c == '.' || (c == '-' && cur.length() == 0);
        case PHONENUMBER: return Character.isDigit(c) || c == '+' || c == '*' || c == '#' || c == ' ';
        default: return c >= ' ';
        }
    }

    static String shown(String s, int constraints) {
        if ((constraints & PASSWORD) == 0)
            return s;
        StringBuffer b = new StringBuffer();
        for (int i = 0; i < s.length(); i++)
            b.append('*');
        return b.toString();
    }

    int height(int w) {
        return labelHeight() + Math.max(1, Theme.wrap(shown(getString(), constraints) + "_", Theme.MEDIUM, w - 8).size())
                * (Theme.MEDIUM.getHeight() + 1) + 6;
    }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        Vector lines = Theme.wrap(shown(getString(), constraints) + (focused ? "_" : ""), Theme.MEDIUM, w - 8);
        int bh = Math.max(1, lines.size()) * (Theme.MEDIUM.getHeight() + 1) + 6;
        g.setColor(Theme.FIELD);
        g.fillRect(x, y, w, bh);
        g.setColor(focused ? Theme.ACCENT : Theme.DIVIDER);
        g.drawRect(x, y, w - 1, bh - 1);
        g.setFont(Theme.MEDIUM);
        g.setColor(Theme.TEXT);
        for (int i = 0; i < lines.size(); i++)
            g.drawString((String) lines.elementAt(i), x + 4, y + 3 + i * (Theme.MEDIUM.getHeight() + 1),
                    Graphics.TOP | Graphics.LEFT);
    }

    boolean key(int code) {
        if (code == -8 && (constraints & UNEDITABLE) == 0) {
            if (text.length() > 0) {
                text.deleteCharAt(text.length() - 1);
                notifyStateChanged();
                Display.requestRepaint();
            }
            return true;
        }
        return super.key(code);
    }

    void charTyped(char c) {
        if ((constraints & UNEDITABLE) != 0 || text.length() >= maxSize || !allowed(constraints, c, getString()))
            return;
        text.append(c);
        notifyStateChanged();
        Display.requestRepaint();
    }
}

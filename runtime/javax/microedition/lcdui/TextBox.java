package javax.microedition.lcdui;

public class TextBox extends Screen {
    private final TextField field;

    public TextBox(String title, String text, int maxSize, int constraints) {
        this.title = title;
        field = new TextField(null, text, maxSize, constraints);
        field.owner = this;
    }

    public String getString() { return field.getString(); }
    public void setString(String s) { field.setString(s); }
    public int getChars(char[] d) { return field.getChars(d); }
    public void setChars(char[] d, int off, int len) { field.setChars(d, off, len); }
    public void insert(String s, int pos) { field.insert(s, pos); }
    public void insert(char[] d, int off, int len, int pos) { field.insert(d, off, len, pos); }
    public void delete(int off, int len) { field.delete(off, len); }
    public int getMaxSize() { return field.getMaxSize(); }
    public int setMaxSize(int m) { return field.setMaxSize(m); }
    public int size() { return field.size(); }
    public int getCaretPosition() { return field.getCaretPosition(); }
    public void setConstraints(int c) { field.setConstraints(c); }
    public int getConstraints() { return field.getConstraints(); }
    public void setInitialInputMode(String m) {}

    void paintContent(Graphics g, int w, int h) {
        field.paint(g, 4, 4 - scroll, w - 8, true);
    }

    void keyDown(int code, boolean printable) {
        if (!printable)
            field.key(code);
    }

    void keyRepeat(int code) {
        if (code == -8)
            field.key(code);
    }

    void charTyped(char c) {
        field.charTyped(c);
    }
}

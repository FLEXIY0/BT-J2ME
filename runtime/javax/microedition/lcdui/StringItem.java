package javax.microedition.lcdui;

import java.util.Vector;

public class StringItem extends Item {
    private String text;
    private Font font;
    private final int appearance;

    public StringItem(String label, String text) {
        this(label, text, PLAIN);
    }

    public StringItem(String label, String text, int appearance) {
        super(label);
        this.text = text;
        this.appearance = appearance;
    }

    public String getText() { return text; }

    public void setText(String s) {
        text = s;
        Display.requestRepaint();
    }

    public int getAppearanceMode() { return appearance; }
    public void setFont(Font f) { font = f; }
    public Font getFont() { return font != null ? font : Theme.MEDIUM; }

    int height(int w) {
        Font f = getFont();
        return labelHeight() + Theme.wrap(text, f, w).size() * (f.getHeight() + 1) + (appearance == BUTTON ? 6 : 0);
    }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        Font f = getFont();
        Vector lines = Theme.wrap(text, f, w - (appearance == BUTTON ? 8 : 0));
        if (appearance == BUTTON) {
            int bh = lines.size() * (f.getHeight() + 1) + 6;
            g.setColor(focused ? Theme.ACCENT : Theme.BAR);
            g.fillRect(x, y, w, bh);
            x += 4;
            y += 3;
        } else if (focused) {
            g.setColor(Theme.BAR);
            g.fillRect(x - 2, y - 1, w + 4, lines.size() * (f.getHeight() + 1) + 2);
        }
        g.setFont(f);
        g.setColor(appearance == BUTTON && focused ? Theme.SEL_TEXT : appearance == HYPERLINK ? Theme.ACCENT : Theme.TEXT);
        for (int i = 0; i < lines.size(); i++) {
            g.drawString((String) lines.elementAt(i), x, y, Graphics.TOP | Graphics.LEFT);
            y += f.getHeight() + 1;
        }
    }
}

package javax.microedition.lcdui;

import java.util.Vector;

public class Alert extends Screen {
    public static final int FOREVER = -2;
    public static final Command DISMISS_COMMAND = new Command("OK", Command.OK, 0);

    private String text;
    private Image image;
    private AlertType type;
    private int timeout = getDefaultTimeout();
    private Gauge indicator;
    Displayable next;

    public Alert(String title) {
        this(title, null, null, null);
    }

    public Alert(String title, String text, Image image, AlertType type) {
        this.title = title;
        this.text = text;
        this.image = image;
        this.type = type;
    }

    public int getDefaultTimeout() { return 2000; }
    public int getTimeout() { return timeout; }

    public void setTimeout(int t) {
        if (t <= 0 && t != FOREVER)
            throw new IllegalArgumentException();
        timeout = t;
    }

    public AlertType getType() { return type; }
    public void setType(AlertType t) { type = t; }
    public String getString() { return text; }

    public void setString(String s) {
        text = s;
        Display.requestRepaint();
    }

    public Image getImage() { return image; }

    public void setImage(Image img) {
        image = img;
        Display.requestRepaint();
    }

    public void setIndicator(Gauge g) { indicator = g; }
    public Gauge getIndicator() { return indicator; }

    boolean isModal() {
        return timeout == FOREVER || !commands.isEmpty();
    }

    Vector visibleCommands() {
        if (!commands.isEmpty())
            return commands;
        Vector v = new Vector();
        v.addElement(DISMISS_COMMAND);
        return v;
    }

    void dismiss() {
        if (listener != null)
            Display.invokeCommand(listener, DISMISS_COMMAND, this);
        else if (Display.instance != null && next != null)
            Display.instance.show(next);
    }

    void fireCommand(Command c) {
        if (listener != null)
            Display.invokeCommand(listener, c, this);
        else if (Display.instance != null && next != null)
            Display.instance.show(next);
    }

    void keyDown(int code, boolean printable) {
        if (Canvas.gameAction(code) == Canvas.FIRE) {
            Vector v = visibleCommands();
            fireCommand((Command) v.elementAt(0));
        } else if (Canvas.gameAction(code) == Canvas.DOWN) {
            scroll += 12;
            Display.requestRepaint();
        } else if (Canvas.gameAction(code) == Canvas.UP) {
            scroll = Math.max(0, scroll - 12);
            Display.requestRepaint();
        }
    }

    void paintContent(Graphics g, int w, int h) {
        int y = 8 - scroll;
        if (image != null) {
            g.drawImage(image, w / 2, y, Graphics.TOP | Graphics.HCENTER);
            y += image.getHeight() + 8;
        }
        Font f = Theme.MEDIUM;
        g.setFont(f);
        g.setColor(Theme.TEXT);
        Vector lines = Theme.wrap(text, f, w - 16);
        for (int i = 0; i < lines.size(); i++) {
            g.drawString((String) lines.elementAt(i), w / 2, y, Graphics.TOP | Graphics.HCENTER);
            y += f.getHeight() + 2;
        }
        if (indicator != null)
            indicator.paint(g, 8, y + 6, w - 16, false);
    }
}

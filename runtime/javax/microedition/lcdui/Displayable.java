package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Displayable {
    String title;
    Ticker ticker;
    final Vector commands = new Vector();
    CommandListener listener;
    boolean shown;

    /* раскладка софт-клавиш: левая — команда или меню, правая — «назад» */
    Command softLeft, softRight;
    Vector menu;

    Displayable() {
    }

    public String getTitle() { return title; }

    public void setTitle(String s) {
        title = s;
        Display.requestRepaint();
    }

    public Ticker getTicker() { return ticker; }

    public void setTicker(Ticker t) {
        ticker = t;
        Display.requestRepaint();
    }

    public boolean isShown() { return shown; }

    public void addCommand(Command c) {
        if (c == null)
            throw new NullPointerException();
        if (commands.contains(c))
            return;
        int i = 0;
        while (i < commands.size() && ((Command) commands.elementAt(i)).getPriority() <= c.getPriority())
            i++;
        commands.insertElementAt(c, i);
        layoutSoft();
        Display.requestRepaint();
    }

    public void removeCommand(Command c) {
        if (commands.removeElement(c)) {
            layoutSoft();
            Display.requestRepaint();
        }
    }

    public void setCommandListener(CommandListener l) {
        listener = l;
    }

    public int getWidth() {
        return Display.W;
    }

    public int getHeight() {
        return Display.H - Theme.SOFTBAR_H - (this instanceof Canvas ? 0 : Theme.TITLE_H);
    }

    protected void sizeChanged(int w, int h) {
    }

    /* команды, которые видит пользователь (List прячет SELECT_COMMAND) */
    Vector visibleCommands() {
        return commands;
    }

    void layoutSoft() {
        Vector cmds = visibleCommands();
        Command negative = null;
        Vector others = new Vector();
        for (int i = 0; i < cmds.size(); i++) {
            Command c = (Command) cmds.elementAt(i);
            if (negative == null && c.isNegative())
                negative = c;
            else
                others.addElement(c);
        }
        menu = null;
        softLeft = softRight = null;
        if (negative == null && others.size() == 2) {
            softLeft = (Command) others.elementAt(0);
            softRight = (Command) others.elementAt(1);
            return;
        }
        softRight = negative;
        if (others.size() == 1)
            softLeft = (Command) others.elementAt(0);
        else if (others.size() > 1)
            menu = others;
    }

    boolean hasSoftBar() {
        return true;
    }

    /* Рисует весь экран: содержимое + полоса софт-клавиш. */
    abstract void paintInternal(Graphics g);

    void paintSoftBar(Graphics g) {
        int w = Display.W, y = Display.H - Theme.SOFTBAR_H;
        g.reset();
        g.setColor(Theme.BAR);
        g.fillRect(0, y, w, Theme.SOFTBAR_H);
        g.setColor(Theme.DIVIDER);
        g.fillRect(0, y, w, 1);
        g.setFont(Theme.SMALL);
        g.setColor(Theme.TEXT);
        int ty = y + (Theme.SOFTBAR_H - Theme.SMALL.getHeight()) / 2 + 1;
        int half = w / 2 - 6;
        if (menu != null)
            Theme.menuIcon(g, 6, y + 5);
        else if (softLeft != null)
            g.drawString(clip(softLeft.getLabel(), half), 4, ty, Graphics.TOP | Graphics.LEFT);
        if (softRight != null)
            g.drawString(clip(softRight.getLabel(), half), w - 4, ty, Graphics.TOP | Graphics.RIGHT);
    }

    static String clip(String s, int width) {
        return Theme.fit(s, Theme.SMALL, width);
    }

    /* Обработка ввода. Возвращает true, если событие съедено. */
    void keyDown(int code, boolean printable) {
    }

    void keyUp(int code) {
    }

    void keyRepeat(int code) {
    }

    void charTyped(char c) {
    }

    void pointerDown(int x, int y) {
    }

    void pointerUp(int x, int y) {
    }

    void pointerDrag(int x, int y) {
    }

    void showNotifyInternal() {
        shown = true;
    }

    void hideNotifyInternal() {
        shown = false;
    }

    void fireCommand(Command c) {
        if (c == null)
            return;
        if (listener != null)
            Display.invokeCommand(listener, c, this);
    }
}

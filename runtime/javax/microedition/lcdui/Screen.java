package javax.microedition.lcdui;

/* Общая основа системных экранов: заголовок, область содержимого с прокруткой, софт-клавиши. */
public abstract class Screen extends Displayable {
    int scroll;

    Screen() {
    }

    int contentHeight() {
        return Display.H - Theme.TITLE_H - Theme.SOFTBAR_H;
    }

    void paintInternal(Graphics g) {
        g.reset();
        g.setColor(Theme.BG);
        g.fillRect(0, 0, Display.W, Display.H);
        g.setClip(0, Theme.TITLE_H, Display.W, contentHeight());
        g.translate(0, Theme.TITLE_H);
        paintContent(g, Display.W, contentHeight());
        g.reset();
        Theme.title(g, title, Display.W);
        paintSoftBar(g);
    }

    abstract void paintContent(Graphics g, int w, int h);

    /* Подвинуть прокрутку, чтобы отрезок [y, y+h) был виден. */
    void reveal(int y, int h) {
        int vh = contentHeight();
        if (y - 2 < scroll)
            scroll = Math.max(0, y - 2);
        else if (y + h + 2 > scroll + vh)
            scroll = y + h + 2 - vh;
    }

    void pointerDown(int x, int y) {
        contentPointer(x, y - Theme.TITLE_H + scroll);
    }

    void contentPointer(int x, int y) {
    }
}

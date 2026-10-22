package javax.microedition.lcdui;

import java.util.Vector;

/* Общая модель выбора для List и ChoiceGroup. */
final class ChoiceModel {
    final int type;
    final Vector strings = new Vector();
    final Vector images = new Vector();
    final Vector selected = new Vector();
    int cursor;
    int fit;

    ChoiceModel(int type) {
        this.type = type;
    }

    int size() { return strings.size(); }

    void check(int i) {
        if (i < 0 || i >= strings.size())
            throw new IndexOutOfBoundsException();
    }

    int append(String s, Image img) {
        insert(strings.size(), s, img);
        return strings.size() - 1;
    }

    void insert(int i, String s, Image img) {
        if (s == null)
            throw new NullPointerException();
        if (i < 0 || i > strings.size())
            throw new IndexOutOfBoundsException();
        strings.insertElementAt(s, i);
        images.insertElementAt(img, i);
        boolean first = type != Choice.MULTIPLE && strings.size() == 1;
        selected.insertElementAt(first ? Boolean.TRUE : Boolean.FALSE, i);
    }

    void delete(int i) {
        check(i);
        boolean was = isSelected(i);
        strings.removeElementAt(i);
        images.removeElementAt(i);
        selected.removeElementAt(i);
        if (cursor >= strings.size())
            cursor = Math.max(0, strings.size() - 1);
        if (was && type != Choice.MULTIPLE && strings.size() > 0)
            setSelected(Math.min(i, strings.size() - 1), true);
    }

    void deleteAll() {
        strings.removeAllElements();
        images.removeAllElements();
        selected.removeAllElements();
        cursor = 0;
    }

    void set(int i, String s, Image img) {
        check(i);
        if (s == null)
            throw new NullPointerException();
        strings.setElementAt(s, i);
        images.setElementAt(img, i);
    }

    boolean isSelected(int i) {
        check(i);
        return selected.elementAt(i) == Boolean.TRUE;
    }

    int selectedIndex() {
        if (type == Choice.MULTIPLE)
            return -1;
        for (int i = 0; i < selected.size(); i++)
            if (selected.elementAt(i) == Boolean.TRUE)
                return i;
        return -1;
    }

    void setSelected(int i, boolean on) {
        check(i);
        if (type == Choice.MULTIPLE) {
            selected.setElementAt(on ? Boolean.TRUE : Boolean.FALSE, i);
        } else if (on) {
            for (int k = 0; k < selected.size(); k++)
                selected.setElementAt(k == i ? Boolean.TRUE : Boolean.FALSE, k);
            cursor = i;
        }
    }

    int getFlags(boolean[] f) {
        int n = 0;
        for (int i = 0; i < selected.size(); i++) {
            f[i] = selected.elementAt(i) == Boolean.TRUE;
            if (f[i])
                n++;
        }
        return n;
    }

    void setFlags(boolean[] f) {
        if (type == Choice.MULTIPLE) {
            for (int i = 0; i < selected.size(); i++)
                selected.setElementAt(f[i] ? Boolean.TRUE : Boolean.FALSE, i);
        } else {
            for (int i = 0; i < selected.size(); i++)
                if (f[i]) {
                    setSelected(i, true);
                    return;
                }
            if (selected.size() > 0)
                setSelected(0, true);
        }
    }

    int rowHeight(int i) {
        Image img = (Image) images.elementAt(i);
        return Math.max(Theme.MEDIUM.getHeight(), img != null ? img.getHeight() : 0) + 6;
    }

    /* Рисует строки; возвращает высоту. */
    int paint(Graphics g, int x, int y, int w, boolean focused, boolean marks) {
        int y0 = y;
        g.setFont(Theme.MEDIUM);
        for (int i = 0; i < strings.size(); i++) {
            int rh = rowHeight(i);
            boolean cur = focused && i == cursor;
            if (cur) {
                g.setColor(Theme.ACCENT);
                g.fillRect(x, y, w, rh);
            }
            int tx = x + 4;
            if (marks) {
                int c = cur ? Theme.SEL_TEXT : Theme.SUB;
                g.setColor(c);
                int my = y + (rh - 9) / 2;
                if (type == Choice.MULTIPLE) {
                    g.drawRect(tx, my, 8, 8);
                    if (isSelected(i))
                        g.fillRect(tx + 2, my + 2, 5, 5);
                } else {
                    g.drawRoundRect(tx, my, 8, 8, 8, 8);
                    if (isSelected(i))
                        g.fillRoundRect(tx + 2, my + 2, 5, 5, 5, 5);
                }
                tx += 14;
            }
            Image img = (Image) images.elementAt(i);
            if (img != null) {
                g.drawImage(img, tx, y + (rh - img.getHeight()) / 2, Graphics.TOP | Graphics.LEFT);
                tx += img.getWidth() + 4;
            }
            g.setColor(cur ? Theme.SEL_TEXT : Theme.TEXT);
            String s = Theme.fit((String) strings.elementAt(i), Theme.MEDIUM, x + w - tx - 2);
            g.drawString(s, tx, y + (rh - Theme.MEDIUM.getHeight()) / 2, Graphics.TOP | Graphics.LEFT);
            y += rh;
        }
        return y - y0;
    }

    int height() {
        int h = 0;
        for (int i = 0; i < strings.size(); i++)
            h += rowHeight(i);
        return h;
    }

    int rowTop(int idx) {
        int h = 0;
        for (int i = 0; i < idx && i < strings.size(); i++)
            h += rowHeight(i);
        return h;
    }

    int rowAt(int y) {
        int h = 0;
        for (int i = 0; i < strings.size(); i++) {
            h += rowHeight(i);
            if (y < h)
                return i;
        }
        return -1;
    }
}

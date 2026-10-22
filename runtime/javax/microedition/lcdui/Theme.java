package javax.microedition.lcdui;

/* Оформление системных экранов в стиле лаунчера: светлая пастель, пиксельные шрифты. */
final class Theme {
    static final int BG = 0xF4EDED, TEXT = 0x2A2430, SUB = 0x84798A, ACCENT = 0x7E6FD6, BAR = 0xE2D9DF,
            DIVIDER = 0xD5CBD0, SEL_TEXT = 0xFFFFFF, FIELD = 0xFFFFFF;
    static final int SOFTBAR_H = 20, TITLE_H = 20;
    static final Font SMALL = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_SMALL);
    static final Font MEDIUM = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_MEDIUM);
    static final Font BOLD = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_MEDIUM);

    private Theme() {
    }

    /* Перенос текста по словам под ширину. */
    static java.util.Vector wrap(String text, Font f, int width) {
        java.util.Vector lines = new java.util.Vector();
        if (text == null)
            return lines;
        int start = 0, n = text.length();
        while (start < n) {
            int nl = text.indexOf('\n', start);
            int end = nl < 0 ? n : nl;
            if (f.substringWidth(text, start, end - start) <= width) {
                lines.addElement(text.substring(start, end));
                start = end + 1;
                if (nl < 0)
                    break;
                continue;
            }
            /* сколько символов влезает в ширину */
            int cut = start + 1, wsum = f.charWidth(text.charAt(start));
            while (cut < end && wsum + f.charWidth(text.charAt(cut)) <= width)
                wsum += f.charWidth(text.charAt(cut++));
            int sp = text.lastIndexOf(' ', cut);
            if (sp <= start)
                sp = cut;
            lines.addElement(text.substring(start, sp));
            start = sp;
            while (start < n && text.charAt(start) == ' ')
                start++;
        }
        return lines;
    }

    static void title(Graphics g, String title, int w) {
        g.setColor(BAR);
        g.fillRect(0, 0, w, TITLE_H);
        g.setColor(DIVIDER);
        g.fillRect(0, TITLE_H - 1, w, 1);
        if (title != null) {
            g.setFont(BOLD);
            g.setColor(TEXT);
            String t = fit(title, BOLD, w - 8);
            g.drawString(t, w / 2, (TITLE_H - BOLD.getHeight()) / 2, Graphics.TOP | Graphics.HCENTER);
        }
    }

    /* Обрезать строку с «…», чтобы влезла в ширину. */
    static String fit(String s, Font f, int width) {
        if (f.stringWidth(s) <= width)
            return s;
        int n = s.length();
        while (n > 0 && f.substringWidth(s, 0, n) + f.charWidth('…') > width)
            n--;
        return s.substring(0, n) + "…";
    }

    /* Значок «меню» — три полоски, без надписи. */
    static void menuIcon(Graphics g, int x, int y) {
        for (int i = 0; i < 3; i++)
            g.fillRect(x, y + i * 4, 12, 2);
    }
}

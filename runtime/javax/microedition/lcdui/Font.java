package javax.microedition.lcdui;

/* Пиксельные шрифты из Terminus: мелкий 6x12, средний 8x16, крупный 10x20; жирный — отдельный рисунок. */
public final class Font {
    public static final int FACE_SYSTEM = 0, FACE_MONOSPACE = 32, FACE_PROPORTIONAL = 64;
    public static final int STYLE_PLAIN = 0, STYLE_BOLD = 1, STYLE_ITALIC = 2, STYLE_UNDERLINED = 4;
    public static final int SIZE_SMALL = 8, SIZE_MEDIUM = 0, SIZE_LARGE = 16;
    public static final int FONT_STATIC_TEXT = 0, FONT_INPUT_TEXT = 1;

    private static final int[] H = { 12, 12, 16, 16, 20, 20 };
    private static final int[] BASE = { 10, 10, 12, 12, 16, 16 };
    private static final Font[] cache = new Font[72];

    private final int face, style, size;
    final int id;
    private final int height, baseline;

    private Font(int face, int style, int size) {
        this.face = face;
        this.style = style;
        this.size = size;
        int k = size == SIZE_SMALL ? 0 : size == SIZE_LARGE ? 4 : 2;
        if ((style & STYLE_BOLD) != 0)
            k++;
        id = k | ((style & STYLE_UNDERLINED) != 0 ? 0x100 : 0) | (face != FACE_MONOSPACE ? 0x200 : 0);
        height = H[k];
        baseline = BASE[k];
    }

    public static Font getDefaultFont() {
        return getFont(FACE_SYSTEM, STYLE_PLAIN, SIZE_MEDIUM);
    }

    public static Font getFont(int specifier) {
        if (specifier != FONT_STATIC_TEXT && specifier != FONT_INPUT_TEXT)
            throw new IllegalArgumentException();
        return getDefaultFont();
    }

    public static synchronized Font getFont(int face, int style, int size) {
        if ((face != FACE_SYSTEM && face != FACE_MONOSPACE && face != FACE_PROPORTIONAL)
                || (style & ~7) != 0 || (size != SIZE_SMALL && size != SIZE_MEDIUM && size != SIZE_LARGE))
            throw new IllegalArgumentException();
        int key = (face >> 5) * 24 + style * 3 + (size >> 3);
        if (cache[key] == null)
            cache[key] = new Font(face, style, size);
        return cache[key];
    }

    public int getFace() { return face; }
    public int getStyle() { return style; }
    public int getSize() { return size; }
    public boolean isPlain() { return style == STYLE_PLAIN; }
    public boolean isBold() { return (style & STYLE_BOLD) != 0; }
    public boolean isItalic() { return (style & STYLE_ITALIC) != 0; }
    public boolean isUnderlined() { return (style & STYLE_UNDERLINED) != 0; }
    public int getHeight() { return height; }
    public int getBaselinePosition() { return baseline; }
    /* ширина считается так же, как рисует натив: пропорционально, по реальным пикселям глифа */
    private static native int charWidth0(int id, char c);

    private static native int stringWidth0(int id, String s);

    public int charWidth(char c) { return charWidth0(id, c); }

    public int charsWidth(char[] ch, int off, int len) {
        if (off < 0 || len < 0 || off + len > ch.length)
            throw new ArrayIndexOutOfBoundsException();
        return stringWidth0(id, new String(ch, off, len));
    }

    public int stringWidth(String s) {
        if (s == null)
            throw new NullPointerException();
        return stringWidth0(id, s);
    }

    public int substringWidth(String s, int off, int len) {
        if (off < 0 || len < 0 || off + len > s.length())
            throw new StringIndexOutOfBoundsException();
        return stringWidth0(id, s.substring(off, off + len));
    }
}

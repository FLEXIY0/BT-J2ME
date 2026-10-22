package javax.microedition.lcdui;

/* Мостик для javax.microedition.lcdui.game: доступ к внутренностям Canvas и Display. */
public final class GameCanvasAccess {
    private GameCanvasAccess() {
    }

    public static void suppress(Canvas c, boolean on) {
        c.suppressGameKeys = on;
    }

    public static int keyStates() {
        return Display.gameKeyStates();
    }

    public static void flush(Canvas c) {
        Display.flushNow(c);
    }

    public static int screenWidth() {
        return Display.W;
    }

    public static int screenHeight() {
        return Display.H;
    }

    public static int[] pixels(Image img) {
        return img.pixels;
    }
}

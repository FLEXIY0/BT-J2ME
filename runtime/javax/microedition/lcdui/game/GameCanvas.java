package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.GameCanvasAccess;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public abstract class GameCanvas extends Canvas {
    public static final int UP_PRESSED = 1 << UP, DOWN_PRESSED = 1 << DOWN, LEFT_PRESSED = 1 << LEFT,
            RIGHT_PRESSED = 1 << RIGHT, FIRE_PRESSED = 1 << FIRE, GAME_A_PRESSED = 1 << GAME_A,
            GAME_B_PRESSED = 1 << GAME_B, GAME_C_PRESSED = 1 << GAME_C, GAME_D_PRESSED = 1 << GAME_D;

    /* буфер во весь экран: переключение полноэкранного режима его не меняет */
    private final Image buffer;

    protected GameCanvas(boolean suppressKeyEvents) {
        GameCanvasAccess.suppress(this, suppressKeyEvents);
        buffer = Image.createImage(GameCanvasAccess.screenWidth(), GameCanvasAccess.screenHeight());
    }

    protected Graphics getGraphics() {
        Graphics g = buffer.getGraphics();
        g.setClip(0, 0, getWidth(), getHeight());
        return g;
    }

    public int getKeyStates() {
        return GameCanvasAccess.keyStates();
    }

    public void paint(Graphics g) {
        g.drawImage(buffer, 0, 0, Graphics.TOP | Graphics.LEFT);
    }

    public void flushGraphics(int x, int y, int w, int h) {
        flushGraphics();
    }

    public void flushGraphics() {
        GameCanvasAccess.flush(this);
    }
}

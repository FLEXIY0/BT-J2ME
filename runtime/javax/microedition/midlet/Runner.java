package javax.microedition.midlet;

import btvm.Native;
import javax.microedition.lcdui.Display;

/* Точка входа машины: создать MIDlet, startApp(), цикл событий, destroyApp(). */
public final class Runner {
    private Runner() {
    }

    public static void main(String className) {
        MIDlet m;
        try {
            m = (MIDlet) Class.forName(className).newInstance();
        } catch (Throwable t) {
            System.out.println("BT-J2ME: не удалось создать " + className + ": " + t);
            Native.exit();
            return;
        }
        try {
            m.startApp();
        } catch (Throwable t) {
            System.out.println("BT-J2ME: startApp: " + t);
            Native.exit();
            return;
        }
        Display.runLoop();
        try {
            m.destroyApp(true);
        } catch (Throwable t) {
        }
        Native.exit();
    }
}

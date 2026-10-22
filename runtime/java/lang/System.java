package java.lang;

import java.io.PrintStream;

public final class System {
    public static final PrintStream out = new PrintStream(new btvm.ConsoleStream());
    public static final PrintStream err = out;

    private System() {
    }

    public static native long currentTimeMillis();

    public static native void arraycopy(Object src, int srcPos, Object dst, int dstPos, int length);

    public static native int identityHashCode(Object x);

    public static String getProperty(String key) {
        if (key == null)
            throw new NullPointerException();
        if (key.equals("microedition.platform"))
            return "BT-J2ME";
        if (key.equals("microedition.configuration"))
            return "CLDC-1.1";
        if (key.equals("microedition.profiles"))
            return "MIDP-2.0";
        if (key.equals("microedition.encoding"))
            return "UTF-8";
        if (key.equals("microedition.locale"))
            return "en-US";
        if (key.equals("microedition.io.file.FileConnection.version"))
            return null;
        return null;
    }

    public static void exit(int status) {
        Runtime.getRuntime().exit(status);
    }

    public static void gc() {
        Runtime.getRuntime().gc();
    }
}

package btvm;

/* Мост к машине и платформе. Не часть MIDP: игры его не видят. */
public final class Native {
    private Native() {
    }

    public static native void print(String s);

    /* события: тип или 0; out = {код клавиши / x, флаги / y, символ} */
    public static native int pollEvent(int[] out);

    /* ждать события не дольше ms (или пока не позовут wake) */
    public static native void waitEvent(int ms);

    public static native void wake();

    public static native void flush(int[] pixels, int w, int h);

    public static native int screenWidth();

    public static native int screenHeight();

    public static native void exit();

    public static native String getAppProperty(String key);

    public static native int[] decodeImage(byte[] data, int off, int len, int[] wh);

    public static native byte[] rmsLoad(String name);

    public static native boolean rmsSave(String name, byte[] data);

    public static native boolean rmsDelete(String name);

    public static native String[] rmsList();

    public static final int EV_KEY_DOWN = 1;
    public static final int EV_KEY_UP = 2;
    public static final int EV_POINTER_DOWN = 3;
    public static final int EV_POINTER_UP = 4;
    public static final int EV_POINTER_MOVE = 5;
    public static final int EV_CHAR = 6;
    public static final int EV_EXIT = 7;
}

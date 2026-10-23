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

    /* экранная клавиатура телефона (Android) для полей ввода */
    public static native void textInput(boolean on);

    public static native void vibrate(int ms);

    /* звук: дескриптор (0 — не вышло); kind 0 — по содержимому, 1 — рингтон Nokia OTT */
    public static native int sndCreate(byte[] data, int kind);

    public static native int sndCreateFreq(int hz, int ms);

    public static native void sndFree(int h);

    public static native void sndPlay(int h);

    public static native void sndPause(int h);

    public static native boolean sndPlaying(int h);

    public static native void sndLoops(int h, int loops);

    public static native void sndVolume(int h, int volume);

    public static native long sndTime(int h);

    public static native long sndSeek(int h, long us);

    public static native long sndDuration(int h);

    public static native void sndTone(int note, int ms, int volume);

    public static final int EV_KEY_DOWN = 1;
    public static final int EV_KEY_UP = 2;
    public static final int EV_POINTER_DOWN = 3;
    public static final int EV_POINTER_UP = 4;
    public static final int EV_POINTER_MOVE = 5;
    public static final int EV_CHAR = 6;
    public static final int EV_EXIT = 7;
}

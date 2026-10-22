package java.lang;

public final class Math {
    public static final double E = 2.718281828459045;
    public static final double PI = 3.141592653589793;

    private Math() {
    }

    public static int abs(int a) { return a < 0 ? -a : a; }
    public static long abs(long a) { return a < 0 ? -a : a; }
    public static float abs(float a) { return a <= 0.0f ? 0.0f - a : a; }
    public static double abs(double a) { return a <= 0.0 ? 0.0 - a : a; }
    public static int max(int a, int b) { return a >= b ? a : b; }
    public static long max(long a, long b) { return a >= b ? a : b; }
    public static float max(float a, float b) { return a >= b ? a : b; }
    public static double max(double a, double b) { return a >= b ? a : b; }
    public static int min(int a, int b) { return a <= b ? a : b; }
    public static long min(long a, long b) { return a <= b ? a : b; }
    public static float min(float a, float b) { return a <= b ? a : b; }
    public static double min(double a, double b) { return a <= b ? a : b; }

    public static native double sqrt(double a);
    public static native double sin(double a);
    public static native double cos(double a);
    public static native double tan(double a);
    public static native double floor(double a);
    public static native double ceil(double a);
    public static native double atan2(double y, double x);
    public static native double pow(double a, double b);

    public static double toRadians(double deg) { return deg / 180.0 * PI; }
    public static double toDegrees(double rad) { return rad * 180.0 / PI; }
    public static int round(float a) { return (int) floor(a + 0.5f); }
    public static long round(double a) { return (long) floor(a + 0.5); }
    public static double random() { return btvm.Text.RNG.nextDouble(); }
}

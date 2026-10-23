package java.util;

public abstract class TimeZone {
    private static TimeZone def;

    /* смещение местного времени от UTC в миллисекундах — от системы */
    private static native int localOffset0();

    public static synchronized TimeZone getDefault() {
        if (def == null) {
            int off = localOffset0();
            def = new Fixed(off == 0 ? "GMT" : name(off), off);
        }
        return def;
    }

    /* "GMT+03:00" */
    private static String name(int off) {
        int m = Math.abs(off) / 60000;
        return "GMT" + (off < 0 ? "-" : "+") + (m / 60 < 10 ? "0" : "") + m / 60 + ":" + (m % 60 < 10 ? "0" : "") + m % 60;
    }

    public static TimeZone getTimeZone(String id) {
        if (id == null)
            throw new NullPointerException();
        TimeZone d = getDefault();
        if (id.equals(d.getID()))
            return d;
        if (id.startsWith("GMT") && id.length() > 4) {
            try {
                String s = id.substring(4);
                int colon = s.indexOf(':');
                int h = Integer.parseInt(colon < 0 ? s : s.substring(0, colon));
                int m = colon < 0 ? 0 : Integer.parseInt(s.substring(colon + 1));
                int off = (h * 60 + m) * 60000 * (id.charAt(3) == '-' ? -1 : 1);
                return new Fixed(name(off), off);
            } catch (RuntimeException e) {
                // непонятный id — как на телефоне, GMT
            }
        }
        return new Fixed("GMT", 0);
    }

    public static String[] getAvailableIDs() {
        return new String[] { getDefault().getID(), "GMT", "UTC" };
    }

    public abstract int getOffset(int era, int year, int month, int day, int dayOfWeek, int millis);

    public abstract int getRawOffset();

    public abstract boolean useDaylightTime();

    public String getID() {
        return "GMT";
    }

    static final class Fixed extends TimeZone {
        private final String id;
        private final int offset;

        Fixed(String id, int offset) {
            this.id = id;
            this.offset = offset;
        }

        public int getOffset(int era, int year, int month, int day, int dayOfWeek, int millis) {
            return offset;
        }

        public int getRawOffset() {
            return offset;
        }

        public boolean useDaylightTime() {
            return false;
        }

        public String getID() {
            return id;
        }
    }
}

package java.util;

public abstract class TimeZone {
    private static TimeZone def;

    public static synchronized TimeZone getDefault() {
        if (def == null)
            def = new Fixed("GMT", 0);
        return def;
    }

    public static TimeZone getTimeZone(String id) {
        return new Fixed(id, 0);
    }

    public static String[] getAvailableIDs() {
        return new String[] { "GMT", "UTC" };
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

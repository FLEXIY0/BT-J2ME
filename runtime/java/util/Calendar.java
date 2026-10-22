package java.util;

/* Простой григорианский календарь поверх миллисекунд (UTC). */
public abstract class Calendar {
    public static final int ERA = 0, YEAR = 1, MONTH = 2, DATE = 5, DAY_OF_MONTH = 5, DAY_OF_WEEK = 7,
            AM_PM = 9, HOUR = 10, HOUR_OF_DAY = 11, MINUTE = 12, SECOND = 13, MILLISECOND = 14;
    public static final int SUNDAY = 1, MONDAY = 2, TUESDAY = 3, WEDNESDAY = 4, THURSDAY = 5, FRIDAY = 6, SATURDAY = 7;
    public static final int JANUARY = 0, FEBRUARY = 1, MARCH = 2, APRIL = 3, MAY = 4, JUNE = 5, JULY = 6,
            AUGUST = 7, SEPTEMBER = 8, OCTOBER = 9, NOVEMBER = 10, DECEMBER = 11;
    public static final int AM = 0, PM = 1;

    protected int[] fields = new int[15];
    protected long time;
    private TimeZone zone = TimeZone.getDefault();

    protected Calendar() {
        setTimeInMillis(System.currentTimeMillis());
    }

    public static synchronized Calendar getInstance() {
        return new Greg();
    }

    public static synchronized Calendar getInstance(TimeZone zone) {
        Calendar c = new Greg();
        c.setTimeZone(zone);
        return c;
    }

    public final Date getTime() {
        return new Date(time);
    }

    public final void setTime(Date d) {
        setTimeInMillis(d.getTime());
    }

    protected long getTimeInMillis() {
        return time;
    }

    protected void setTimeInMillis(long millis) {
        time = millis;
        computeFields();
    }

    public final int get(int field) {
        return fields[field];
    }

    public final void set(int field, int value) {
        fields[field] = value;
        if (field == HOUR)
            fields[HOUR_OF_DAY] = value + (fields[AM_PM] == PM ? 12 : 0);
        computeTime();
        computeFields();
    }

    public void setTimeZone(TimeZone z) {
        zone = z;
        computeFields();
    }

    public TimeZone getTimeZone() {
        return zone;
    }

    public boolean equals(Object o) {
        return o instanceof Calendar && ((Calendar) o).time == time;
    }

    public boolean before(Object o) {
        return o instanceof Calendar && time < ((Calendar) o).time;
    }

    public boolean after(Object o) {
        return o instanceof Calendar && time > ((Calendar) o).time;
    }

    protected abstract void computeFields();

    protected abstract void computeTime();

    public String toString() {
        return fields[YEAR] + "-" + (fields[MONTH] + 1) + "-" + fields[DATE] + " " + fields[HOUR_OF_DAY] + ":"
                + (fields[MINUTE] < 10 ? "0" : "") + fields[MINUTE];
    }

    static final class Greg extends Calendar {
        private static long daysFromCivil(long y, int m, int d) {
            y -= m <= 2 ? 1 : 0;
            long era = (y >= 0 ? y : y - 399) / 400;
            long yoe = y - era * 400;
            long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }

        protected void computeFields() {
            long ms = time + (zone() == null ? 0 : zone().getRawOffset());
            long days = ms >= 0 ? ms / 86400000L : (ms - 86399999L) / 86400000L;
            long rem = ms - days * 86400000L;
            long z = days + 719468;
            long era = (z >= 0 ? z : z - 146096) / 146097;
            long doe = z - era * 146097;
            long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            long mp = (5 * doy + 2) / 153;
            int d = (int) (doy - (153 * mp + 2) / 5 + 1);
            int m = (int) (mp < 10 ? mp + 3 : mp - 9);
            long y = yoe + era * 400 + (m <= 2 ? 1 : 0);
            fields[ERA] = 1;
            fields[YEAR] = (int) y;
            fields[MONTH] = m - 1;
            fields[DATE] = d;
            fields[DAY_OF_WEEK] = (int) (((days % 7) + 11) % 7) + 1;
            fields[HOUR_OF_DAY] = (int) (rem / 3600000L);
            fields[AM_PM] = fields[HOUR_OF_DAY] >= 12 ? PM : AM;
            fields[HOUR] = fields[HOUR_OF_DAY] % 12;
            fields[MINUTE] = (int) (rem / 60000L % 60);
            fields[SECOND] = (int) (rem / 1000L % 60);
            fields[MILLISECOND] = (int) (rem % 1000L);
        }

        protected void computeTime() {
            long days = daysFromCivil(fields[YEAR], fields[MONTH] + 1, fields[DATE]);
            time = days * 86400000L + fields[HOUR_OF_DAY] * 3600000L + fields[MINUTE] * 60000L
                    + fields[SECOND] * 1000L + fields[MILLISECOND] - (zone() == null ? 0 : zone().getRawOffset());
        }

        private TimeZone zone() {
            return getTimeZone();
        }
    }
}

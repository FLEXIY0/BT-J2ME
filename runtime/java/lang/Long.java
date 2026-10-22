package java.lang;

public final class Long {
    public static final long MIN_VALUE = 0x8000000000000000L;
    public static final long MAX_VALUE = 0x7fffffffffffffffL;
    private final long value;

    public Long(long value) {
        this.value = value;
    }

    public static long parseLong(String s) throws NumberFormatException {
        return parseLong(s, 10);
    }

    public static long parseLong(String s, int radix) throws NumberFormatException {
        if (s == null || radix < Character.MIN_RADIX || radix > Character.MAX_RADIX)
            throw new NumberFormatException(s);
        int i = 0, n = s.length();
        boolean neg = false;
        if (n > 0 && (s.charAt(0) == '-' || s.charAt(0) == '+')) {
            neg = s.charAt(0) == '-';
            i++;
        }
        if (i >= n)
            throw new NumberFormatException(s);
        long result = 0, limit = neg ? MIN_VALUE : -MAX_VALUE;
        for (; i < n; i++) {
            int d = Character.digit(s.charAt(i), radix);
            if (d < 0 || result < limit / radix)
                throw new NumberFormatException(s);
            result *= radix;
            if (result < limit + d)
                throw new NumberFormatException(s);
            result -= d;
        }
        return neg ? result : -result;
    }

    public static String toString(long i) {
        return toString(i, 10);
    }

    public static String toString(long i, int radix) {
        if (radix < Character.MIN_RADIX || radix > Character.MAX_RADIX)
            radix = 10;
        char[] buf = new char[65];
        int pos = 65;
        boolean neg = i < 0;
        if (!neg)
            i = -i;
        do {
            buf[--pos] = Character.forDigit((int) -(i % radix), radix);
            i /= radix;
        } while (i != 0);
        if (neg)
            buf[--pos] = '-';
        return new String(buf, pos, 65 - pos);
    }

    public long longValue() { return value; }
    public float floatValue() { return value; }
    public double doubleValue() { return value; }
    public String toString() { return toString(value); }
    public int hashCode() { return (int) (value ^ (value >>> 32)); }

    public boolean equals(Object o) {
        return o instanceof Long && ((Long) o).value == value;
    }
}

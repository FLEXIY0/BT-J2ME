package java.lang;

import java.io.UnsupportedEncodingException;

/* Раскладка полей value/offset/count известна машине: она сама создаёт строки-литералы. */
public final class String {
    private final char[] value;
    private final int offset;
    private final int count;
    private int hash;

    public String() {
        value = new char[0];
        offset = 0;
        count = 0;
    }

    public String(String s) {
        value = s.value;
        offset = s.offset;
        count = s.count;
    }

    public String(char[] chars) {
        this(chars, 0, chars.length);
    }

    public String(char[] chars, int off, int len) {
        if (off < 0 || len < 0 || off > chars.length - len)
            throw new StringIndexOutOfBoundsException();
        value = new char[len];
        System.arraycopy(chars, off, value, 0, len);
        offset = 0;
        count = len;
    }

    /* без копирования — для StringBuffer и substring */
    String(int off, int len, char[] chars) {
        value = chars;
        offset = off;
        count = len;
    }

    public String(byte[] bytes) {
        this(bytes, 0, bytes.length);
    }

    public String(byte[] bytes, int off, int len) {
        char[] c = btvm.Text.decode(bytes, off, len);
        value = c;
        offset = 0;
        count = c.length;
    }

    public String(byte[] bytes, String enc) throws UnsupportedEncodingException {
        this(bytes, 0, bytes.length, enc);
    }

    public String(byte[] bytes, int off, int len, String enc) throws UnsupportedEncodingException {
        char[] c = btvm.Text.decode(bytes, off, len, enc);
        value = c;
        offset = 0;
        count = c.length;
    }

    public String(StringBuffer sb) {
        this(sb.toString());
    }

    public int length() {
        return count;
    }

    public char charAt(int i) {
        if (i < 0 || i >= count)
            throw new StringIndexOutOfBoundsException(String.valueOf(i));
        return value[offset + i];
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > count || srcBegin > srcEnd)
            throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, offset + srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public byte[] getBytes() {
        return btvm.Text.encode(value, offset, count);
    }

    public byte[] getBytes(String enc) throws UnsupportedEncodingException {
        return btvm.Text.encode(value, offset, count, enc);
    }

    public boolean equals(Object o) {
        if (this == o)
            return true;
        if (!(o instanceof String))
            return false;
        String s = (String) o;
        if (s.count != count)
            return false;
        for (int i = 0; i < count; i++)
            if (value[offset + i] != s.value[s.offset + i])
                return false;
        return true;
    }

    public boolean equalsIgnoreCase(String s) {
        return s != null && s.count == count && regionMatches(true, 0, s, 0, count);
    }

    public int compareTo(String s) {
        int n = Math.min(count, s.count);
        for (int i = 0; i < n; i++) {
            int d = value[offset + i] - s.value[s.offset + i];
            if (d != 0)
                return d;
        }
        return count - s.count;
    }

    public boolean regionMatches(boolean ignoreCase, int toff, String other, int ooff, int len) {
        if (toff < 0 || ooff < 0 || toff > count - len || ooff > other.count - len)
            return false;
        for (int i = 0; i < len; i++) {
            char a = value[offset + toff + i], b = other.value[other.offset + ooff + i];
            if (a == b)
                continue;
            if (ignoreCase && (Character.toLowerCase(a) == Character.toLowerCase(b)
                    || Character.toUpperCase(a) == Character.toUpperCase(b)))
                continue;
            return false;
        }
        return true;
    }

    public boolean startsWith(String prefix, int toff) {
        return regionMatches(false, toff, prefix, 0, prefix.count);
    }

    public boolean startsWith(String prefix) {
        return startsWith(prefix, 0);
    }

    public boolean endsWith(String suffix) {
        return startsWith(suffix, count - suffix.count);
    }

    public int hashCode() {
        int h = hash;
        if (h == 0) {
            for (int i = 0; i < count; i++)
                h = 31 * h + value[offset + i];
            hash = h;
        }
        return h;
    }

    public int indexOf(int ch) {
        return indexOf(ch, 0);
    }

    public int indexOf(int ch, int from) {
        if (from < 0)
            from = 0;
        for (int i = from; i < count; i++)
            if (value[offset + i] == ch)
                return i;
        return -1;
    }

    public int lastIndexOf(int ch) {
        return lastIndexOf(ch, count - 1);
    }

    public int lastIndexOf(int ch, int from) {
        if (from >= count)
            from = count - 1;
        for (int i = from; i >= 0; i--)
            if (value[offset + i] == ch)
                return i;
        return -1;
    }

    public int indexOf(String s) {
        return indexOf(s, 0);
    }

    public int indexOf(String s, int from) {
        if (from < 0)
            from = 0;
        for (int i = from; i <= count - s.count; i++)
            if (regionMatches(false, i, s, 0, s.count))
                return i;
        return -1;
    }

    public int lastIndexOf(String s) {
        for (int i = count - s.count; i >= 0; i--)
            if (regionMatches(false, i, s, 0, s.count))
                return i;
        return -1;
    }

    public String substring(int begin) {
        return substring(begin, count);
    }

    public String substring(int begin, int end) {
        if (begin < 0 || end > count || begin > end)
            throw new StringIndexOutOfBoundsException(begin + "," + end);
        if (begin == 0 && end == count)
            return this;
        return new String(offset + begin, end - begin, value);
    }

    public String concat(String s) {
        if (s.count == 0)
            return this;
        char[] c = new char[count + s.count];
        getChars(0, count, c, 0);
        s.getChars(0, s.count, c, count);
        return new String(0, c.length, c);
    }

    public String replace(char oldChar, char newChar) {
        if (indexOf(oldChar) < 0)
            return this;
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++)
            if (c[i] == oldChar)
                c[i] = newChar;
        return new String(0, c.length, c);
    }

    public String toLowerCase() {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++)
            c[i] = Character.toLowerCase(c[i]);
        return new String(0, c.length, c);
    }

    public String toUpperCase() {
        char[] c = toCharArray();
        for (int i = 0; i < c.length; i++)
            c[i] = Character.toUpperCase(c[i]);
        return new String(0, c.length, c);
    }

    public String trim() {
        int b = 0, e = count;
        while (b < e && value[offset + b] <= ' ')
            b++;
        while (e > b && value[offset + e - 1] <= ' ')
            e--;
        return substring(b, e);
    }

    public char[] toCharArray() {
        char[] c = new char[count];
        System.arraycopy(value, offset, c, 0, count);
        return c;
    }

    public String toString() {
        return this;
    }

    public native String intern();

    public static String valueOf(Object o) {
        return o == null ? "null" : o.toString();
    }

    public static String valueOf(char[] c) {
        return new String(c);
    }

    public static String valueOf(char[] c, int off, int len) {
        return new String(c, off, len);
    }

    public static String valueOf(boolean b) {
        return b ? "true" : "false";
    }

    public static String valueOf(char ch) {
        char[] c = { ch };
        return new String(0, 1, c);
    }

    public static String valueOf(int i) {
        return Integer.toString(i);
    }

    public static String valueOf(long l) {
        return Long.toString(l);
    }

    public static String valueOf(float f) {
        return Float.toString(f);
    }

    public static String valueOf(double d) {
        return Double.toString(d);
    }
}

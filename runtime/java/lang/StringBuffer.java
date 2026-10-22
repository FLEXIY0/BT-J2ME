package java.lang;

public final class StringBuffer {
    private char[] value;
    private int count;

    public StringBuffer() {
        this(16);
    }

    public StringBuffer(int capacity) {
        value = new char[capacity];
    }

    public StringBuffer(String s) {
        this(s.length() + 16);
        append(s);
    }

    public int length() {
        return count;
    }

    public int capacity() {
        return value.length;
    }

    public void ensureCapacity(int min) {
        if (min > value.length) {
            int n = value.length * 2 + 2;
            if (n < min)
                n = min;
            char[] c = new char[n];
            System.arraycopy(value, 0, c, 0, count);
            value = c;
        }
    }

    public void setLength(int len) {
        if (len < 0)
            throw new StringIndexOutOfBoundsException();
        ensureCapacity(len);
        for (int i = count; i < len; i++)
            value[i] = 0;
        count = len;
    }

    public char charAt(int i) {
        if (i < 0 || i >= count)
            throw new StringIndexOutOfBoundsException(String.valueOf(i));
        return value[i];
    }

    public void setCharAt(int i, char ch) {
        if (i < 0 || i >= count)
            throw new StringIndexOutOfBoundsException(String.valueOf(i));
        value[i] = ch;
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        if (srcBegin < 0 || srcEnd > count || srcBegin > srcEnd)
            throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, srcBegin, dst, dstBegin, srcEnd - srcBegin);
    }

    public synchronized StringBuffer append(String s) {
        if (s == null)
            s = "null";
        int n = s.length();
        ensureCapacity(count + n);
        s.getChars(0, n, value, count);
        count += n;
        return this;
    }

    public StringBuffer append(Object o) {
        return append(String.valueOf(o));
    }

    public StringBuffer append(char[] c) {
        return append(c, 0, c.length);
    }

    public synchronized StringBuffer append(char[] c, int off, int len) {
        ensureCapacity(count + len);
        System.arraycopy(c, off, value, count, len);
        count += len;
        return this;
    }

    public StringBuffer append(boolean b) {
        return append(b ? "true" : "false");
    }

    public synchronized StringBuffer append(char ch) {
        ensureCapacity(count + 1);
        value[count++] = ch;
        return this;
    }

    public StringBuffer append(int i) {
        return append(Integer.toString(i));
    }

    public StringBuffer append(long l) {
        return append(Long.toString(l));
    }

    public StringBuffer append(float f) {
        return append(Float.toString(f));
    }

    public StringBuffer append(double d) {
        return append(Double.toString(d));
    }

    public synchronized StringBuffer delete(int start, int end) {
        if (end > count)
            end = count;
        if (start < 0 || start > end)
            throw new StringIndexOutOfBoundsException();
        System.arraycopy(value, end, value, start, count - end);
        count -= end - start;
        return this;
    }

    public StringBuffer deleteCharAt(int i) {
        if (i < 0 || i >= count)
            throw new StringIndexOutOfBoundsException();
        return delete(i, i + 1);
    }

    public synchronized StringBuffer insert(int off, String s) {
        if (off < 0 || off > count)
            throw new StringIndexOutOfBoundsException();
        if (s == null)
            s = "null";
        int n = s.length();
        ensureCapacity(count + n);
        System.arraycopy(value, off, value, off + n, count - off);
        s.getChars(0, n, value, off);
        count += n;
        return this;
    }

    public StringBuffer insert(int off, Object o) {
        return insert(off, String.valueOf(o));
    }

    public StringBuffer insert(int off, char[] c) {
        return insert(off, new String(c));
    }

    public StringBuffer insert(int off, boolean b) {
        return insert(off, String.valueOf(b));
    }

    public StringBuffer insert(int off, char c) {
        return insert(off, String.valueOf(c));
    }

    public StringBuffer insert(int off, int i) {
        return insert(off, String.valueOf(i));
    }

    public StringBuffer insert(int off, long l) {
        return insert(off, String.valueOf(l));
    }

    public StringBuffer insert(int off, float f) {
        return insert(off, String.valueOf(f));
    }

    public StringBuffer insert(int off, double d) {
        return insert(off, String.valueOf(d));
    }

    public synchronized StringBuffer reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) {
            char c = value[i];
            value[i] = value[j];
            value[j] = c;
        }
        return this;
    }

    public String toString() {
        char[] c = new char[count];
        System.arraycopy(value, 0, c, 0, count);
        return new String(0, count, c);
    }
}

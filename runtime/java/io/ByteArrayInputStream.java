package java.io;

public class ByteArrayInputStream extends InputStream {
    protected byte[] buf;
    protected int pos;
    protected int mark;
    protected int count;

    public ByteArrayInputStream(byte[] buf) {
        this(buf, 0, buf.length);
    }

    public ByteArrayInputStream(byte[] buf, int offset, int length) {
        this.buf = buf;
        pos = offset;
        mark = offset;
        count = Math.min(offset + length, buf.length);
    }

    public synchronized int read() {
        return pos < count ? (buf[pos++] & 0xff) : -1;
    }

    public synchronized int read(byte[] b, int off, int len) {
        if (b == null)
            throw new NullPointerException();
        if (off < 0 || len < 0 || off + len > b.length)
            throw new IndexOutOfBoundsException();
        if (pos >= count)
            return -1;
        if (len > count - pos)
            len = count - pos;
        System.arraycopy(buf, pos, b, off, len);
        pos += len;
        return len;
    }

    public synchronized long skip(long n) {
        if (n > count - pos)
            n = count - pos;
        if (n < 0)
            return 0;
        pos += (int) n;
        return n;
    }

    public synchronized int available() {
        return count - pos;
    }

    public boolean markSupported() {
        return true;
    }

    public synchronized void mark(int limit) {
        mark = pos;
    }

    public synchronized void reset() {
        pos = mark;
    }

    public synchronized void close() {
    }
}

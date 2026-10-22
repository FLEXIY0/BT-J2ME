package java.io;

public class DataInputStream extends InputStream implements DataInput {
    protected InputStream in;

    public DataInputStream(InputStream in) {
        this.in = in;
    }

    public int read() throws IOException {
        return in.read();
    }

    public final int read(byte[] b) throws IOException {
        return in.read(b, 0, b.length);
    }

    public final int read(byte[] b, int off, int len) throws IOException {
        return in.read(b, off, len);
    }

    public final void readFully(byte[] b) throws IOException {
        readFully(b, 0, b.length);
    }

    public final void readFully(byte[] b, int off, int len) throws IOException {
        int n = 0;
        while (n < len) {
            int c = in.read(b, off + n, len - n);
            if (c < 0)
                throw new EOFException();
            n += c;
        }
    }

    public final int skipBytes(int n) throws IOException {
        int k = 0;
        while (k < n) {
            long s = in.skip(n - k);
            if (s <= 0)
                break;
            k += (int) s;
        }
        return k;
    }

    private int next() throws IOException {
        int c = in.read();
        if (c < 0)
            throw new EOFException();
        return c;
    }

    public final boolean readBoolean() throws IOException { return next() != 0; }
    public final byte readByte() throws IOException { return (byte) next(); }
    public final int readUnsignedByte() throws IOException { return next(); }
    public final short readShort() throws IOException { return (short) ((next() << 8) | next()); }
    public final int readUnsignedShort() throws IOException { return (next() << 8) | next(); }
    public final char readChar() throws IOException { return (char) ((next() << 8) | next()); }
    public final int readInt() throws IOException { return (next() << 24) | (next() << 16) | (next() << 8) | next(); }
    public final long readLong() throws IOException { return ((long) readInt() << 32) | (readInt() & 0xFFFFFFFFL); }
    public final float readFloat() throws IOException { return Float.intBitsToFloat(readInt()); }
    public final double readDouble() throws IOException { return Double.longBitsToDouble(readLong()); }

    public final String readUTF() throws IOException {
        return readUTF(this);
    }

    public static final String readUTF(DataInput in) throws IOException {
        int len = in.readUnsignedShort();
        byte[] b = new byte[len];
        in.readFully(b);
        char[] c = new char[len];
        int n = 0;
        for (int i = 0; i < len;) {
            int x = b[i] & 0xff;
            if (x < 0x80) {
                c[n++] = (char) x;
                i++;
            } else if ((x & 0xE0) == 0xC0) {
                if (i + 1 >= len)
                    throw new UTFDataFormatException();
                c[n++] = (char) (((x & 0x1F) << 6) | (b[i + 1] & 0x3F));
                i += 2;
            } else if ((x & 0xF0) == 0xE0) {
                if (i + 2 >= len)
                    throw new UTFDataFormatException();
                c[n++] = (char) (((x & 0x0F) << 12) | ((b[i + 1] & 0x3F) << 6) | (b[i + 2] & 0x3F));
                i += 3;
            } else {
                throw new UTFDataFormatException();
            }
        }
        return new String(c, 0, n);
    }

    public long skip(long n) throws IOException {
        return in.skip(n);
    }

    public int available() throws IOException {
        return in.available();
    }

    public void close() throws IOException {
        in.close();
    }

    public synchronized void mark(int limit) {
        in.mark(limit);
    }

    public synchronized void reset() throws IOException {
        in.reset();
    }

    public boolean markSupported() {
        return in.markSupported();
    }
}

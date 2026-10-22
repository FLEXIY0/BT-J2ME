package java.io;

public abstract class Reader {
    protected Object lock;

    protected Reader() {
        lock = this;
    }

    protected Reader(Object lock) {
        this.lock = lock;
    }

    public int read() throws IOException {
        char[] c = new char[1];
        return read(c, 0, 1) == -1 ? -1 : c[0];
    }

    public int read(char[] cbuf) throws IOException {
        return read(cbuf, 0, cbuf.length);
    }

    public abstract int read(char[] cbuf, int off, int len) throws IOException;

    public long skip(long n) throws IOException {
        long k = 0;
        while (k < n && read() >= 0)
            k++;
        return k;
    }

    public boolean ready() throws IOException {
        return false;
    }

    public boolean markSupported() {
        return false;
    }

    public void mark(int limit) throws IOException {
        throw new IOException("mark() not supported");
    }

    public void reset() throws IOException {
        throw new IOException("reset() not supported");
    }

    public abstract void close() throws IOException;
}

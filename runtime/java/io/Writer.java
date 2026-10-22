package java.io;

public abstract class Writer {
    protected Object lock;

    protected Writer() {
        lock = this;
    }

    protected Writer(Object lock) {
        this.lock = lock;
    }

    public void write(int c) throws IOException {
        char[] b = { (char) c };
        write(b, 0, 1);
    }

    public void write(char[] cbuf) throws IOException {
        write(cbuf, 0, cbuf.length);
    }

    public abstract void write(char[] cbuf, int off, int len) throws IOException;

    public void write(String s) throws IOException {
        write(s.toCharArray(), 0, s.length());
    }

    public void write(String s, int off, int len) throws IOException {
        write(s.substring(off, off + len));
    }

    public abstract void flush() throws IOException;

    public abstract void close() throws IOException;
}

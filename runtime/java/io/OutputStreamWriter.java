package java.io;

public class OutputStreamWriter extends Writer {
    private final OutputStream out;
    private final String enc;

    public OutputStreamWriter(OutputStream out) {
        this.out = out;
        enc = null;
    }

    public OutputStreamWriter(OutputStream out, String enc) throws UnsupportedEncodingException {
        this.out = out;
        this.enc = enc;
        btvm.Text.encode(new char[0], 0, 0, enc);
    }

    public void write(char[] cbuf, int off, int len) throws IOException {
        out.write(btvm.Text.encode(cbuf, off, len, enc));
    }

    public void flush() throws IOException {
        out.flush();
    }

    public void close() throws IOException {
        out.close();
    }
}

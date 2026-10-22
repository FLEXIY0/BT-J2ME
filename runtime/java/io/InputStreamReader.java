package java.io;

/* Читает поток целиком при первом обращении и декодирует (UTF-8 по умолчанию). */
public class InputStreamReader extends Reader {
    private InputStream in;
    private final String enc;
    private char[] chars;
    private int pos;

    public InputStreamReader(InputStream in) {
        this.in = in;
        enc = null;
    }

    public InputStreamReader(InputStream in, String enc) throws UnsupportedEncodingException {
        this.in = in;
        this.enc = enc;
        btvm.Text.decode(new byte[0], 0, 0, enc);
    }

    private void fill() throws IOException {
        if (chars != null)
            return;
        if (in == null)
            throw new IOException("closed");
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[512];
        int n;
        while ((n = in.read(buf, 0, buf.length)) > 0)
            bo.write(buf, 0, n);
        byte[] all = bo.toByteArray();
        chars = btvm.Text.decode(all, 0, all.length, enc);
    }

    public int read() throws IOException {
        fill();
        return pos < chars.length ? chars[pos++] : -1;
    }

    public int read(char[] cbuf, int off, int len) throws IOException {
        fill();
        if (pos >= chars.length)
            return -1;
        if (len > chars.length - pos)
            len = chars.length - pos;
        System.arraycopy(chars, pos, cbuf, off, len);
        pos += len;
        return len;
    }

    public boolean ready() throws IOException {
        fill();
        return pos < chars.length;
    }

    public void close() throws IOException {
        if (in != null)
            in.close();
        in = null;
    }
}

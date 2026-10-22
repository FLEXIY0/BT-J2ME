package java.io;

public class PrintStream extends OutputStream {
    private final OutputStream out;
    private boolean error;

    public PrintStream(OutputStream out) {
        this.out = out;
    }

    public void write(int b) {
        try {
            out.write(b);
        } catch (IOException e) {
            error = true;
        }
    }

    public void write(byte[] b, int off, int len) {
        try {
            out.write(b, off, len);
        } catch (IOException e) {
            error = true;
        }
    }

    public void flush() {
        try {
            out.flush();
        } catch (IOException e) {
            error = true;
        }
    }

    public void close() {
        flush();
    }

    public boolean checkError() {
        return error;
    }

    public void print(String s) {
        byte[] b = String.valueOf(s).getBytes();
        write(b, 0, b.length);
    }

    public void print(boolean b) { print(String.valueOf(b)); }
    public void print(char c) { print(String.valueOf(c)); }
    public void print(int i) { print(String.valueOf(i)); }
    public void print(long l) { print(String.valueOf(l)); }
    public void print(float f) { print(String.valueOf(f)); }
    public void print(double d) { print(String.valueOf(d)); }
    public void print(char[] s) { print(new String(s)); }
    public void print(Object o) { print(String.valueOf(o)); }
    public void println() { print("\n"); }
    public void println(String s) { print(String.valueOf(s) + "\n"); }
    public void println(boolean b) { println(String.valueOf(b)); }
    public void println(char c) { println(String.valueOf(c)); }
    public void println(int i) { println(String.valueOf(i)); }
    public void println(long l) { println(String.valueOf(l)); }
    public void println(float f) { println(String.valueOf(f)); }
    public void println(double d) { println(String.valueOf(d)); }
    public void println(char[] s) { println(new String(s)); }
    public void println(Object o) { println(String.valueOf(o)); }
}

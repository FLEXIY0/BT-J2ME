package btvm;

import java.io.OutputStream;

/* System.out: копим байты до перевода строки и печатаем через машину. */
public final class ConsoleStream extends OutputStream {
    private final java.io.ByteArrayOutputStream line = new java.io.ByteArrayOutputStream();

    public synchronized void write(int b) {
        line.write(b);
        if (b == '\n')
            flush();
    }

    public synchronized void flush() {
        if (line.size() > 0) {
            Native.print(new String(line.toByteArray()));
            line.reset();
        }
    }
}

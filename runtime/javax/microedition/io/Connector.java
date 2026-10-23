package javax.microedition.io;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/* Сети нет: старые игры ходили на серверы, которых давно нет. Поддерживаем resource:/ и
 * sms:// — платные SMS «уходят» успешно (btvm.SmsConnection), чтобы покупки в играх работали. */
public class Connector {
    public static final int READ = 1, WRITE = 2, READ_WRITE = 3;

    private Connector() {
    }

    public static Connection open(String name) throws IOException {
        return open(name, READ_WRITE, false);
    }

    public static Connection open(String name, int mode) throws IOException {
        return open(name, mode, false);
    }

    public static Connection open(String name, int mode, boolean timeouts) throws IOException {
        if (name == null)
            throw new IllegalArgumentException();
        if (name.startsWith("sms:") || name.startsWith("mms:"))     /* платные SMS — эмуляция оплаты */
            return new btvm.SmsConnection(name);
        throw new ConnectionNotFoundException(name);
    }

    public static InputStream openInputStream(String name) throws IOException {
        if (name != null && name.startsWith("resource:")) {
            InputStream in = Connector.class.getResourceAsStream(name.substring(9).startsWith("/") ? name.substring(9) : "/" + name.substring(9));
            if (in != null)
                return in;
        }
        throw new ConnectionNotFoundException(name);
    }

    public static DataInputStream openDataInputStream(String name) throws IOException {
        return new DataInputStream(openInputStream(name));
    }

    public static OutputStream openOutputStream(String name) throws IOException {
        throw new ConnectionNotFoundException(name);
    }

    public static DataOutputStream openDataOutputStream(String name) throws IOException {
        throw new ConnectionNotFoundException(name);
    }
}

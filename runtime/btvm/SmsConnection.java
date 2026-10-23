package btvm;

import java.io.IOException;
import java.io.InterruptedIOException;
import java.util.Date;

import javax.wireless.messaging.BinaryMessage;
import javax.wireless.messaging.Message;
import javax.wireless.messaging.MessageConnection;
import javax.wireless.messaging.MessageListener;
import javax.wireless.messaging.TextMessage;

/* «Отправка» SMS для старых платных функций: короткие номера давно не работают, поэтому
 * сообщение никуда не уходит, а игра получает ответ «отправлено» — покупка проходит. */
public class SmsConnection implements MessageConnection {
    private final String address;
    private boolean closed;

    public SmsConnection(String address) {
        this.address = address;
    }

    public Message newMessage(String type) {
        return newMessage(type, address);
    }

    public Message newMessage(String type, String addr) {
        if (BINARY_MESSAGE.equals(type))
            return new Bin(addr);
        if (TEXT_MESSAGE.equals(type))
            return new Text(addr);
        throw new IllegalArgumentException(type);
    }

    public void send(Message msg) throws IOException, InterruptedIOException {
        if (closed)
            throw new IOException("closed");
        if (msg == null)
            throw new NullPointerException();
        String body = msg instanceof TextMessage ? ((TextMessage) msg).getPayloadText() : "(двоичное)";
        System.out.println("покупка: SMS на " + msg.getAddress() + " «" + body + "» — считаем оплаченной");
        try {
            Native.vibrate(40);         /* отклик «отправлено» */
        } catch (Throwable e) {
            // вне игры вибрации нет
        }
        try {
            Thread.sleep(600);          /* как будто сеть подумала */
        } catch (InterruptedException e) {
            // ничего
        }
    }

    public Message receive() throws IOException, InterruptedIOException {
        while (!closed) {               /* ответных SMS не бывает — ждём, пока не закроют */
            try {
                Thread.sleep(500);
            } catch (InterruptedException e) {
                throw new InterruptedIOException();
            }
        }
        throw new IOException("closed");
    }

    public void setMessageListener(MessageListener l) throws IOException {
    }

    public int numberOfSegments(Message msg) {
        return 1;
    }

    public void close() throws IOException {
        closed = true;
    }

    static class Base implements Message {
        private String addr;
        private final Date time = new Date();

        Base(String addr) {
            this.addr = addr;
        }

        public String getAddress() { return addr; }
        public void setAddress(String a) { addr = a; }
        public Date getTimestamp() { return time; }
    }

    static class Text extends Base implements TextMessage {
        private String text;

        Text(String addr) { super(addr); }
        public String getPayloadText() { return text; }
        public void setPayloadText(String t) { text = t; }
    }

    static class Bin extends Base implements BinaryMessage {
        private byte[] data;

        Bin(String addr) { super(addr); }
        public byte[] getPayloadData() { return data; }
        public void setPayloadData(byte[] d) { data = d; }
    }
}

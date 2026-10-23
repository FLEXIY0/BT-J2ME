package javax.wireless.messaging;

import java.io.IOException;
import java.io.InterruptedIOException;

/* Connector.open("sms://...") даёт btvm.SmsConnection: сообщение никуда не уходит, но игра
 * считает его отправленным — так проходят покупки за SMS. */
public interface MessageConnection extends javax.microedition.io.Connection {
    String TEXT_MESSAGE = "text", BINARY_MESSAGE = "binary", MULTIPART_MESSAGE = "multipart";

    Message newMessage(String type);
    Message newMessage(String type, String address);
    void send(Message msg) throws IOException, InterruptedIOException;
    Message receive() throws IOException, InterruptedIOException;
    void setMessageListener(MessageListener l) throws IOException;
    int numberOfSegments(Message msg);
}

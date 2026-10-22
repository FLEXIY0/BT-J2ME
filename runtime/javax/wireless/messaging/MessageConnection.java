package javax.wireless.messaging;

import java.io.IOException;
import java.io.InterruptedIOException;

/* SMS не отправляются: Connector.open("sms://...") бросает ConnectionNotFoundException. */
public interface MessageConnection extends javax.microedition.io.Connection {
    String TEXT_MESSAGE = "text", BINARY_MESSAGE = "binary", MULTIPART_MESSAGE = "multipart";

    Message newMessage(String type);
    Message newMessage(String type, String address);
    void send(Message msg) throws IOException, InterruptedIOException;
    Message receive() throws IOException, InterruptedIOException;
    void setMessageListener(MessageListener l) throws IOException;
    int numberOfSegments(Message msg);
}

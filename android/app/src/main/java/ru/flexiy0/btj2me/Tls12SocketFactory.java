package ru.flexiy0.btj2me;

import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/* Android 4.1–4.4 умеет TLS 1.2, но не включает его сам, а GitHub без него не пускает. */
class Tls12SocketFactory extends SSLSocketFactory {
    private final SSLSocketFactory base;

    Tls12SocketFactory() throws Exception {
        SSLContext ctx = SSLContext.getInstance("TLSv1.2");
        ctx.init(null, null, null);
        base = ctx.getSocketFactory();
    }

    private Socket tls(Socket s) {
        if (s instanceof SSLSocket)
            ((SSLSocket) s).setEnabledProtocols(new String[] { "TLSv1.1", "TLSv1.2" });
        return s;
    }

    @Override public String[] getDefaultCipherSuites() { return base.getDefaultCipherSuites(); }
    @Override public String[] getSupportedCipherSuites() { return base.getSupportedCipherSuites(); }

    @Override
    public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException {
        return tls(base.createSocket(s, host, port, autoClose));
    }

    @Override
    public Socket createSocket(String host, int port) throws IOException {
        return tls(base.createSocket(host, port));
    }

    @Override
    public Socket createSocket(String host, int port, InetAddress local, int localPort) throws IOException {
        return tls(base.createSocket(host, port, local, localPort));
    }

    @Override
    public Socket createSocket(InetAddress host, int port) throws IOException {
        return tls(base.createSocket(host, port));
    }

    @Override
    public Socket createSocket(InetAddress addr, int port, InetAddress local, int localPort) throws IOException {
        return tls(base.createSocket(addr, port, local, localPort));
    }
}

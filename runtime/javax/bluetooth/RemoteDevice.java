package javax.bluetooth;

import java.io.IOException;

public class RemoteDevice {
    private final String address;

    protected RemoteDevice(String address) {
        if (address == null)
            throw new NullPointerException();
        this.address = address;
    }

    public boolean isTrustedDevice() { return false; }
    public String getFriendlyName(boolean alwaysAsk) throws IOException { return address; }
    public final String getBluetoothAddress() { return address; }
    public boolean equals(Object o) { return o instanceof RemoteDevice && ((RemoteDevice) o).address.equals(address); }
    public int hashCode() { return address.hashCode(); }

    public static RemoteDevice getRemoteDevice(javax.microedition.io.Connection conn) throws IOException {
        throw new IOException("Bluetooth недоступен");
    }

    public boolean authenticate() throws IOException { return false; }
    public boolean authorize(javax.microedition.io.Connection conn) throws IOException { return false; }
    public boolean encrypt(javax.microedition.io.Connection conn, boolean on) throws IOException { return false; }
    public boolean isAuthenticated() { return false; }
    public boolean isAuthorized(javax.microedition.io.Connection conn) throws IOException { return false; }
    public boolean isEncrypted() { return false; }
}

package javax.bluetooth;

public class DiscoveryAgent {
    public static final int NOT_DISCOVERABLE = 0, GIAC = 0x9E8B33, LIAC = 0x9E8B00, CACHED = 0, PREKNOWN = 1;

    DiscoveryAgent() {
    }

    public RemoteDevice[] retrieveDevices(int option) { return null; }
    public boolean startInquiry(int accessCode, DiscoveryListener l) throws BluetoothStateException { throw new BluetoothStateException(); }
    public boolean cancelInquiry(DiscoveryListener l) { return false; }

    public int searchServices(int[] attrSet, UUID[] uuidSet, RemoteDevice btDev, DiscoveryListener l) throws BluetoothStateException {
        throw new BluetoothStateException();
    }

    public boolean cancelServiceSearch(int transID) { return false; }

    public String selectService(UUID uuid, int security, boolean master) throws BluetoothStateException {
        throw new BluetoothStateException();
    }
}

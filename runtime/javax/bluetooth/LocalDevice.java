package javax.bluetooth;

/* Bluetooth нет: getLocalDevice() честно сообщает об этом, игры показывают свою ошибку. */
public class LocalDevice {
    private LocalDevice() {
    }

    public static LocalDevice getLocalDevice() throws BluetoothStateException {
        throw new BluetoothStateException("Bluetooth недоступен");
    }

    public static String getProperty(String property) {
        return null;
    }

    public static boolean isPowerOn() {
        return false;
    }

    public DiscoveryAgent getDiscoveryAgent() { return null; }
    public String getFriendlyName() { return "BT-J2ME"; }
    public DeviceClass getDeviceClass() { return null; }
    public int getDiscoverable() { return DiscoveryAgent.NOT_DISCOVERABLE; }
    public boolean setDiscoverable(int mode) throws BluetoothStateException { throw new BluetoothStateException(); }
    public String getBluetoothAddress() { return "000000000000"; }
    public ServiceRecord getRecord(javax.microedition.io.Connection notifier) { throw new IllegalArgumentException(); }
    public void updateRecord(ServiceRecord r) throws ServiceRegistrationException { throw new ServiceRegistrationException(); }
}

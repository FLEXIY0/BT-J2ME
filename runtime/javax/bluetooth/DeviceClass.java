package javax.bluetooth;

public class DeviceClass {
    private final int record;

    public DeviceClass(int record) {
        this.record = record;
    }

    public int getServiceClasses() { return record & 0xFFE000; }
    public int getMajorDeviceClass() { return record & 0x1F00; }
    public int getMinorDeviceClass() { return record & 0xFC; }
}

package javax.bluetooth;

public interface DiscoveryListener {
    int INQUIRY_COMPLETED = 0, INQUIRY_TERMINATED = 5, INQUIRY_ERROR = 7;
    int SERVICE_SEARCH_COMPLETED = 1, SERVICE_SEARCH_TERMINATED = 2, SERVICE_SEARCH_ERROR = 3,
            SERVICE_SEARCH_NO_RECORDS = 4, SERVICE_SEARCH_DEVICE_NOT_REACHABLE = 6;

    void deviceDiscovered(RemoteDevice btDevice, DeviceClass cod);
    void servicesDiscovered(int transID, ServiceRecord[] servRecord);
    void serviceSearchCompleted(int transID, int respCode);
    void inquiryCompleted(int discType);
}

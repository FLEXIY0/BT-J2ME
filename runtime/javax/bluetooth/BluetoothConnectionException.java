package javax.bluetooth;

public class BluetoothConnectionException extends java.io.IOException {
    public static final int UNKNOWN_PSM = 1, SECURITY_BLOCK = 2, NO_RESOURCES = 3, FAILED_NOINFO = 4, TIMEOUT = 5, UNACCEPTABLE_PARAMS = 6;
    private final int status;

    public BluetoothConnectionException(int status) {
        this.status = status;
    }

    public BluetoothConnectionException(int status, String msg) {
        super(msg);
        this.status = status;
    }

    public int getStatus() {
        return status;
    }
}

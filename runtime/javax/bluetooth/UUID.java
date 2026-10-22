package javax.bluetooth;

public class UUID {
    private final String hex;

    public UUID(long uuidValue) {
        if (uuidValue < 0 || uuidValue > 0xFFFFFFFFL)
            throw new IllegalArgumentException();
        hex = Long.toString(uuidValue, 16).toUpperCase();
    }

    public UUID(String uuidValue, boolean shortUUID) {
        if (uuidValue == null)
            throw new NullPointerException();
        if (uuidValue.length() == 0 || uuidValue.length() > (shortUUID ? 8 : 32))
            throw new IllegalArgumentException();
        hex = uuidValue.toUpperCase();
    }

    public String toString() { return hex; }
    public boolean equals(Object o) { return o instanceof UUID && ((UUID) o).hex.equals(hex); }
    public int hashCode() { return hex.hashCode(); }
}

package javax.bluetooth;

public class DataElement {
    public static final int NULL = 0, U_INT_1 = 8, U_INT_2 = 9, U_INT_4 = 10, U_INT_8 = 11, U_INT_16 = 12,
            INT_1 = 16, INT_2 = 17, INT_4 = 18, INT_8 = 19, INT_16 = 20, URL = 64, UUID = 24, BOOL = 40,
            STRING = 32, DATSEQ = 48, DATALT = 56;

    private final int type;
    private final Object value;
    private long num;

    public DataElement(int type) { this.type = type; value = null; }
    public DataElement(boolean b) { type = BOOL; value = new Boolean(b); }
    public DataElement(int type, long value) { this.type = type; this.value = null; num = value; }
    public DataElement(int type, Object value) { this.type = type; this.value = value; }

    public int getDataType() { return type; }
    public long getLong() { return num; }
    public boolean getBoolean() { return value instanceof Boolean && ((Boolean) value).booleanValue(); }
    public Object getValue() { return value; }
    public int getSize() { return 0; }
    public void addElement(DataElement e) {}
    public void insertElementAt(DataElement e, int index) {}
    public boolean removeElement(DataElement e) { return false; }
}

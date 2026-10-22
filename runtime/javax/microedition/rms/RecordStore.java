package javax.microedition.rms;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.Hashtable;
import java.util.Vector;

import btvm.Native;

/* Хранилище целиком в памяти; на диск (через Native) пишется после каждого изменения. */
public class RecordStore {
    public static final int AUTHMODE_PRIVATE = 0, AUTHMODE_ANY = 1;

    private static final Hashtable open = new Hashtable();

    private final String name;
    private int openCount;
    private int nextId = 1;
    private int version;
    private long modified;
    final Vector ids = new Vector();
    final Vector data = new Vector();
    private final Vector listeners = new Vector();

    private RecordStore(String name) {
        this.name = name;
    }

    public static synchronized RecordStore openRecordStore(String name, boolean create)
            throws RecordStoreException, RecordStoreFullException, RecordStoreNotFoundException {
        if (name == null || name.length() == 0 || name.length() > 32)
            throw new IllegalArgumentException();
        RecordStore rs = (RecordStore) open.get(name);
        if (rs == null) {
            byte[] raw = Native.rmsLoad(name);
            if (raw == null && !create)
                throw new RecordStoreNotFoundException(name);
            rs = new RecordStore(name);
            if (raw != null)
                rs.load(raw);
            else
                rs.save();
            open.put(name, rs);
        }
        rs.openCount++;
        return rs;
    }

    public static RecordStore openRecordStore(String name, boolean create, int authmode, boolean writable)
            throws RecordStoreException {
        return openRecordStore(name, create);
    }

    public static RecordStore openRecordStore(String name, String vendor, String suite) throws RecordStoreException {
        return openRecordStore(name, false);
    }

    public static synchronized void deleteRecordStore(String name) throws RecordStoreException {
        RecordStore rs = (RecordStore) open.get(name);
        if (rs != null && rs.openCount > 0)
            throw new RecordStoreException("хранилище открыто");
        if (!Native.rmsDelete(name))
            throw new RecordStoreNotFoundException(name);
        open.remove(name);
    }

    public static String[] listRecordStores() {
        String[] s = Native.rmsList();
        return s == null || s.length == 0 ? null : s;
    }

    private void load(byte[] raw) throws RecordStoreException {
        try {
            DataInputStream in = new DataInputStream(new ByteArrayInputStream(raw));
            nextId = in.readInt();
            version = in.readInt();
            modified = in.readLong();
            int n = in.readInt();
            for (int i = 0; i < n; i++) {
                int id = in.readInt();
                byte[] d = new byte[in.readInt()];
                in.readFully(d);
                ids.addElement(new Integer(id));
                data.addElement(d);
            }
        } catch (IOException e) {
            throw new RecordStoreException("хранилище повреждено");
        }
    }

    private void save() throws RecordStoreFullException {
        try {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bo);
            out.writeInt(nextId);
            out.writeInt(version);
            out.writeLong(modified);
            out.writeInt(ids.size());
            for (int i = 0; i < ids.size(); i++) {
                byte[] d = (byte[]) data.elementAt(i);
                out.writeInt(((Integer) ids.elementAt(i)).intValue());
                out.writeInt(d.length);
                out.write(d, 0, d.length);
            }
            if (!Native.rmsSave(name, bo.toByteArray()))
                throw new RecordStoreFullException();
        } catch (IOException e) {
            throw new RecordStoreFullException();
        }
    }

    private void checkOpen() throws RecordStoreNotOpenException {
        if (openCount <= 0)
            throw new RecordStoreNotOpenException(name);
    }

    private int indexOf(int id) throws InvalidRecordIDException {
        for (int i = 0; i < ids.size(); i++)
            if (((Integer) ids.elementAt(i)).intValue() == id)
                return i;
        throw new InvalidRecordIDException(String.valueOf(id));
    }

    private void touch() throws RecordStoreFullException {
        version++;
        modified = System.currentTimeMillis();
        save();
    }

    public synchronized void closeRecordStore() throws RecordStoreNotOpenException, RecordStoreException {
        checkOpen();
        if (--openCount == 0) {
            listeners.removeAllElements();
            open.remove(name);
        }
    }

    public String getName() throws RecordStoreNotOpenException { checkOpen(); return name; }
    public int getVersion() throws RecordStoreNotOpenException { checkOpen(); return version; }
    public long getLastModified() throws RecordStoreNotOpenException { checkOpen(); return modified; }
    public int getNumRecords() throws RecordStoreNotOpenException { checkOpen(); return ids.size(); }
    public int getNextRecordID() throws RecordStoreNotOpenException { checkOpen(); return nextId; }
    public int getSizeAvailable() throws RecordStoreNotOpenException { checkOpen(); return 1 << 20; }

    public synchronized int getSize() throws RecordStoreNotOpenException {
        checkOpen();
        int s = 16;
        for (int i = 0; i < data.size(); i++)
            s += ((byte[]) data.elementAt(i)).length + 8;
        return s;
    }

    public void setMode(int authmode, boolean writable) throws RecordStoreException {
    }

    public void addRecordListener(RecordListener l) {
        if (!listeners.contains(l))
            listeners.addElement(l);
    }

    public void removeRecordListener(RecordListener l) {
        listeners.removeElement(l);
    }

    public int addRecord(byte[] d, int off, int len)
            throws RecordStoreNotOpenException, RecordStoreException, RecordStoreFullException {
        int id;
        synchronized (this) {
            checkOpen();
            byte[] copy = new byte[len];
            if (len > 0)
                System.arraycopy(d, off, copy, 0, len);
            id = nextId++;
            ids.addElement(new Integer(id));
            data.addElement(copy);
            touch();
        }
        for (int i = 0; i < listeners.size(); i++)
            ((RecordListener) listeners.elementAt(i)).recordAdded(this, id);
        return id;
    }

    public void deleteRecord(int id) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        synchronized (this) {
            checkOpen();
            int i = indexOf(id);
            ids.removeElementAt(i);
            data.removeElementAt(i);
            touch();
        }
        for (int i = 0; i < listeners.size(); i++)
            ((RecordListener) listeners.elementAt(i)).recordDeleted(this, id);
    }

    public synchronized int getRecordSize(int id) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        return ((byte[]) data.elementAt(indexOf(id))).length;
    }

    public synchronized int getRecord(int id, byte[] buf, int off)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        byte[] d = (byte[]) data.elementAt(indexOf(id));
        System.arraycopy(d, 0, buf, off, d.length);
        return d.length;
    }

    public synchronized byte[] getRecord(int id) throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        byte[] d = (byte[]) data.elementAt(indexOf(id));
        if (d.length == 0)
            return null;
        byte[] r = new byte[d.length];
        System.arraycopy(d, 0, r, 0, d.length);
        return r;
    }

    public void setRecord(int id, byte[] d, int off, int len)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException, RecordStoreFullException {
        synchronized (this) {
            checkOpen();
            int i = indexOf(id);
            byte[] copy = new byte[len];
            if (len > 0)
                System.arraycopy(d, off, copy, 0, len);
            data.setElementAt(copy, i);
            touch();
        }
        for (int i = 0; i < listeners.size(); i++)
            ((RecordListener) listeners.elementAt(i)).recordChanged(this, id);
    }

    public synchronized RecordEnumeration enumerateRecords(RecordFilter filter, RecordComparator comparator,
            boolean keepUpdated) throws RecordStoreNotOpenException {
        checkOpen();
        return new Enum(this, filter, comparator);
    }

    private static final class Enum implements RecordEnumeration {
        private final RecordStore rs;
        private final RecordFilter filter;
        private final RecordComparator cmp;
        private int[] list;
        private int pos;
        private boolean destroyed;

        Enum(RecordStore rs, RecordFilter f, RecordComparator c) {
            this.rs = rs;
            filter = f;
            cmp = c;
            rebuild();
        }

        public void rebuild() {
            Vector ids = new Vector(), recs = new Vector();
            synchronized (rs) {
                for (int i = 0; i < rs.ids.size(); i++) {
                    byte[] d = (byte[]) rs.data.elementAt(i);
                    if (filter == null || filter.matches(d)) {
                        ids.addElement(rs.ids.elementAt(i));
                        recs.addElement(d);
                    }
                }
            }
            int n = ids.size();
            list = new int[n];
            byte[][] r = new byte[n][];
            for (int i = 0; i < n; i++) {
                list[i] = ((Integer) ids.elementAt(i)).intValue();
                r[i] = (byte[]) recs.elementAt(i);
            }
            if (cmp != null)
                for (int i = 1; i < n; i++)
                    for (int j = i; j > 0 && cmp.compare(r[j - 1], r[j]) == RecordComparator.FOLLOWS; j--) {
                        int t = list[j];
                        list[j] = list[j - 1];
                        list[j - 1] = t;
                        byte[] tb = r[j];
                        r[j] = r[j - 1];
                        r[j - 1] = tb;
                    }
            else
                /* без компаратора — в обратном порядке добавления, как на большинстве телефонов */
                for (int i = 0, j = n - 1; i < j; i++, j--) {
                    int t = list[i];
                    list[i] = list[j];
                    list[j] = t;
                }
            pos = 0;
        }

        private void check() {
            if (destroyed)
                throw new IllegalStateException();
        }

        public int numRecords() { check(); return list.length; }
        public boolean hasNextElement() { check(); return pos < list.length; }
        public boolean hasPreviousElement() { check(); return pos > 0; }
        public void reset() { check(); pos = 0; }
        public void keepUpdated(boolean k) { check(); }
        public boolean isKeptUpdated() { return false; }
        public void destroy() { destroyed = true; }

        public int nextRecordId() throws InvalidRecordIDException {
            check();
            if (pos >= list.length)
                throw new InvalidRecordIDException();
            return list[pos++];
        }

        public int previousRecordId() throws InvalidRecordIDException {
            check();
            if (pos <= 0)
                throw new InvalidRecordIDException();
            return list[--pos];
        }

        public byte[] nextRecord() throws InvalidRecordIDException, RecordStoreNotOpenException, RecordStoreException {
            return rs.getRecord(nextRecordId());
        }

        public byte[] previousRecord() throws InvalidRecordIDException, RecordStoreNotOpenException, RecordStoreException {
            return rs.getRecord(previousRecordId());
        }
    }
}

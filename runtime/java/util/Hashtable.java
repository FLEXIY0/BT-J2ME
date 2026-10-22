package java.util;

public class Hashtable {
    private static final class Entry {
        final Object key;
        Object value;
        Entry next;

        Entry(Object k, Object v, Entry n) {
            key = k;
            value = v;
            next = n;
        }
    }

    private Entry[] table;
    private int count;

    public Hashtable() {
        this(11);
    }

    public Hashtable(int initialCapacity) {
        table = new Entry[initialCapacity > 0 ? initialCapacity : 1];
    }

    private int index(Object key) {
        return (key.hashCode() & 0x7FFFFFFF) % table.length;
    }

    public int size() {
        return count;
    }

    public boolean isEmpty() {
        return count == 0;
    }

    public synchronized boolean contains(Object value) {
        if (value == null)
            throw new NullPointerException();
        for (int i = 0; i < table.length; i++)
            for (Entry e = table[i]; e != null; e = e.next)
                if (e.value.equals(value))
                    return true;
        return false;
    }

    public synchronized boolean containsKey(Object key) {
        for (Entry e = table[index(key)]; e != null; e = e.next)
            if (e.key.equals(key))
                return true;
        return false;
    }

    public synchronized Object get(Object key) {
        for (Entry e = table[index(key)]; e != null; e = e.next)
            if (e.key.equals(key))
                return e.value;
        return null;
    }

    protected void rehash() {
        Entry[] old = table;
        table = new Entry[old.length * 2 + 1];
        for (int i = 0; i < old.length; i++)
            for (Entry e = old[i]; e != null;) {
                Entry n = e.next;
                int j = index(e.key);
                e.next = table[j];
                table[j] = e;
                e = n;
            }
    }

    public synchronized Object put(Object key, Object value) {
        if (key == null || value == null)
            throw new NullPointerException();
        int i = index(key);
        for (Entry e = table[i]; e != null; e = e.next)
            if (e.key.equals(key)) {
                Object old = e.value;
                e.value = value;
                return old;
            }
        if (count >= table.length * 3 / 4) {
            rehash();
            i = index(key);
        }
        table[i] = new Entry(key, value, table[i]);
        count++;
        return null;
    }

    public synchronized Object remove(Object key) {
        int i = index(key);
        for (Entry e = table[i], prev = null; e != null; prev = e, e = e.next)
            if (e.key.equals(key)) {
                if (prev == null)
                    table[i] = e.next;
                else
                    prev.next = e.next;
                count--;
                return e.value;
            }
        return null;
    }

    public synchronized void clear() {
        for (int i = 0; i < table.length; i++)
            table[i] = null;
        count = 0;
    }

    private Enumeration enumerate(final boolean keys) {
        final Vector v = new Vector(count);
        for (int i = 0; i < table.length; i++)
            for (Entry e = table[i]; e != null; e = e.next)
                v.addElement(keys ? e.key : e.value);
        return v.elements();
    }

    public synchronized Enumeration keys() {
        return enumerate(true);
    }

    public synchronized Enumeration elements() {
        return enumerate(false);
    }

    public synchronized String toString() {
        StringBuffer sb = new StringBuffer("{");
        boolean first = true;
        for (int i = 0; i < table.length; i++)
            for (Entry e = table[i]; e != null; e = e.next) {
                if (!first)
                    sb.append(", ");
                first = false;
                sb.append(String.valueOf(e.key)).append('=').append(String.valueOf(e.value));
            }
        return sb.append('}').toString();
    }
}

package java.util;

public class Vector {
    protected Object[] elementData;
    protected int elementCount;
    protected int capacityIncrement;

    public Vector() {
        this(10);
    }

    public Vector(int initialCapacity) {
        this(initialCapacity, 0);
    }

    public Vector(int initialCapacity, int capacityIncrement) {
        if (initialCapacity < 0)
            throw new IllegalArgumentException();
        elementData = new Object[initialCapacity];
        this.capacityIncrement = capacityIncrement;
    }

    public synchronized void copyInto(Object[] a) {
        System.arraycopy(elementData, 0, a, 0, elementCount);
    }

    public synchronized void trimToSize() {
        if (elementCount < elementData.length) {
            Object[] d = new Object[elementCount];
            System.arraycopy(elementData, 0, d, 0, elementCount);
            elementData = d;
        }
    }

    public synchronized void ensureCapacity(int min) {
        if (min > elementData.length) {
            int n = capacityIncrement > 0 ? elementData.length + capacityIncrement : elementData.length * 2;
            if (n < min)
                n = min;
            Object[] d = new Object[n];
            System.arraycopy(elementData, 0, d, 0, elementCount);
            elementData = d;
        }
    }

    public synchronized void setSize(int size) {
        ensureCapacity(size);
        for (int i = size; i < elementCount; i++)
            elementData[i] = null;
        elementCount = size;
    }

    public int capacity() {
        return elementData.length;
    }

    public int size() {
        return elementCount;
    }

    public boolean isEmpty() {
        return elementCount == 0;
    }

    public synchronized Enumeration elements() {
        return new Enumeration() {
            int i;

            public boolean hasMoreElements() {
                return i < elementCount;
            }

            public Object nextElement() {
                synchronized (Vector.this) {
                    if (i < elementCount)
                        return elementData[i++];
                }
                throw new NoSuchElementException();
            }
        };
    }

    public boolean contains(Object o) {
        return indexOf(o, 0) >= 0;
    }

    public int indexOf(Object o) {
        return indexOf(o, 0);
    }

    public synchronized int indexOf(Object o, int index) {
        for (int i = index; i < elementCount; i++)
            if (o == null ? elementData[i] == null : o.equals(elementData[i]))
                return i;
        return -1;
    }

    public int lastIndexOf(Object o) {
        return lastIndexOf(o, elementCount - 1);
    }

    public synchronized int lastIndexOf(Object o, int index) {
        if (index >= elementCount)
            throw new IndexOutOfBoundsException();
        for (int i = index; i >= 0; i--)
            if (o == null ? elementData[i] == null : o.equals(elementData[i]))
                return i;
        return -1;
    }

    public synchronized Object elementAt(int index) {
        if (index < 0 || index >= elementCount)
            throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        return elementData[index];
    }

    public synchronized Object firstElement() {
        if (elementCount == 0)
            throw new NoSuchElementException();
        return elementData[0];
    }

    public synchronized Object lastElement() {
        if (elementCount == 0)
            throw new NoSuchElementException();
        return elementData[elementCount - 1];
    }

    public synchronized void setElementAt(Object o, int index) {
        if (index < 0 || index >= elementCount)
            throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        elementData[index] = o;
    }

    public synchronized void removeElementAt(int index) {
        if (index < 0 || index >= elementCount)
            throw new ArrayIndexOutOfBoundsException(index + " >= " + elementCount);
        System.arraycopy(elementData, index + 1, elementData, index, elementCount - index - 1);
        elementData[--elementCount] = null;
    }

    public synchronized void insertElementAt(Object o, int index) {
        if (index < 0 || index > elementCount)
            throw new ArrayIndexOutOfBoundsException(index + " > " + elementCount);
        ensureCapacity(elementCount + 1);
        System.arraycopy(elementData, index, elementData, index + 1, elementCount - index);
        elementData[index] = o;
        elementCount++;
    }

    public synchronized void addElement(Object o) {
        ensureCapacity(elementCount + 1);
        elementData[elementCount++] = o;
    }

    public synchronized boolean removeElement(Object o) {
        int i = indexOf(o);
        if (i < 0)
            return false;
        removeElementAt(i);
        return true;
    }

    public synchronized void removeAllElements() {
        for (int i = 0; i < elementCount; i++)
            elementData[i] = null;
        elementCount = 0;
    }

    public synchronized String toString() {
        StringBuffer sb = new StringBuffer("[");
        for (int i = 0; i < elementCount; i++) {
            if (i > 0)
                sb.append(", ");
            sb.append(String.valueOf(elementData[i]));
        }
        return sb.append(']').toString();
    }
}

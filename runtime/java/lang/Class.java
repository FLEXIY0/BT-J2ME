package java.lang;

import java.io.ByteArrayInputStream;
import java.io.InputStream;

public final class Class {
    private long vmref;   /* указатель на класс внутри машины */

    private Class() {
    }

    public static native Class forName(String className) throws ClassNotFoundException;

    public native String getName();

    public native boolean isInstance(Object obj);

    public native boolean isAssignableFrom(Class cls);

    public native boolean isArray();

    public native boolean isInterface();

    private native Object newInstance0() throws InstantiationException;

    private static native void init0(Object o);

    private native byte[] getResource0(String name);

    public Object newInstance() throws InstantiationException, IllegalAccessException {
        Object o = newInstance0();
        init0(o);
        return o;
    }

    public InputStream getResourceAsStream(String name) {
        byte[] data = getResource0(name);
        return data == null ? null : new ByteArrayInputStream(data);
    }

    public String toString() {
        return (isInterface() ? "interface " : "class ") + getName();
    }
}

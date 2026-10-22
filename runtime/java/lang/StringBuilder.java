package java.lang;

/* Не из CLDC: нужен только javac для «+» строк внутри самой библиотеки. */
public final class StringBuilder {
    private final StringBuffer sb = new StringBuffer();

    public StringBuilder() {
    }

    public StringBuilder(String s) {
        sb.append(s);
    }

    public StringBuilder append(String s) { sb.append(s); return this; }
    public StringBuilder append(Object o) { sb.append(o); return this; }
    public StringBuilder append(char c) { sb.append(c); return this; }
    public StringBuilder append(int i) { sb.append(i); return this; }
    public StringBuilder append(long l) { sb.append(l); return this; }
    public StringBuilder append(boolean b) { sb.append(b); return this; }
    public StringBuilder append(float f) { sb.append(f); return this; }
    public StringBuilder append(double d) { sb.append(d); return this; }
    public StringBuilder append(char[] c) { sb.append(c); return this; }
    public int length() { return sb.length(); }
    public String toString() { return sb.toString(); }
}

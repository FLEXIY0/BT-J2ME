import java.io.*;
import java.util.*;

/* Самопроверка Java-машины: каждая проверка печатает ok/FAIL, в конце — итог. */
public class VmTest {
    static int fails, passed;
    static StringBuffer log = new StringBuffer();
    static int clinitOrder = 0;

    static void check(String name, boolean ok) {
        if (ok)
            passed++;
        else {
            fails++;
            System.out.println("FAIL " + name);
        }
    }

    static void eq(String name, String got, String want) {
        if (!got.equals(want)) {
            fails++;
            System.out.println("FAIL " + name + ": got [" + got + "] want [" + want + "]");
        } else
            passed++;
    }

    interface Shape { int area(); String name(); }
    static abstract class Base implements Shape {
        public String name() { return "base:" + area(); }
    }
    static class Sq extends Base {
        int s;
        Sq(int s) { this.s = s; }
        public int area() { return s * s; }
    }
    static class Rect extends Sq {
        int w;
        Rect(int s, int w) { super(s); this.w = w; }
        public int area() { return s * w; }
        public String name() { return "rect/" + super.name(); }
    }

    static class MyEx extends Exception {
        int code;
        MyEx(String m, int code) { super(m); this.code = code; }
    }

    static class Init {
        static int v;
        static { v = ++clinitOrder * 10; }
    }

    static int thrower(int x) throws MyEx {
        if (x > 2)
            throw new MyEx("big", x);
        return x;
    }

    static int fin() {
        int r = 0;
        try {
            r = 1;
            throw new RuntimeException("x");
        } catch (RuntimeException e) {
            r = 2;
            return r;
        } finally {
            log.append("fin");
        }
    }

    static int sw(int k) {
        switch (k) {
        case 1: return 10;
        case 2: return 20;
        case 3: return 30;
        case 4: return 40;
        default: return -1;
        }
    }

    static int lsw(int k) {
        switch (k) {
        case -100: return 1;
        case 7: return 2;
        case 100000: return 3;
        default: return 0;
        }
    }

    static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

    static final Object lock = new Object();
    static int counter;
    static boolean ready;

    public static void main(String arg) throws Exception {
        /* целые и переполнения */
        int a = Integer.MAX_VALUE;
        check("int overflow", a + 1 == Integer.MIN_VALUE);
        check("int div neg", -7 / 2 == -3 && -7 % 2 == -1);
        check("min/-1", Integer.MIN_VALUE / -1 == Integer.MIN_VALUE);
        check("shifts", (-16 >> 2) == -4 && (-16 >>> 28) == 15 && (1 << 33) == 2);
        long l = 1L << 40;
        check("long shift", l == 1099511627776L && (l >>> 39) == 2);
        check("long mul", 123456789L * 987654321L == 121932631112635269L);
        check("long div", Long.MIN_VALUE / -1 == Long.MIN_VALUE && -9L % 4 == -1);
        check("long cmp", 5L > 3L && -5L < 3L && Long.MAX_VALUE > 0);
        check("i2b/i2c/i2s", (byte) 200 == -56 && (char) -1 == 65535 && (short) 70000 == 4464);
        try {
            int z = 0;
            int q = 5 / z;
            check("div zero", false);
        } catch (ArithmeticException e) {
            check("div zero", true);
        }

        /* плавающая точка */
        float f = 1.5f;
        double d = 2.25;
        check("float ops", f * 2 == 3.0f && d / 1.5 == 1.5);
        check("f2i sat", (int) 1e20f == Integer.MAX_VALUE && (int) Float.NaN == 0 && (long) -1e30 == Long.MIN_VALUE);
        check("sqrt", Math.sqrt(16.0) == 4.0);
        eq("double str", String.valueOf(0.5), "0.5");
        eq("double str2", Double.toString(100.0), "100.0");
        check("fcmp nan", !(Float.NaN < 1f) && !(Float.NaN > 1f));

        /* строки */
        String s = "Привет, " + "мир" + '!' + 42;
        eq("concat", s, "Привет, мир!42");
        check("str len", s.length() == 14);
        check("intern", ("ab" + "c").intern() == "abc");
        eq("substring", "hello world".substring(6), "world");
        check("indexOf", "abcabc".indexOf("ca") == 2 && "abcabc".lastIndexOf('b') == 4);
        eq("upper", "Ёлка Abc".toUpperCase(), "ЁЛКА ABC");
        eq("trim", "  x y  ".trim(), "x y");
        eq("replace", "a-b-c".replace('-', '+'), "a+b+c");
        check("equals", "abc".equals(new String(new char[] { 'a', 'b', 'c' })) && "abc".hashCode() == 96354);
        eq("int parse", String.valueOf(Integer.parseInt("-12345") + Integer.parseInt("ff", 16)), "-12090");
        eq("hex", Integer.toHexString(-1), "ffffffff");
        eq("long str", Long.toString(Long.MIN_VALUE), "-9223372036854775808");
        StringBuffer sb = new StringBuffer("abc");
        sb.insert(1, "XY").append(3.5).deleteCharAt(0).reverse();
        eq("stringbuffer", sb.toString(), "5.3cbYX");
        byte[] utf = "жук".getBytes();
        check("utf8 bytes", utf.length == 6 && new String(utf).equals("жук"));
        try {
            Integer.parseInt("12x");
            check("nfe", false);
        } catch (NumberFormatException e) {
            check("nfe", true);
        }

        /* объекты, интерфейсы, виртуальные вызовы */
        Shape[] shapes = { new Sq(3), new Rect(2, 5) };
        eq("virtual", shapes[0].name() + " " + shapes[1].name(), "base:9 rect/base:10");
        Object o = shapes[1];
        check("instanceof", o instanceof Sq && o instanceof Shape && !(o instanceof String));
        try {
            String bad = (String) o;
            check("cce", false);
        } catch (ClassCastException e) {
            check("cce", true);
        }
        check("clinit", Init.v == 10 && clinitOrder == 1 && Init.v == 10);

        /* исключения */
        int sum = 0;
        for (int i = 0; i < 5; i++) {
            try {
                sum += thrower(i);
            } catch (MyEx e) {
                sum += e.code * 100;
            }
        }
        check("exceptions", sum == 0 + 1 + 2 + 300 + 400);
        check("finally", fin() == 2 && log.toString().equals("fin"));
        try {
            Object n = null;
            n.toString();
            check("npe", false);
        } catch (NullPointerException e) {
            check("npe", true);
        }
        try {
            int[] arr = new int[3];
            arr[3] = 1;
            check("aioobe", false);
        } catch (ArrayIndexOutOfBoundsException e) {
            check("aioobe", true);
        }
        try {
            Object[] objs = new String[1];
            objs[0] = new Integer(1);
            check("ase", false);
        } catch (ArrayStoreException e) {
            check("ase", true);
        }

        /* массивы и switch */
        int[][] grid = new int[4][5];
        grid[3][4] = 7;
        check("multiarray", grid.length == 4 && grid[0].length == 5 && grid[3][4] == 7);
        long[][][] cube = new long[2][3][4];
        cube[1][2][3] = 99L;
        check("multiarray3", cube[1][2][3] == 99L && cube[1].length == 3);
        char[] cs = new char[2];
        cs[0] = 'Ж';
        check("char array", cs[0] == 'Ж' && cs[1] == 0);
        boolean[] bs = new boolean[3];
        bs[1] = true;
        check("bool array", !bs[0] && bs[1]);
        int[] src = { 1, 2, 3, 4, 5 };
        System.arraycopy(src, 0, src, 1, 4);
        check("arraycopy overlap", src[0] == 1 && src[1] == 1 && src[4] == 4);
        check("tableswitch", sw(1) + sw(3) + sw(4) + sw(9) == 79);
        check("lookupswitch", lsw(-100) + lsw(7) + lsw(100000) + lsw(5) == 6);
        check("recursion", fib(20) == 6765);

        /* коллекции */
        Vector v = new Vector();
        for (int i = 0; i < 100; i++)
            v.addElement(new Integer(i));
        v.removeElementAt(0);
        check("vector", v.size() == 99 && ((Integer) v.elementAt(0)).intValue() == 1);
        Hashtable h = new Hashtable();
        for (int i = 0; i < 200; i++)
            h.put("k" + i, new Integer(i * 2));
        check("hashtable", h.size() == 200 && ((Integer) h.get("k150")).intValue() == 300 && h.get("zz") == null);
        Random r1 = new Random(42), r2 = new Random(42);
        check("random", r1.nextInt(1000) == r2.nextInt(1000) && new Random(42).nextInt() == -1170105035);

        /* потоки ввода-вывода */
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        DataOutputStream dout = new DataOutputStream(bo);
        dout.writeInt(-5);
        dout.writeLong(1L << 50);
        dout.writeUTF("Тест");
        dout.writeBoolean(true);
        DataInputStream din = new DataInputStream(new ByteArrayInputStream(bo.toByteArray()));
        check("data io", din.readInt() == -5 && din.readLong() == (1L << 50) && din.readUTF().equals("Тест") && din.readBoolean());

        /* потоки и мониторы */
        Thread t = new Thread() {
            public void run() {
                for (int i = 0; i < 1000; i++)
                    synchronized (lock) {
                        counter++;
                    }
                synchronized (lock) {
                    ready = true;
                    lock.notifyAll();
                }
            }
        };
        t.start();
        for (int i = 0; i < 1000; i++)
            synchronized (lock) {
                counter++;
            }
        synchronized (lock) {
            while (!ready)
                lock.wait();
        }
        check("threads", counter == 2000);
        long t0 = System.currentTimeMillis();
        Thread.sleep(30);
        check("sleep", System.currentTimeMillis() - t0 >= 25);
        final int[] fired = new int[1];
        Timer timer = new Timer();
        timer.schedule(new TimerTask() {
            public void run() {
                fired[0]++;
            }
        }, 5, 10);
        Thread.sleep(80);
        timer.cancel();
        check("timer", fired[0] >= 4);

        /* сборщик мусора: много мусора, живые данные целы */
        Vector keep = new Vector();
        for (int i = 0; i < 20000; i++) {
            int[] junk = new int[64];
            junk[0] = i;
            if (i % 1000 == 0)
                keep.addElement(junk);
        }
        System.gc();
        Thread.sleep(1);
        boolean alive = keep.size() == 20;
        for (int i = 0; i < keep.size(); i++)
            alive &= ((int[]) keep.elementAt(i))[0] == i * 1000;
        check("gc", alive);

        /* Class.forName / newInstance */
        Object vec = Class.forName("java.util.Vector").newInstance();
        check("forName", vec instanceof Vector && vec.getClass().getName().equals("java.util.Vector"));

        System.out.println("DONE passed=" + passed + " fails=" + fails);
    }
}

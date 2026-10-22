package btvm;

import java.io.UnsupportedEncodingException;
import java.util.Random;

/* Кодировки: UTF-8 по умолчанию; ISO-8859-1 и Windows-1251 — для старых русских игр. */
public final class Text {
    public static final Random RNG = new Random();

    private Text() {
    }

    private static int kind(String enc) throws UnsupportedEncodingException {
        if (enc == null)
            return 0;
        String e = enc.toUpperCase();
        if (e.equals("UTF-8") || e.equals("UTF8"))
            return 0;
        if (e.equals("ISO-8859-1") || e.equals("ISO8859_1") || e.equals("US-ASCII") || e.equals("ASCII"))
            return 1;
        if (e.equals("WINDOWS-1251") || e.equals("CP1251") || e.equals("WIN1251"))
            return 2;
        if (e.equals("UTF-16BE") || e.equals("UNICODEBIG") || e.equals("UTF-16"))
            return 3;
        throw new UnsupportedEncodingException(enc);
    }

    private static char cp1251(int b) {
        if (b < 0x80)
            return (char) b;
        if (b >= 0xC0)
            return (char) (0x410 + b - 0xC0);
        if (b == 0xA8)
            return 'Ё';
        if (b == 0xB8)
            return 'ё';
        return (char) b;
    }

    public static char[] decode(byte[] b, int off, int len, String enc) throws UnsupportedEncodingException {
        int k = enc == null ? 0 : kind(enc);
        char[] out = new char[len];
        int n = 0, end = off + len;
        if (k == 3) {
            out = new char[len / 2];
            for (int i = off; i + 1 < end; i += 2)
                out[n++] = (char) (((b[i] & 0xff) << 8) | (b[i + 1] & 0xff));
        } else if (k != 0) {
            for (int i = off; i < end; i++)
                out[n++] = k == 1 ? (char) (b[i] & 0xff) : cp1251(b[i] & 0xff);
        } else {
            for (int i = off; i < end;) {
                int c = b[i] & 0xff;
                if (c < 0x80) {
                    out[n++] = (char) c;
                    i++;
                } else if ((c & 0xE0) == 0xC0 && i + 1 < end && (b[i + 1] & 0xC0) == 0x80) {
                    out[n++] = (char) (((c & 0x1F) << 6) | (b[i + 1] & 0x3F));
                    i += 2;
                } else if ((c & 0xF0) == 0xE0 && i + 2 < end && (b[i + 1] & 0xC0) == 0x80 && (b[i + 2] & 0xC0) == 0x80) {
                    out[n++] = (char) (((c & 0x0F) << 12) | ((b[i + 1] & 0x3F) << 6) | (b[i + 2] & 0x3F));
                    i += 3;
                } else {
                    /* не UTF-8: старые игры часто в Latin-1/CP1251 — берём байт как есть */
                    out[n++] = cp1251(c);
                    i++;
                }
            }
        }
        if (n == out.length)
            return out;
        char[] r = new char[n];
        System.arraycopy(out, 0, r, 0, n);
        return r;
    }

    public static byte[] encode(char[] c, int off, int len, String enc) throws UnsupportedEncodingException {
        int k = enc == null ? 0 : kind(enc);
        java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream(len);
        for (int i = off; i < off + len; i++) {
            char ch = c[i];
            if (k == 3) {
                o.write(ch >> 8);
                o.write(ch);
            } else if (k == 1) {
                o.write(ch < 256 ? ch : '?');
            } else if (k == 2) {
                if (ch < 0x80)
                    o.write(ch);
                else if (ch >= 0x410 && ch <= 0x44F)
                    o.write(ch - 0x410 + 0xC0);
                else if (ch == 0x401)
                    o.write(0xA8);
                else if (ch == 0x451)
                    o.write(0xB8);
                else
                    o.write('?');
            } else if (ch < 0x80) {
                o.write(ch);
            } else if (ch < 0x800) {
                o.write(0xC0 | (ch >> 6));
                o.write(0x80 | (ch & 0x3F));
            } else {
                o.write(0xE0 | (ch >> 12));
                o.write(0x80 | ((ch >> 6) & 0x3F));
                o.write(0x80 | (ch & 0x3F));
            }
        }
        return o.toByteArray();
    }

    public static byte[] encode(char[] c, int off, int len) {
        try {
            return encode(c, off, len, null);
        } catch (UnsupportedEncodingException e) {
            return new byte[0];
        }
    }

    public static char[] decode(byte[] b, int off, int len) {
        try {
            return decode(b, off, len, null);
        } catch (UnsupportedEncodingException e) {
            return new char[0];
        }
    }
}

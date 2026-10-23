package javax.microedition.m3g;

/* Матрица 4×4 по строкам. */
public class Transform {
    final float[] m = new float[16];

    public Transform() { setIdentity(); }
    public Transform(Transform t) { set(t); }

    public void setIdentity() {
        for (int i = 0; i < 16; i++)
            m[i] = (i % 5 == 0) ? 1 : 0;
    }

    public void set(Transform t) {
        System.arraycopy(t.m, 0, m, 0, 16);
    }

    public void set(float[] a) {
        if (a.length < 16)
            throw new IllegalArgumentException();
        System.arraycopy(a, 0, m, 0, 16);
    }

    public void get(float[] a) {
        if (a.length < 16)
            throw new IllegalArgumentException();
        System.arraycopy(m, 0, a, 0, 16);
    }

    public void postMultiply(Transform t) { mul(t.m); }

    void mul(float[] b) {
        float[] r = new float[16];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                float s = 0;
                for (int k = 0; k < 4; k++)
                    s += m[i * 4 + k] * b[k * 4 + j];
                r[i * 4 + j] = s;
            }
        System.arraycopy(r, 0, m, 0, 16);
    }

    public void postTranslate(float x, float y, float z) {
        mul(new float[] { 1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1 });
    }

    public void postScale(float x, float y, float z) {
        mul(new float[] { x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1 });
    }

    public void postRotate(float angle, float ax, float ay, float az) {
        mul(rotation(angle, ax, ay, az));
    }

    public void postRotateQuat(float qx, float qy, float qz, float qw) {
        float n = (float) Math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (n == 0)
            return;
        qx /= n; qy /= n; qz /= n; qw /= n;
        mul(new float[] {
            1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw), 0,
            2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw), 0,
            2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy), 0,
            0, 0, 0, 1 });
    }

    static float[] rotation(float angle, float x, float y, float z) {
        float n = (float) Math.sqrt(x * x + y * y + z * z);
        if (angle == 0 || n == 0)
            return new float[] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        x /= n; y /= n; z /= n;
        double a = Math.toRadians(angle);
        float c = (float) Math.cos(a), s = (float) Math.sin(a), t = 1 - c;
        return new float[] {
            t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0,
            t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0,
            0, 0, 0, 1 };
    }

    public void transpose() {
        for (int i = 0; i < 4; i++)
            for (int j = i + 1; j < 4; j++) {
                float t = m[i * 4 + j];
                m[i * 4 + j] = m[j * 4 + i];
                m[j * 4 + i] = t;
            }
    }

    public void invert() {
        float[] a = new float[16], inv = new float[16];
        System.arraycopy(m, 0, a, 0, 16);
        for (int i = 0; i < 16; i++)
            inv[i] = (i % 5 == 0) ? 1 : 0;
        for (int c = 0; c < 4; c++) {
            int p = c;
            for (int r = c + 1; r < 4; r++)
                if (Math.abs(a[r * 4 + c]) > Math.abs(a[p * 4 + c]))
                    p = r;
            if (a[p * 4 + c] == 0)
                throw new ArithmeticException("матрица не обращается");
            for (int k = 0; k < 4; k++) {
                float t = a[c * 4 + k]; a[c * 4 + k] = a[p * 4 + k]; a[p * 4 + k] = t;
                t = inv[c * 4 + k]; inv[c * 4 + k] = inv[p * 4 + k]; inv[p * 4 + k] = t;
            }
            float d = a[c * 4 + c];
            for (int k = 0; k < 4; k++) {
                a[c * 4 + k] /= d;
                inv[c * 4 + k] /= d;
            }
            for (int r = 0; r < 4; r++)
                if (r != c) {
                    float f = a[r * 4 + c];
                    for (int k = 0; k < 4; k++) {
                        a[r * 4 + k] -= f * a[c * 4 + k];
                        inv[r * 4 + k] -= f * inv[c * 4 + k];
                    }
                }
        }
        System.arraycopy(inv, 0, m, 0, 16);
    }

    public void transform(float[] v) {
        if (v.length % 4 != 0)
            throw new IllegalArgumentException();
        for (int i = 0; i < v.length; i += 4) {
            float x = v[i], y = v[i + 1], z = v[i + 2], w = v[i + 3];
            for (int r = 0; r < 4; r++)
                v[i + r] = m[r * 4] * x + m[r * 4 + 1] * y + m[r * 4 + 2] * z + m[r * 4 + 3] * w;
        }
    }
}

package javax.microedition.m3g;

public abstract class Transformable extends Object3D {
    float tx, ty, tz, sx = 1, sy = 1, sz = 1;
    float[] rot = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float angle, ax, ay = 0, az = 1;
    final Transform user = new Transform();

    public void setTranslation(float x, float y, float z) { tx = x; ty = y; tz = z; }
    public void translate(float x, float y, float z) { tx += x; ty += y; tz += z; }

    public void getTranslation(float[] xyz) {
        xyz[0] = tx; xyz[1] = ty; xyz[2] = tz;
    }

    public void setScale(float x, float y, float z) { sx = x; sy = y; sz = z; }
    public void scale(float x, float y, float z) { sx *= x; sy *= y; sz *= z; }

    public void getScale(float[] xyz) {
        xyz[0] = sx; xyz[1] = sy; xyz[2] = sz;
    }

    public void setOrientation(float a, float x, float y, float z) {
        angle = a; ax = x; ay = y; az = z;
        rot = Transform.rotation(a, x, y, z);
    }

    public void preRotate(float a, float x, float y, float z) {
        Transform t = new Transform();
        t.set(Transform.rotation(a, x, y, z));
        t.mul(rot);
        rot = t.m;
    }

    public void postRotate(float a, float x, float y, float z) {
        Transform t = new Transform();
        t.set(rot);
        t.mul(Transform.rotation(a, x, y, z));
        rot = t.m;
    }

    public void getOrientation(float[] axisAngle) {
        axisAngle[0] = angle; axisAngle[1] = ax; axisAngle[2] = ay; axisAngle[3] = az;
    }

    public void setTransform(Transform t) {
        if (t == null)
            user.setIdentity();
        else
            user.set(t);
    }

    public void getTransform(Transform t) { t.set(user); }

    /* T · R · S · M, как в спецификации. */
    public void getCompositeTransform(Transform t) {
        t.setIdentity();
        t.postTranslate(tx, ty, tz);
        t.mul(rot);
        t.postScale(sx, sy, sz);
        t.postMultiply(user);
    }
}

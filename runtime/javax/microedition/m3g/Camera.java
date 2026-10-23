package javax.microedition.m3g;

public class Camera extends Node {
    public static final int GENERIC = 48, PARALLEL = 49, PERSPECTIVE = 50;

    int type = GENERIC;
    float fovy, aspect = 1, near = 0.1f, far = 1;
    final Transform proj = new Transform();

    public void setParallel(float h, float aspect, float near, float far) {
        if (h <= 0 || aspect <= 0 || near == far)
            throw new IllegalArgumentException();
        type = PARALLEL;
        fovy = h; this.aspect = aspect; this.near = near; this.far = far;
    }

    public void setPerspective(float fovy, float aspect, float near, float far) {
        if (fovy <= 0 || fovy >= 180 || aspect <= 0 || near <= 0 || far <= 0 || near == far)
            throw new IllegalArgumentException();
        type = PERSPECTIVE;
        this.fovy = fovy; this.aspect = aspect; this.near = near; this.far = far;
    }

    public void setGeneric(Transform t) {
        type = GENERIC;
        proj.set(t);
    }

    public int getProjection(float[] p) {
        if (p != null) {
            p[0] = fovy; p[1] = aspect; p[2] = near; p[3] = far;
        }
        return type;
    }

    public int getProjection(Transform t) {
        if (t != null)
            t.set(proj);
        return type;
    }
}

package javax.microedition.m3g;

public abstract class Node extends Transformable {
    public static final int NONE = 144, ORIGIN = 145, X_AXIS = 146, Y_AXIS = 147, Z_AXIS = 148;

    Node parent;
    boolean rendering = true, picking = true;
    float alpha = 1;
    int scope = -1;

    public Node getParent() { return parent; }
    public void setRenderingEnable(boolean e) { rendering = e; }
    public boolean isRenderingEnabled() { return rendering; }
    public void setPickingEnable(boolean e) { picking = e; }
    public boolean isPickingEnabled() { return picking; }

    public void setAlphaFactor(float a) {
        if (a < 0 || a > 1)
            throw new IllegalArgumentException();
        alpha = a;
    }

    public float getAlphaFactor() { return alpha; }
    public void setScope(int s) { scope = s; }
    public int getScope() { return scope; }
    public void setAlignment(Node zRef, int zTarget, Node yRef, int yTarget) {}
    public void align(Node ref) {}

    /* Положение узла в мире: произведение преобразований от корня. */
    void worldTransform(Transform out) {
        Transform t = new Transform();
        out.setIdentity();
        for (Node n = this; n != null; n = n.parent) {
            n.getCompositeTransform(t);
            t.postMultiply(out);
            out.set(t);
        }
    }

    public boolean getTransformTo(Node target, Transform out) {
        Transform a = new Transform(), b = new Transform();
        worldTransform(a);
        target.worldTransform(b);
        b.invert();
        b.postMultiply(a);
        out.set(b);
        return true;
    }
}

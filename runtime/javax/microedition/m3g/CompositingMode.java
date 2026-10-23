package javax.microedition.m3g;

public class CompositingMode extends Object3D {
    public static final int ALPHA = 64, ALPHA_ADD = 65, MODULATE = 66, MODULATE_X2 = 67, REPLACE = 68;

    int blending = REPLACE;
    float threshold;
    boolean depthTest = true, depthWrite = true, colorWrite = true, alphaWrite = true;
    float offFactor, offUnits;

    public void setBlending(int mode) {
        if (mode < ALPHA || mode > REPLACE)
            throw new IllegalArgumentException();
        blending = mode;
    }

    public int getBlending() { return blending; }

    public void setAlphaThreshold(float t) {
        if (t < 0 || t > 1)
            throw new IllegalArgumentException();
        threshold = t;
    }

    public float getAlphaThreshold() { return threshold; }
    public void setDepthTestEnable(boolean e) { depthTest = e; }
    public boolean isDepthTestEnabled() { return depthTest; }
    public void setDepthWriteEnable(boolean e) { depthWrite = e; }
    public boolean isDepthWriteEnabled() { return depthWrite; }
    public void setColorWriteEnable(boolean e) { colorWrite = e; }
    public boolean isColorWriteEnabled() { return colorWrite; }
    public void setAlphaWriteEnable(boolean e) { alphaWrite = e; }
    public boolean isAlphaWriteEnabled() { return alphaWrite; }
    public void setDepthOffset(float f, float u) { offFactor = f; offUnits = u; }
    public float getDepthOffsetFactor() { return offFactor; }
    public float getDepthOffsetUnits() { return offUnits; }
}

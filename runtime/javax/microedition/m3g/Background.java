package javax.microedition.m3g;

public class Background extends Object3D {
    public static final int BORDER = 32, REPEAT = 33;

    int color;
    boolean colorClear = true, depthClear = true;
    Image2D image;
    int modeX = BORDER, modeY = BORDER, cropX, cropY, cropW, cropH;

    public void setColor(int argb) { color = argb; }
    public int getColor() { return color; }
    public void setColorClearEnable(boolean e) { colorClear = e; }
    public boolean isColorClearEnabled() { return colorClear; }
    public void setDepthClearEnable(boolean e) { depthClear = e; }
    public boolean isDepthClearEnabled() { return depthClear; }

    public void setImage(Image2D img) {
        image = img;
        if (img != null) {
            cropX = cropY = 0;
            cropW = img.w;
            cropH = img.h;
        }
    }

    public Image2D getImage() { return image; }
    public void setImageMode(int x, int y) { modeX = x; modeY = y; }
    public int getImageModeX() { return modeX; }
    public int getImageModeY() { return modeY; }

    public void setCrop(int x, int y, int w, int h) {
        cropX = x; cropY = y; cropW = w; cropH = h;
    }

    public int getCropX() { return cropX; }
    public int getCropY() { return cropY; }
    public int getCropWidth() { return cropW; }
    public int getCropHeight() { return cropH; }
}

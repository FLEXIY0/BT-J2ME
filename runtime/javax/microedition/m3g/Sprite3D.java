package javax.microedition.m3g;

public class Sprite3D extends Node {
    final boolean scaled;
    Image2D image;
    Appearance appearance;
    int cropX, cropY, cropW, cropH;

    public Sprite3D(boolean scaled, Image2D image, Appearance a) {
        this.scaled = scaled;
        setImage(image);
        appearance = a;
    }

    public boolean isScaled() { return scaled; }
    public void setAppearance(Appearance a) { appearance = a; }
    public Appearance getAppearance() { return appearance; }

    public void setImage(Image2D img) {
        if (img == null)
            throw new NullPointerException();
        image = img;
        cropX = cropY = 0;
        cropW = img.w;
        cropH = img.h;
    }

    public Image2D getImage() { return image; }

    public void setCrop(int x, int y, int w, int h) {
        cropX = x; cropY = y; cropW = w; cropH = h;
    }

    public int getCropX() { return cropX; }
    public int getCropY() { return cropY; }
    public int getCropWidth() { return cropW; }
    public int getCropHeight() { return cropH; }
}

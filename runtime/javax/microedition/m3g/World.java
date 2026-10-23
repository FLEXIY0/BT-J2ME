package javax.microedition.m3g;

public class World extends Group {
    Camera camera;
    Background background;

    public void setActiveCamera(Camera c) {
        if (c == null)
            throw new NullPointerException();
        camera = c;
    }

    public Camera getActiveCamera() { return camera; }
    public void setBackground(Background b) { background = b; }
    public Background getBackground() { return background; }
}

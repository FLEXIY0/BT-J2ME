package javax.microedition.lcdui;

public class ImageItem extends Item {
    private Image image;
    private String altText;
    private final int appearance;

    public ImageItem(String label, Image img, int layout, String altText) {
        this(label, img, layout, altText, PLAIN);
    }

    public ImageItem(String label, Image img, int layout, String altText, int appearance) {
        super(label);
        image = img;
        this.layout = layout;
        this.altText = altText;
        this.appearance = appearance;
    }

    public Image getImage() { return image; }

    public void setImage(Image img) {
        image = img;
        Display.requestRepaint();
    }

    public String getAltText() { return altText; }
    public void setAltText(String s) { altText = s; }
    public int getAppearanceMode() { return appearance; }

    int height(int w) {
        return labelHeight() + (image != null ? image.getHeight() : 0);
    }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        if (image == null)
            return;
        int h = layout & 3;
        int ix = h == LAYOUT_CENTER ? x + (w - image.getWidth()) / 2 : h == LAYOUT_RIGHT ? x + w - image.getWidth() : x;
        if (focused) {
            g.setColor(Theme.ACCENT);
            g.drawRect(ix - 2, y - 2, image.getWidth() + 3, image.getHeight() + 3);
        }
        g.drawImage(image, ix, y, Graphics.TOP | Graphics.LEFT);
    }
}

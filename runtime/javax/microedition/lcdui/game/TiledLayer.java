package javax.microedition.lcdui.game;

import javax.microedition.lcdui.GameCanvasAccess;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;

public class TiledLayer extends Layer {
    final int cols, rows;
    int cellW, cellH;
    private Image image;
    private int tiles;
    private final int[] cells;
    private int[] animated = new int[4];
    private int animCount;

    public TiledLayer(int columns, int rows, Image img, int tw, int th) {
        super(columns * tw, rows * th);
        if (columns <= 0 || rows <= 0)
            throw new IllegalArgumentException();
        cols = columns;
        this.rows = rows;
        cells = new int[columns * rows];
        setStaticTileSet(img, tw, th);
    }

    public void setStaticTileSet(Image img, int tw, int th) {
        if (img == null)
            throw new NullPointerException();
        if (tw <= 0 || th <= 0 || img.getWidth() % tw != 0 || img.getHeight() % th != 0)
            throw new IllegalArgumentException();
        int newTiles = (img.getWidth() / tw) * (img.getHeight() / th);
        if (newTiles < tiles) {
            for (int i = 0; i < cells.length; i++)
                cells[i] = 0;
            animCount = 0;
        }
        image = img;
        cellW = tw;
        cellH = th;
        tiles = newTiles;
        setSize(cols * tw, rows * th);
    }

    public int createAnimatedTile(int staticIndex) {
        if (staticIndex < 0 || staticIndex > tiles)
            throw new IndexOutOfBoundsException();
        if (animCount == animated.length) {
            int[] n = new int[animated.length * 2];
            System.arraycopy(animated, 0, n, 0, animCount);
            animated = n;
        }
        animated[animCount++] = staticIndex;
        return -animCount;
    }

    public void setAnimatedTile(int idx, int staticIndex) {
        if (idx >= 0 || -idx > animCount || staticIndex < 0 || staticIndex > tiles)
            throw new IndexOutOfBoundsException();
        animated[-idx - 1] = staticIndex;
    }

    public int getAnimatedTile(int idx) {
        if (idx >= 0 || -idx > animCount)
            throw new IndexOutOfBoundsException();
        return animated[-idx - 1];
    }

    public void setCell(int col, int row, int idx) {
        if (col < 0 || col >= cols || row < 0 || row >= rows)
            throw new IndexOutOfBoundsException();
        if (idx > tiles || -idx > animCount)
            throw new IndexOutOfBoundsException();
        cells[row * cols + col] = idx;
    }

    public int getCell(int col, int row) {
        if (col < 0 || col >= cols || row < 0 || row >= rows)
            throw new IndexOutOfBoundsException();
        return cells[row * cols + col];
    }

    public void fillCells(int col, int row, int nc, int nr, int idx) {
        if (nc < 0 || nr < 0 || col < 0 || row < 0 || col + nc > cols || row + nr > rows)
            throw new IndexOutOfBoundsException();
        for (int r = row; r < row + nr; r++)
            for (int c = col; c < col + nc; c++)
                cells[r * cols + c] = idx;
    }

    public final int getCellWidth() { return cellW; }
    public final int getCellHeight() { return cellH; }
    public final int getColumns() { return cols; }
    public final int getRows() { return rows; }

    private int resolve(int idx) {
        return idx < 0 ? animated[-idx - 1] : idx;
    }

    boolean opaqueAt(int sx, int sy) {
        int c = (sx - x) / cellW, r = (sy - y) / cellH;
        if (sx < x || sy < y || c >= cols || r >= rows)
            return false;
        int t = resolve(cells[r * cols + c]);
        if (t == 0)
            return false;
        int perRow = image.getWidth() / cellW;
        int ix = ((t - 1) % perRow) * cellW + (sx - x) % cellW, iy = ((t - 1) / perRow) * cellH + (sy - y) % cellH;
        return (GameCanvasAccess.pixels(image)[iy * image.getWidth() + ix] >>> 24) != 0;
    }

    public final void paint(Graphics g) {
        if (!visible)
            return;
        int perRow = image.getWidth() / cellW;
        int clipX = g.getClipX(), clipY = g.getClipY(), clipW = g.getClipWidth(), clipH = g.getClipHeight();
        int c0 = Math.max(0, (clipX - x) / cellW), c1 = Math.min(cols - 1, (clipX + clipW - x) / cellW);
        int r0 = Math.max(0, (clipY - y) / cellH), r1 = Math.min(rows - 1, (clipY + clipH - y) / cellH);
        for (int r = r0; r <= r1; r++)
            for (int c = c0; c <= c1; c++) {
                int t = resolve(cells[r * cols + c]);
                if (t == 0)
                    continue;
                t--;
                g.drawRegion(image, (t % perRow) * cellW, (t / perRow) * cellH, cellW, cellH, 0,
                        x + c * cellW, y + r * cellH, Graphics.TOP | Graphics.LEFT);
            }
    }
}

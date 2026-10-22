import javax.microedition.lcdui.*;
import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.*;

/* Самопроверка MIDP: рисование (по пикселям), шрифты, RMS, экраны, цикл событий. */
public class MidpTest extends MIDlet {
    static int passed, fails;

    static void check(String name, boolean ok) {
        if (ok)
            passed++;
        else {
            fails++;
            System.out.println("FAIL " + name);
        }
    }

    static int px(Image img, int x, int y) {
        int[] p = new int[1];
        img.getRGB(p, 0, 1, x, y, 1, 1);
        return p[0];
    }

    class TestCanvas extends Canvas {
        int paints;

        protected void paint(Graphics g) {
            paints++;
            g.setColor(0x336699);
            g.fillRect(0, 0, getWidth(), getHeight());
        }
    }

    protected void startApp() {
        /* рисование в изменяемую картинку */
        Image img = Image.createImage(20, 20);
        check("white", px(img, 5, 5) == 0xFFFFFFFF);
        Graphics g = img.getGraphics();
        g.setColor(0xFF0000);
        g.fillRect(2, 2, 4, 4);
        check("fillRect", px(img, 2, 2) == 0xFFFF0000 && px(img, 5, 5) == 0xFFFF0000 && px(img, 6, 6) == 0xFFFFFFFF);
        g.setColor(0x00FF00);
        g.drawLine(0, 10, 19, 10);
        check("drawLine", px(img, 0, 10) == 0xFF00FF00 && px(img, 19, 10) == 0xFF00FF00 && px(img, 0, 11) == 0xFFFFFFFF);
        g.setColor(0x0000FF);
        g.drawRect(12, 12, 4, 4);
        check("drawRect", px(img, 12, 12) == 0xFF0000FF && px(img, 16, 16) == 0xFF0000FF && px(img, 14, 14) == 0xFFFFFFFF);
        g.translate(1, 1);
        g.setClip(0, 0, 2, 2);
        g.setColor(0x123456);
        g.fillRect(-5, -5, 50, 50);
        check("clip+translate", px(img, 1, 1) == 0xFF123456 && px(img, 2, 2) == 0xFF123456 && px(img, 3, 3) == 0xFFFF0000);

        /* картинка с прозрачностью и поворотом */
        int[] rgb = { 0xFFFF0000, 0x00000000, 0xFF00FF00, 0xFF0000FF };
        Image src = Image.createRGBImage(rgb, 2, 2, true);
        Image dst = Image.createImage(4, 4);
        Graphics dg = dst.getGraphics();
        dg.drawImage(src, 0, 0, Graphics.TOP | Graphics.LEFT);
        check("drawImage", px(dst, 0, 0) == 0xFFFF0000 && px(dst, 1, 0) == 0xFFFFFFFF && px(dst, 1, 1) == 0xFF0000FF);
        dg.drawRegion(src, 0, 0, 2, 2, 5 /* ROT90 */, 2, 2, Graphics.TOP | Graphics.LEFT);
        check("drawRegion rot90", px(dst, 2, 2) == 0xFF00FF00 && px(dst, 3, 2) == 0xFFFF0000 && px(dst, 2, 3) == 0xFF0000FF);
        Image mirrored = Image.createImage(src, 0, 0, 2, 2, 2 /* MIRROR */);
        check("createImage mirror", px(mirrored, 1, 0) == 0xFFFF0000 && px(mirrored, 0, 1) == 0xFF0000FF);

        /* шрифты: пропорциональные ширины и якоря текста */
        Font f = Font.getFont(Font.FACE_PROPORTIONAL, Font.STYLE_PLAIN, Font.SIZE_MEDIUM);
        check("font widths", f.charWidth('i') < f.charWidth('m') && f.stringWidth("ab") == f.charWidth('a') + f.charWidth('b'));
        check("font height", f.getHeight() > 8 && f.getBaselinePosition() <= f.getHeight());
        Image t = Image.createImage(60, 20);
        Graphics tg = t.getGraphics();
        tg.setColor(0);
        tg.setFont(f);
        tg.drawString("Ж", 30, 0, Graphics.TOP | Graphics.HCENTER);
        boolean ink = false;
        for (int x = 0; x < 60; x++)
            for (int y = 0; y < 20; y++)
                ink |= px(t, x, y) == 0xFF000000;
        check("drawString cyrillic", ink);

        /* RMS: запись, чтение, перебор, удаление */
        try {
            try {
                RecordStore.deleteRecordStore("test");
            } catch (RecordStoreNotFoundException e) {
            }
            RecordStore rs = RecordStore.openRecordStore("test", true);
            int a = rs.addRecord("one".getBytes(), 0, 3);
            int b = rs.addRecord("two!".getBytes(), 0, 4);
            rs.setRecord(a, "uno".getBytes(), 0, 3);
            check("rms get", new String(rs.getRecord(a)).equals("uno") && rs.getRecordSize(b) == 4);
            RecordEnumeration en = rs.enumerateRecords(null, null, false);
            check("rms enum", en.numRecords() == 2);
            rs.deleteRecord(b);
            rs.closeRecordStore();
            RecordStore again = RecordStore.openRecordStore("test", false);
            check("rms persisted", again.getNumRecords() == 1 && new String(again.getRecord(a)).equals("uno"));
            again.closeRecordStore();
            String[] list = RecordStore.listRecordStores();
            check("rms list", list != null && list.length >= 1);
        } catch (Exception e) {
            check("rms exception " + e, false);
        }

        /* системные экраны создаются и показываются */
        Form form = new Form("Форма");
        form.append(new TextField("Имя", "abc", 10, TextField.ANY));
        ChoiceGroup cg = new ChoiceGroup("Выбор", Choice.EXCLUSIVE, new String[] { "a", "b" }, null);
        form.append(cg);
        check("form", form.size() == 2 && cg.getSelectedIndex() == 0);
        List list = new List("Список", List.IMPLICIT, new String[] { "x", "y", "z" }, null);
        check("list", list.size() == 3 && list.getSelectedIndex() == 0);

        final Display d = Display.getDisplay(this);
        final TestCanvas c = new TestCanvas();
        d.setCurrent(form);
        d.setCurrent(c);
        c.repaint();
        c.serviceRepaints();
        check("canvas painted", c.paints >= 1 && d.getCurrent() == c);
        check("canvas size", c.getWidth() == 240 && c.getHeight() > 200);
        check("game action", c.getGameAction(Canvas.KEY_NUM2) == Canvas.UP && c.getKeyCode(Canvas.FIRE) == -5);

        d.callSerially(new Runnable() {
            public void run() {
                check("callSerially", true);
                System.out.println("MIDP DONE passed=" + passed + " fails=" + fails);
                notifyDestroyed();
            }
        });
    }

    protected void pauseApp() {
    }

    protected void destroyApp(boolean unconditional) {
    }
}

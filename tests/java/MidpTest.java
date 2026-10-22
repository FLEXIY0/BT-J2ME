import javax.microedition.lcdui.*;
import javax.microedition.media.*;
import javax.microedition.media.control.*;
import javax.microedition.midlet.MIDlet;
import javax.microedition.rms.*;

import com.nokia.mid.sound.Sound;
import com.nokia.mid.sound.SoundListener;

/* Самопроверка MIDP: рисование (по пикселям), шрифты, RMS, звук, экраны, цикл событий. */
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


    static void sleep(int ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException e) {
        }
    }

    /* ждём, пока плеер доиграет (не дольше ms) */
    static boolean waitState(Player p, int state, int ms) {
        for (int t = 0; t < ms && p.getState() != state; t += 10)
            sleep(10);
        return p.getState() == state;
    }

    /* MIDI-файл формата 0: одна нота длиной 1 такт (480 тиков) при темпе 500000 мкс — ровно 0,5 с */
    static byte[] midi() {
        return new byte[] { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 1, (byte) 0xE0,
            'M', 'T', 'r', 'k', 0, 0, 0, 16,
            0, (byte) 0xC0, 80,
            0, (byte) 0x90, 69, 100,
            (byte) 0x83, 0x60, (byte) 0x80, 69, 0,
            0, (byte) 0xFF, 0x2F, 0 };
    }

    /* WAV 8 кГц 8 бит: 2000 сэмплов — 0,25 с */
    static byte[] wav() {
        int n = 2000;
        byte[] b = new byte[44 + n];
        String h = "RIFF....WAVEfmt ";
        for (int i = 0; i < 16; i++)
            b[i] = (byte) h.charAt(i);
        int[] fmt = { 16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1F, 0, 0, 0x40, 0x1F, 0, 0, 1, 0, 8, 0 };
        for (int i = 0; i < fmt.length; i++)
            b[16 + i] = (byte) fmt[i];
        b[36] = 'd'; b[37] = 'a'; b[38] = 't'; b[39] = 'a';
        b[40] = (byte) (n & 0xFF);
        b[41] = (byte) (n >> 8);
        for (int i = 0; i < n; i++)
            b[44 + i] = (byte) (128 + ((i / 10) % 2 == 0 ? 60 : -60));
        return b;
    }

    /* рингтон Nokia OTT: 4 четвертные ноты при 125 уд/мин — 1,92 с */
    static byte[] ott() {
        int[] v = { 2, 8, 0x25, 7, 0, 1, 0x1D, 7, 0, 3, 1, 8, 0, 3, 0, 2, 0, 4, 6, 8,
            4, 3, 14, 5, 2, 3, 1, 2,
            1, 3, 1, 4, 2, 3, 0, 2,  1, 3, 5, 4, 2, 3, 0, 2,  1, 3, 8, 4, 2, 3, 0, 2,  1, 3, 1, 4, 2, 3, 0, 2 };
        /* пары «значение, число бит» после первого числа-счётчика команд */
        byte[] out = new byte[16];
        int bit = 0;
        for (int i = 0; i + 1 < v.length; i += 2) {
            for (int k = v[i + 1] - 1; k >= 0; k--, bit++)
                if (((v[i] >> k) & 1) != 0)
                    out[bit >> 3] |= (byte) (0x80 >> (bit & 7));
        }
        return out;
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


        /* звук: тоновая последовательность, MIDI, WAV, повторы, слушатели, Nokia Sound */
        try {
            Player tp = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
            tp.realize();
            ToneControl tc = (ToneControl) tp.getControl("ToneControl");
            /* темп 120 уд/мин (байт 30), 8 нот по 1/16 (4 из 64): 0,125 с каждая — 1 с */
            tc.setSequence(new byte[] { ToneControl.VERSION, 1, ToneControl.TEMPO, 30,
                60, 4, 62, 4, 64, 4, ToneControl.SILENCE, 4, 65, 4, 67, 4, 69, 4, 71, 4 });
            check("tone duration", Math.abs(tp.getDuration() - 1000000) < 2000);
            final int[] events = new int[3];
            tp.addPlayerListener(new PlayerListener() {
                public void playerUpdate(Player p, String ev, Object data) {
                    if (ev == STARTED) events[0]++;
                    if (ev == END_OF_MEDIA) events[1]++;
                    if (ev == STOPPED) events[2]++;
                }
            });
            tp.start();
            check("tone started", tp.getState() == Player.STARTED && events[0] == 1);
            sleep(300);
            long mt = tp.getMediaTime();
            check("tone media time", mt > 150000 && mt < 700000);
            check("tone end", waitState(tp, Player.PREFETCHED, 2500) && events[1] >= 1);
            tp.close();
            check("closed", tp.getState() == Player.CLOSED);

            Player mp = Manager.createPlayer(new java.io.ByteArrayInputStream(midi()), "audio/midi");
            mp.prefetch();
            check("midi duration", Math.abs(mp.getDuration() - 500000) < 2000);
            mp.setLoopCount(2);
            long t0 = System.currentTimeMillis();
            mp.start();
            boolean ended = waitState(mp, Player.PREFETCHED, 3000);
            long took = System.currentTimeMillis() - t0;
            check("midi loops " + took + " ms", ended && took >= 900 && took < 1500);
            mp.start();
            sleep(100);
            mp.stop();
            check("midi stop", mp.getState() == Player.PREFETCHED);
            mp.close();

            Player wp = Manager.createPlayer(new java.io.ByteArrayInputStream(wav()), "audio/x-wav");
            wp.realize();
            check("wav duration", wp.getDuration() == 250000);
            check("wav seek", Math.abs(wp.setMediaTime(100000) - 100000) < 1000 && Math.abs(wp.getMediaTime() - 100000) < 1000);
            VolumeControl vc = (VolumeControl) wp.getControl("VolumeControl");
            check("volume", vc.setLevel(150) == 100 && vc.getLevel() == 100);
            wp.start();
            check("wav end", waitState(wp, Player.PREFETCHED, 1500));
            wp.close();

            Player bad = Manager.createPlayer(new java.io.ByteArrayInputStream(new byte[] { 1, 2, 3, 4 }), "audio/amr");
            bad.start();
            check("unknown format plays silence", bad.getDuration() == Player.TIME_UNKNOWN && bad.getState() == Player.STARTED);
            bad.close();

            Manager.playTone(69, 100, 50);
            check("playTone", true);
        } catch (Exception e) {
            check("sound exception " + e, false);
        }
        {
            final int[] ev = new int[2];
            SoundListener sl = new SoundListener() {
                public void soundStateChanged(Sound s, int e) {
                    ev[e == Sound.SOUND_PLAYING ? 0 : 1]++;
                }
            };
            Sound s = new Sound(880, 200);
            s.setSoundListener(sl);
            s.play(1);
            check("nokia tone playing", s.getState() == Sound.SOUND_PLAYING);
            for (int w = 0; w < 1500 && s.getState() == Sound.SOUND_PLAYING; w += 10)
                sleep(10);
            check("nokia tone stopped", s.getState() == Sound.SOUND_STOPPED && ev[0] == 1 && ev[1] == 1);
            Sound o = new Sound(ott(), Sound.FORMAT_TONE);
            o.play(1);
            sleep(500);
            check("nokia ott playing", o.getState() == Sound.SOUND_PLAYING);
            o.stop();
            check("nokia ott stop", o.getState() == Sound.SOUND_STOPPED);
            o.release();
            s.release();
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

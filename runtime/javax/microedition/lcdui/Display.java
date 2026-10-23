package javax.microedition.lcdui;

import java.util.Vector;

import btvm.Native;

/* Экран и цикл событий. Главный поток MIDlet после startApp() крутит runLoop(): разбирает
 * клавиши, перерисовывает текущий Displayable и выполняет callSerially. */
public class Display {
    public static final int LIST_ELEMENT = 1, CHOICE_GROUP_ELEMENT = 2, ALERT = 3;
    public static final int COLOR_BACKGROUND = 0, COLOR_FOREGROUND = 1, COLOR_HIGHLIGHTED_BACKGROUND = 2,
            COLOR_HIGHLIGHTED_FOREGROUND = 3, COLOR_BORDER = 4, COLOR_HIGHLIGHTED_BORDER = 5;

    static final int W = Native.screenWidth(), H = Native.screenHeight();
    static final Object LOCK = new Object();
    static final Image screen = Image.createImage(W, H);
    static final Graphics sg = new Graphics(screen.pixels, W, H);

    static Display instance;
    static volatile boolean repaintPending;
    static final Vector serial = new Vector();
    static boolean menuOpen;
    static int menuSel;
    static int keyStates, keyLatch;
    static int heldKey;
    static long heldSince, lastRepeat;
    static long alertDeadline;
    static boolean exitRequested;

    Displayable current;

    private Display() {
    }

    public static Display getDisplay(javax.microedition.midlet.MIDlet m) {
        if (m == null)
            throw new NullPointerException();
        synchronized (LOCK) {
            if (instance == null)
                instance = new Display();
        }
        return instance;
    }

    public Displayable getCurrent() {
        return current;
    }

    public void setCurrent(Displayable next) {
        if (next == null)
            return;
        if (next instanceof Alert) {
            setCurrent((Alert) next, current);
            return;
        }
        show(next);
    }

    public void setCurrent(Alert alert, Displayable next) {
        if (alert == null)
            throw new NullPointerException();
        if (next instanceof Alert)
            throw new IllegalArgumentException();
        alert.next = next != null ? next : current;
        show(alert);
        alertDeadline = alert.isModal() ? 0 : System.currentTimeMillis() + alert.getTimeout();
    }

    public void setCurrentItem(Item item) {
        if (item.owner != null) {
            if (item.owner instanceof Form)
                ((Form) item.owner).focusItem(item);
            setCurrent(item.owner);
        }
    }

    void show(Displayable d) {
        Displayable old = current;
        menuOpen = false;
        heldKey = 0;
        keyStates = 0;
        if (old == d) {
            requestRepaint();
            return;
        }
        current = d;
        if (!(d instanceof Alert))
            alertDeadline = 0;
        if (old != null)
            old.hideNotifyInternal();
        d.layoutSoft();
        d.showNotifyInternal();
        Native.textInput(wantsText(d));
        requestRepaint();
    }

    /* Нужна ли экранная клавиатура: TextBox или форма с полем ввода. */
    private static boolean wantsText(Displayable d) {
        if (d instanceof TextBox)
            return true;
        if (d instanceof Form) {
            Form f = (Form) d;
            for (int i = 0; i < f.items.size(); i++)
                if (f.items.elementAt(i) instanceof TextField)
                    return true;
        }
        return false;
    }

    public void callSerially(Runnable r) {
        synchronized (serial) {
            serial.addElement(r);
        }
        Native.wake();
    }

    public boolean isColor() { return true; }
    public int numColors() { return 1 << 16; }
    public int numAlphaLevels() { return 256; }
    public boolean flashBacklight(int duration) { return true; }
    public boolean vibrate(int duration) {
        if (duration < 0)
            throw new IllegalArgumentException();
        Native.vibrate(Math.min(duration, 2000));
        return true;
    }

    public int getColor(int specifier) {
        switch (specifier) {
        case COLOR_BACKGROUND: return Theme.BG;
        case COLOR_FOREGROUND: return Theme.TEXT;
        case COLOR_HIGHLIGHTED_BACKGROUND: return Theme.ACCENT;
        case COLOR_HIGHLIGHTED_FOREGROUND: return Theme.SEL_TEXT;
        case COLOR_BORDER: return Theme.DIVIDER;
        case COLOR_HIGHLIGHTED_BORDER: return Theme.ACCENT;
        default: throw new IllegalArgumentException();
        }
    }

    public int getBorderStyle(boolean highlighted) { return Graphics.SOLID; }
    public int getBestImageWidth(int type) { return 32; }
    public int getBestImageHeight(int type) { return 32; }

    /* ---------- внутреннее ---------- */

    static void requestRepaint() {
        repaintPending = true;
        Native.wake();
    }

    static void serviceRepaints(Canvas c) {
        if (repaintPending && instance != null && instance.current == c)
            paintNow();
    }

    static void report(Throwable t) {
        System.out.println("BT-J2ME: исключение в игре: " + t);
    }

    static void paintNow() {
        synchronized (LOCK) {
            repaintPending = false;
            Displayable cur = instance == null ? null : instance.current;
            if (cur == null) {
                sg.reset();
                sg.setColor(0);
                sg.fillRect(0, 0, W, H);
            } else {
                try {
                    cur.paintInternal(sg);
                } catch (Throwable t) {
                    report(t);
                }
                if (menuOpen)
                    paintMenu(cur);
            }
            Native.flush(screen.pixels, W, H);
        }
    }

    /* GameCanvas.flushGraphics: сразу показать буфер, без ожидания цикла событий */
    static void flushNow(Displayable d) {
        synchronized (LOCK) {
            if (instance == null || instance.current != d)
                return;
            d.paintInternal(sg);
            if (menuOpen)
                paintMenu(d);
            Native.flush(screen.pixels, W, H);
        }
    }

    static void invokeCommand(CommandListener l, Command c, Displayable d) {
        try {
            l.commandAction(c, d);
        } catch (Throwable t) {
            report(t);
        }
    }

    private static void paintMenu(Displayable d) {
        Vector m = d.menu;
        if (m == null) {
            menuOpen = false;
            return;
        }
        Graphics g = sg;
        g.reset();
        Font f = Theme.MEDIUM;
        int rowH = f.getHeight() + 6, w = 0;
        for (int i = 0; i < m.size(); i++)
            w = Math.max(w, f.stringWidth(((Command) m.elementAt(i)).getLabel()));
        w = Math.min(W - 8, w + 20);
        int h = rowH * m.size() + 4, x = 2, y = H - Theme.SOFTBAR_H - h - 2;
        if (y < 2)
            y = 2;
        g.setColor(0x2A2430);
        g.fillRect(x + 2, y + 2, w, h);
        g.setColor(Theme.BG);
        g.fillRect(x, y, w, h);
        g.setColor(Theme.DIVIDER);
        g.drawRect(x, y, w - 1, h - 1);
        g.setFont(f);
        for (int i = 0; i < m.size(); i++) {
            int ry = y + 2 + i * rowH;
            if (ry + rowH > H - Theme.SOFTBAR_H)
                break;
            if (i == menuSel) {
                g.setColor(Theme.ACCENT);
                g.fillRect(x + 2, ry, w - 4, rowH);
                g.setColor(Theme.SEL_TEXT);
            } else {
                g.setColor(Theme.TEXT);
            }
            g.drawString(((Command) m.elementAt(i)).getLabel(), x + 8, ry + 3, Graphics.TOP | Graphics.LEFT);
        }
    }

    private static void menuKey(Displayable d, int code) {
        Vector m = d.menu;
        int a = Canvas.gameAction(code);
        if (m == null) {
            menuOpen = false;
        } else if (a == Canvas.UP) {
            menuSel = (menuSel + m.size() - 1) % m.size();
        } else if (a == Canvas.DOWN) {
            menuSel = (menuSel + 1) % m.size();
        } else if (a == Canvas.FIRE || code == -6) {
            menuOpen = false;
            requestRepaint();
            d.fireCommand((Command) m.elementAt(menuSel));
            return;
        } else if (code == -7 || a == Canvas.LEFT) {
            menuOpen = false;
        }
        requestRepaint();
    }

    private static boolean softKey(Displayable d, int code) {
        if (code == -6) {
            if (d.menu != null) {
                menuOpen = true;
                menuSel = 0;
                requestRepaint();
                return true;
            }
            if (d.softLeft != null) {
                d.fireCommand(d.softLeft);
                return true;
            }
        } else if (code == -7 && d.softRight != null) {
            d.fireCommand(d.softRight);
            return true;
        }
        return false;
    }

    private static void updateKeyState(int code, boolean down) {
        int a = Canvas.gameAction(code);
        if (a == 0)
            return;
        if (down) {
            keyStates |= 1 << a;
            keyLatch |= 1 << a;
        } else {
            keyStates &= ~(1 << a);
        }
    }

    private static void dispatch(int type, int[] ev) {
        Displayable cur = instance == null ? null : instance.current;
        long now = System.currentTimeMillis();
        switch (type) {
        case Native.EV_EXIT:
            exitRequested = true;
            return;
        case Native.EV_KEY_DOWN: {
            int code = ev[0];
            updateKeyState(code, true);
            if (cur == null)
                return;
            if (menuOpen) {
                menuKey(cur, code);
                return;
            }
            if ((code == -6 || code == -7) && softKey(cur, code))
                return;
            heldKey = code;
            heldSince = lastRepeat = now;
            cur.keyDown(code, ev[1] != 0);
            return;
        }
        case Native.EV_KEY_UP: {
            int code = ev[0];
            updateKeyState(code, false);
            if (heldKey == code)
                heldKey = 0;
            if (cur != null && !menuOpen)
                cur.keyUp(code);
            return;
        }
        case Native.EV_POINTER_DOWN:
        case Native.EV_POINTER_UP:
        case Native.EV_POINTER_MOVE: {
            int x = ev[0], y = ev[1];
            if (cur == null)
                return;
            if (cur.hasSoftBar() && y >= H - Theme.SOFTBAR_H) {
                if (type == Native.EV_POINTER_DOWN) {
                    if (menuOpen)
                        menuOpen = false;
                    softKey(cur, x < W / 2 ? -6 : -7);
                    requestRepaint();
                }
                return;
            }
            if (menuOpen) {
                if (type == Native.EV_POINTER_DOWN) {
                    menuOpen = false;
                    requestRepaint();
                }
                return;
            }
            if (type == Native.EV_POINTER_DOWN)
                cur.pointerDown(x, y);
            else if (type == Native.EV_POINTER_UP)
                cur.pointerUp(x, y);
            else
                cur.pointerDrag(x, y);
            return;
        }
        case Native.EV_CHAR:
            if (cur != null && !menuOpen)
                cur.charTyped((char) ev[0]);
            return;
        }
    }

    private static void runSerial() {
        for (;;) {
            Runnable r;
            synchronized (serial) {
                if (serial.isEmpty())
                    return;
                r = (Runnable) serial.elementAt(0);
                serial.removeElementAt(0);
            }
            try {
                r.run();
            } catch (Throwable t) {
                report(t);
            }
        }
    }

    private static void dismissAlert() {
        Displayable cur = instance == null ? null : instance.current;
        if (cur instanceof Alert)
            ((Alert) cur).dismiss();
    }

    /* Вызывается из javax.microedition.midlet.Runner после startApp(). */
    public static void runLoop() {
        int[] ev = new int[4];
        while (!exitRequested) {
            runSerial();
            if (repaintPending)
                paintNow();
            long now = System.currentTimeMillis();
            if (alertDeadline != 0 && now >= alertDeadline) {
                alertDeadline = 0;
                dismissAlert();
            }
            Displayable cur = instance == null ? null : instance.current;
            if (heldKey != 0 && cur != null && now - heldSince >= 400 && now - lastRepeat >= 80) {
                lastRepeat = now;
                try {
                    cur.keyRepeat(heldKey);
                } catch (Throwable t) {
                    report(t);
                }
            }
            int type = Native.pollEvent(ev);
            if (type == 0) {
                boolean idle;
                synchronized (serial) {
                    idle = serial.isEmpty();
                }
                if (idle && !repaintPending) {
                    long wait = 100;
                    if (heldKey != 0)
                        wait = 20;
                    if (alertDeadline != 0)
                        wait = Math.max(1, Math.min(wait, alertDeadline - now));
                    Native.waitEvent((int) wait);
                }
                continue;
            }
            try {
                dispatch(type, ev);
            } catch (Throwable t) {
                report(t);
            }
        }
    }

    static int gameKeyStates() {
        int s = keyStates | keyLatch;
        keyLatch = 0;
        return s;
    }
}

package javax.microedition.media;

import java.io.IOException;
import java.io.InputStream;
import java.util.Vector;

import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;

import btvm.Native;

/* Плеер поверх нативного микшера (audio.c). Конец звука замечает общий поток-сторож:
 * он переводит плеер из STARTED в PREFETCHED и рассылает END_OF_MEDIA. */
final class AudioPlayer implements Player, VolumeControl, ToneControl {
    private static final Vector active = new Vector();
    private static Thread watcher;

    private int state = UNREALIZED;
    private int handle;
    private final String type;
    private final Vector listeners = new Vector();
    private int level = 100;
    private boolean muted;
    private int loops = 1;

    AudioPlayer(byte[] data, String type) {
        this.type = type;
        handle = Native.sndCreate(data, 0);
    }

    static byte[] readAll(InputStream in) throws IOException {
        byte[] buf = new byte[4096];
        int n = 0, r;

        while ((r = in.read(buf, n, buf.length - n)) > 0) {
            n += r;
            if (n == buf.length) {
                byte[] nb = new byte[buf.length * 2];
                System.arraycopy(buf, 0, nb, 0, n);
                buf = nb;
            }
        }
        byte[] out = new byte[n];
        System.arraycopy(buf, 0, out, 0, n);
        return out;
    }

    private void fire(String ev, Object data) {
        for (int i = 0; i < listeners.size(); i++) {
            try {
                ((PlayerListener) listeners.elementAt(i)).playerUpdate(this, ev, data);
            } catch (RuntimeException e) {
                /* ошибка в слушателе игры не должна ломать звук */
            }
        }
    }

    private void checkClosed() {
        if (state == CLOSED)
            throw new IllegalStateException();
    }

    /* Звук доиграл сам: как на телефоне — PREFETCHED и END_OF_MEDIA. */
    private boolean poll() {
        boolean ended;

        synchronized (this) {
            ended = state == STARTED && !Native.sndPlaying(handle);
            if (ended)
                state = PREFETCHED;
        }
        if (ended)
            fire(PlayerListener.END_OF_MEDIA, new Long(getDuration()));
        return ended;
    }

    private static void watch(AudioPlayer p) {
        synchronized (active) {
            if (!active.contains(p))
                active.addElement(p);
            if (watcher != null)
                return;
            watcher = new Thread() {
                public void run() {
                    for (;;) {
                        AudioPlayer[] list;
                        synchronized (active) {
                            if (active.isEmpty()) {
                                watcher = null;
                                return;
                            }
                            list = new AudioPlayer[active.size()];
                            active.copyInto(list);
                        }
                        for (int i = 0; i < list.length; i++)
                            if (list[i].state != STARTED || list[i].poll())
                                synchronized (active) {
                                    if (list[i].state != STARTED)
                                        active.removeElement(list[i]);
                                }
                        try {
                            Thread.sleep(20);
                        } catch (InterruptedException e) {
                        }
                    }
                }
            };
            watcher.start();
        }
    }

    public synchronized void realize() throws MediaException {
        checkClosed();
        if (state < REALIZED)
            state = REALIZED;
    }

    public synchronized void prefetch() throws MediaException {
        checkClosed();
        if (state < PREFETCHED)
            state = PREFETCHED;
    }

    public void start() throws MediaException {
        synchronized (this) {
            checkClosed();
            if (state == STARTED)
                return;
            state = STARTED;
            Native.sndLoops(handle, loops);
            Native.sndVolume(handle, muted ? 0 : level);
            Native.sndPlay(handle);
        }
        watch(this);
        fire(PlayerListener.STARTED, new Long(getMediaTime()));
    }

    public void stop() throws MediaException {
        boolean was;

        synchronized (this) {
            checkClosed();
            was = state == STARTED;
            if (was) {
                Native.sndPause(handle);
                state = PREFETCHED;
            }
        }
        if (was)
            fire(PlayerListener.STOPPED, new Long(getMediaTime()));
    }

    public void deallocate() {
        checkClosed();
        if (state == STARTED) {
            try {
                stop();
            } catch (MediaException e) {
            }
        }
        synchronized (this) {
            if (state > REALIZED)
                state = REALIZED;
        }
    }

    public void close() {
        synchronized (this) {
            if (state == CLOSED)
                return;
            Native.sndFree(handle);
            handle = 0;
            state = CLOSED;
        }
        fire(PlayerListener.CLOSED, null);
    }

    public void setTimeBase(TimeBase master) throws MediaException {
        checkClosed();
    }

    public TimeBase getTimeBase() {
        return Manager.getSystemTimeBase();
    }

    public synchronized long setMediaTime(long now) throws MediaException {
        checkClosed();
        if (state == UNREALIZED)
            throw new IllegalStateException();
        return Native.sndSeek(handle, now);
    }

    public synchronized long getMediaTime() {
        return state == CLOSED ? TIME_UNKNOWN : Native.sndTime(handle);
    }

    public int getState() {
        if (state == STARTED)
            poll();
        return state;
    }

    public synchronized long getDuration() {
        return state == CLOSED ? TIME_UNKNOWN : Native.sndDuration(handle);
    }

    public String getContentType() {
        return type;
    }

    public synchronized void setLoopCount(int count) {
        checkClosed();
        if (count == 0 || state == STARTED)
            throw count == 0 ? (RuntimeException) new IllegalArgumentException() : new IllegalStateException();
        loops = count;
    }

    public void addPlayerListener(PlayerListener l) {
        checkClosed();
        if (l != null && !listeners.contains(l))
            listeners.addElement(l);
    }

    public void removePlayerListener(PlayerListener l) {
        listeners.removeElement(l);
    }

    public Control[] getControls() {
        return new Control[] { this };
    }

    public Control getControl(String t) {
        if (t.endsWith("VolumeControl") || t.endsWith("ToneControl"))
            return this;
        return null;
    }

    public synchronized void setMute(boolean m) {
        muted = m;
        Native.sndVolume(handle, muted ? 0 : level);
    }

    public boolean isMuted() {
        return muted;
    }

    public synchronized int setLevel(int l) {
        level = l < 0 ? 0 : l > 100 ? 100 : l;
        if (!muted)
            Native.sndVolume(handle, level);
        fire(PlayerListener.VOLUME_CHANGED, this);
        return level;
    }

    public int getLevel() {
        return level;
    }

    public synchronized void setSequence(byte[] seq) {
        if (seq == null)
            throw new IllegalArgumentException();
        if (state >= PREFETCHED)
            throw new IllegalStateException();
        Native.sndFree(handle);
        handle = Native.sndCreate(seq, 0);
    }
}

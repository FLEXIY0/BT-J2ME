package javax.microedition.media;

import java.util.Vector;

import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;

final class SilentPlayer implements Player, VolumeControl, ToneControl {
    private int state = UNREALIZED;
    private final String type;
    private final Vector listeners = new Vector();
    private int level = 100;
    private boolean muted;

    SilentPlayer(String type) {
        this.type = type;
    }

    private void fire(String ev) {
        for (int i = 0; i < listeners.size(); i++)
            ((PlayerListener) listeners.elementAt(i)).playerUpdate(this, ev, null);
    }

    private void checkClosed() {
        if (state == CLOSED)
            throw new IllegalStateException();
    }

    public void realize() { checkClosed(); if (state < REALIZED) state = REALIZED; }
    public void prefetch() { checkClosed(); if (state < PREFETCHED) state = PREFETCHED; }

    public void start() {
        checkClosed();
        state = STARTED;
        fire(PlayerListener.STARTED);
        state = PREFETCHED;
        fire(PlayerListener.END_OF_MEDIA);
    }

    public void stop() { checkClosed(); if (state == STARTED) state = PREFETCHED; }
    public void deallocate() { checkClosed(); if (state > REALIZED) state = REALIZED; }
    public void close() { state = CLOSED; fire(PlayerListener.CLOSED); }
    public void setTimeBase(TimeBase master) {}
    public TimeBase getTimeBase() { return Manager.getSystemTimeBase(); }
    public long setMediaTime(long now) { return 0; }
    public long getMediaTime() { return 0; }
    public int getState() { return state; }
    public long getDuration() { return 0; }
    public String getContentType() { return type; }
    public void setLoopCount(int count) {}
    public void addPlayerListener(PlayerListener l) { if (l != null) listeners.addElement(l); }
    public void removePlayerListener(PlayerListener l) { listeners.removeElement(l); }
    public Control[] getControls() { return new Control[] { this }; }

    public Control getControl(String t) {
        if (t.endsWith("VolumeControl") || t.endsWith("ToneControl"))
            return this;
        return null;
    }

    public void setMute(boolean m) { muted = m; }
    public boolean isMuted() { return muted; }
    public int setLevel(int l) { level = Math.max(0, Math.min(100, l)); return level; }
    public int getLevel() { return level; }
    public void setSequence(byte[] seq) {}
}

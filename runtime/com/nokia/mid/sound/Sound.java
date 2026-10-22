package com.nokia.mid.sound;

import btvm.Native;

/* Звук Nokia UI API: тон заданной частоты, рингтон Smart Messaging (FORMAT_TONE) или WAV. */
public class Sound {
    public static final int FORMAT_TONE = 1, FORMAT_WAV = 5, SOUND_PLAYING = 0, SOUND_STOPPED = 1, SOUND_UNINITIALIZED = 3;

    private int state = SOUND_UNINITIALIZED;
    private int gain = 255;
    private int handle;
    private SoundListener listener;
    private Thread watcher;

    public Sound(int freq, long duration) {
        init(freq, duration);
    }

    public Sound(byte[] data, int type) {
        init(data, type);
    }

    public static int getConcurrentSoundCount(int type) {
        return 4;
    }

    public static int[] getSupportedFormats() {
        return new int[] { FORMAT_TONE, FORMAT_WAV };
    }

    public void init(int freq, long duration) {
        if (freq < 0 || duration <= 0)
            throw new IllegalArgumentException();
        replace(Native.sndCreateFreq(freq, (int) Math.min(duration, 600000)));
    }

    public void init(byte[] data, int type) {
        if (data == null)
            throw new NullPointerException();
        if (type != FORMAT_TONE && type != FORMAT_WAV)
            throw new IllegalArgumentException();
        replace(Native.sndCreate(data, type == FORMAT_TONE ? 1 : 0));
    }

    private synchronized void replace(int h) {
        if (handle != 0)
            Native.sndFree(handle);
        handle = h;
        state = SOUND_STOPPED;
        Native.sndVolume(handle, gain * 100 / 255);
    }

    private void changed(int s) {
        SoundListener l;

        synchronized (this) {
            if (state == s)
                return;
            state = s;
            l = listener;
        }
        if (l != null)
            l.soundStateChanged(this, s);
    }

    /* loop: 0 — бесконечно, иначе сколько раз (до 255) */
    public void play(int loop) {
        if (loop < 0 || loop > 255)
            throw new IllegalArgumentException();
        synchronized (this) {
            if (state == SOUND_UNINITIALIZED)
                return;
            Native.sndPause(handle);
            Native.sndSeek(handle, 0);
            Native.sndLoops(handle, loop == 0 ? -1 : loop);
            Native.sndPlay(handle);
        }
        changed(SOUND_PLAYING);
        watch();
    }

    private synchronized void watch() {
        if (watcher != null)
            return;
        watcher = new Thread() {
            public void run() {
                for (;;) {
                    try {
                        Thread.sleep(20);
                    } catch (InterruptedException e) {
                    }
                    synchronized (Sound.this) {
                        if (state == SOUND_PLAYING && Native.sndPlaying(handle))
                            continue;
                        watcher = null;
                        if (state != SOUND_PLAYING)
                            return;
                    }
                    changed(SOUND_STOPPED);
                    return;
                }
            }
        };
        watcher.start();
    }

    public void stop() {
        synchronized (this) {
            if (state != SOUND_PLAYING)
                return;
            Native.sndPause(handle);
        }
        changed(SOUND_STOPPED);
    }

    public void resume() {
        synchronized (this) {
            if (state != SOUND_STOPPED)
                return;
            Native.sndPlay(handle);
        }
        changed(SOUND_PLAYING);
        watch();
    }

    public void release() {
        synchronized (this) {
            Native.sndFree(handle);
            handle = 0;
        }
        changed(SOUND_UNINITIALIZED);
    }

    public int getState() {
        return state;
    }

    public synchronized void setGain(int g) {
        gain = g < 0 ? 0 : g > 255 ? 255 : g;
        Native.sndVolume(handle, gain * 100 / 255);
    }

    public int getGain() {
        return gain;
    }

    public void setSoundListener(SoundListener l) {
        listener = l;
    }
}

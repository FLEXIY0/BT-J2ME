package com.nokia.mid.sound;

public class Sound {
    public static final int FORMAT_TONE = 1, FORMAT_WAV = 5, SOUND_PLAYING = 0, SOUND_STOPPED = 1, SOUND_UNINITIALIZED = 3;

    private int state = SOUND_STOPPED;
    private int gain = 255;

    public Sound(int freq, long duration) {
    }

    public Sound(byte[] data, int type) {
    }

    public static int getConcurrentSoundCount(int type) { return 1; }
    public static int[] getSupportedFormats() { return new int[] { FORMAT_TONE, FORMAT_WAV }; }
    public void init(int freq, long duration) {}
    public void init(byte[] data, int type) {}
    public void play(int loop) { state = SOUND_STOPPED; }
    public void stop() { state = SOUND_STOPPED; }
    public void resume() {}
    public void release() { state = SOUND_UNINITIALIZED; }
    public int getState() { return state; }
    public void setGain(int g) { gain = g; }
    public int getGain() { return gain; }
    public void setSoundListener(SoundListener l) {}
}

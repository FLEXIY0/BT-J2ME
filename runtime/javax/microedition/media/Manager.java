package javax.microedition.media;

import java.io.InputStream;

/* Звука пока нет: плееры «играют» беззвучно, чтобы игры не падали. */
public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone", MIDI_DEVICE_LOCATOR = "device://midi";

    private Manager() {
    }

    public static String[] getSupportedContentTypes(String protocol) {
        return new String[] { "audio/midi", "audio/x-wav", "audio/x-tone-seq" };
    }

    public static String[] getSupportedProtocols(String contentType) {
        return new String[] { "device", "resource" };
    }

    public static Player createPlayer(String locator) throws java.io.IOException, MediaException {
        if (locator == null)
            throw new IllegalArgumentException();
        return new SilentPlayer("audio/midi");
    }

    public static Player createPlayer(InputStream in, String type) throws java.io.IOException, MediaException {
        if (in == null)
            throw new IllegalArgumentException();
        return new SilentPlayer(type);
    }

    public static void playTone(int note, int duration, int volume) throws MediaException {
        if (duration <= 0 || note < 0 || note > 127)
            throw new IllegalArgumentException();
    }

    public static TimeBase getSystemTimeBase() {
        return new TimeBase() {
            public long getTime() {
                return System.currentTimeMillis() * 1000;
            }
        };
    }
}

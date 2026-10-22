package javax.microedition.media;

import java.io.IOException;
import java.io.InputStream;

import btvm.Native;

/* Звук играет нативный микшер: MIDI, WAV, AU, тоновые последовательности.
 * Остальные форматы (AMR, MP3) «играют» тишину, чтобы игры не падали. */
public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone", MIDI_DEVICE_LOCATOR = "device://midi";

    private Manager() {
    }

    public static String[] getSupportedContentTypes(String protocol) {
        return new String[] { "audio/midi", "audio/x-wav", "audio/basic", "audio/x-tone-seq" };
    }

    public static String[] getSupportedProtocols(String contentType) {
        return new String[] { "device", "resource" };
    }

    public static Player createPlayer(String locator) throws IOException, MediaException {
        if (locator == null)
            throw new IllegalArgumentException();
        if (locator.startsWith("resource:")) {
            InputStream in = Manager.class.getResourceAsStream(locator.substring(9));
            if (in == null)
                throw new MediaException("нет ресурса " + locator);
            return createPlayer(in, null);
        }
        /* device://tone, device://midi и всё, что не умеем открыть, — пустой плеер */
        return new AudioPlayer(null, locator.startsWith("device://tone") ? "audio/x-tone-seq" : "audio/midi");
    }

    public static Player createPlayer(InputStream in, String type) throws IOException, MediaException {
        if (in == null)
            throw new IllegalArgumentException();
        return new AudioPlayer(AudioPlayer.readAll(in), type);
    }

    public static void playTone(int note, int duration, int volume) throws MediaException {
        if (duration <= 0 || note < 0 || note > 127)
            throw new IllegalArgumentException();
        Native.sndTone(note, duration, volume < 0 ? 0 : volume > 100 ? 100 : volume);
    }

    public static TimeBase getSystemTimeBase() {
        return new TimeBase() {
            public long getTime() {
                return System.currentTimeMillis() * 1000;
            }
        };
    }
}

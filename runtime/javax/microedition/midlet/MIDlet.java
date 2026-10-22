package javax.microedition.midlet;

import btvm.Native;

public abstract class MIDlet {
    static MIDlet current;

    protected MIDlet() {
        current = this;
    }

    protected abstract void startApp() throws MIDletStateChangeException;

    protected abstract void pauseApp();

    protected abstract void destroyApp(boolean unconditional) throws MIDletStateChangeException;

    public final void notifyDestroyed() {
        Native.exit();
    }

    public final void notifyPaused() {
    }

    public final void resumeRequest() {
    }

    public final String getAppProperty(String key) {
        if (key == null)
            throw new NullPointerException();
        return Native.getAppProperty(key);
    }

    public final boolean platformRequest(String url) throws javax.microedition.io.ConnectionNotFoundException {
        return false;
    }

    public final int checkPermission(String permission) {
        return 1;
    }
}

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

    /* Ссылку открывает браузер телефона (на Android), «tel:»/«sms:» — набор номера или письмо,
     * отправить их может только сам человек. false — закрывать игру для этого не нужно. */
    public final boolean platformRequest(String url) throws javax.microedition.io.ConnectionNotFoundException {
        if (url == null || url.length() == 0)
            return false;
        if (!btvm.Native.openUrl(url))
            throw new javax.microedition.io.ConnectionNotFoundException(url);
        return false;
    }

    public final int checkPermission(String permission) {
        return 1;
    }
}

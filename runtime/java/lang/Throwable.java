package java.lang;

public class Throwable {
    private String message;

    public Throwable() {
    }

    public Throwable(String message) {
        this.message = message;
    }

    public String getMessage() {
        return message;
    }

    public String toString() {
        String n = getClass().getName();
        return message != null ? n + ": " + message : n;
    }

    public void printStackTrace() {
        System.err.println(toString());
    }
}

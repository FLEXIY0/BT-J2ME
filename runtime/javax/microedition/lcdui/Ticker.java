package javax.microedition.lcdui;

public class Ticker {
    private String text;

    public Ticker(String text) {
        setString(text);
    }

    public void setString(String s) {
        if (s == null)
            throw new NullPointerException();
        text = s;
    }

    public String getString() {
        return text;
    }
}

package javax.microedition.lcdui;

import java.util.Calendar;
import java.util.Date;
import java.util.TimeZone;

/* Показывает дату; правка с клавиатуры не поддерживается. */
public class DateField extends Item {
    public static final int DATE = 1, TIME = 2, DATE_TIME = 3;

    private Date date;
    private int mode;

    public DateField(String label, int mode) {
        this(label, mode, null);
    }

    public DateField(String label, int mode, TimeZone tz) {
        super(label);
        setInputMode(mode);
    }

    public Date getDate() { return date; }

    public void setDate(Date d) {
        date = d;
        Display.requestRepaint();
    }

    public int getInputMode() { return mode; }

    public void setInputMode(int m) {
        if (m < DATE || m > DATE_TIME)
            throw new IllegalArgumentException();
        mode = m;
    }

    int height(int w) { return labelHeight() + Theme.MEDIUM.getHeight() + 2; }

    void paint(Graphics g, int x, int y, int w, boolean focused) {
        y = paintLabel(g, x, y);
        g.setFont(Theme.MEDIUM);
        g.setColor(Theme.TEXT);
        String s = "--";
        if (date != null) {
            Calendar c = Calendar.getInstance();
            c.setTime(date);
            String d = c.get(Calendar.DATE) + "." + (c.get(Calendar.MONTH) + 1) + "." + c.get(Calendar.YEAR);
            int mi = c.get(Calendar.MINUTE);
            String t = c.get(Calendar.HOUR_OF_DAY) + ":" + (mi < 10 ? "0" : "") + mi;
            s = mode == DATE ? d : mode == TIME ? t : d + " " + t;
        }
        g.drawString(s, x, y, Graphics.TOP | Graphics.LEFT);
    }
}

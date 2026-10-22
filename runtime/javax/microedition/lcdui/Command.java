package javax.microedition.lcdui;

public class Command {
    public static final int SCREEN = 1, BACK = 2, CANCEL = 3, OK = 4, HELP = 5, STOP = 6, EXIT = 7, ITEM = 8;

    private final String label, longLabel;
    private final int type, priority;

    public Command(String label, int type, int priority) {
        this(label, null, type, priority);
    }

    public Command(String shortLabel, String longLabel, int type, int priority) {
        if (shortLabel == null)
            throw new NullPointerException();
        if (type < SCREEN || type > ITEM)
            throw new IllegalArgumentException();
        label = shortLabel;
        this.longLabel = longLabel;
        this.type = type;
        this.priority = priority;
    }

    public String getLabel() { return label; }
    public String getLongLabel() { return longLabel; }
    public int getCommandType() { return type; }
    public int getPriority() { return priority; }

    boolean isNegative() {
        return type == BACK || type == CANCEL || type == EXIT || type == STOP;
    }
}

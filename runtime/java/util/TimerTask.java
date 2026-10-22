package java.util;

public abstract class TimerTask implements Runnable {
    Timer timer;
    long next;
    long period;
    boolean fixedRate;
    boolean cancelled;
    long lastRun;

    protected TimerTask() {
    }

    public abstract void run();

    public boolean cancel() {
        boolean was = !cancelled && timer != null;
        cancelled = true;
        if (timer != null)
            timer.remove(this);
        return was;
    }

    public long scheduledExecutionTime() {
        return lastRun;
    }
}

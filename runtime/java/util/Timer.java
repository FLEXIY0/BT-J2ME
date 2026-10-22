package java.util;

public class Timer {
    private final Vector tasks = new Vector();
    private boolean cancelled;

    public Timer() {
        Thread t = new Thread() {
            public void run() {
                loop();
            }
        };
        t.start();
    }

    private void add(TimerTask task, long first, long period, boolean fixedRate) {
        if (first < 0 || period < 0)
            throw new IllegalArgumentException();
        synchronized (tasks) {
            if (cancelled || task.cancelled || task.timer != null)
                throw new IllegalStateException();
            task.timer = this;
            task.next = first;
            task.period = period;
            task.fixedRate = fixedRate;
            tasks.addElement(task);
            tasks.notify();
        }
    }

    void remove(TimerTask task) {
        synchronized (tasks) {
            tasks.removeElement(task);
            tasks.notify();
        }
    }

    public void schedule(TimerTask task, long delay) {
        if (delay < 0)
            throw new IllegalArgumentException();
        add(task, System.currentTimeMillis() + delay, 0, false);
    }

    public void schedule(TimerTask task, Date time) {
        add(task, time.getTime(), 0, false);
    }

    public void schedule(TimerTask task, long delay, long period) {
        if (delay < 0 || period <= 0)
            throw new IllegalArgumentException();
        add(task, System.currentTimeMillis() + delay, period, false);
    }

    public void schedule(TimerTask task, Date first, long period) {
        if (period <= 0)
            throw new IllegalArgumentException();
        add(task, first.getTime(), period, false);
    }

    public void scheduleAtFixedRate(TimerTask task, long delay, long period) {
        if (delay < 0 || period <= 0)
            throw new IllegalArgumentException();
        add(task, System.currentTimeMillis() + delay, period, true);
    }

    public void scheduleAtFixedRate(TimerTask task, Date first, long period) {
        if (period <= 0)
            throw new IllegalArgumentException();
        add(task, first.getTime(), period, true);
    }

    public void cancel() {
        synchronized (tasks) {
            cancelled = true;
            tasks.removeAllElements();
            tasks.notify();
        }
    }

    private void loop() {
        for (;;) {
            TimerTask due = null;
            synchronized (tasks) {
                while (!cancelled && tasks.isEmpty()) {
                    try {
                        tasks.wait();
                    } catch (InterruptedException e) {
                    }
                }
                if (cancelled)
                    return;
                TimerTask first = (TimerTask) tasks.elementAt(0);
                for (int i = 1; i < tasks.size(); i++) {
                    TimerTask t = (TimerTask) tasks.elementAt(i);
                    if (t.next < first.next)
                        first = t;
                }
                long now = System.currentTimeMillis();
                if (first.next > now) {
                    try {
                        tasks.wait(first.next - now);
                    } catch (InterruptedException e) {
                    }
                    continue;
                }
                due = first;
                due.lastRun = due.next;
                if (due.period > 0) {
                    due.next = due.fixedRate ? due.next + due.period : now + due.period;
                } else {
                    tasks.removeElement(due);
                    due.cancelled = true;
                }
            }
            try {
                due.run();
            } catch (Throwable e) {
                System.out.println("TimerTask: " + e);
            }
        }
    }
}

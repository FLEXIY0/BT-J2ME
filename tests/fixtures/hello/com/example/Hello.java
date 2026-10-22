package com.example;

/* Заглушка вместо настоящего MIDlet: проверяем только упаковку и разбор класса. */
public class Hello {
    private static final long BIG = 1234567890123L;
    private static final double PI = 3.14159;
    private int score;

    public int tick(int dt) {
        score += dt;
        return score > 100 ? (int) (BIG % 7) : (int) PI;
    }
}

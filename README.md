# BT-J2ME

Лёгкий нативный движок для запуска J2ME-игр на Linux: на весь экран,
минималистично, без Java на компьютере. Пишется на C99, чтобы работать на
старых и слабых машинах любой разрядности (i386, x86_64, ARM, MIPS).

Концепт и дорожная карта: [docs/CONCEPT.md](docs/CONCEPT.md).

> Статус: этап 0 — каркас. Чтение JAR, манифеста и class-файлов, полноэкранный
> вывод и клавиатура готовы; интерпретатор байткода — следующий этап.

## Сборка

Нужны C-компилятор, `make`, `zlib` и `SDL2` (dev-пакеты).

```sh
sudo apt install build-essential zlib1g-dev libsdl2-dev   # Debian/Ubuntu
make            # SDL2, оптимизация по размеру
make test       # смоук-тесты (для JAR-тестов нужен javac)
make strip      # уменьшить бинарник

make BACKEND=null                        # без графики
make CROSS=arm-linux-gnueabihf-          # кросс-сборка
make CFLAGS="-O2 -march=pentium2"        # под конкретный старый процессор
```

## Использование

```sh
./bt-j2me info игра.jar          # название, MIDlet-ы, проверка всех классов
./bt-j2me display-test           # проверить экран и клавиши
./bt-j2me display-test -s 176x208 --windowed
```

Управление: стрелки/WASD — джойстик, Enter/пробел — «огонь», цифры — цифры,
F1/F2 (Q/E) — софт-клавиши, Esc — выход.

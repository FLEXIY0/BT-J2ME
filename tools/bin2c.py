#!/usr/bin/env python3
"""Превращает двоичный файл в C-массив: tools/bin2c.py ИМЯ файл > заголовок.h"""
import sys

name, path = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()
out = sys.stdout
out.write("/* Сгенерировано tools/bin2c.py из %s — не редактировать вручную. */\n" % path.split("/")[-1])
out.write("static const unsigned char %s[%d] = {\n" % (name, len(data)))
# десятичные числа без пробелов — заголовок почти вдвое меньше, чем с 0x..
for i in range(0, len(data), 32):
    out.write("".join("%d," % b for b in data[i:i + 32]) + "\n")
out.write("};\n")

#!/usr/bin/env python3
"""Сверяет декодер PNG с Pillow на всех типах цвета, глубинах, tRNS и Adam7."""
import os
import random
import subprocess
import sys

from PIL import Image

dump, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
rnd = random.Random(7)


def noise(mode, w, h):
    img = Image.new("RGBA", (w, h))
    img.putdata([(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256), rnd.choice([0, 128, 255]))
                 for _ in range(w * h)])
    return img if mode == "RGBA" else img.convert(mode)


cases = []
for mode in ("RGBA", "RGB", "L", "LA", "1"):
    cases.append((mode, noise(mode, 37, 23), {}))
pal = noise("RGB", 31, 17).quantize(16)
cases.append(("P16", pal, {"bits": 4}))
cases.append(("P-trns", pal, {"transparency": 3}))
cases.append(("L-trns", noise("L", 19, 11), {"transparency": 128}))

have_convert = subprocess.call(["sh", "-c", "command -v convert >/dev/null"]) == 0
if have_convert:
    # 16-битный RGBA делаем через ImageMagick — Pillow такие не пишет
    noise("RGBA", 13, 9).save(os.path.join(out, "src16.png"))
    subprocess.check_call(["convert", os.path.join(out, "src16.png"), "-depth", "16",
                           "PNG64:" + os.path.join(out, "RGBA16.png")])
    cases.append(("RGBA16", None, {}))

fails = 0
for name, img, opts in cases:
    path = os.path.join(out, name + ".png")
    if img is not None:
        img.save(path, **opts)
    variants = [path]
    if have_convert:
        ipath = os.path.join(out, name + "-adam7.png")
        subprocess.check_call(["convert", path, "-interlace", "PNG", ipath])
        variants.append(ipath)
    for p in variants:
        ref = Image.open(p).convert("RGBA")
        res = subprocess.run([dump, p], capture_output=True)
        ok = res.returncode == 0
        if ok:
            head, _, body = res.stdout.partition(b"\n")
            w, h = map(int, head.split())
            got = [(body[i + 1], body[i + 2], body[i + 3], body[i]) for i in range(0, len(body), 4)]
            exp = [ref.getpixel((x, y)) for y in range(ref.size[1]) for x in range(ref.size[0])]
            # у полностью прозрачных пикселей цвет не важен
            ok = (w, h) == ref.size and all(g == e or g[3] == e[3] == 0 for g, e in zip(got, exp))
        print(("ok   " if ok else "FAIL ") + "png " + os.path.basename(p))
        fails += not ok
sys.exit(1 if fails else 0)

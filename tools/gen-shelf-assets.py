#!/usr/bin/env python3
# Встроенные ресурсы полки → src/shelf_assets.h:
#   шрифты Inter (SemiBold, Medium, Regular) и JetBrains Mono — только латиница, кириллица и
#   знаки препинания (fontTools/pyftsubset), сжаты zlib; картинки носителей из third_party/shelf.
#   python3 tools/gen-shelf-assets.py [папка-со-шрифтами-inter] [путь-к-jetbrains-mono.ttf]
import io, os, sys, zlib
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INTER = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/opentype/inter'
MONO = sys.argv[2] if len(sys.argv) > 2 else '/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf'
UNICODES = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + list(range(0x400, 0x460)) + \
           [0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2116, 0x2122]


def sub(path):
    f = TTFont(path)
    opt = subset.Options()
    opt.layout_features = ['kern']
    opt.hinting = False
    opt.desubroutinize = True
    opt.name_IDs = []
    s = subset.Subsetter(opt)
    s.populate(unicodes=UNICODES)
    s.subset(f)
    b = io.BytesIO()
    f.save(b)
    return b.getvalue()


def arr(name, data, out):
    out.write('static const unsigned char %s[%d] = {\n' % (name, len(data)))
    for i in range(0, len(data), 20):
        out.write('    ' + ','.join(str(x) for x in data[i:i + 20]) + ',\n')
    out.write('};\n')


fonts = [('font_inter_semibold', os.path.join(INTER, 'Inter-SemiBold.otf')),
         ('font_inter_medium', os.path.join(INTER, 'Inter-Medium.otf')),
         ('font_inter_regular', os.path.join(INTER, 'Inter-Regular.otf')),
         ('font_mono', MONO)]
with open(os.path.join(ROOT, 'src', 'shelf_assets.h'), 'w') as out:
    out.write('/* Сгенерировано tools/gen-shelf-assets.py — не править руками.\n'
              ' * Шрифты Inter и JetBrains Mono (SIL OFL 1.1, third_party/fonts), сжаты zlib;\n'
              ' * картинки носителей (third_party/shelf) — PNG как есть. */\n')
    for name, path in fonts:
        raw = sub(path)
        z = zlib.compress(raw, 9)
        out.write('#define %s_size %d\n' % (name, len(raw)))
        arr(name + '_z', z, out)
        print(name, len(raw), '->', len(z))
    for name in ('nes_cart', 'microsd'):
        data = open(os.path.join(ROOT, 'third_party', 'shelf', name + '.png'), 'rb').read()
        arr('png_' + name, data, out)
        print(name, len(data))

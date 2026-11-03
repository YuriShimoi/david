# Builds assets/fonts/david-icons.ttf and ui/ToolIcons.h: the Tabler and Phosphor icons David uses, copied out of
# their webfonts into one font at U+EA00.. (free in Font Awesome), scaled to Tabler's 1000-unit box.
# Usage: python tools/gen-tool-icons.py <tabler webfont/fonts dir> <phosphor webfont/regular dir>
# Needs fonttools and brotli (woff2).
import glob, os, re, sys
from fontTools.ttLib import TTFont
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0xEA00

# (macro suffix, source, glyph name); source 'svg' is a file in the repo with one path in a 1000-unit box
ICONS = [
  ('TB_POINTER', 'tb', 'pointer'),
  ('TB_MARQUEE_2', 'tb', 'marquee-2'),
  ('TB_CIRCLE_DASHED', 'tb', 'circle-dashed'),
  ('TB_LASSO', 'tb', 'lasso'),
  ('TB_LASSO_POLYGON', 'tb', 'lasso-polygon'),
  ('TB_ARROWS_MOVE', 'tb', 'arrows-move'),
  ('TB_DRAG_DROP', 'tb', 'drag-drop'),
  ('TB_ZOOM', 'tb', 'zoom'),
  ('TB_HAND_STOP', 'tb', 'hand-stop'),
  ('TB_WAND', 'tb', 'wand'),
  ('TB_BUCKET_DROPLET', 'tb', 'bucket-droplet'),
  ('PH_GRADIENT', 'ph', 'gradient'),
  ('PH_EYEDROPPER', 'ph', 'eyedropper'),
  ('TB_BRUSH', 'tb', 'brush'),
  ('TB_ERASER', 'tb', 'eraser'),
  ('TB_PENCIL', 'tb', 'pencil'),
  ('TB_TYPOGRAPHY', 'tb', 'typography'),
  ('TB_LINE', 'tb', 'line'),
  ('TB_VECTOR_BEZIER_2', 'tb', 'vector-bezier-2'),
  ('TB_TRIANGLE_SQUARE_CIRCLE', 'tb', 'triangle-square-circle'),
  ('TB_SPRAY', 'tb', 'spray'),
  ('TB_SCRIBBLE', 'tb', 'scribble'),
  # Tool options bar (picked on design-samples/david-option-icons)
  ('TB_WAVE_SINE', 'tb', 'wave-sine'),
  ('PH_CHECKERBOARD', 'ph', 'checkerboard'),
  ('TB_FLIP_HORIZONTAL', 'tb', 'flip-horizontal'),
  ('TB_GRID_4X4', 'tb', 'grid-4x4'),
  ('TB_GRID_3X3', 'tb', 'grid-3x3'),
  ('PH_MAGNET', 'ph', 'magnet'),
  ('PH_POLYGON', 'ph', 'polygon'),
  ('TB_WORLD', 'tb', 'world'),
  ('TB_ADJUSTMENTS_HORIZONTAL', 'tb', 'adjustments-horizontal'),
  ('TB_MATH_FUNCTION', 'tb', 'math-function'),
  ('PH_FEATHER', 'ph', 'feather'),
  ('TB_BORDER_OUTER', 'tb', 'border-outer'),
  ('TB_SHAPE_2', 'tb', 'shape-2'),
  ('TB_STACK_2', 'tb', 'stack-2'),
  ('TB_CIRCLE', 'tb', 'circle'),
  ('PH_PEN_NIB', 'ph', 'pen-nib'),
  ('TB_DROPLET', 'tb', 'droplet'),
  ('TB_STAIRS_UP', 'tb', 'stairs-up'),
  ('TB_GRAIN', 'tb', 'grain'),
  ('TB_ARROW_BACK_UP', 'tb', 'arrow-back-up'),
  ('PH_SQUARE', 'ph', 'square'),
  ('PH_UNITE_SQUARE', 'ph', 'unite-square'),
  ('PH_SUBTRACT_SQUARE', 'ph', 'subtract-square'),
  ('PH_INTERSECT_SQUARE', 'ph', 'intersect-square'),
  ('PH_EXCLUDE_SQUARE', 'ph', 'exclude-square'),
  ('TB_ROTATE_CLOCKWISE', 'tb', 'rotate-clockwise'),
  ('TB_ROTATE_2', 'tb', 'rotate-2'),
  ('TB_ROTATE_CLOCKWISE_2', 'tb', 'rotate-clockwise-2'),
  ('TB_ARROWS_DIAGONAL', 'tb', 'arrows-diagonal'),
  ('TB_GRIP_HORIZONTAL', 'tb', 'grip-horizontal'),
  ('TB_DROPLET_OFF', 'tb', 'droplet-off'),
  ('TB_GRID_DOTS', 'tb', 'grid-dots'),
  ('TB_ANGLE', 'tb', 'angle'),
  ('TB_ARROW_NARROW_LEFT', 'tb', 'arrow-narrow-left'),
  ('TB_ARROW_NARROW_RIGHT', 'tb', 'arrow-narrow-right'),
  ('TB_SQUARE', 'tb', 'square'),
  ('TB_SQUARE_ROUNDED', 'tb', 'square-rounded'),
  ('TB_PENTAGON', 'tb', 'pentagon'),
  ('TB_TEXT_SIZE', 'tb', 'text-size'),
  ('TB_BOLD', 'tb', 'bold'),
  ('TB_ITALIC', 'tb', 'italic'),
  ('TB_ALIGN_LEFT', 'tb', 'align-left'),
  ('TB_ALIGN_CENTER', 'tb', 'align-center'),
  ('TB_ALIGN_RIGHT', 'tb', 'align-right'),
  ('TB_STAR', 'tb', 'star'),
  ('TB_STAR_HALF', 'tb', 'star-half'),
  # Animation (Timeline and Animation Preview)
  ('TB_PLAYER_PLAY', 'tb', 'player-play'),
  ('TB_PLAYER_PAUSE', 'tb', 'player-pause'),
  ('TB_PLAYER_SKIP_BACK', 'tb', 'player-skip-back'),
  ('TB_PLAYER_SKIP_FORWARD', 'tb', 'player-skip-forward'),
  ('TB_PLAYER_TRACK_PREV', 'tb', 'player-track-prev'),
  ('TB_PLAYER_TRACK_NEXT', 'tb', 'player-track-next'),
  ('TB_MOVIE', 'tb', 'movie'),
  ('TB_GHOST', 'tb', 'ghost'),
  ('TB_GHOST_2', 'tb', 'ghost-2'),
  ('TB_KEYFRAMES', 'tb', 'keyframes'),
  ('TB_LINK', 'tb', 'link'),
  ('TB_UNLINK', 'tb', 'unlink'),
  ('TB_REPEAT', 'tb', 'repeat'),
  ('TB_REPEAT_ONCE', 'tb', 'repeat-once'),
  ('TB_ARROWS_RIGHT_LEFT', 'tb', 'arrows-right-left'),
  ('TB_TAG', 'tb', 'tag'),
  ('TB_CLOCK', 'tb', 'clock'),
  ('TB_SQUARE_PLUS', 'tb', 'square-plus'),
  ('TB_COPY', 'tb', 'copy'),
  ('TB_TRASH', 'tb', 'trash'),
  ('TB_SWITCH_HORIZONTAL', 'tb', 'switch-horizontal'),
  ('TB_TABLE_IMPORT', 'tb', 'table-import'),
  ('TB_TABLE_EXPORT', 'tb', 'table-export'),
  ('TB_REPLACE', 'tb', 'replace'),
  # Vector tool row (1.2.1)
  ('TB_VECTOR', 'tb', 'vector'),
  ('TB_FLIP_VERTICAL', 'tb', 'flip-vertical'),
  ('TB_VECTOR_SPLINE', 'tb', 'vector-spline'),
  ('TB_LAYERS_INTERSECT', 'tb', 'layers-intersect'),
  ('DAVID_LOGO', 'svg', 'assets/icons/david-mono.svg'),
]

tabler = [TTFont(p) for p in sorted(glob.glob(os.path.join(sys.argv[1], 'tabler-icons.*.woff2')))]
phosphor = [TTFont(os.path.join(sys.argv[2], 'Phosphor.woff2'))]

# Icon name -> codepoint, from each set's CSS (glyph names inside the fonts can be older aliases)
def codes(css, prefix):
  text = open(css, encoding='utf-8').read()
  return { m[0]: int(m[1], 16) for m in re.findall(r'\.' + prefix + r'-([a-z0-9-]+):+before\s*\{\s*content:\s*"\\([0-9a-f]+)"', text) }
tbCodes = codes(os.path.join(sys.argv[1], '..', 'tabler-icons.css'), 'ti')
phCodes = codes(os.path.join(sys.argv[2], 'style.css'), r'ph\.ph')

# The font holding an icon, and its glyph name there
def find(fonts, table, name):
  cp = table.get(name)
  for f in fonts:
    if cp in f.getBestCmap(): return f, f.getBestCmap()[cp]
  raise SystemExit('missing glyph: ' + name)

order, glyphs, cmap, metrics, defines = ['.notdef'], {}, {}, {'.notdef': (1000, 0)}, []
glyphs['.notdef'] = TTGlyphPen(None).glyph()
for i, (macro, src, name) in enumerate(ICONS):
  pen = TTGlyphPen(None)
  if src == 'svg':
    # SVG units (y down, 0..1000) onto [0, 1000] x [-100, 900]; only the path, not the transparent backdrop
    d = re.search(r'<path d="([^"]+)"', open(os.path.join(ROOT, name), encoding='utf-8').read()).group(1)
    parse_path(d, TransformPen(pen, (1, 0, 0, -1, 0, 900)))
  else:
    f, glyph = find(tabler, tbCodes, name) if src == 'tb' else find(phosphor, phCodes, name)
    upm, desc = f['head'].unitsPerEm, f['hhea'].descent
    # Source em box [0, upm] x [desc, desc + upm] onto [0, 1000] x [-100, 900]
    s = 1000 / upm
    f.getGlyphSet()[glyph].draw(TransformPen(pen, (s, 0, 0, s, 0, -100 - desc * s)))
  gname = 'i%02d' % i
  order.append(gname)
  glyphs[gname] = pen.glyph()
  cmap[BASE + i] = gname
  metrics[gname] = (1000, 0)
  defines.append('#define ICON_%s "%s"' % (macro, ''.join(r'\x%02x' % b for b in chr(BASE + i).encode('utf-8'))))

fb = FontBuilder(1000, isTTF=True)
fb.setupGlyphOrder(order)
fb.setupCharacterMap(cmap)
fb.setupGlyf(glyphs)
for g in glyphs.values(): g.recalcBounds(fb.font['glyf'])
fb.setupHorizontalMetrics({n: (w, glyphs[n].xMin if hasattr(glyphs[n], 'xMin') else 0) for n, (w, _) in metrics.items()})
fb.setupHorizontalHeader(ascent=900, descent=-100)
fb.setupNameTable({'familyName': 'David Icons', 'styleName': 'Regular'})
fb.setupOS2(sTypoAscender=900, sTypoDescender=-100, usWinAscent=900, usWinDescent=100)
fb.setupPost()
fb.save(os.path.join(ROOT, 'assets', 'fonts', 'david-icons.ttf'))

head = ['// Generated by tools/gen-tool-icons.py from Tabler Icons and Phosphor (MIT). Do not edit.',
        '// Glyphs live in assets/fonts/david-icons.ttf at U+EA00..', '#pragma once', '',
        '#define ICON_MIN_DAVID 0x%X' % BASE, '#define ICON_MAX_DAVID 0x%X' % (BASE + len(ICONS) - 1), ''] + defines + ['']
open(os.path.join(ROOT, 'ui', 'ToolIcons.h'), 'w', encoding='utf-8', newline='\n').write('\n'.join(head))
print(len(ICONS), 'icons')

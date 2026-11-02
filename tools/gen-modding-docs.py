# Builds docs/modding/*.html, the modding guide: the pages from docs/modding/src/*.md, the API reference from the
# //@api blocks in scripting/LuaApi.cpp and sdk/david_plugin.h, the built-in effects from david-cli, and a search
# index. Everything opens from file:// (no fetch). Run it after changing the API.
# Translations: docs/modding/src/<code>/*.md (and api.json for the reference text) become docs/modding/<code>/; effect
# names come from lang/<code>.json. A page with no translation falls back to the English source.
# Usage: python tools/gen-modding-docs.py [path to david-cli] [folder with screenshots p7-*.png]
import html, json, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'docs', 'modding')
SRC = os.path.join(OUT, 'src')

PAGES = [
  ('index', 'Getting started'),
  ('tutorial-shader', 'Shader effect'),
  ('tutorial-lua-command', 'Lua command'),
  ('tutorial-lua-tool', 'Lua tool'),
  ('tutorial-lua-window', 'Lua windows'),
  ('tutorial-native', 'Native plugin'),
  ('concepts', 'Concepts'),
  ('cookbook', 'Cookbook'),
  ('reference', 'API reference'),
  ('effects', 'Built-in effects'),
  ('formats', 'Patterns, themes, files'),
  ('versioning', 'Versioning'),
]

# Languages: the folder under docs/modding ('' = English, at the top), page titles and the guide's own text
LANGS = {
  'en': { 'dir': '', 'name': 'English', 'titles': {}, 'ui': {} },
  'pt-BR': {
    'dir': 'pt-BR', 'name': 'Português (Brasil)',
    'titles': {
      'index': 'Primeiros passos', 'tutorial-shader': 'Efeito de shader', 'tutorial-lua-command': 'Comando Lua',
      'tutorial-lua-tool': 'Ferramenta Lua', 'tutorial-lua-window': 'Janelas Lua', 'tutorial-native': 'Plugin nativo',
      'concepts': 'Conceitos', 'cookbook': 'Receitas', 'reference': 'Referência da API', 'effects': 'Efeitos embutidos',
      'formats': 'Padrões, temas, arquivos', 'versioning': 'Versionamento',
    },
    'ui': {
      'David modding': 'Mods do David', 'modding': 'mods', 'Search the guide': 'Buscar no guia', 'Start': 'Início',
      'Reference': 'Referência', 'On this page': 'Nesta página', 'Nothing found': 'Nada encontrado', 'Page': 'Página',
      'API reference': 'Referência da API',
      'Generated from the annotations in <code>scripting/LuaApi.cpp</code> and <code>sdk/david_plugin.h</code>, so it always matches the code. Colors are integers (use <code>david.rgba</code> and <code>david.channels</code>); layer indices start at 1.':
        'Gerada a partir das anotações em <code>scripting/LuaApi.cpp</code> e <code>sdk/david_plugin.h</code>, então sempre bate com o código. Cores são inteiros (use <code>david.rgba</code> e <code>david.channels</code>); os índices de camada começam em 1.',
      'C ABI (native plugins)': 'ABI C (plugins nativos)',
      'From <code>sdk/david_plugin.h</code>. Pixels are straight RGBA8, red in the low byte, rows top first.':
        'De <code>sdk/david_plugin.h</code>. Os pixels são RGBA8 sem pré-multiplicação, vermelho no byte baixo, linhas de cima para baixo.',
      'Built-in effects': 'Efeitos embutidos',
      'Every effect and adjustment, with the ids the CLI, Lua (<code>doc:applyEffect</code>) and the dialogs share. Colors are written <code>#rrggbb</code> or <code>#rgb</code>; a choice takes its name or index; a curve is <code>x:y/x:y</code> per channel, channels split by <code>|</code>.':
        'Todos os efeitos e ajustes, com os ids que a CLI, o Lua (<code>doc:applyEffect</code>) e as janelas compartilham. Cores são escritas <code>#rrggbb</code> ou <code>#rgb</code>; uma escolha aceita o nome ou o índice; uma curva é <code>x:y/x:y</code> por canal, com os canais separados por <code>|</code>.',
      '(Run the generator with the path to david-cli to fill this page.)': '(Rode o gerador com o caminho do david-cli para preencher esta página.)',
      'Param': 'Parâmetro', 'Label': 'Rótulo', 'Type': 'Tipo', 'Range': 'Faixa', 'Default': 'Padrão', 'No parameters.': 'Sem parâmetros.',
    },
  },
}
L = LANGS['en']      # the language being built
UI_TR = {}           # lang/<code>.json of that language: effect names, labels, choices
API_TR = {}          # src/<code>/api.json: the reference descriptions

def T(text):
  return L['ui'].get(text, text)

def U(text):
  return UI_TR.get(text) or text

#region [ Markdown ]
def inline(t):
  t = html.escape(t, quote=False)
  t = re.sub(r'`([^`]+)`', lambda m: '<code>' + m.group(1) + '</code>', t)
  t = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', t)
  up = '../' if L['dir'] else ''
  t = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)', lambda m: '<figure><img src="%s%s" alt="%s"><figcaption>%s</figcaption></figure>' % (up if m.group(2).startswith('img/') else '', m.group(2), m.group(1), m.group(1)), t)
  t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', t)
  return t

def slug(t):
  return re.sub(r'[^a-z0-9]+', '-', t.lower()).strip('-')

# A small Markdown subset: headings, paragraphs, fenced code, lists, tables, images
def markdown(text, toc):
  out, lines, i = [], text.split('\n'), 0
  while i < len(lines):
    l = lines[i]
    if l.startswith('```'):
      lang = l[3:].strip()
      code = []
      i += 1
      while i < len(lines) and not lines[i].startswith('```'):
        code.append(lines[i])
        i += 1
      out.append('<pre class="lang-%s"><code>%s</code></pre>' % (lang or 'text', html.escape('\n'.join(code), quote=False)))
      i += 1
      continue
    m = re.match(r'(#{1,3}) (.*)', l)
    if m:
      level, title = len(m.group(1)), m.group(2)
      sid = slug(title)
      if level > 1: toc.append((level, title, sid))
      out.append('<h%d id="%s">%s</h%d>' % (level, sid, inline(title), level))
      i += 1
      continue
    if l.startswith('|'):
      rows = []
      while i < len(lines) and lines[i].startswith('|'):
        cells = [c.strip() for c in re.split(r'(?<!\\)\|', lines[i].strip()[1:-1])]
        if not all(re.fullmatch(r':?-+:?', c) for c in cells): rows.append([c.replace('\\|', '|') for c in cells])
        i += 1
      t = '<table><thead><tr>' + ''.join('<th>%s</th>' % inline(c) for c in rows[0]) + '</tr></thead><tbody>'
      t += ''.join('<tr>' + ''.join('<td>%s</td>' % inline(c) for c in r) + '</tr>' for r in rows[1:]) + '</tbody></table>'
      out.append(t)
      continue
    if re.match(r'\s*- ', l):
      items = []
      while i < len(lines) and re.match(r'\s*- ', lines[i]):
        items.append(re.sub(r'^\s*- ', '', lines[i]))
        i += 1
      out.append('<ul>' + ''.join('<li>%s</li>' % inline(x) for x in items) + '</ul>')
      continue
    if not l.strip():
      i += 1
      continue
    para = []
    while i < len(lines) and lines[i].strip() and not re.match(r'(#|```|\||\s*- )', lines[i]):
      para.append(lines[i])
      i += 1
    p = inline(' '.join(para))
    out.append(p if p.startswith('<figure>') else '<p>' + p + '</p>')
  return '\n'.join(out)
#endregion

#region [ API reference ]
def api_blocks(path, lang):
  blocks, cur = [], None
  for line in open(path, encoding='utf-8'):
    s = line.strip()
    m = re.match(r'//@api (\w+) (.+)', s)
    if m:
      cur = { 'lang': m.group(1), 'name': m.group(2).strip(), 'sig': '', 'doc': '', 'ex': '' }
      blocks.append(cur)
      continue
    m = re.match(r'//@(sig|doc|ex) (.*)', s)
    if m and cur: cur[m.group(1)] += (' ' if cur[m.group(1)] else '') + m.group(2)
    elif not s.startswith('//@'): cur = None
  return [b for b in blocks if b['lang'] == lang]

def reference(toc):
  lua = api_blocks(os.path.join(ROOT, 'scripting', 'LuaApi.cpp'), 'lua')
  c = api_blocks(os.path.join(ROOT, 'sdk', 'david_plugin.h'), 'c')
  groups = {}
  for b in lua: groups.setdefault(re.split(r'[.:]', b['name'])[0], []).append(b)
  order = ['david', 'Image', 'Document', 'Layer', 'Ui']
  parts = ['<h1>%s</h1>' % T('API reference'),
           '<p>%s</p>' % T('Generated from the annotations in <code>scripting/LuaApi.cpp</code> and <code>sdk/david_plugin.h</code>, so it always matches the code. Colors are integers (use <code>david.rgba</code> and <code>david.channels</code>); layer indices start at 1.')]
  def entry(b):
    sid = slug(b['lang'] + '-' + b['name'])
    toc.append((3, b['name'], sid))
    h = '<section class="api" id="%s"><h3><code>%s</code></h3>' % (sid, html.escape(b['name']))
    if b['sig']: h += '<pre class="sig"><code>%s</code></pre>' % html.escape(b['sig'])
    if b['doc']: h += '<p>%s</p>' % inline(API_TR.get(b['doc']) or b['doc'])
    if b['ex']: h += '<pre class="lang-%s"><code>%s</code></pre>' % (b['lang'], html.escape(b['ex']))
    return h + '</section>'
  for g in order + sorted(k for k in groups if k not in order):
    if g not in groups: continue
    toc.append((2, 'Lua: ' + g, slug('lua-' + g)))
    parts.append('<h2 id="%s">Lua: %s</h2>' % (slug('lua-' + g), g))
    parts += [entry(b) for b in groups[g]]
  toc.append((2, T('C ABI (native plugins)'), 'c-abi'))
  parts.append('<h2 id="c-abi">%s</h2><p>%s</p>' % (T('C ABI (native plugins)'), T('From <code>sdk/david_plugin.h</code>. Pixels are straight RGBA8, red in the low byte, rows top first.')))
  parts += [entry(b) for b in c]
  return '\n'.join(parts), len(lua), len(c)
#endregion

#region [ Built-in effects ]
def effects_page(cli, toc):
  parts = ['<h1>%s</h1>' % T('Built-in effects'), '<p>%s</p>' % T('Every effect and adjustment, with the ids the CLI, Lua (<code>doc:applyEffect</code>) and the dialogs share. Colors are written <code>#rrggbb</code> or <code>#rgb</code>; a choice takes its name or index; a curve is <code>x:y/x:y</code> per channel, channels split by <code>|</code>.'),
           '<pre><code>david-cli in.png --effect gaussian-blur:radius=3 --effect outline:color=#000 -o out.png</code></pre>']
  data = []
  if cli:
    try:
      r = subprocess.run([cli, '--list-effects', '--json'], capture_output=True, timeout=60)
      data = [e for e in json.loads(r.stdout) if 'mod' not in e]
    except Exception as ex:
      print('effects: ' + str(ex))
  if not data:
    parts.append('<p>%s</p>' % T('(Run the generator with the path to david-cli to fill this page.)'))
    return '\n'.join(parts)
  for e in sorted(data, key=lambda e: (e['category'], e['name'])):
    sid = slug('effect-' + e['id'])
    toc.append((3, U(e['name']), sid))
    rows = ''
    for p in e['params']:
      rng = ('%g – %g' % (p['min'], p['max'])) if 'min' in p else (' | '.join(U(c) for c in p.get('choices', [])) or '')
      rows += '<tr><td><code>%s</code></td><td>%s</td><td>%s</td><td>%s</td><td><code>%s</code></td></tr>' % (
        p['id'], html.escape(U(p['label'])), p['type'], html.escape(rng), html.escape(str(p.get('default', ''))))
    head = ''.join('<th>%s</th>' % T(h) for h in ('Param', 'Label', 'Type', 'Range', 'Default'))
    table = '<table><thead><tr>%s</tr></thead><tbody>%s</tbody></table>' % (head, rows) if rows else '<p class="muted">%s</p>' % T('No parameters.')
    category = ' › '.join(U(c) for c in e['category'].split('/'))
    parts.append('<section class="api" id="%s"><h3>%s <code>%s</code></h3><p class="muted">%s</p>%s</section>' % (sid, html.escape(U(e['name'])), e['id'], html.escape(category), table))
  return '\n'.join(parts)
#endregion

CSS = '''
:root{--bg:#1A1A1A;--surface:#212121;--raised:#2A2A2A;--line:#44475A;--hover:#525568;--text:#FFFFFF;--muted:#A3A3A3;--accent:#8BE9FD;--good:#50FA7B;--warn:#FFB86C;--pink:#FF79C6;--purple:#BD93F9;--yellow:#F1FA8C}
*{box-sizing:border-box}html,body{margin:0;height:100%}
body{background:var(--bg);color:var(--text);font:15px/1.6 "Segoe UI",system-ui,sans-serif;display:flex}
nav{width:260px;flex:none;background:var(--surface);height:100vh;position:sticky;top:0;overflow:auto;padding:20px 14px}
nav .brand{font-weight:700;font-size:17px;margin:0 6px 14px;display:flex;gap:8px;align-items:center}
nav .brand span{color:var(--accent)} nav .brand a.lang{margin-left:auto;font-size:12px;font-weight:400;padding:2px 8px;color:var(--muted)}
nav input{width:100%;background:var(--raised);border:0;border-radius:6px;color:var(--text);padding:8px 10px;font:inherit;margin-bottom:14px}
nav input:focus{outline:2px solid var(--accent)}
nav .head{font-size:12px;font-weight:700;letter-spacing:.07em;text-transform:uppercase;color:var(--muted);margin:16px 6px 6px}
nav a{display:block;color:var(--text);text-decoration:none;padding:5px 8px;border-radius:6px}
nav a:hover{background:var(--line)} nav a.on{color:var(--accent);background:var(--raised)}
nav .toc a{font-size:13px;color:var(--muted);padding:2px 8px 2px 18px} nav .toc a.l3{padding-left:30px}
#results a{font-size:14px} #results .where{display:block;color:var(--muted);font-size:12px}
main{flex:1;min-width:0;padding:32px 48px 80px}
article{max-width:820px}
h1{font-size:28px;margin:0 0 16px} h2{font-size:20px;margin:40px 0 12px;padding-bottom:6px;border-bottom:1px solid var(--line)} h3{font-size:16px;margin:24px 0 8px}
a{color:var(--accent)} p,ul,table,pre,figure{margin:0 0 16px}
code{font-family:Consolas,"Cascadia Mono",monospace;font-size:13px;background:var(--raised);border-radius:4px;padding:1px 5px}
pre{background:var(--surface);border-radius:8px;padding:14px 16px;overflow:auto} pre code{background:none;padding:0;font-size:13px;line-height:1.5}
pre.sig{border-left:3px solid var(--accent)}
table{border-collapse:collapse;width:100%} th{text-align:left;font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);font-weight:700}
th,td{padding:7px 10px;border-bottom:1px solid var(--line);vertical-align:top}
figure{background:var(--surface);border-radius:8px;padding:10px} figure img{max-width:100%;border-radius:4px;display:block} figcaption{color:var(--muted);font-size:13px;margin-top:6px}
section.api{background:var(--surface);border-radius:8px;padding:4px 16px 2px;margin:0 0 12px} section.api h3{margin-top:12px}
.muted{color:var(--muted)} .kw{color:var(--pink)} .str{color:var(--yellow)} .com{color:#6272A4} .num{color:var(--purple)}
@media(max-width:800px){body{display:block}nav{width:auto;height:auto;position:static}main{padding:20px}}
'''

# Search over the inline index (works from file://), and light syntax colors for code blocks
JS = r'''
(function(){
  var box = document.getElementById('search'), res = document.getElementById('results'), toc = document.getElementById('toc');
  box.addEventListener('input', function(){
    var q = box.value.trim().toLowerCase();
    if(!q){ res.innerHTML = ''; toc.style.display = ''; return; }
    toc.style.display = 'none';
    var words = q.split(/\s+/), hits = [];
    DAVID_INDEX.forEach(function(e){
      var t = (e.title + ' ' + e.text).toLowerCase(), score = 0;
      for(var i = 0; i < words.length; i++){ if(t.indexOf(words[i]) < 0) return; score += e.title.toLowerCase().indexOf(words[i]) >= 0 ? 3 : 1; }
      hits.push([score, e]);
    });
    hits.sort(function(a, b){ return b[0] - a[0]; });
    res.innerHTML = hits.slice(0, 30).map(function(h){
      var e = h[1];
      return '<a href="' + e.url + '">' + e.title.replace(/</g, '&lt;') + '<span class="where">' + e.page + '</span></a>';
    }).join('') || '<div class="muted" style="padding:6px 8px">Nothing found</div>';
  });
  var kw = /\b(local|function|end|if|then|else|elseif|for|do|while|return|nil|true|false|and|or|not|in|vec2|vec3|vec4|float|int|bool|uniform|static|const|struct|typedef|void|uint32_t|uint8_t|size_t|return|include|define)\b/g;
  document.querySelectorAll('pre:not(.lang-json):not(.lang-text) code').forEach(function(c){
    var h = c.innerHTML;
    h = h.replace(/(--[^\n]*|\/\/[^\n]*|\/\*[\s\S]*?\*\/)/g, '\u0001$1\u0002');
    h = h.split(/(\u0001[^\u0002]*\u0002)/).map(function(s){
      if(s.charAt(0) === '\u0001') return '<span class="com">' + s.slice(1, -1) + '</span>';
      return s.replace(/(&quot;[^&]*&quot;|"[^"\n]*")/g, '<span class="str">$1</span>').replace(kw, '<span class="kw">$1</span>');
    }).join('');
    c.innerHTML = h;
  });
})();
'''

def page(name, title, body, toc, code):
  titles = L['titles']
  nav = ''.join('<a href="%s.html"%s>%s</a>' % (n, ' class="on"' if n == name else '', html.escape(titles.get(n, t))) for n, t in PAGES[:5])
  nav2 = ''.join('<a href="%s.html"%s>%s</a>' % (n, ' class="on"' if n == name else '', html.escape(titles.get(n, t))) for n, t in PAGES[5:])
  t = ''.join('<a class="l%d" href="#%s">%s</a>' % (lvl, sid, html.escape(tt)) for lvl, tt, sid in toc if lvl <= 3)
  # The same page in the other languages
  up = '../' if L['dir'] else ''
  langs = ''.join('<a class="lang" href="%s%s%s.html">%s</a>' % (up, (o['dir'] + '/') if o['dir'] else '', name, c.split('-')[0].upper())
                  for c, o in LANGS.items() if o is not L)
  return '''<!doctype html><html lang="%s"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s · %s</title><style>%s</style></head><body>
<nav><div class="brand">David <span>%s</span>%s</div><input id="search" placeholder="%s" autocomplete="off"><div id="results"></div>
<div class="head">%s</div>%s<div class="head">%s</div>%s<div id="toc"><div class="head">%s</div><div class="toc">%s</div></div></nav>
<main><article>%s</article></main><script src="search-index.js"></script><script>%s</script></body></html>''' % (
    code, html.escape(title), T('David modding'), CSS, T('modding'), langs, T('Search the guide'), T('Start'), nav, T('Reference'), nav2,
    T('On this page'), t, body, JS.replace('Nothing found', T('Nothing found')))

def text_of(h):
  return re.sub(r'\s+', ' ', html.unescape(re.sub(r'<[^>]+>', ' ', h))).strip()

def main():
  cli = sys.argv[1] if len(sys.argv) > 1 else ''
  shots = sys.argv[2] if len(sys.argv) > 2 else ''
  os.makedirs(os.path.join(OUT, 'img'), exist_ok=True)
  if shots:
    for src, dst in [('p7-mods.png', 'mods.png'), ('p7-scanlines.png', 'scanlines.png'), ('p7-checker.png', 'checker.png')]:
      p = os.path.join(shots, src)
      if os.path.exists(p): shutil.copy(p, os.path.join(OUT, 'img', dst))
  global L, UI_TR, API_TR
  for code, L in LANGS.items():
    src = os.path.join(SRC, L['dir']) if L['dir'] else SRC
    out = os.path.join(OUT, L['dir']) if L['dir'] else OUT
    os.makedirs(out, exist_ok=True)
    lang_file = os.path.join(ROOT, 'lang', code + '.json')
    UI_TR = json.load(open(lang_file, encoding='utf-8')) if L['dir'] and os.path.exists(lang_file) else {}
    api_file = os.path.join(src, 'api.json')
    API_TR = json.load(open(api_file, encoding='utf-8')) if L['dir'] and os.path.exists(api_file) else {}
    index = []
    counts = None
    for name, title in PAGES:
      title = L['titles'].get(name, title)
      toc = []
      md = os.path.join(src, name + '.md')
      if not os.path.exists(md): md = os.path.join(SRC, name + '.md')
      if name == 'reference': body, nl, nc = reference(toc); counts = (nl, nc)
      elif name == 'effects': body = effects_page(cli, toc)
      else: body = markdown(open(md, encoding='utf-8').read(), toc)
      # Search entries: the page, then each section with the text up to the next heading
      for m in re.finditer(r'<(h[123]) id="([^"]+)">(.*?)</\1>(.*?)(?=<h[123] |\Z)', body, re.S):
        index.append({ 'title': text_of(m.group(3)), 'page': title, 'url': '%s.html#%s' % (name, m.group(2)), 'text': text_of(m.group(4))[:400] })
      index.append({ 'title': title, 'page': T('Page'), 'url': name + '.html', 'text': text_of(body)[:300] })
      open(os.path.join(out, name + '.html'), 'w', encoding='utf-8', newline='\n').write(page(name, title, body, toc, code))
    open(os.path.join(out, 'search-index.js'), 'w', encoding='utf-8', newline='\n').write('var DAVID_INDEX = ' + json.dumps(index, ensure_ascii=False) + ';\n')
    print('%s: %d pages, %d Lua and %d C reference entries, %d search entries' % (code, len(PAGES), counts[0], counts[1], len(index)))

main()

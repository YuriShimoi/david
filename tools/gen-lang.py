# Builds lang/en.json, the built-in English translation, from every tr("...") and N_("...") string in the code.
# A translation copies it to lang/<code>.json, sets "_name" and translates the values; keys stay English.
# Usage: python tools/gen-lang.py   (from the repo root; keeps the values of keys that already exist)
import json, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STR = r'"((?:[^"\\]|\\.)*)"'
PATTERN = re.compile(r'\b(?:tr|N_)\(' + STR + r'\)')
# tr(cond ? "A" : "B")
TERNARY = re.compile(r'\btr\([^;"]*\?\s*' + STR + r'\s*:\s*' + STR + r'\)')
# Built-in effects (core/effects): the name, each part of the category, param labels and choice names
EFFECT_NAME = re.compile(r'\be\.name = ' + STR + r'|\bmake\("[^"]*", ' + STR)
CATEGORY = re.compile(r'\be\.category = ' + STR)
PARAM = re.compile(r'\{\s*"[^"]*",\s*' + STR + r',\s*EParam::(\w+)([^\n]*)')

found = set()
def add(s):
  found.add(bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8'))

for folder in ('app', 'core', 'ui', 'platform', 'render'):
  for base, _, files in os.walk(os.path.join(ROOT, folder)):
    for f in files:
      if f.endswith(('.cpp', '.h')):
        text = open(os.path.join(base, f), encoding='utf-8').read()
        for m in PATTERN.finditer(text): add(m.group(1))
        for m in TERNARY.finditer(text): add(m.group(1)); add(m.group(2))
        if os.path.basename(base) != 'effects': continue
        for m in EFFECT_NAME.finditer(text): add(m.group(1) or m.group(2))
        for m in CATEGORY.finditer(text):
          for part in m.group(1).split('/'): add(part)
        for m in PARAM.finditer(text):
          add(m.group(1))
          if m.group(2) == 'Choice':
            for c in re.findall(STR, m.group(3)): add(c)

path = os.path.join(ROOT, 'lang', 'en.json')
old = json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}
out = { '_name': 'English' }
for key in sorted(found, key=str.lower):
  out[key] = old.get(key, key)
os.makedirs(os.path.dirname(path), exist_ok=True)
json.dump(out, open(path, 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent=2)
print(len(found), 'strings ->', path)

# The other languages follow en.json's keys and order: translated values stay, new keys come in English
for f in sorted(os.listdir(os.path.dirname(path))):
  if not f.endswith('.json') or f == 'en.json': continue
  lp = os.path.join(os.path.dirname(path), f)
  lang = json.load(open(lp, encoding='utf-8'))
  synced = { '_name': lang.get('_name', f[:-5]) }
  for key in out:
    if key != '_name': synced[key] = lang.get(key, out[key])
  json.dump(synced, open(lp, 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent=2)
  print(f, sum(1 for k in out if k != '_name' and k not in lang), 'new keys')

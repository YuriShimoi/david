# Packs a release into dist/: the Windows portable zip and installer, and the Linux AppImage.
# Build first (build.cmd win-release; cmake --build --preset linux-release in WSL), then:
#   python tools/package.py win      zip + installer (makensis through WSL, or on PATH)
#   python tools/package.py linux    tar.gz, plus the AppImage when build/appimagetool-x86_64.AppImage is there
#                                    (run inside WSL/Linux; get the tool from github.com/AppImage/appimagetool)
import os, re, shutil, subprocess, sys, tarfile, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, 'dist')
VERSION = re.search(r'project\(david VERSION ([0-9.]+)', open(os.path.join(ROOT, 'CMakeLists.txt'), encoding='utf-8').read()).group(1)
FOLDERS = ['assets', 'themes', 'lang', 'patterns', 'mods', 'docs']
# Build leftovers that don't ship
SKIP = re.compile(r'\.(exp|lib|pdb|ilk|a)$')

def stage(build, out, exes):
  shutil.rmtree(out, ignore_errors=True)
  os.makedirs(out)
  for e in exes: shutil.copy2(os.path.join(build, e), out)
  for f in FOLDERS:
    shutil.copytree(os.path.join(build, f), os.path.join(out, f), ignore=lambda d, names: [n for n in names if SKIP.search(n) or n == 'src'])
  shutil.copy2(os.path.join(ROOT, 'LICENSE'), os.path.join(out, 'LICENSE.txt'))
  shutil.copy2(os.path.join(ROOT, 'README.md'), out)
  shutil.copy2(os.path.join(ROOT, 'README.pt-BR.md'), out)
  shutil.copy2(os.path.join(ROOT, 'CHANGELOG.md'), out)

def zipdir(src, dst, extra=None):
  with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    base = os.path.basename(src)
    for d, _, files in os.walk(src):
      for f in files:
        p = os.path.join(d, f)
        z.write(p, os.path.join(base, os.path.relpath(p, src)))
    for name, text in (extra or {}).items(): z.writestr(os.path.join(base, name), text)

def wslpath(p):
  drive, rest = os.path.splitdrive(os.path.abspath(p))
  return '/mnt/' + drive[0].lower() + rest.replace('\\', '/')

def windows():
  name = 'david-%s-win64' % VERSION
  app = os.path.join(DIST, name)
  stage(os.path.join(ROOT, 'build', 'win-release'), app, ['david.exe', 'david-cli.exe'])
  # Portable: the marker keeps the settings in a config folder next to the program
  zipdir(app, os.path.join(DIST, name + '-portable.zip'),
         { 'portable': 'Settings, themes and mods of this copy live in the config folder next to david.exe.\n' })
  setup = os.path.join(DIST, 'david-%s-setup.exe' % VERSION)
  nsi = os.path.join(ROOT, 'packaging', 'david.nsi')
  if shutil.which('makensis'):
    cmd = ['makensis', '-V2', '-DVERSION=' + VERSION, '-DSRCDIR=' + app, '-DOUTFILE=' + setup, nsi]
  else:
    # NSIS in WSL builds the same Windows installer; it wants Windows-style paths inside the script
    cmd = ['wsl', '-d', 'Ubuntu-24.04', '-u', 'root', '--', 'makensis', '-V2', '-DVERSION=' + VERSION,
           '-DSRCDIR=' + wslpath(app), '-DOUTFILE=' + wslpath(setup), wslpath(nsi)]
  subprocess.run(cmd, check=True)
  for f in (name + '-portable.zip', os.path.basename(setup)):
    print('%-36s %6.1f MB' % (f, os.path.getsize(os.path.join(DIST, f)) / 1e6))

def linux():
  build = os.path.join(ROOT, 'build', 'linux-release')
  appdir = os.path.join('/tmp', 'David.AppDir')
  # The program keeps its folders next to the binary, so they all go in usr/bin
  stage(build, os.path.join(appdir, 'usr', 'bin'), ['david', 'david-cli'])
  shutil.copy2(os.path.join(ROOT, 'assets', 'icons', 'david-256.png'), os.path.join(appdir, 'david.png'))
  with open(os.path.join(appdir, 'david.desktop'), 'w') as f:
    f.write('[Desktop Entry]\nType=Application\nName=David\nComment=Raster paint tool with pixel-art tools and mods\n'
            'Exec=david %F\nIcon=david\nCategories=Graphics;2DGraphics;RasterGraphics;\n'
            'MimeType=image/png;image/gif;image/webp;image/bmp;image/jpeg;\n')
  with open(os.path.join(appdir, 'AppRun'), 'w') as f:
    f.write('#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\n'
            'case "$(basename "$ARGV0")" in david-cli*) exec "$HERE/usr/bin/david-cli" "$@";; esac\n'
            'exec "$HERE/usr/bin/david" "$@"\n')
  os.chmod(os.path.join(appdir, 'AppRun'), 0o755)
  os.makedirs(DIST, exist_ok=True)
  # The plain archive: the program folder with the desktop entry and icon
  name = 'david-%s-linux-x86_64' % VERSION
  tgz = os.path.join(DIST, name + '.tar.gz')
  with tarfile.open(tgz, 'w:gz') as t:
    t.add(os.path.join(appdir, 'usr', 'bin'), name)
    t.add(os.path.join(appdir, 'david.desktop'), name + '/david.desktop')
  print('%-36s %6.1f MB' % (os.path.basename(tgz), os.path.getsize(tgz) / 1e6))
  tool = os.path.join(ROOT, 'build', 'appimagetool-x86_64.AppImage')
  if not os.path.exists(tool):
    print('no AppImage: put appimagetool at ' + tool)
    return
  os.chmod(tool, 0o755)
  out = os.path.join(DIST, 'David-%s-x86_64.AppImage' % VERSION)
  env = dict(os.environ, ARCH='x86_64', APPIMAGE_EXTRACT_AND_RUN='1')
  subprocess.run([tool, '--no-appstream', appdir, out], check=True, env=env)
  print('%-36s %6.1f MB' % (os.path.basename(out), os.path.getsize(out) / 1e6))

if __name__ == '__main__':
  { 'win': windows, 'linux': linux }[sys.argv[1] if len(sys.argv) > 1 else 'win']()

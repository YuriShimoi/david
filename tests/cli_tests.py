"""Checks david-cli end to end: conversions, effect chains, pipes, batch, info, scripts, mods, exit codes.
Usage: python tests/cli_tests.py <folder with david-cli>"""
import json, os, shutil, struct, subprocess, sys, tempfile, zlib

BIN = os.path.join(sys.argv[1] if len(sys.argv) > 1 else 'build/win-debug', 'david-cli')
failed = 0


def run(*args, stdin=None):
    r = subprocess.run([BIN, *args], input=stdin, capture_output=True)
    return r.returncode, r.stdout, r.stderr.decode('utf-8', 'replace')


def check(name, ok, detail=''):
    global failed
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else '  ' + detail))
    failed += 0 if ok else 1


tmp = tempfile.mkdtemp(prefix='david-cli-')
try:
    src = os.path.join(tmp, 'src.png')
    code, out, err = run('--list-effects', '--json')
    effects = json.loads(out) if code == 0 else []
    check('--list-effects --json lists the built-in effects', any(e['id'] == 'dither' for e in effects) and any(e['id'] == 'curves' for e in effects), err)

    # A tiny PNG written here (one red pixel), so the test needs no outside tools
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png1 = bytes.fromhex('89504e470d0a1a0a') + chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes([0, 255, 0, 0, 255]))) + chunk(b'IEND', b'')
    with open(os.path.join(tmp, 'one.png'), 'wb') as f: f.write(png1)
    code, _, err = run(os.path.join(tmp, 'one.png'), '--resize', '40x30', '--filter', 'nearest', '-o', src)
    check('resize and save', code == 0 and os.path.exists(src), err)

    code, out, err = run('info', src, '--json')
    info = json.loads(out) if code == 0 else {}
    check('info --json gives the size', info.get('width') == 40 and info.get('height') == 30, err)

    out_webp = os.path.join(tmp, 'out.webp')
    code, _, err = run(src, '--effect', 'gaussian-blur:radius=3', '--effect', 'outline:color=#000', '-o', out_webp)
    check('effect chain to WebP', code == 0 and os.path.getsize(out_webp) > 0, err)

    code, data, err = run('-', '--effect', 'invert', '-o', '-', stdin=open(src, 'rb').read())
    check('stdin to stdout as PNG', code == 0 and data[:4] == b'\x89PNG', err)
    code, out, err = run('info', '-', '--json', stdin=data)
    check('info reads stdin', code == 0 and json.loads(out)['width'] == 40, err)

    script = os.path.join(tmp, 'fill.lua')
    with open(script, 'w') as f:
        f.write('local img = david.image\nlocal c = david.rgba(0, 255, 0)\n'
                'local n = tonumber(david.args.n)\nfor x = 0, n - 1 do img:layer():set(x, 0, c) end\n')
    out_png = os.path.join(tmp, 'script.png')
    code, _, err = run(src, '--script', script, '--arg', 'n=5', '-o', out_png)
    check('Lua script with args', code == 0 and os.path.exists(out_png), err)

    bad = os.path.join(tmp, 'bad.lua')
    with open(bad, 'w') as f: f.write('error("boom")\n')
    code, _, err = run(src, '--script', bad, '-o', out_png)
    check('a failing script exits 3', code == 3 and 'boom' in err, err)
    with open(bad, 'w') as f: f.write('os.execute("echo hi")\n')
    code, _, err = run(src, '--script', bad, '-o', out_png)
    check('the sandbox has no os', code == 3, err)

    code, _, err = run(src, '--effect', 'nope', '-o', out_png)
    check('an unknown effect exits 1', code == 1 and 'nope' in err, err)
    code, _, err = run(src, '--effect', 'gaussian-blur:radius=abc', '-o', out_png)
    check('a bad param exits 1', code == 1, err)
    code, _, err = run(os.path.join(tmp, 'missing.png'), '-o', out_png)
    check('a missing file exits 2', code == 2, err)

    for ext in ['david', 'ora', 'qoi', 'bmp', 'tga', 'jpg']:
        p = os.path.join(tmp, 'conv.' + ext)
        code, _, err = run(src, '-o', p)
        code2, out, err2 = run('info', p, '--json')
        check('convert to .' + ext, code == 0 and code2 == 0 and json.loads(out)['width'] == 40, err + err2)

    os.makedirs(os.path.join(tmp, 'in'))
    for i in range(3): shutil.copy(src, os.path.join(tmp, 'in', 'f%d.png' % i))
    code, _, err = run('--batch', os.path.join(tmp, 'in', '*.png'), '--resize', '200%', '--filter', 'nearest', '--out-dir', os.path.join(tmp, 'big'), '--format', 'qoi')
    outs = sorted(os.listdir(os.path.join(tmp, 'big'))) if code == 0 else []
    code2, out, _ = run('info', os.path.join(tmp, 'big', 'f0.qoi'), '--json')
    check('batch resize into a folder', outs == ['f0.qoi', 'f1.qoi', 'f2.qoi'] and json.loads(out)['width'] == 80, err)

    # Mods: the shader runs in a hidden GL context, the native plugin adds a format and an effect
    code, _, err = run('--all-mods', src, '--effect', 'scanlines:spacing=4', '-o', os.path.join(tmp, 'scan.png'))
    check('shader mod effect from the CLI', code == 0, err)
    q = os.path.join(tmp, 'x.qlike')
    code, _, err = run('--all-mods', src, '--effect', 'swap-red-blue', '-o', q)
    code2, out, err2 = run('--all-mods', 'info', q, '--json')
    check('native mod format and effect', code == 0 and code2 == 0 and json.loads(out)['height'] == 30, err + err2)

    # Animation: a 40x30 image cut into 10x10 frames, effects on every frame, GIF / sheet / frame outputs
    gif, noisy = os.path.join(tmp, 'anim.gif'), os.path.join(tmp, 'noisy.png')
    run(src, '--effect', 'add-noise', '-o', noisy)  # every frame different (WebP merges equal frames in a row)
    code, _, err = run(noisy, '--slice', '10x10', '--fps', '10', '--effect', 'invert', '--all-frames', '-o', gif)
    code2, out, err2 = run('info', gif, '--json')
    info = json.loads(out) if code2 == 0 else {}
    check('--slice into an animated GIF', code == 0 and len(info.get('frames', [])) == 12 and info.get('width') == 10, err + err2)
    check('the frame rate reaches the file', code == 0 and info.get('frames', [{}])[0].get('duration') == 100, err)
    sheet = os.path.join(tmp, 'sheet.png')
    code, _, err = run(gif, '--sheet', 'cols=4,pad=1,json', '--scale', '2', '-o', sheet)
    code2, out, err2 = run('info', sheet, '--json')
    meta = json.load(open(os.path.join(tmp, 'sheet.json'))) if os.path.exists(os.path.join(tmp, 'sheet.json')) else {}
    check('--sheet with JSON', code == 0 and json.loads(out)['width'] == 4 * 20 + 5 and len(meta.get('frames', {})) == 12, err + err2)
    one = os.path.join(tmp, 'f3.png')
    code, _, err = run(gif, '--frame', '3', '-o', os.path.join(tmp, 'f3.qoi'))
    check('--frame picks one frame for a flat output', code == 0, err)
    code, _, err = run(gif, '--frame', '99', '-o', one)
    check('a missing frame exits 1', code == 1, err)
    for ext in ('png', 'webp'):
        o = os.path.join(tmp, 'anim.' + ext)
        code, _, err = run(gif, '-o', o)
        code2, out, err2 = run('info', o, '--json')
        fr = json.loads(out).get('frames', []) if code2 == 0 else []
        # WebP merges identical frames in a row into one longer frame: the total time is what must match
        check('animated .' + ext, code == 0 and len(fr) > 1 and sum(f['duration'] for f in fr) == 1200, err + err2)
    code, _, err = run(gif, '--tag', 'nope', '-o', os.path.join(tmp, 'x.gif'))
    check('an unknown tag exits 1', code == 1 and 'nope' in err, err)
    script = os.path.join(tmp, 'frames.lua')
    with open(script, 'w') as f:
        f.write('local doc = david.image\nassert(doc:frameCount() == 12)\ndoc.activeFrame = 2\n'
                'doc:addFrame("copy")\ndoc:setFrameDuration(3, 500)\nassert(doc.activeFrame == 3)\n')
    code, _, err = run(gif, '--script', script, '-o', os.path.join(tmp, 'more.gif'))
    code2, out, err2 = run('info', os.path.join(tmp, 'more.gif'), '--json')
    fr = json.loads(out).get('frames', []) if code2 == 0 else []
    check('Lua frame API', code == 0 and len(fr) == 13 and fr[2]['duration'] == 500, err + err2)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print('%d FAILED' % failed if failed else 'all passed')
sys.exit(1 if failed else 0)

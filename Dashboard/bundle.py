#!/usr/bin/env python3
"""
Produce a single self-contained Tezuka Dashboard HTML.

All JS (vendor + JSX sources, precompiled) and CSS are inlined so the file
can be served from the device without any internet access. JSX is
transpiled and minified at build time via esbuild -- unlike the dev-mode
`Tezuka Dashboard.html`, no Babel-in-browser runtime is shipped.

Usage:
  python3 bundle.py > output.html
  python3 bundle.py /path/to/output.html
"""
import os, sys, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ESBUILD = os.path.join(HERE, 'node_modules', '.bin', 'esbuild')

def npm_install():
    """Install exactly what package-lock.json pins -- never touches
    package.json/package-lock.json (unlike `npm install`, which rewrites
    them on every run even when nothing actually changed)."""
    print('[bundle] npm ci...', file=sys.stderr)
    subprocess.run(['npm', 'ci'], cwd=HERE, check=True)

def esbuild(args, src=None):
    """Run the locally-installed esbuild binary directly (no npx -- avoids
    a registry round-trip per invocation). Feeds `src` on stdin and
    returns stdout when given; otherwise just runs `args` for side effects
    (e.g. --outfile)."""
    result = subprocess.run([ESBUILD] + args, cwd=HERE, input=src,
                             capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        result.check_returncode()
    return result.stdout

def build_signals():
    """Rebuild vendor/signals.bundle.js via esbuild."""
    bundle = os.path.join(HERE, 'vendor', 'signals.bundle.js')
    entry  = os.path.join(HERE, 'signals-entry.js')
    print('[bundle] esbuild signals-entry.js -> vendor/signals.bundle.js...', file=sys.stderr)
    esbuild([entry, '--bundle', '--minify', '--format=iife',
             '--global-name=Signals', f'--outfile={bundle}'])
    print(f'[bundle] signals.bundle.js ({os.path.getsize(bundle)//1024} KB)', file=sys.stderr)

npm_install()
build_signals()

VENDOR = [
    ('vendor/react.js',     'https://unpkg.com/react@18.3.1/umd/react.production.min.js'),
    ('vendor/react-dom.js', 'https://unpkg.com/react-dom@18.3.1/umd/react-dom.production.min.js'),
]

# Load order matters — same as the <script> tags in Tezuka Dashboard.html
SOURCES = [
    ('js',  'paho-mqtt-min.js'),
    ('jsx', 'tweaks-panel.jsx'),
    ('jsx', 'icons.jsx'),
    ('jsx', 'charts.jsx'),
    ('jsx', 'data.jsx'),
    ('jsx', 'tuner.jsx'),
    ('jsx', 'pages1.jsx'),
    ('jsx', 'pages2.jsx'),
    ('jsx', 'pages3.jsx'),
    ('jsx', 'pages4.jsx'),
    ('js',  'vendor/signals.bundle.js'),
    ('jsx', 'pages5.jsx'),
    ('jsx', 'radioastro.jsx'),
    ('jsx', 'classifier.jsx'),
    ('jsx', 'midi.jsx'),
    ('jsx', 'webtioune.jsx'),
    ('jsx', 'rftest.jsx'),
    ('jsx', 'app.jsx'),
]

def fetch_vendor(rel, url):
    path = os.path.join(HERE, rel)
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        print(f'[bundle] downloading {rel}...', file=sys.stderr)
        
        # Use a pure system call to curl
        # -L follows redirects, -s silences progress bars (but keeps errors on failure)
        subprocess.run(['curl', '-L', '-s', '-f', '-o', path, url], check=True)
        
    return open(path, encoding='utf-8').read()

def read(rel):
    src = open(os.path.join(HERE, rel), encoding='utf-8').read()
    if rel == 'data.jsx':
        # On-device: broker is always on localhost
        import re
        src = re.sub(r"const MQTT_DEV_HOST\s*=\s*'[^']*'",
                     "const MQTT_DEV_HOST = null", src)
    return src

out = []

out.append('''\
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tezuka Dashboard</title>
<link rel="icon" href="data:,">
<style>
''')
out.append(read('styles.css'))
out.append('</style>\n')

for rel, url in VENDOR:
    out.append('<script>\n')
    out.append(fetch_vendor(rel, url))
    out.append('\n</script>\n')

out.append('</head>\n<body>\n<div id="root"></div>\n')

for kind, fname in SOURCES:
    content = read(fname)
    if kind == 'jsx':
        # Transform+minify via stdin/stdout (not a file path) so
        # --loader=jsx is valid, and so the MQTT_DEV_HOST rewrite in
        # read() above runs on the source *before* esbuild ever sees it --
        # esbuild would otherwise constant-fold the placeholder literal
        # before it could be substituted. Each file is transformed on its
        # own (no --bundle): these are classic scripts sharing one global
        # scope via `Object.assign(window, {...})` (see Dashboard/CLAUDE.md),
        # and esbuild's non-bundle transform leaves top-level identifiers
        # alone, so that pattern survives unchanged.
        print(f'[bundle] esbuild {fname}...', file=sys.stderr)
        js = esbuild(['--loader=jsx', '--target=es2020', '--minify'], src=content)
        out.append(f'<script>\n{js}</script>\n')
    else:
        out.append(f'<script>\n{content}\n</script>\n')

out.append('</body>\n</html>\n')

result = ''.join(out)

if len(sys.argv) > 1:
    dest = sys.argv[1]
    os.makedirs(os.path.dirname(dest) or '.', exist_ok=True)
    with open(dest, 'w', encoding='utf-8') as f:
        f.write(result)
    print(f'[bundle] {dest} ({len(result)//1024} KB)', file=sys.stderr)
else:
    sys.stdout.write(result)

#!/usr/bin/env python3
"""
Produce a single self-contained Tezuka Dashboard HTML.

All JS (vendor + JSX sources) and CSS are inlined so the file can be served
from the device without any internet access.

Usage:
  python3 bundle.py > output.html
  python3 bundle.py /path/to/output.html
"""
import os, sys, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))

def build_signals():
    """Rebuild vendor/signals.bundle.js via npm + esbuild."""
    bundle = os.path.join(HERE, 'vendor', 'signals.bundle.js')
    entry  = os.path.join(HERE, 'signals-entry.js')
    print('[bundle] npm install @jtarrio/signals esbuild...', file=sys.stderr)
    subprocess.run(['npm', 'install', '@jtarrio/signals', 'esbuild'],
                   cwd=HERE, check=True)
    print('[bundle] esbuild signals-entry.js -> vendor/signals.bundle.js...', file=sys.stderr)
    subprocess.run(
        ['npx', 'esbuild', entry,
         '--bundle', '--format=iife', '--global-name=Signals',
         f'--outfile={bundle}'],
        cwd=HERE, check=True)
    print(f'[bundle] signals.bundle.js ({os.path.getsize(bundle)//1024} KB)', file=sys.stderr)

build_signals()

VENDOR = [
    ('vendor/react.js',     'https://unpkg.com/react@18.3.1/umd/react.production.min.js'),
    ('vendor/react-dom.js', 'https://unpkg.com/react-dom@18.3.1/umd/react-dom.production.min.js'),
    ('vendor/babel.js',     'https://unpkg.com/@babel/standalone@7.29.0/babel.min.js'),
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
        out.append(f'<script type="text/babel">\n{content}\n</script>\n')
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

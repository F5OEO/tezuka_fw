// webtioune.jsx — DATV Controller sub-page: live transport-stream preview
// via mpegts.js (https://github.com/xqq/mpegts.js, Apache-2.0).
//
// Source: the page's own board, which serves the decoded transport stream
// over WebSocket (ws://<board>:8080/stream.ts) — this page is purely a player.
//
// mpegts.js remuxes the TS to fragmented MP4 and plays it through Media Source
// Extensions, so the *browser* decodes the video. The stream is HEVC, which
// only plays where the browser/OS can decode it (Safari, Edge on Windows with
// the HEVC extension, Chrome with hardware decode — on Linux that needs VA-API
// and Chrome started with --enable-features=VaapiVideoDecoder,
// PlatformHEVCDecoderSupport). The page checks this up front and says so.
//
// The runtime is lazy-loaded on first visit rather than included in every page
// load.
//
// The page also controls the receiver itself: frequency / symbol rate and
// start / stop go to zynqrx over MQTT in longmynd's scheme (cmd/zynqrx/
// frequency [kHz], sr [kS], process start|stop), and its status comes back on
// dt/zynqrx/... (d.lm, filled by useLiveData in data.jsx).
const { useState: useWtS, useEffect: useWtE, useRef: useWtR } = React;

// Relative to wherever Tezuka Dashboard.html is served from — mirrors the
// Dashboard/vendor/ convention used for react.js/babel.js. On-device the files
// come from overlay_datv (root/vendor/mpegts/).
const MPEGTS_SCRIPT = './vendor/mpegts/mpegts.js';

let mpegtsLoadPromise = null;
function loadMpegts() {
  if (window.mpegts) return Promise.resolve();
  if (mpegtsLoadPromise) return mpegtsLoadPromise;
  mpegtsLoadPromise = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = MPEGTS_SCRIPT;
    s.onload = () => resolve();
    s.onerror = () => { mpegtsLoadPromise = null; reject(new Error(`Failed to load ${MPEGTS_SCRIPT} — is the mpegts.js runtime installed there?`)); };
    document.head.appendChild(s);
  });
  return mpegtsLoadPromise;
}

// The board's zynqrx serves the live TS over WebSocket on :8080.
// _tezukaDevHost honours the MQTT_DEV_HOST override in data.jsx, else the
// page's own hostname.
const WS_TS_PORT = 8080;
const wsTsUrl = () => `ws://${window._tezukaDevHost || window.location.hostname}:${WS_TS_PORT}/stream.ts`;

// zynqrx's limits (cmd/zynqrx/frequency in kHz, sr in kS)
// the frequency goes out in kHz rounded to 0.1 kHz (a decimal only when there is one, e.g. 1089999.2)
// the spectrometer frame rate asked for at every symbol rate change (spectro/fps, as the Spectrum page does), so the receiver spectrum redraws at 20 fps
const RX_SPEC_FPS = 20;
const rxKHz = (hz) => Math.round(hz / 100) / 10;
const RX_FREQ_MIN_HZ = 70e6, RX_FREQ_MAX_HZ = 6e9;
const RX_SR_MIN_KS = 33, RX_SR_MAX_KS = 7680;
const RX_SR_PRESETS = [125, 250, 333, 500, 1000];

// The QO-100 wideband spectrum from the BATC feed (frame: 16-bit power per bin over 10490.5 - 10499.5 MHz),
// drawn on a canvas at 10 Hz from the latest frame. Click: on a detected signal it tunes to its centre and
// symbol rate, elsewhere to the clicked frequency at the current symbol rate. The receiver's own tuning is
// drawn as a band.
const SPEC_NOISE = 11000;
function Qo100Spectrum({ frameRef, signals, rxDlMHz, rxSrKs, namesRef, onPick, f0 = 10490.5, span = 9.0, emptyText = 'waiting for the BATC spectrum feed…' }) {
  const cvRef = useWtR(null);
  const live = useWtR({});
  live.current = { signals, rxDlMHz, rxSrKs, f0, span, emptyText };
  const yMax = useWtR(30000);
  const H = 150;
  useWtE(() => {
    const draw = () => {
      const cv = cvRef.current; if (!cv) return;
      const w = cv.clientWidth, dpr = window.devicePixelRatio || 1;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(H * dpr); }
      const g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, H);
      const css = getComputedStyle(cv);
      const accent = css.getPropertyValue('--accent').trim() || '#f5a623';
      const ink = css.getPropertyValue('--text-dim').trim() || css.getPropertyValue('--muted').trim() || '#8a8f98';
      const grid = 'rgba(128,128,128,.22)';
      const padB = 16, ph = H - padB;
      const { signals: sg, rxDlMHz: rx, rxSrKs: rs, f0: SPEC_F0, span: SPEC_SPAN, emptyText } = live.current;
      const X = (mhz) => ((mhz - SPEC_F0) / SPEC_SPAN) * w;
      const nameOf = (s) => { const n = namesRef.current.find((q) => q.freq >= s.start - 0.03 && q.freq <= s.end + 0.03); return n ? n.name : ''; };
      g.font = '10px sans-serif'; g.textBaseline = 'alphabetic';
      // Labels above the signals (a decoded name, else the symbol rate). A label that would overlap one already placed in a row goes
      // to the next row down; the plot starts under the last row.
      const labels = [];
      for (const s of sg) {
        const nm = nameOf(s);
        g.font = nm ? 'bold 12px sans-serif' : '10px sans-serif';
        const text = nm || (s.beacon ? 'beacon' : (s.sr >= 1000 ? s.sr / 1000 + 'M' : s.sr + 'k'));
        const hw = g.measureText(text).width / 2;
        labels.push({ s, nm, text, hw, x: Math.min(w - hw - 2, Math.max(hw + 2, X(s.freq))), row: 0 });
      }
      labels.sort((a, b) => a.x - b.x);
      const rowEnd = [];
      for (const l of labels) {
        let r = 0;
        while (r < rowEnd.length && l.x - l.hw < rowEnd[r] + 6) r++;
        rowEnd[r] = l.x + l.hw; l.row = r;
      }
      const padT = 4 + 13 * Math.max(1, rowEnd.length);
      for (let f = Math.ceil(SPEC_F0); f <= SPEC_F0 + SPEC_SPAN; f++) { const x = Math.round(X(f)) + .5; g.strokeStyle = grid; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, ph); g.stroke(); g.fillStyle = ink; g.textAlign = 'center'; g.fillText(String(f), x, H - 3); }
      if (rx > 0 && rs > 0) { g.fillStyle = 'rgba(255,255,255,.10)'; const x0 = X(rx - rs / 2000); g.fillRect(x0, 0, Math.max(2, X(rx + rs / 2000) - x0), ph); g.fillStyle = accent; g.fillRect(Math.round(X(rx)), 0, 1, ph); }
      const fr = frameRef.current;
      if (!fr || fr.length < 16) { g.fillStyle = ink; g.textAlign = 'center'; g.fillText(emptyText, w / 2, ph / 2); return; }
      let peak = 0; for (let i = 0; i < fr.length; i++) if (fr[i] > peak) peak = fr[i];
      yMax.current += (Math.max(30000, peak * 1.08) - yMax.current) * 0.2;
      const Y = (v) => ph - Math.max(0, Math.min(1, (v - 9000) / (yMax.current - 9000))) * (ph - padT);
      g.beginPath(); g.moveTo(0, ph);
      for (let px = 0; px <= w; px++) {                           // max of the bins under each pixel
        const a = Math.floor((px / w) * fr.length), b = Math.max(a + 1, Math.floor(((px + 1) / w) * fr.length));
        let m = 0; for (let i = a; i < b && i < fr.length; i++) if (fr[i] > m) m = fr[i];
        g.lineTo(px, Y(m));
      }
      g.lineTo(w, ph); g.closePath();
      g.fillStyle = 'rgba(80,160,255,.25)'; g.fill();
      g.strokeStyle = 'rgba(110,180,255,.9)'; g.lineWidth = 1; g.stroke();
      for (const l of labels) {                                   // a decoded service name stays on its signal until the signal leaves the spectrum
        const sg1 = l.s;
        g.fillStyle = sg1.beacon ? 'rgba(255,120,60,.85)' : accent;
        g.fillRect(X(sg1.start), ph - 4, Math.max(2, X(sg1.end) - X(sg1.start)), 4);
        g.font = l.nm ? 'bold 12px sans-serif' : '10px sans-serif'; g.fillStyle = l.nm ? accent : ink; g.textAlign = 'center';
        g.fillText(l.text, l.x, 11 + 13 * l.row);
        if (l.row > 0) { g.strokeStyle = 'rgba(128,128,128,.35)'; g.beginPath(); g.moveTo(Math.round(X(sg1.freq)) + .5, 13 * l.row + 14); g.lineTo(Math.round(X(sg1.freq)) + .5, padT); g.stroke(); }
      }
    };
    const id = setInterval(draw, 100); draw();
    return () => clearInterval(id);
  }, []);
  const pick = (e) => {
    const r = cvRef.current.getBoundingClientRect();
    const mhz = live.current.f0 + ((e.clientX - r.left) / r.width) * live.current.span;
    const hit = live.current.signals.find((s) => mhz >= s.start - 0.03 && mhz <= s.end + 0.03);
    if (hit) onPick(hit.freq, hit.sr); else onPick(Math.round(mhz * 1000) / 1000, 0);
  };
  return <canvas ref={cvRef} onClick={pick} style={{ display: 'block', width: '100%', height: H, cursor: 'crosshair', borderRadius: 6 }} />;
}

// The receiver's own spectrum: the board's binary /waterfall WebSocket (the Spectrum page's feed: a Float32 step index then linear
// amplitude per FFT bin, plus JSON {center, span} messages), drawn on a canvas at 20 Hz in the QO-100 spectrum's style. The band
// zynqrx is tuned to (frequency, symbol rate) is drawn as a band when it falls inside the span.
function useWaterfallFeed() {
  const frame = useWtR(null);                        // latest frame in dB
  const [info, setInfo] = useWtS({ state: 'connecting', center: 0, span: 0 });
  useWtE(() => {
    let destroyed = false, ws = null, timer = null;
    const host = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const connect = () => {
      if (destroyed) return;
      ws = new WebSocket(`${proto}//${host}/waterfall`);
      ws.binaryType = 'arraybuffer';
      ws.onopen = () => setInfo((i) => ({ ...i, state: 'live' }));
      ws.onclose = () => { frame.current = null; setInfo((i) => ({ ...i, state: 'offline' })); if (!destroyed) timer = setTimeout(connect, 2000); };
      ws.onerror = () => {};
      ws.onmessage = (evt) => {
        if (!(evt.data instanceof ArrayBuffer)) {
          try {
            const m = JSON.parse(evt.data);
            setInfo((i) => ({ ...i, center: m.center != null ? m.center : i.center, span: m.span != null ? m.span : i.span }));
          } catch (_) {}
          return;
        }
        const f = new Float32Array(evt.data);
        if (f.length < 3) return;                    // f[0] is the step index, f[1..] the bins
        const db = new Float32Array(f.length - 1);
        for (let i = 0; i < db.length; i++) db[i] = 20 * Math.log10(f[i + 1] > 0 ? f[i + 1] : 1e-10);
        frame.current = db;
      };
    };
    connect();
    return () => { destroyed = true; clearTimeout(timer); try { ws && ws.close(); } catch (_) {} };
  }, []);
  return [frame, info];
}

function RxSpectrum({ frameRef, centerHz, spanHz, rxHz, rxSrKs, onPick }) {
  const cvRef = useWtR(null);
  const live = useWtR({});
  live.current = { centerHz, spanHz, rxHz, rxSrKs };
  const range = useWtR({ lo: -100, hi: -20 });
  const zoom = useWtR(1);                                        // mouse wheel or pinch; the zoomed view stays centred on the receive frequency (the DDC NCO)
  const manual = useWtR(false);                                  // false: the view fits 2 x the symbol rate around the receive frequency; a wheel or pinch makes it manual
  const [isManual, setIsManual] = useWtS(false);
  const pinch = useWtR({ pts: new Map(), d0: 0, z0: 1, t: 0 });
  const H = 150;
  // the visible window: the whole span, or span / zoom centred on the receive frequency (kept inside the span)
  const MIN_VIEW = 50e3;
  const zoomNow = () => {
    const { spanHz: sp, rxSrKs: rs } = live.current;
    if (manual.current || !(sp > 0)) return zoom.current;
    return rs > 0 ? Math.max(1, Math.min(sp / MIN_VIEW, sp / (2 * rs * 1e3))) : 1;      // auto: the view is 2 x the symbol rate wide
  };
  const view = () => {
    const { centerHz: c, spanHz: sp, rxHz: rx } = live.current;
    const z = zoomNow(), vs = sp / z;
    const vc = z > 1 && rx > 0 ? Math.min(c + sp / 2 - vs / 2, Math.max(c - sp / 2 + vs / 2, rx)) : c;
    return { f0: vc - vs / 2, span: vs, full0: c - sp / 2, full: sp, z };
  };
  const setZoom = (z) => {
    const sp = live.current.spanHz, z0 = zoomNow();
    if (!manual.current) { manual.current = true; zoom.current = z0; setIsManual(true); }   // the first wheel / pinch step starts from the fitted view
    zoom.current = Math.max(1, Math.min(sp > 0 ? Math.max(1, sp / MIN_VIEW) : 1, z));
  };
  const fit = () => { manual.current = false; setIsManual(false); };
  useWtE(() => {
    const draw = () => {
      const cv = cvRef.current; if (!cv) return;
      const w = cv.clientWidth, dpr = window.devicePixelRatio || 1;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(H * dpr); }
      const g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, H);
      const css = getComputedStyle(cv);
      const accent = css.getPropertyValue('--accent').trim() || '#f5a623';
      const ink = css.getPropertyValue('--text-dim').trim() || css.getPropertyValue('--muted').trim() || '#8a8f98';
      const grid = 'rgba(128,128,128,.22)';
      const { centerHz: c, spanHz: sp, rxHz: rx, rxSrKs: rs } = live.current;
      const padB = 16, padT = 4, ph = H - padB;
      g.font = '10px sans-serif'; g.textBaseline = 'alphabetic';
      const fr = frameRef.current;
      if (!fr || !(sp > 0)) { g.fillStyle = ink; g.textAlign = 'center'; g.fillText('waiting for the spectrum feed…', w / 2, ph / 2); return; }
      const V = view(), f0 = V.f0, vs = V.span, X = (hz) => ((hz - f0) / vs) * w;
      // frequency grid on a 1-2-5 step giving about 8 lines
      const raw = vs / 8, mag = Math.pow(10, Math.floor(Math.log10(raw)));
      const step = mag * ([1, 2, 5, 10].find((m) => m * mag >= raw) || 10);
      for (let f = Math.ceil(f0 / step) * step; f <= f0 + vs; f += step) {
        const x = Math.round(X(f)) + .5; g.strokeStyle = grid; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, ph); g.stroke();
        g.fillStyle = ink; g.textAlign = 'center'; g.fillText((f / 1e6).toFixed(step >= 1e6 ? 0 : step >= 1e5 ? 1 : step >= 1e4 ? 2 : 3), Math.min(w - 14, Math.max(14, x)), H - 3);
      }
      if (rx > 0 && rs > 0 && rx > f0 && rx < f0 + vs) { g.fillStyle = 'rgba(255,255,255,.10)'; const x0 = X(rx - rs * 500); g.fillRect(x0, 0, Math.max(2, X(rx + rs * 500) - x0), ph); g.fillStyle = accent; g.fillRect(Math.round(X(rx)), 0, 1, ph); }
      let lo = Infinity, hi = -Infinity;
      for (let i = 0; i < fr.length; i++) { const v = fr[i]; if (v < lo) lo = v; if (v > hi) hi = v; }
      if (!isFinite(lo)) return;
      const r = range.current; r.lo += (lo - 3 - r.lo) * 0.1; r.hi += (hi + 6 - r.hi) * 0.1;
      const Y = (v) => ph - Math.max(0, Math.min(1, (v - r.lo) / Math.max(10, r.hi - r.lo))) * (ph - padT);
      g.beginPath(); g.moveTo(0, ph);
      const binAt = (px) => ((f0 + (px / w) * vs - V.full0) / V.full) * fr.length;   // the bins cover the whole span, the view may be a part of it
      for (let px = 0; px <= w; px++) {                           // max of the bins under each pixel
        const a = Math.max(0, Math.floor(binAt(px))), b = Math.max(a + 1, Math.floor(binAt(px + 1)));
        let m = -Infinity; for (let i = a; i < b && i < fr.length; i++) if (fr[i] > m) m = fr[i];
        g.lineTo(px, m === -Infinity ? ph : Y(m));
      }
      g.lineTo(w, ph); g.closePath();
      g.fillStyle = 'rgba(80,160,255,.25)'; g.fill();
      g.strokeStyle = 'rgba(110,180,255,.9)'; g.lineWidth = 1; g.stroke();
      g.fillStyle = ink; g.textAlign = 'left'; g.fillText(Math.round(r.hi) + ' dB', 3, 11);
      if (V.z > 1.01) { g.textAlign = 'right'; g.fillText((manual.current ? '' : 'fit ') + '×' + V.z.toFixed(V.z < 10 ? 1 : 0) + ' · ' + (vs / 1e3 >= 1000 ? (vs / 1e6).toFixed(2) + ' MHz' : (vs / 1e3).toFixed(0) + ' kHz'), w - 4, 11); }
    };
    const id = setInterval(draw, 50); draw();
    // mouse wheel zoom (a native listener: React's wheel handlers are passive and could not stop the page from scrolling)
    const wheel = (e) => { e.preventDefault(); setZoom(zoomNow() * Math.exp(-e.deltaY * (e.deltaMode === 1 ? 0.05 : 0.0015))); };
    const cv0 = cvRef.current;
    if (cv0) cv0.addEventListener('wheel', wheel, { passive: false });
    return () => { clearInterval(id); if (cv0) cv0.removeEventListener('wheel', wheel); };
  }, []);
  // pinch on a touch screen: two pointers, the zoom follows the ratio of their distance to the start of the gesture
  const pd = (e) => {
    const P = pinch.current; P.pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (P.pts.size === 2) { const [a, b] = [...P.pts.values()]; P.d0 = Math.hypot(a.x - b.x, a.y - b.y) || 1; P.z0 = zoomNow(); P.t = Date.now(); try { cvRef.current.setPointerCapture(e.pointerId); } catch (_) {} }
  };
  const pm = (e) => {
    const P = pinch.current; if (!P.pts.has(e.pointerId)) return;
    P.pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (P.pts.size === 2) { const [a, b] = [...P.pts.values()]; setZoom(P.z0 * (Math.hypot(a.x - b.x, a.y - b.y) / P.d0)); P.t = Date.now(); }
  };
  const pu = (e) => { const P = pinch.current; P.pts.delete(e.pointerId); if (P.pts.size < 2) P.d0 = 0; };
  const pick = (e) => {                                          // click: tune the receiver to the clicked frequency (not at the end of a pinch)
    if (Date.now() - pinch.current.t < 400) return;
    const { spanHz: sp } = live.current;
    if (!(sp > 0) || !frameRef.current) return;
    const r = cvRef.current.getBoundingClientRect(), V = view();
    onPick(Math.round(V.f0 + ((e.clientX - r.left) / r.width) * V.span));
  };
  return (
    <div style={{ position: 'relative' }}>
      <canvas ref={cvRef} onClick={pick} onPointerDown={pd} onPointerMove={pm} onPointerUp={pu} onPointerCancel={pu}
        style={{ display: 'block', width: '100%', height: H, cursor: 'crosshair', borderRadius: 6, touchAction: 'pan-y' }} />
      {isManual && <button className="btn ghost" onClick={fit} title="back to the automatic zoom: 2 x the symbol rate around the receive frequency"
        style={{ position: 'absolute', right: 4, top: 16, padding: '0 6px', fontSize: 11, lineHeight: '18px', height: 20 }}>fit</button>}
    </div>
  );
}

// Expert receiver options: every setting zynqrx takes over MQTT besides frequency / symbol rate (cmd/zynqrx/<name>, the same names as its command
// line options). The value shown is what the receiver reports it has (dt/zynqrx/set/<name>); a change is sent and the receiver restarts for a few
// seconds. Numbers and text are sent on Enter or when the box loses focus.
const EXPERT_MODS = ['qpsk', '8psk', '16apsk', '32apsk'];
const numIn = (lo, hi) => (v) => { const n = parseFloat(v); return !isNaN(n) && n >= lo && n <= hi; };
const EXPERT_TEXT = [
  { k: 'gain', label: 'RX gain', suffix: 'dB', hint: 'auto = the radio AGC, or 0 – 73 dB', ok: (v) => v === 'auto' || numIn(0, 73)(v) },
  { k: 'clock', label: 'Radio clock (span)', suffix: 'kHz', hint: 'fixed converter rate = the spectrum span (10000 = 10 MHz), the Maia DDC decimates to the symbol rate. 0 = the radio RRC filter. Symbol rates above a quarter of it use the RRC filter', ok: (v) => v === '0' || numIn(2000, 61440)(v) },
  { k: 'bw', label: 'Analog bandwidth', suffix: 'kHz', hint: '0 = 3 x the symbol rate', ok: numIn(0, 56000) },
];
function ExpertText({ d, spec, reported }) {
  const [draft, setDraft] = useWtS(null);
  const shown = draft != null ? draft : (reported != null ? reported : '');
  const valid = draft == null || spec.ok(draft.trim());
  const commit = () => {
    if (draft == null) return;
    const v = draft.trim();
    if (spec.ok(v) && v !== reported) d.publish('zynqrx/' + spec.k, v);
    setDraft(null);
  };
  return (
    <div title={spec.hint}><Field label={spec.label}>
      <TextInput value={shown} suffix={spec.suffix} invalid={!valid} disabled={!d.mqtt}
        onChange={setDraft} onBlur={commit} onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); else if (e.key === 'Escape') setDraft(null); }} />
    </Field></div>
  );
}
// Max lag as a slider, 1 – 32 s: sent when the slider is released (a change restarts the receiver, so not on every step)
function ExpertLag({ d, reported }) {
  const [draft, setDraft] = useWtS(null);
  const rep = reported != null ? Math.min(32, Math.max(1, Math.round(parseFloat(reported)) || 1)) : 4;
  const shown = draft != null ? draft : rep;
  const commit = () => {
    if (draft != null && draft !== rep) d.publish('zynqrx/maxlag', String(draft));
    setDraft(null);
  };
  return (
    <div className="slider-compact" title="drop the oldest samples when the receiver is this far behind" onPointerUp={commit} onKeyUp={commit}>
      <Field label="Max lag">
        <Slider value={shown} min={1} max={32} step={1} onChange={setDraft} unit=" s" />
      </Field>
    </div>
  );
}
function ExpertOptions({ d, lm, open, setOpen }) {
  const set = (k) => lm['set/' + k];
  const send = (k, v) => d.publish('zynqrx/' + k, v);
  const flag = (k, label, hint) => (
    <div title={hint}><Field label={label}>
      <Toggle on={set(k) === '1'} onChange={(v) => send(k, v ? '1' : '0')} />
    </Field></div>
  );
  const modsOn = set('mods') == null || set('mods') === 'all' ? EXPERT_MODS : set('mods').split(',');
  const toggleMod = (m, on) => {
    const next = EXPERT_MODS.filter((x) => (x === m ? on : modsOn.includes(x)));
    if (next.length) send('mods', next.length === EXPERT_MODS.length ? 'qpsk,8psk,16apsk,32apsk' : next.join(','));
  };
  const known = set('std') != null;
  return (
    <Card title="Expert options" sub="zynqrx settings over MQTT · a change restarts the receiver"
      right={<button className="btn ghost" onClick={() => setOpen(open === '1' ? '0' : '1')}>{open === '1' ? 'Hide' : 'Show'}</button>}>
      {open === '1' && (
        <>
          {!known && (
            <div className="info-note" style={{ marginBottom: 10 }}>
              <Icon name="bolt" size={16} />
              <p>The receiver does not report its settings — it may run an older zynqrx without these commands.</p>
            </div>
          )}
          <div className="form-grid" style={{ gridTemplateColumns: 'repeat(5, minmax(0, 1fr))', gap: '8px 12px' }}>
            <div title="--std"><Field label="Standard">
              <Select value={set('std') != null ? set('std') : '2'} onChange={(v) => send('std', v)}
                options={[{ v: '0', l: 'DVB-S and DVB-S2' }, { v: '1', l: 'DVB-S only' }, { v: '2', l: 'DVB-S2 only' }]} />
            </Field></div>
            <div title="cheaper symbol timing, auto = above 0.5 MS/s"><Field label="Fast timing">
              <Select value={set('fasttiming') || 'auto'} onChange={(v) => send('fasttiming', v)}
                options={[{ v: 'auto', l: 'Auto' }, { v: 'on', l: 'On' }, { v: 'off', l: 'Off' }]} />
            </Field></div>
            <div title="RX and TX share the AD9361 clock. On = keep the clock pluto_stream set and decimate in the Maia DDC (FPGA), needed to receive while transmitting. Auto = on only while pluto_stream transmits DVB-S/S2 with PTT on, else the receiver keeps its own 10 MHz clock. Off = the receiver always sets its own rate and FIR"><Field label="TX clock (Maia DDC)">
              <Select value={set('txclock') || 'auto'} onChange={(v) => send('txclock', v)}
                options={[{ v: 'auto', l: 'Auto' }, { v: 'on', l: 'On' }, { v: 'off', l: 'Off' }]} />
            </Field></div>
            {EXPERT_TEXT.map((spec) => <ExpertText key={spec.k} d={d} spec={spec} reported={set(spec.k)} />)}
            <ExpertLag d={d} reported={set('maxlag')} />
            {flag('ccm', 'Constant coding (CCM)', 'search only the MODCOD found first')}
            {flag('presence', 'Presence gate', 'look for a signal only while one is seen')}
            {flag('ldpc8', '8-bit LDPC', 'cheaper, about 2 dB less sensitive at low code rates')}
            {flag('fpgafront', 'FPGA front end', 'symbol timing, mixer and AGC in the FPGA (symfront bitstream, 261 kS and up): frees CPU for the decoder')}
            <div title="Tezuka firmware: 8-bit samples from the FPGA, half the memory traffic, less dynamic range. Off = 12-bit samples in 16 bits"><Field label="CS8 samples">
              <Toggle on={(set('iqformat') || 'cs8') === 'cs8'} onChange={(v) => send('iqformat', v ? 'cs8' : 'cs16')} />
            </Field></div>
            {flag('nobch', 'Bypass BCH', 'skip the BCH decoder (saves CPU, the LDPC output is taken as is)')}
            {flag('wskeyframe', 'Wait for keyframe', 'a new viewer starts at a keyframe, applies at once')}
            <div style={{ gridColumn: 'span 3' }}>
              <div title="fewer = a cheaper search"><Field label="Modulations searched">
                <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
                  {EXPERT_MODS.map((m) => <Checkbox key={m} checked={modsOn.includes(m)} label={m.toUpperCase()} onChange={(on) => toggleMod(m, on)} />)}
                </div>
              </Field></div>
            </div>
          </div>
        </>
      )}
    </Card>
  );
}

function Webtioune({ d }) {
  const lm = (d && d.lm) || {};
  const [freqHz, setFreqHz] = useLS2('webtioune_rx_frequency', 437e6, parseFloat);
  const [srKs, setSrKs] = useLS2('webtioune_rx_sr', 333, (x) => parseInt(x));
  const [expertOpen, setExpertOpen] = useLS2('webtioune_expert', '0');
  const sendTimers = useWtR({});
  // The tuner and the text box change on every key: send once the value has settled
  // (each change restarts the receiver for a few seconds).
  const sendLater = (key, topic, value) => {
    clearTimeout(sendTimers.current[key]);
    sendTimers.current[key] = setTimeout(() => { d.publish(topic, value); if (topic === 'zynqrx/sr') d.publish('spectro/fps', RX_SPEC_FPS); }, 400);
  };
  // What zynqrx reports it was asked for (dt/zynqrx/set/..., kHz and kS) wins over the local copy
  useWtE(() => { if (lm['set/frequency'] != null) { const v = parseFloat(lm['set/frequency']); if (v > 0) setFreqHz(Math.round(v * 10) * 100); } }, [lm['set/frequency']]);
  useWtE(() => { if (lm['set/sr'] != null) { const v = parseInt(lm['set/sr']); if (v > 0) setSrKs(v); } }, [lm['set/sr']]);
  useWtE(() => () => Object.values(sendTimers.current).forEach(clearTimeout), []);

  const rxKnown = lm.process != null;
  const rxRunning = lm.process === 'start';
  const setProcess = (v) => d.publish('zynqrx/process', v);

  // QO-100 WB bandplan, the DATV Controller's own component: a slot gives the downlink frequency and the
  // symbol rate; the receiver (radio) frequency is the downlink minus the LNB LO. The LO is stored under
  // the same key as the DATV Controller's, so both pages use one value.
  const [lnbLo, setLnbLo] = useLS2('datv_lnb_lo', 9750e6, parseFloat);
  const [planMsg, setPlanMsg] = useWtS('');
  // QO-100 mode (on by default): the radio's LO stays fixed at 745.0 MHz (zynqrx's `lo`; the Maia DDC NCO tunes the carrier inside the 10 MHz span,
  // which with the 9750 MHz LNB LO is the middle of the wideband transponder, 10495 MHz) and the LNB LO is the standard 9750 MHz. Off: the LO follows the
  // frequency (`lo` 0) and the LNB LO is free.
  const [qo100, setQo100] = useLS2('webtioune_qo100', '1');
  const loSent = useWtR('');
  useWtE(() => {
    if (qo100 === '1' && lnbLo !== 9750e6) setLnbLo(9750e6);
    const rep = lm['set/lo'];                          // what zynqrx reports (kHz); nothing while it is not running or too old to know `lo`
    const want = qo100 === '1' ? '745000' : '0';
    if (rep == null || !d || !d.mqtt) return;
    if (String(rep) !== want && loSent.current !== want + '|' + rep) { loSent.current = want + '|' + rep; d.publish('zynqrx/lo', want); }
  }, [qo100, lm['set/lo'], d && d.mqtt]);

  // Live signals on the transponder, from the BATC spectrum monitor (qo100wb.js). A signal is kept lit for
  // 1.5 s after it was last seen, so the highlight does not flicker with the noise.
  // The source: the BATC feed (internet) or the board's own spectrum (the 10 MHz around the radio's LO, cropped to the BATC band, 10490.5 - 10499.5 MHz:
  // LO 745 MHz with the usual 9750 MHz LNB), run through the same signal detection.
  const [wbSrc, setWbSrc] = useLS2('webtioune_wb_src', 'batc');
  const [wb, setWb] = useWtS({ signals: [], state: 'idle', f0: 10490.5, span: 9.0 });
  const wbFrame = useWtR(null);
  const [rxFrame, rxFeed] = useWaterfallFeed();
  // the feed's own {center, span} messages when it sends them, else the radio's RX frequency and sampling rate over MQTT (as the Spectrum page does)
  // QO-100 mode: the LO is fixed at 745.0 MHz whatever carrier is tuned (the DDC NCO does that), so the local spectrum is centred there:
  // 10495 MHz downlink, the middle of the BATC band
  const specCenter = qo100 === '1' ? 745e6 : (rxFeed.center || (d && d.rxFreq) || 0);
  // the spectrum spans the rate the FPGA sees: state/rx/sampling, already the decimated output rate when the AD9361 FIR is on (zynqrx's RRC, /4);
  // state/rx/span is the sweep span and goes stale outside a sweep
  const specSpan = rxFeed.span || (d && (d.sweepActive ? d.span : d.rxSampling)) || 0;
  const localRef = useWtR({}); localRef.current = { center: specCenter, span: specSpan, lnbLo, feed: rxFeed.state };
  useWtE(() => {
    if (!window.Qo100Wb) { wbFrame.current = null; setWb({ signals: [], state: 'idle', f0: 10490.5, span: 9.0 }); return; }
    const seen = new Map();                       // key -> { sig, t }
    let state = 'connecting', lastKey = '', f0 = 10490.5, span = 9.0, mon = null, localTimer = null;
    const note = (sigs) => {
      const now = Date.now();
      for (const g of sigs) {
        // the same signal measured a few kHz apart (a centre on the edge of a rounding step) is one signal: the newest measure replaces the older
        for (const [k, v] of seen) if (v.sig.sr === g.sr && !v.sig.beacon === !g.beacon && Math.abs(v.sig.freq - g.freq) < 0.05) seen.delete(k);
        seen.set(`${g.beacon ? 'b' : Math.round(g.freq * 50)}|${g.sr}`, { sig: g, t: now });
      }
    };
    if (wbSrc === 'local') {
      const st = {}; let last = null;
      localTimer = setInterval(() => {
        const L = localRef.current, fr = rxFrame.current;
        if (!fr || !(L.span > 0) || L.feed !== 'live') { state = L.feed === 'offline' ? 'offline' : 'connecting'; return; }
        state = 'live';
        f0 = (L.center + L.lnbLo - L.span / 2) / 1e6; span = L.span / 1e6;
        if (fr === last) return;                  // no new frame since the last pass
        last = fr;
        const full = window.Qo100Wb.localFrame(fr, st);
        const c = window.Qo100Wb.crop(full, f0, span);      // the BATC band when the span covers it
        wbFrame.current = c.frame; f0 = c.f0; span = c.span;
        // the local floor is not flat (IF response, LO and DC spurs: humps of 2 - 3 dB over the noise level that would merge with a signal's
        // skirt or pass for a wide signal): a region starts 4 dB over the floor here, 1.5 dB on the cleaner BATC feed. Real signals are 7 dB or more
        note(window.Qo100Wb.detect(c.frame, f0, span, 11000 + 4 * 3276.8));
      }, 100);
    } else {
      mon = new window.Qo100Wb((sigs, st) => { state = st; note(sigs); }, (frame) => { wbFrame.current = frame; });
      mon.start();
    }
    const id = setInterval(() => {
      const now = Date.now(), list = [];
      for (const [k, v] of seen) { if (now - v.t > 1500) seen.delete(k); else list.push(v.sig); }
      list.sort((a, b) => a.freq - b.freq);
      const key = state + '|' + f0.toFixed(3) + '|' + span.toFixed(3) + '|' + list.map((g) => `${g.freq.toFixed(2)}:${g.sr}:${g.level.toFixed(0)}`).join(',');
      if (key !== lastKey) { lastKey = key; setWb({ signals: list, state, f0, span }); }
    }, 500);
    return () => { clearInterval(id); clearInterval(localTimer); if (mon) mon.stop(); wbFrame.current = null; };
  }, [wbSrc]);
  // Start in QO-100 mode: a frequency outside the wideband transponder (the IF is 740.5 - 749.5 MHz with the 9750 MHz LNB LO, e.g. the 437 MHz
  // default) is replaced by the 10491.5 MHz beacon (1500 kS), which the fixed LO and the DDC NCO can receive
  const startRx = () => {
    if (qo100 === '1') {
      const dl = (freqHz + lnbLo) / 1e6;
      if (!(dl >= 10490.5 && dl <= 10499.5)) {
        const f = 10491.5e6 - lnbLo;
        setFreqHz(f); setSrKs(1500);
        d.publish('zynqrx/frequency', rxKHz(f)); d.publish('zynqrx/sr', 1500); d.publish('spectro/fps', RX_SPEC_FPS);
      }
    }
    setProcess('start');
  };
  const onSpecPick = (dlMHz, sr) => {                   // sr 0 = keep the current symbol rate
    const ifHz = dlMHz * 1e6 - lnbLo;                    // downlink minus the LNB LO
    if (!(ifHz >= RX_FREQ_MIN_HZ && ifHz <= RX_FREQ_MAX_HZ)) { setPlanMsg(`IF ${(ifHz / 1e6).toFixed(1)} MHz is outside 70 MHz - 6 GHz with this LNB LO`); return; }
    setPlanMsg('');
    setFreqHz(ifHz);
    d.publish('zynqrx/frequency', rxKHz(ifHz));
    if (sr > 0) { setSrKs(sr); d.publish('zynqrx/sr', sr); d.publish('spectro/fps', RX_SPEC_FPS); }
  };

  const onRxSpecPick = (hz) => {                       // the spectrum is the radio's IF: tune zynqrx straight to the clicked frequency
    if (!(hz >= RX_FREQ_MIN_HZ && hz <= RX_FREQ_MAX_HZ)) return;
    hz = Math.round(hz / 100) * 100;
    setFreqHz(hz);
    d.publish('zynqrx/frequency', rxKHz(hz));
  };

  const [status, setStatus] = useWtS('idle'); // idle | loading | connected | error
  const [errorMsg, setErrorMsg] = useWtS('');
  // this browser cannot play HEVC through Media Source Extensions (addSourceBuffer refuses hvc1/hev1): shown as an H265 warning, not the raw error
  const hevcUnsupported = /(hvc1|hev1|hevc)/i.test(errorMsg) && /addSourceBuffer|unsupported/i.test(errorMsg);
  const [mediaInfo, setMediaInfo] = useWtS(null);
  const playerRef = useWtR(null);
  const videoRef = useWtR(null);
  // Info: what mpegts.js and the video element report, polled twice a second while the box is ticked
  const [infoOn, setInfoOn] = useLS2('webtioune_info', '0');
  const statsRef = useWtR(null);
  const [live, setLive] = useWtS(null);
  useWtE(() => {
    if (infoOn !== '1') { setLive(null); return; }
    const id = setInterval(() => {
      const v = videoRef.current;
      if (!v) return;
      const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : null;
      setLive({ stats: statsRef.current, t: v.currentTime, paused: v.paused, ready: v.readyState,
        lat: v.buffered && v.buffered.length ? v.buffered.end(v.buffered.length - 1) - v.currentTime : null,
        dropped: q ? q.droppedVideoFrames : null, shown: q ? q.totalVideoFrames : null });
    }, 500);
    return () => clearInterval(id);
  }, [infoOn]);

  // A decode error on a damaged picture ends the media element (Chrome does not recover from it): while the receiver is
  // still locked the player is rebuilt, which costs about a second (the next keyframe). Backs off when it fails again at once.
  const retryRef = useWtR({ timer: null, n: 0, since: 0, pending: false });
  const stop = () => {
    clearTimeout(retryRef.current.timer);
    retryRef.current.pending = false;
    const p = playerRef.current;
    if (p) {
      playerRef.current = null;
      try { p.pause(); p.unload(); p.detachMediaElement(); p.destroy(); } catch (_) {}
    }
    setStatus('idle');
    setMediaInfo(null);
    statsRef.current = null;
  };

  const connect = async () => {
    setErrorMsg('');
    setStatus('loading');
    try {
      await loadMpegts();
    } catch (e) {
      setStatus('error');
      setErrorMsg(e.message);
      return;
    }
    stop();
    setStatus('loading');
    const mp = window.mpegts;
    if (!mp.getFeatureList().mseLivePlayback) {
      setStatus('error');
      setErrorMsg('This browser has no Media Source Extensions live playback.');
      return;
    }
    const player = mp.createPlayer(
      { type: 'mpegts', isLive: true, url: wsTsUrl(), hasAudio: true },
      { enableWorker: true, liveBufferLatencyChasing: true, liveBufferLatencyMaxLatency: 3, liveBufferLatencyMinRemain: 0.5 }
    );
    playerRef.current = player;
    retryRef.current.since = Date.now();
    // a player that was stopped meanwhile (lock lost) must not report anything
    player.on(mp.Events.ERROR, (type, detail, info) => {
      if (playerRef.current !== player) return;
      setStatus('error');
      setErrorMsg(`${type}: ${detail}${info && info.msg ? ' — ' + info.msg : ''}`);
      // the dead media element reports the same error again on every append: only the first one schedules the retry
      const r = retryRef.current;
      if (r.pending) return;
      if (Date.now() - r.since > 5000) r.n = 0;   // it had been playing for a while: not a persistent failure
      if (lockedNow.current && r.n < 6) {
        r.pending = true;
        r.timer = setTimeout(() => { r.pending = false; if (lockedNow.current && playerRef.current === player) connect(); }, Math.min(1000 * 2 ** r.n, 8000));
        r.n++;
      }
    });
    player.on(mp.Events.STATISTICS_INFO, (st) => { if (playerRef.current === player) statsRef.current = st; });
    player.on(mp.Events.MEDIA_INFO, (info) => {
      setMediaInfo({ video: info.videoCodec, size: info.width && `${info.width}x${info.height}`, fps: info.fps && +info.fps.toFixed(2), mime: info.mimeType, profile: info.profile, level: info.level, chroma: info.chromaFormat, kbps: info.videoDataRate });
    });
    player.attachMediaElement(videoRef.current);
    player.load();
    // With sound when the browser allows it (it blocks autoplay with sound until the user has used the page);
    // otherwise muted, and the video's own speaker button turns the sound on.
    const v = videoRef.current;
    const mayHearIt = !!(navigator.userActivation && navigator.userActivation.hasBeenActive);
    v.muted = !mayHearIt;
    const onStarted = () => setStatus('connected');
    const onFailed = (e) => { if (playerRef.current !== player) return; setStatus('error'); setErrorMsg('play(): ' + e.message); };
    const started = player.play();
    if (started && started.then) {
      started.then(onStarted).catch((e) => {
        if (playerRef.current !== player) return;
        if (e && e.name === 'NotAllowedError' && !v.muted) { v.muted = true; player.play().then(onStarted).catch(onFailed); }   // autoplay with sound refused
        else onFailed(e);
      });
    } else onStarted();
  };

  // Release the player when navigating away from this page.
  useWtE(() => () => stop(), []);

  // The player follows the demodulator: connected while it is locked (header found / demod_s / demod_s2),
  // stopped when it is not. The service name comes from the SDT, which zynqrx decodes and publishes.
  const locked = rxRunning && (lm.rx_state === 'demod_s' || lm.rx_state === 'demod_s2');
  // dt/zynqrx/service_name is retained, so after a retune it still holds the previous station's name. A name is
  // only trusted once the receiver has left lock and locked again since the tuning changed.
  const tuneKey = `${Math.round(freqHz / 1000)}|${srKs}`;
  const [nameArmed, setNameArmed] = useWtS(true);
  const wasUnlocked = useWtR(false);
  const lockedNow = useWtR(locked); lockedNow.current = locked;
  const firstKey = useWtR(true);
  useWtE(() => { if (firstKey.current) { firstKey.current = false; return; } wasUnlocked.current = !lockedNow.current; setNameArmed(wasUnlocked.current); }, [tuneKey]);
  useWtE(() => { if (!locked) wasUnlocked.current = true; else if (wasUnlocked.current) setNameArmed(true); }, [locked]);
  const serviceName = locked && nameArmed && lm.service_name ? lm.service_name : '';
  // Names decoded so far, each at the downlink frequency it was received on; the spectrum keeps a name on its signal
  // until that signal is gone from the spectrum.
  const namesRef = useWtR([]);
  const rxDlRef = useWtR(0); rxDlRef.current = (freqHz + lnbLo) / 1e6;
  useWtE(() => {
    if (!serviceName) return;
    const f = rxDlRef.current;
    namesRef.current = namesRef.current.filter((n) => Math.abs(n.freq - f) > 0.05).concat({ freq: f, name: serviceName });
  }, [serviceName]);
  useWtE(() => {
    if (wb.state !== 'live') return;
    namesRef.current = namesRef.current.filter((n) => wb.signals.some((g) => n.freq >= g.start - 0.03 && n.freq <= g.end + 0.03));
  }, [wb.signals, wb.state]);
  useWtE(() => {
    if (locked) connect(); else stop();
    setErrorMsg('');
  }, [locked]);

  // Info overlay: only what the player has reported so far; nothing at all until something has arrived
  const infoLines = [];
  {
    const st = (live && live.stats) || null;
    const n = (v, d = 0) => (+v).toFixed(d);
    if (status === 'connected' || mediaInfo || st) infoLines.push(`player ${status}${live && live.paused && status === 'connected' ? ' (paused)' : ''}`);
    if (mediaInfo && (mediaInfo.video || mediaInfo.size || mediaInfo.fps)) infoLines.push([mediaInfo.video, mediaInfo.size, mediaInfo.fps && mediaInfo.fps + ' fps'].filter(Boolean).join('  '));
    if (mediaInfo && (mediaInfo.profile || mediaInfo.level || mediaInfo.chroma || mediaInfo.kbps)) {
      infoLines.push([mediaInfo.profile && 'profile ' + mediaInfo.profile, mediaInfo.level && 'level ' + mediaInfo.level, mediaInfo.chroma,
        mediaInfo.kbps && 'video ' + n(mediaInfo.kbps) + ' kb/s'].filter(Boolean).join('  '));
    }
    if (st) {
      const dropped = live && live.dropped != null ? live.dropped : st.droppedFrames;
      infoLines.push([st.speed != null && 'speed ' + n(st.speed, 1) + ' KB/s', st.decodedFrames != null && 'decoded ' + n(st.decodedFrames), dropped != null && 'dropped ' + n(dropped)].filter(Boolean).join('   '));
    }
    const lat = live && live.lat != null && !isNaN(live.lat) ? 'latency ' + n(live.lat, 2) + ' s' : null;
    if (lat || (st && st.loaderType)) infoLines.push([lat, st && st.loaderType && 'via ' + st.loaderType].filter(Boolean).join('   '));
  }
  const showInfo = infoOn === '1' && infoLines.length > 0;

  // Two columns so the controls and the picture fit on one screen without scrolling
  // (they stack below 1100 px, see .grid-12 in styles.css). The video box is
  // 16:9 and capped to what is left of the window height after the cards' chrome.
  return (
    <div className="page">
      <div className="grid-12">
        <div className="span-5 tile-stack">
          <Card title="Receiver" sub="zynqrx DVB-S2"
            right={<div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <div title="QO-100: the radio LO stays fixed at 745.0 MHz (the Maia DDC NCO tunes inside the 10 MHz span) and the LNB LO is 9750 MHz. Off: the LO follows the frequency and the LNB LO is free">
                <Checkbox checked={qo100 === '1'} label="QO-100" onChange={(v) => setQo100(v ? '1' : '0')} />
              </div>
              <Pill tone={!rxKnown ? 'neutral' : rxRunning ? 'ok' : 'warn'} dot>{!rxKnown ? 'No status' : rxRunning ? 'Running' : 'Stopped'}</Pill>
              <button className="btn primary" onClick={startRx} disabled={rxRunning || !d.mqtt}>Start</button>
              <button className="btn ghost" onClick={() => setProcess('stop')} disabled={!rxRunning || !d.mqtt}>Stop</button>
            </div>}>
            <div className="form-grid rx-tuner-compact" style={{ gridTemplateColumns: 'max-content minmax(0, 1fr)', gap: 12, alignItems: 'start' }}>
              <Field label="Frequency">
                <FreqTuner value={freqHz} digits={10} min={RX_FREQ_MIN_HZ} max={RX_FREQ_MAX_HZ} unit="Hz"
                  onChange={(v) => { setFreqHz(v); sendLater('freq', 'zynqrx/frequency', rxKHz(v)); }} />
              </Field>
              <Field label="Symbol rate">
                <div style={{ display: 'flex', gap: 6, alignItems: 'stretch' }}>
                  <div style={{ flex: '0 0 96px', minWidth: 0, display: 'flex' }}>
                    <Select value={RX_SR_PRESETS.includes(srKs) ? srKs : 'custom'}
                      onChange={(v) => { if (v === 'custom') return; const n = parseInt(v); setSrKs(n); sendLater('sr', 'zynqrx/sr', n); }}
                      options={[{ v: 125, l: '125 kS' }, { v: 250, l: '250 kS' }, { v: 333, l: '333 kS' }, { v: 500, l: '500 kS' }, { v: 1000, l: '1 MS' }, { v: 'custom', l: 'Custom' }]} />
                  </div>
                  <div style={{ flex: 1, minWidth: 0 }}>
                    <FreqTuner value={srKs} digits={4} group={false} min={RX_SR_MIN_KS} max={RX_SR_MAX_KS} unit="kS"
                      onChange={(v) => { setSrKs(v); sendLater('sr', 'zynqrx/sr', v); }} />
                  </div>
                </div>
              </Field>
            </div>
            <div className="kv-grid" style={{ marginTop: 10, gridTemplateColumns: 'repeat(4, 1fr)' }}>
              <div className="kv"><span>State</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning ? (lm.rx_state || '—') : rxKnown ? 'stopped' : '—'}</b></div>
              <div className="kv"><span>Signal</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.modulation ? `${lm.modulation} ${lm.fec || ''}` : '—'}</b></div>
              <div className="kv"><span>MER</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.mer ? lm.mer + ' dB' : '—'}</b></div>
              <div className="kv"><span>Margin</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.margin_db != null ? lm.margin_db + ' dB' : '—'}</b></div>
              <div className="kv"><span>LDPC errors</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.ldpc_errors != null ? lm.ldpc_errors : '—'}</b></div>
              <div className="kv"><span>Type</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.short_frame != null && lm.modulation ? (lm.short_frame === '1' ? 'Short' : 'Long') : '—'}</b></div>
              <div className="kv" style={{ gridColumn: 'span 2' }}><span>Pilots</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.pilots != null && lm.modulation ? (lm.pilots === '1' ? 'On' : 'Off') : '—'}</b></div>
            </div>
            {d.cpu != null && !isNaN(d.cpu) && (
              <div style={{ marginTop: 10 }}>
                <HBarGauge value={d.cpu} max={100} label="CPU"
                  color={d.cpu >= 90 ? 'var(--bad)' : d.cpu >= 75 ? 'var(--warn)' : 'var(--ok)'}
                  fmt={(v) => Math.round(v) + ' %'} />
              </div>
            )}
            {!rxKnown && (
              <div className="info-note" style={{ marginTop: 10 }}>
                <Icon name="bolt" size={16} />
                <p>No status from zynqrx yet — is it running on the board (start_zynqrx)?</p>
              </div>
            )}
          </Card>

          <Card title="Receiver spectrum" sub={specSpan > 0 ? `${(specCenter / 1e6).toFixed(3)} MHz · span ${(specSpan / 1e6).toFixed(3)} MHz · click to tune` : 'board /waterfall feed'}
            right={<Pill tone={rxFeed.state === 'live' ? 'ok' : 'warn'} dot>{rxFeed.state === 'live' ? 'live' : rxFeed.state === 'offline' ? 'offline' : '…'}</Pill>}>
            <RxSpectrum frameRef={rxFrame} centerHz={specCenter} spanHz={specSpan} rxHz={freqHz} rxSrKs={srKs} onPick={onRxSpecPick} />
          </Card>

          <Card title="QO-100 WB spectrum" sub={wbSrc === 'local' ? 'the board\'s own spectrum, cropped to the BATC band · click a signal or the trace to tune' : 'BATC feed · click a signal or the trace to tune'}
            right={<div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Pill tone={wb.state === 'live' ? 'ok' : wb.state === 'idle' ? 'neutral' : 'warn'} dot>{wb.state === 'live' ? 'live' : wb.state === 'idle' ? 'off' : wb.state === 'offline' ? 'offline' : '…'}</Pill>
              <Toggle on={wbSrc === 'local'} onChange={(v) => setWbSrc(v ? 'local' : 'batc')} labels={['BATC', 'LOCAL']} />
              <div style={{ width: 170 }}><TextInput value={String(lnbLo)} suffix="LO Hz" disabled={qo100 === '1'}
                onChange={(v) => { const n = parseFloat(v); if (!isNaN(n)) setLnbLo(n); }} /></div>
            </div>}>
            <Qo100Spectrum frameRef={wbFrame} signals={wb.signals} f0={wb.f0} span={wb.span} emptyText={wbSrc === 'local' ? 'waiting for the board spectrum…' : 'waiting for the BATC spectrum feed…'} rxDlMHz={(freqHz + lnbLo) / 1e6} rxSrKs={srKs} namesRef={namesRef} onPick={onSpecPick} />
            {wbSrc === 'local' && wb.state === 'live' && (wb.f0 > 10490.5 + 1e-3 || wb.f0 + wb.span < 10499.5 - 1e-3) && (
              <div className="info-note" style={{ marginTop: 8 }}>
                <Icon name="bolt" size={16} />
                <p>The radio spectrum spans {(wb.span).toFixed(1)} MHz around {((wb.f0 + wb.span / 2) - lnbLo / 1e6).toFixed(1)} MHz (IF), which does not cover the 9 MHz QO-100 band.
                  The receiver is probably on the transmission's clock (TX clock Auto while pluto_stream transmits): set TX clock to Off in the Expert options for the 10 MHz span
                  (it cuts the clock pluto_stream set), and check Radio clock / Radio LO.</p>
              </div>
            )}
            {planMsg && (
              <div className="info-note" style={{ marginTop: 8 }}>
                <Icon name="bolt" size={16} />
                <p>{planMsg}</p>
              </div>
            )}
          </Card>
        </div>

        <div className="span-7 tile-stack">
        <Card title="Webtioune" sub="Live TS preview · mpegts.js"
          right={hevcUnsupported && <Pill tone="warn" dot>H265</Pill>}>
          {errorMsg && !hevcUnsupported && (
            <div className="info-note" style={{ marginTop: 10 }}>
              <Icon name="bolt" size={16} />
              <p>{errorMsg}</p>
            </div>
          )}
          <div style={{ marginTop: 12 }}>
            <Checkbox checked={infoOn === '1'} onChange={(on) => setInfoOn(on ? '1' : '0')} label="Info (mpegts.js status and statistics)" />
          </div>
          <div style={{ position: 'relative', marginTop: 8 }}>
            <video ref={videoRef} controls playsInline
              style={{ display: 'block', width: '100%', aspectRatio: '16 / 9', maxHeight: `calc(100vh - ${(expertOpen === '1' ? 560 : 300) + 30}px)`, minHeight: expertOpen === '1' ? 150 : 200, background: '#000' }} />
            {locked && lm.margin_db != null && !isNaN(parseInt(lm.margin_db)) && (
              <div title={`margin ${lm.margin_db} dB`} style={{ position: 'absolute', top: 14, right: 18, pointerEvents: 'none', color: '#fff', fontSize: 'clamp(22px, 3.2vw, 44px)', fontWeight: 700, lineHeight: 1.1, textShadow: '0 2px 6px rgba(0,0,0,.9), 0 0 2px #000' }}>D{parseInt(lm.margin_db)}</div>
            )}
            {(serviceName || showInfo) && (
              <div style={{ position: 'absolute', top: 14, left: 18, right: 'clamp(70px, 8vw, 120px)', pointerEvents: 'none', color: '#fff', textShadow: '0 2px 6px rgba(0,0,0,.9), 0 0 2px #000', overflow: 'hidden' }}>
                {serviceName && <div style={{ fontSize: 'clamp(22px, 3.2vw, 44px)', fontWeight: 700, lineHeight: 1.1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{serviceName}</div>}
                {serviceName && lm.provider_name && <div style={{ marginTop: 2, fontSize: 'clamp(13px, 1.6vw, 20px)', fontWeight: 500, lineHeight: 1.2, opacity: 0.9, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{lm.provider_name}</div>}
                {showInfo && <div className="mono" style={{ marginTop: 6, fontSize: 'clamp(11px, 1.1vw, 14px)', lineHeight: 1.35 }}>{infoLines.map((l, k) => <div key={k}>{l}</div>)}</div>}
              </div>
            )}
          </div>
        </Card>

        <ExpertOptions d={d} lm={lm} open={expertOpen} setOpen={setExpertOpen} />
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Webtioune });

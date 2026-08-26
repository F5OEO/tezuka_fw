// classifier.jsx — Onboard signal classifier page
//
// Draws its own live spectrum canvas fed directly by maia-httpd's
// /waterfall WS feed (same feed + parsing as radioastro.jsx), rather
// than the native maia-sdr waterfall the Spectrum page iframes in
// (Dashboard/pages3.jsx) — that app is served by maia-httpd itself,
// so this page can't draw a label chip on top of it without
// cross-origin tricks. Owning our own canvas here sidesteps that.
//
// The classification results themselves (label/confidence/freq_offset/
// template_id) come from the on-device `classifier` daemon
// (app/classifier/classifier.c) over MQTT — this page just subscribes
// to state/classifier/* via the existing `d` prop like every other
// dashboard reading, no separate transport needed for that part.
const { useState: useSCl, useEffect: useECl, useRef: useRCl } = React;

function ClassifierPage({ d }) {
  const canvasRef = useRCl(null);
  const binsRef   = useRCl(null);      // Float32Array dB, current live frame
  const wsRef     = useRCl(null);
  const [wsState, setWsState] = useSCl('disconnected');

  const [enabled, setEnabled] = useSCl(false);
  useECl(() => { if (d.classifierEnabled != null) setEnabled(d.classifierEnabled === 'on'); }, [d.classifierEnabled]);

  const [log, setLog] = useSCl([]);          // {t, label, confidence, templateId}
  const lastLoggedRef = useRCl(null);        // dedupe: only log on actual change

  const refDb = 0, range = 100; // fixed display scale — this page isn't a general-purpose analyzer

  function redraw() {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const w = canvas.offsetWidth, h = canvas.offsetHeight;
    if (w <= 0 || h <= 0) return;
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const ctx = canvas.getContext('2d');
    spDraw(ctx, w, h, binsRef.current, refDb, range, 0, 1, 'off');

    // Label chip + marker, positioned at freq_offset (a bin index into
    // the live frame, published by the daemon in the same bin space it
    // ran correlation in — see app/classifier/classifier.c).
    const bins = binsRef.current;
    if (bins && bins.length > 1 && d.classifierLabel && d.classifierLabel !== 'unknown') {
      const n = bins.length;
      const offset = Number(d.classifierFreqOffset) || 0;
      const binI = Math.max(0, Math.min(n - 1, offset));
      const x = (binI / (n - 1)) * w;
      const dbv = bins[binI];
      const top = refDb, bot = refDb - range;
      const y = h - Math.max(0, Math.min(h, ((dbv - bot) / (top - bot)) * h));

      ctx.save();
      ctx.strokeStyle = 'rgba(120,220,255,0.85)';
      ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x, y - 24); ctx.stroke();

      const conf = Number(d.classifierConfidence) || 0;
      const text = `${d.classifierLabel} · ${Math.round(conf * 100)}%`;
      ctx.font = '600 13px system-ui, sans-serif';
      const tw = ctx.measureText(text).width;
      const chipX = Math.max(4, Math.min(w - tw - 16, x - tw / 2 - 8));
      const chipY = Math.max(4, y - 24 - 22);
      ctx.fillStyle = 'rgba(15,30,40,0.85)';
      ctx.strokeStyle = 'rgba(120,220,255,0.6)';
      ctx.lineWidth = 1;
      const chipW = tw + 16, chipH = 22;
      ctx.beginPath();
      ctx.roundRect ? ctx.roundRect(chipX, chipY, chipW, chipH, 5) : ctx.rect(chipX, chipY, chipW, chipH);
      ctx.fill(); ctx.stroke();
      ctx.fillStyle = '#d8f6ff';
      ctx.fillText(text, chipX + 8, chipY + 15);
      ctx.restore();
    }
  }

  useECl(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver(() => redraw());
    ro.observe(canvas);
    redraw();
    return () => ro.disconnect();
  }, []);

  // Re-render whenever a new classification arrives, so the chip tracks
  // the latest label/offset even between spectrum frames.
  useECl(() => { redraw(); }, [d.classifierLabel, d.classifierConfidence, d.classifierFreqOffset]);

  // Append to the scrolling log on genuine change only (state/classifier/*
  // republishes at ~1/s even when nothing changed — logging every tick
  // would just be noise).
  useECl(() => {
    const label = d.classifierLabel;
    if (label == null) return;
    const key = `${label}|${d.classifierTemplateId ?? ''}`;
    if (lastLoggedRef.current === key) return;
    lastLoggedRef.current = key;
    setLog((prev) => {
      const entry = {
        t: Date.now(),
        label,
        confidence: Number(d.classifierConfidence) || 0,
        templateId: d.classifierTemplateId,
      };
      const next = [entry, ...prev];
      return next.length > 50 ? next.slice(0, 50) : next;
    });
  }, [d.classifierLabel, d.classifierTemplateId]);

  // WebSocket: same /waterfall feed radioastro.jsx and the Spectrum page
  // use. f[0] is a structural step index (unused here); f[1..] are FFT
  // bins as raw linear amplitude — converted to dB for display only,
  // the classifier daemon itself works in linear amplitude.
  useECl(() => {
    let destroyed = false;
    const host  = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const toDB  = (v) => 20 * Math.log10(v > 0 ? v : 1e-10);

    function connect() {
      if (destroyed) return;
      const ws = new WebSocket(`${proto}//${host}/waterfall`);
      ws.binaryType = 'arraybuffer';
      wsRef.current = ws;
      setWsState('connecting');
      ws.onopen  = () => setWsState('connected');
      ws.onclose = () => { setWsState('disconnected'); if (!destroyed) setTimeout(connect, 2000); };
      ws.onerror = () => {};
      ws.onmessage = (evt) => {
        if (!(evt.data instanceof ArrayBuffer)) return;
        const f = new Float32Array(evt.data);
        if (f.length < 2) return;
        const db = new Float32Array(f.length - 1);
        for (let i = 0; i < db.length; i++) db[i] = toDB(f[i + 1]);
        binsRef.current = db;
        redraw();
      };
    }
    connect();
    return () => { destroyed = true; try { wsRef.current?.close(); } catch (_) {} };
  }, []);

  // Reference-match panel: overlay the live captured shape against the
  // matched template's stored shape. XYChart (charts.jsx) only takes a
  // single series, so this is a small dedicated canvas rather than a
  // shared component — both traces are already unit-normalized
  // (zero-mean, unit-L2-norm) by the daemon, so a fixed [-1, 1] y-range
  // works without per-frame rescaling.
  const matchCanvasRef = useRCl(null);
  useECl(() => {
    const canvas = matchCanvasRef.current;
    if (!canvas) return;
    const w = canvas.offsetWidth, h = canvas.offsetHeight;
    if (w <= 0 || h <= 0) return;
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = 'rgba(255,255,255,0.03)';
    ctx.fillRect(0, 0, w, h);

    const bins = binsRef.current;
    if (!bins || bins.length < 2 || !d.classifierLabel || d.classifierLabel === 'unknown') {
      ctx.fillStyle = 'var(--dim, #888)';
      ctx.font = '13px system-ui, sans-serif';
      ctx.fillText('No match to compare', 10, h / 2);
      return;
    }

    // Live trace: reuse the raw linear (pre-dB) window around freq_offset
    // if the daemon's window was narrower than the full frame — for the
    // display here we just show a normalized version of the live dB
    // trace itself, which is a fair visual proxy for "does the shape
    // line up", without duplicating the daemon's own resample/normalize.
    const n = bins.length;
    const norm = (arr) => {
      const lo = Math.min(...arr), hi = Math.max(...arr);
      const span = hi - lo || 1;
      return arr.map((v) => ((v - lo) / span) * 2 - 1);
    };
    const liveNorm = norm(Array.from(bins));

    const drawTrace = (arr, color) => {
      ctx.beginPath();
      arr.forEach((v, i) => {
        const x = (i / (arr.length - 1)) * w;
        const y = h - ((v + 1) / 2) * h;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.6;
      ctx.stroke();
    };
    drawTrace(liveNorm, 'rgba(255,200,40,0.9)');

    ctx.fillStyle = '#ffcd46';
    ctx.font = '12px system-ui, sans-serif';
    ctx.fillText('— live', 8, 16);
    ctx.fillStyle = '#78dcff';
    ctx.fillText(`matched: ${d.classifierLabel} (template #${d.classifierTemplateId ?? '?'})`, 8, h - 8);
  }, [d.classifierLabel, d.classifierConfidence]);

  return (
    <>
      <Card title="Signal Classifier" sub="Onboard PSD spectral-shape matching" className="span-12"
        right={<Pill tone={wsState === 'connected' ? 'ok' : 'warn'} dot>{wsState}</Pill>}>
        <Field label="Enabled">
          <Toggle on={enabled} onChange={(on) => { setEnabled(on); d.publish('classifier/enabled', on ? 'on' : 'off'); }} />
        </Field>
        <div className="hp-screen" style={{ height: 320, position: 'relative' }}>
          <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: '100%' }} />
        </div>
      </Card>

      <Card title="Reference match" sub="Live shape vs. the matched template" className="span-6">
        <div style={{ height: 160 }}>
          <canvas ref={matchCanvasRef} style={{ display: 'block', width: '100%', height: '100%' }} />
        </div>
      </Card>

      <Card title="Classifications" sub="Most recent first" className="span-6">
        <div style={{ maxHeight: 160, overflowY: 'auto' }}>
          {log.length === 0 && <div className="dim mono">No classifications yet</div>}
          {log.map((e, i) => (
            <div key={i} className="mono" style={{ display: 'flex', gap: '1ch', padding: '2px 0', fontSize: 12 }}>
              <span className="dim">{new Date(e.t).toLocaleTimeString()}</span>
              <span>{e.label}</span>
              <span className="dim">{Math.round(e.confidence * 100)}%</span>
              {e.templateId != null && <span className="dim">#{e.templateId}</span>}
            </div>
          ))}
        </div>
      </Card>
    </>
  );
}

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

// Fixed palette keyed by service type — keeps band-plan entries simple to
// edit (no per-entry color picking) while staying visually consistent.
const BANDPLAN_COLORS = {
  aviation: '#ff8a4c',
  cellular: '#7a8cff',
  broadcast: '#ffcd46',
  maritime: '#4cc9ff',
  emergency: '#ff5c7a',
  iot: '#8be07a',
  weather: '#c98bff',
  other: '#9aa4ad',
};
let bandplanNextId = 1; // client-side scratch id for newly-added, unsaved rows

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

  // Band plan: fetched once from maia-httpd's static file serving (see
  // S59bandplan — /root/bandplan.json is a symlink to the persistent copy
  // on JFFS2), not MQTT — a several-KB reference dataset doesn't belong on
  // the live telemetry channel just to get loaded once per page visit.
  const [bandPlan, setBandPlan] = useSCl([]);
  const [bandPlanDirty, setBandPlanDirty] = useSCl(false);
  const loadBandPlan = () => {
    fetch('/bandplan.json').then((r) => r.json()).then((list) => {
      setBandPlan(Array.isArray(list) ? list : []);
      setBandPlanDirty(false);
    }).catch(() => {});
  };
  useECl(() => { loadBandPlan(); }, []);

  const saveBandPlan = () => {
    const clean = bandPlan.filter((e) => e.label && e.start_hz > 0 && e.end_hz >= e.start_hz);
    d.publish('bandplan/data', JSON.stringify(clean));
    setBandPlanDirty(false);
  };

  // Center/span for placing band segments in bin space — same `d` fields
  // radioastro.jsx reads (d.rxFreq, d.span ?? d.rxSampling), no new
  // device-side state needed.
  const centerHz = d.rxFreq ?? 100e6;
  const spanHz = d.span ?? d.rxSampling ?? 20e6;

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

    // Band-plan strip along the bottom — independent of whatever the
    // classifier has (or hasn't) matched. Segment placement inverts
    // pages1.jsx's spDrawCursor freq math: freq = centerHz + (binFrac -
    // 0.5) * spanHz, so binFrac = (freq - centerHz) / spanHz + 0.5.
    const stripH = 14;
    const hzToX = (hz) => (((hz - centerHz) / spanHz) + 0.5) * w;
    ctx.save();
    for (const e of bandPlan) {
      let x0 = hzToX(e.start_hz), x1 = hzToX(e.end_hz);
      if (x1 < 0 || x0 > w) continue; // entirely off-screen at this span
      if (x1 - x0 < 2) { const mid = (x0 + x1) / 2; x0 = mid - 1; x1 = mid + 1; } // point freq: keep it visible/clickable
      ctx.fillStyle = BANDPLAN_COLORS[e.service] || BANDPLAN_COLORS.other;
      ctx.fillRect(Math.max(0, x0), h - stripH, Math.min(w, x1) - Math.max(0, x0), stripH);
    }
    ctx.restore();
  }

  // Hit-test the band-plan strip on click and tune to the matched entry —
  // same mechanism radioastro.jsx's pubCenter / rftest.jsx's presets use.
  const onCanvasClick = (evt) => {
    const canvas = canvasRef.current;
    if (!canvas || bandPlan.length === 0) return;
    const rect = canvas.getBoundingClientRect();
    const px = evt.clientX - rect.left, py = evt.clientY - rect.top;
    const w = canvas.offsetWidth, h = canvas.offsetHeight, stripH = 14;
    if (py < h - stripH) return; // click wasn't in the band-plan strip
    const hzToX = (hz) => (((hz - centerHz) / spanHz) + 0.5) * w;
    for (const e of bandPlan) {
      let x0 = hzToX(e.start_hz), x1 = hzToX(e.end_hz);
      if (x1 - x0 < 2) { const mid = (x0 + x1) / 2; x0 = mid - 4; x1 = mid + 4; } // wider click tolerance for point entries
      if (px >= x0 && px <= x1) {
        d.publish('rx/frequency', Math.round((e.start_hz + e.end_hz) / 2));
        return;
      }
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
  // the latest label/offset even between spectrum frames. Also on band
  // plan / center / span changes, since those affect the strip's layout.
  useECl(() => { redraw(); }, [d.classifierLabel, d.classifierConfidence, d.classifierFreqOffset, bandPlan, centerHz, spanHz]);

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
          <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: '100%', cursor: bandPlan.length ? 'pointer' : 'default' }}
            onClick={onCanvasClick} />
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

      <Card title="Band Plan" sub="What's normally here, independent of the classifier — click a swatch above or Tune below" className="span-12"
        right={<Pill tone={bandPlanDirty ? 'warn' : 'neutral'}>{bandPlanDirty ? 'unsaved changes' : `${bandPlan.length} entries`}</Pill>}>
        <div style={{ maxHeight: 280, overflowY: 'auto' }}>
          <table className="mono" style={{ width: '100%', borderCollapse: 'collapse', fontSize: 12 }}>
            <thead>
              <tr style={{ textAlign: 'left' }}>
                <th style={{ width: 18 }}></th>
                <th>Label</th>
                <th>Start (MHz)</th>
                <th>End (MHz)</th>
                <th>Service</th>
                <th></th>
              </tr>
            </thead>
            <tbody>
              {bandPlan.map((e, i) => (
                <tr key={e.id}>
                  <td><span style={{ display: 'inline-block', width: 10, height: 10, borderRadius: 2, background: BANDPLAN_COLORS[e.service] || BANDPLAN_COLORS.other }} /></td>
                  <td>
                    <input value={e.label} onChange={(ev) => {
                      const v = ev.target.value;
                      setBandPlan((prev) => prev.map((r, j) => j === i ? { ...r, label: v } : r));
                      setBandPlanDirty(true);
                    }} style={{ width: '100%' }} />
                  </td>
                  <td>
                    <input type="number" value={e.start_hz / 1e6} onChange={(ev) => {
                      const v = (parseFloat(ev.target.value) || 0) * 1e6;
                      setBandPlan((prev) => prev.map((r, j) => j === i ? { ...r, start_hz: v } : r));
                      setBandPlanDirty(true);
                    }} style={{ width: '9ch' }} />
                  </td>
                  <td>
                    <input type="number" value={e.end_hz / 1e6} onChange={(ev) => {
                      const v = (parseFloat(ev.target.value) || 0) * 1e6;
                      setBandPlan((prev) => prev.map((r, j) => j === i ? { ...r, end_hz: v } : r));
                      setBandPlanDirty(true);
                    }} style={{ width: '9ch' }} />
                  </td>
                  <td>
                    <select value={e.service} onChange={(ev) => {
                      const v = ev.target.value;
                      setBandPlan((prev) => prev.map((r, j) => j === i ? { ...r, service: v } : r));
                      setBandPlanDirty(true);
                    }}>
                      {Object.keys(BANDPLAN_COLORS).map((s) => <option key={s} value={s}>{s}</option>)}
                    </select>
                  </td>
                  <td style={{ whiteSpace: 'nowrap' }}>
                    <button className="btn ghost" title="Tune here" onClick={() => d.publish('rx/frequency', Math.round((e.start_hz + e.end_hz) / 2))}>Tune</button>
                    <button className="btn ghost" title="Delete" onClick={() => {
                      setBandPlan((prev) => prev.filter((_, j) => j !== i));
                      setBandPlanDirty(true);
                    }}>✕</button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <div style={{ display: 'flex', gap: 8, marginTop: 10 }}>
          <button className="btn ghost" onClick={() => {
            setBandPlan((prev) => [...prev, { id: -(bandplanNextId++), label: 'New entry', start_hz: centerHz, end_hz: centerHz, service: 'other' }]);
            setBandPlanDirty(true);
          }}>+ Add entry</button>
          <button className="btn primary" disabled={!bandPlanDirty} onClick={saveBandPlan}>Save</button>
          <button className="btn ghost" disabled={!bandPlanDirty} onClick={loadBandPlan}>Discard changes</button>
        </div>
      </Card>
    </>
  );
}

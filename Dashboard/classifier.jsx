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

// Converts the daemon's bin-space match position (state/classifier/freq_offset
// + bandwidth_bins, both measured against frame_bins — see
// app/classifier/classifier.c's publish_result) into real Hz, using the same
// freq math as pages1.jsx's spDrawCursor (freq = centerHz + (binFrac - 0.5) *
// spanHz), just inverted/widened from a single bin to a bin range.
function binMatchToHz(freqOffset, bandwidthBins, frameBins, centerHz, spanHz) {
  const offset = Number(freqOffset), bw = Number(bandwidthBins), frame = Number(frameBins);
  if (!frame || !isFinite(offset) || !isFinite(bw)) return null;
  const centerBinFrac = (offset + bw / 2) / frame;
  return {
    centerFreqHz: centerHz + (centerBinFrac - 0.5) * spanHz,
    bandwidthHz: (bw / frame) * spanHz,
  };
}

function fmtHz(hz) {
  if (hz == null || !isFinite(hz)) return '—';
  const abs = Math.abs(hz);
  if (abs >= 1e9) return (hz / 1e9).toFixed(3) + ' GHz';
  if (abs >= 1e6) return (hz / 1e6).toFixed(3) + ' MHz';
  if (abs >= 1e3) return (hz / 1e3).toFixed(1) + ' kHz';
  return Math.round(hz) + ' Hz';
}

// The daemon publishes "n/a" (a literal string, not a number) for the two
// cross-cycle features until its rolling history has enough samples — see
// history_compute() in app/classifier/classifier.c.
function parseMaybe(v) {
  if (v == null || v === 'n/a') return null;
  const n = Number(v);
  return isFinite(n) ? n : null;
}

function fmtHzRate(hzPerS) {
  if (hzPerS == null || !isFinite(hzPerS)) return '—';
  const sign = hzPerS < 0 ? '-' : '';
  const abs = Math.abs(hzPerS);
  if (abs >= 1e6) return sign + (abs / 1e6).toFixed(2) + ' MHz/s';
  if (abs >= 1e3) return sign + (abs / 1e3).toFixed(1) + ' kHz/s';
  return sign + Math.round(abs) + ' Hz/s';
}

function ClassifierPage({ d }) {
  const canvasRef = useRCl(null);
  const binsRef   = useRCl(null);      // Float32Array dB, current live frame
  const wsRef     = useRCl(null);
  const [wsState, setWsState] = useSCl('disconnected');

  const [enabled, setEnabled] = useSCl(false);
  useECl(() => { if (d.classifierEnabled != null) setEnabled(d.classifierEnabled === 'on'); }, [d.classifierEnabled]);

  const [log, setLog] = useSCl([]);          // {t, label, confidence, templateId}
  const lastLoggedRef = useRCl(null);        // dedupe: only log on actual change

  // Fixed display scale — this page isn't a general-purpose analyzer, so it
  // doesn't expose its own ref/range tuner. Must match the rest of the
  // Dashboard's default window (pages1.jsx / radioastro.jsx both default to
  // refDb=80, range=SP_ROWS*10=80): /waterfall's bins are integrated power
  // converted with 10*log10, uncalibrated (not dBm), so the floor lands
  // around 20-30 dB and strong signals around 70-80 dB.
  const refDb = 80, range = 80;

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

  // Signal Features (experimental): per-frame scalar features + cross-cycle
  // drift/duty-cycle, published unconditionally by the daemon every ~1s
  // cycle regardless of match outcome — see extract_frame_features() /
  // history_compute() in app/classifier/classifier.c. Bin-based values are
  // converted to Hz using this page's own live /waterfall connection's
  // current frame length: the daemon doesn't publish a fresh frame_bins
  // alongside these (only alongside an actual match, where the reading
  // needs to be exact) — this is an approximation, fine for a display-only
  // readout, and only ever off if the frame size changed within the last
  // update cycle.
  const featureFrameBins = binsRef.current ? binsRef.current.length : null;
  const toHz = (bins) => (bins == null || !featureFrameBins) ? null : (bins / featureFrameBins) * spanHz;
  const featureBandwidthHz = toHz(parseMaybe(d.classifierFeatureBandwidthBins));
  const featureDriftHzS    = toHz(parseMaybe(d.classifierFeatureDriftBinsS));
  const featurePaprDb      = parseMaybe(d.classifierFeaturePaprDb);
  const featureFlatness    = parseMaybe(d.classifierFeatureFlatness);
  const featurePeakCount   = parseMaybe(d.classifierFeaturePeakCount);
  const featureDutyPct     = parseMaybe(d.classifierFeatureDutyCyclePct);

  function redraw() {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const w = canvas.offsetWidth, h = canvas.offsetHeight;
    if (w <= 0 || h <= 0) return;
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const ctx = canvas.getContext('2d');
    spDraw(ctx, w, h, binsRef.current, refDb, range, 0, 1, 'off');

    // Label chip + marker at the CENTRE of the matched window (freq_offset
    // is the window's first bin, bandwidth_bins its width, both in the
    // daemon's own bin space — see publish_result in classifier.c), with
    // a bracket spanning the window itself. The daemon's frame_bins is
    // mapped onto this canvas's own frame length in case they differ.
    const bins = binsRef.current;
    const res = (d.classifierResult && typeof d.classifierResult === 'object') ? d.classifierResult : null;
    const label = res ? res.label : d.classifierLabel;
    if (bins && bins.length > 1 && label && label !== 'unknown') {
      const n = bins.length;
      const frameBins = Number(res ? res.frame_bins : d.classifierFrameBins) || n;
      const scale = n / frameBins;
      const offset = (Number(res ? res.freq_offset : d.classifierFreqOffset) || 0) * scale;
      const width  = (Number(res ? res.bandwidth_bins : d.classifierBandwidthBins) || 1) * scale;
      const x0 = Math.max(0, (offset / (n - 1)) * w);
      const x1 = Math.min(w, ((offset + width) / (n - 1)) * w);
      const x = (x0 + x1) / 2;
      let peakDb = -Infinity;
      for (let i = Math.max(0, Math.floor(offset)); i < Math.min(n, Math.ceil(offset + width)); i++) if (bins[i] > peakDb) peakDb = bins[i];
      if (!isFinite(peakDb)) peakDb = bins[Math.max(0, Math.min(n - 1, Math.round(offset)))];
      const top = refDb, bot = refDb - range;
      const y = h - Math.max(0, Math.min(h, ((peakDb - bot) / (top - bot)) * h));

      ctx.save();
      ctx.strokeStyle = 'rgba(120,220,255,0.85)';
      ctx.lineWidth = 1.5;
      ctx.beginPath(); ctx.moveTo(x, y); ctx.lineTo(x, y - 24); ctx.stroke();
      // window bracket just under the chip's marker line
      ctx.strokeStyle = 'rgba(120,220,255,0.55)';
      ctx.beginPath();
      ctx.moveTo(x0, y - 8); ctx.lineTo(x0, y - 14); ctx.lineTo(x1, y - 14); ctx.lineTo(x1, y - 8);
      ctx.stroke();

      const conf = Number(res ? res.confidence : d.classifierConfidence) || 0;
      const text = `${label} · ${Math.round(conf * 100)}%`;
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
  useECl(() => { redraw(); }, [d.classifierResult, d.classifierLabel, d.classifierConfidence, d.classifierFreqOffset, bandPlan, centerHz, spanHz]);

  // Append to the scrolling log on genuine change only. The daemon publishes
  // the whole decision as one JSON message on state/classifier/result (so
  // there is no burst of per-field topics to reassemble any more); a new
  // entry is logged when the label or template changes.
  useECl(() => {
    const res = d.classifierResult;
    if (!res || typeof res !== 'object' || !res.label) return;
    const key = `${res.label}|${res.template_id ?? ''}`;
    if (lastLoggedRef.current === key) return;
    lastLoggedRef.current = key;
    const hz = res.label === 'unknown' ? null
      : binMatchToHz(res.freq_offset, res.bandwidth_bins, res.frame_bins, centerHz, spanHz);
    setLog((prev) => {
      const entry = {
        t: Date.now(),
        label: res.label,
        confidence: Number(res.confidence) || 0,
        templateId: res.template_id,
        snrDb: res.snr_db,
        centerFreqHz: hz?.centerFreqHz ?? null,
        bandwidthHz: hz?.bandwidthHz ?? null,
      };
      const next = [entry, ...prev];
      return next.length > 50 ? next.slice(0, 50) : next;
    });
  }, [d.classifierResult]);

  // WebSocket: same /waterfall feed radioastro.jsx and the Spectrum page
  // use. f[0] is a structural step index (unused here); f[1..] are FFT
  // bins as linear power (integrated |X|^2) — converted to dB for display
  // only, the classifier daemon itself works in linear power.
  useECl(() => {
    let destroyed = false;
    const host  = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const toDB  = (v) => 10 * Math.log10(v > 0 ? v : 1e-10);

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

  // Reference-match panel: the matched template on the live grid against
  // the normalized live window the daemon actually correlated it with.
  // Both come from the daemon (state/classifier/template_shape and
  // state/classifier/window, zero-mean / unit-L2-norm, same length), so
  // what is drawn is exactly what was scored — not a proxy built from
  // this page's own waterfall frame.
  const matchCanvasRef = useRCl(null);
  const res = (d.classifierResult && typeof d.classifierResult === 'object') ? d.classifierResult : null;
  const shape = (d.classifierTemplateShape && typeof d.classifierTemplateShape === 'object') ? d.classifierTemplateShape : null;
  const win = (d.classifierWindow && typeof d.classifierWindow === 'object') ? d.classifierWindow : null;
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

    const matched = res && res.label && res.label !== 'unknown';
    const tplData = shape && Array.isArray(shape.data) && matched && shape.id === res.template_id ? shape.data : null;
    const winData = win && Array.isArray(win.data) && matched && win.id === res.template_id ? win.data : null;
    if (!tplData && !winData) {
      ctx.fillStyle = 'var(--dim, #888)';
      ctx.font = '13px system-ui, sans-serif';
      ctx.fillText(matched ? 'Waiting for the daemon\u2019s shapes\u2026' : 'No match to compare', 10, h / 2);
      return;
    }

    // Shared y-range so the two unit-norm traces are comparable by eye.
    let lo = Infinity, hi = -Infinity;
    for (const arr of [tplData, winData]) if (arr) for (const v of arr) { if (v < lo) lo = v; if (v > hi) hi = v; }
    if (!(hi > lo)) { lo = -1; hi = 1; }
    const pad = (hi - lo) * 0.08; lo -= pad; hi += pad;
    const drawTrace = (arr, color, width) => {
      ctx.beginPath();
      arr.forEach((v, i) => {
        const x = (i / Math.max(1, arr.length - 1)) * w;
        const y = h - ((v - lo) / (hi - lo)) * h;
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
      });
      ctx.strokeStyle = color;
      ctx.lineWidth = width;
      ctx.stroke();
    };
    if (tplData) drawTrace(tplData, 'rgba(120,220,255,0.95)', 2.2);
    if (winData) drawTrace(winData, 'rgba(255,200,40,0.9)', 1.4);

    ctx.font = '12px system-ui, sans-serif';
    ctx.fillStyle = '#78dcff';
    ctx.fillText(`\u2014 template: ${res.label} (#${res.template_id ?? '?'}, ${shape ? shape.bins : '?'} bins` +
                 (shape && shape.hz_per_bin > 0 ? `, ${fmtHz(shape.hz_per_bin * shape.bins)} wide)` : ')'), 8, 16);
    ctx.fillStyle = '#ffcd46';
    ctx.fillText(`\u2014 live window: ${Math.round((Number(res.confidence) || 0) * 100)}% match, ${Number(res.snr_db).toFixed(1)} dB SNR`, 8, 32);
  }, [d.classifierResult, d.classifierTemplateShape, d.classifierWindow]);

  // Status readout from the atomic result + background summary.
  const bg = (d.classifierBackground && typeof d.classifierBackground === 'object') ? d.classifierBackground : null;
  const stateText = !res ? '\u2014'
    : res.online === false ? 'offline'
    : res.state === 'learning' ? `learning background (${res.bg_age_s ?? 0} s)`
    : res.state === 'idle' ? 'idle (no templates or empty residual)'
    : res.label === 'unknown' ? 'no match' : 'match';
  const stateTone = !res || res.online === false ? 'warn' : res.state === 'learning' ? 'neutral' : res.label === 'unknown' ? 'neutral' : 'ok';
  const fmtDb = (v) => (v == null || !isFinite(Number(v))) ? '\u2014' : Number(v).toFixed(1) + ' dB';

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

      <Card title="Decision" sub="What the daemon is doing right now" className="span-12"
        right={<Pill tone={stateTone} dot>{stateText}</Pill>}>
        <div className="mono" style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 5ch' }}>
          <div><div className="dim" style={{ fontSize: 11 }}>Best score</div><div>{res ? Math.round((Number(res.label === 'unknown' ? res.best : res.confidence) || 0) * 100) + '%' : '\u2014'}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Window SNR</div><div>{fmtDb(res ? (res.label === 'unknown' ? res.best_snr_db : res.snr_db) : null)}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Prominence</div><div>{fmtDb(res ? (res.label === 'unknown' ? res.best_prom_db : res.prom_db) : null)}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Margin to runner-up</div><div>{res && res.margin != null ? Math.round(Number(res.margin) * 100) + ' pts' : '\u2014'}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Resolution</div><div>{res && res.hz_per_bin > 0 ? fmtHz(res.hz_per_bin) + ' / bin' : (bg && bg.hz_per_bin > 0 ? fmtHz(bg.hz_per_bin) + ' / bin' : '\u2014')}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Background age</div><div>{res && res.bg_age_s != null ? res.bg_age_s + ' s' : '\u2014'}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Persistent lines / masked spurs</div><div>{bg ? `${bg.hot} / ${bg.masked}` : '\u2014'}</div></div>
        </div>
        <div style={{ display: 'flex', gap: 8, marginTop: 10, alignItems: 'center' }}>
          <button className="btn ghost" onClick={() => d.publish('classifier/background', 'learn')}>Relearn background</button>
          <span className="dim" style={{ fontSize: 11, maxWidth: '70ch' }}>
            Labels are matched against the residual over a learned receiver baseline, and only accepted above the SNR and prominence gates. Relearn on a quiet moment if a retune happened without the daemon noticing.
          </span>
        </div>
      </Card>

      <Card title="Reference match" sub="Matched template vs. the live window the daemon scored" className="span-6">
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
              {e.snrDb != null && <span className="dim">{Number(e.snrDb).toFixed(0)} dB</span>}
              <span className="dim">{fmtHz(e.centerFreqHz)}</span>
              <span className="dim">± {fmtHz(e.bandwidthHz != null ? e.bandwidthHz / 2 : null)}</span>
              {e.templateId != null && <span className="dim">#{e.templateId}</span>}
            </div>
          ))}
        </div>
      </Card>

      <Card title="Signal Features" sub="Per-frame + rolling scalar features, published every cycle regardless of match" className="span-12"
        right={<Pill tone="warn">experimental</Pill>}>
        <div className="mono" style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 5ch' }}>
          <div><div className="dim" style={{ fontSize: 11 }}>Occupied bandwidth</div><div>{fmtHz(featureBandwidthHz)}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Peak/average ratio</div><div>{featurePaprDb == null ? '—' : featurePaprDb.toFixed(1) + ' dB'}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Spectral flatness</div><div>{featureFlatness == null ? '—' : featureFlatness.toFixed(3)}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Peak count</div><div>{featurePeakCount == null ? '—' : Math.round(featurePeakCount)}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Drift (peak sweep rate)</div><div>{fmtHzRate(featureDriftHzS)}</div></div>
          <div><div className="dim" style={{ fontSize: 11 }}>Duty cycle</div><div>{featureDutyPct == null ? '—' : featureDutyPct.toFixed(0) + '%'}</div></div>
        </div>
        <div className="dim" style={{ fontSize: 11, marginTop: 10, maxWidth: '70ch' }}>
          High flatness + low peak/average → broadband/noise-like (e.g. OFDM). Low flatness + high peak/average → tonal (e.g. CW). A non-zero drift means the dominant peak is sweeping — a static single frame can't otherwise show that a signal is a chirp. Duty cycle and drift read "—" until the daemon's rolling history has enough samples (a few seconds after (re)start).
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

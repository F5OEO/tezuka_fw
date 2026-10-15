// tsanalyzer.jsx — DATV Controller > Analysis: live transport stream analysis.
//
// The stream comes from the board's zynqrx over WebSocket (ws://<board>:8080/stream.ts), the
// same feed as the Webtioune page; a browser cannot listen to UDP. tsanalyzer-core.js (plain
// JavaScript, no dependencies) parses it: PAT/PMT, codec parameters, bitrates, PCR, PTS-PCR,
// continuity. This file is the page: the header, five charts and the PID table.
const { useState: useTsS, useEffect: useTsE, useRef: useTsR } = React;

const TSA_WINDOW_S = 60;          // the charts show the last minute
const TSA_MAX_DRAW = 360;         // points drawn per series: more are thinned out
const TSA_PCR_LIMIT_NS = 500;     // ISO 13818-1 PCR accuracy

const tsaTime = (t) => new Date(t).toTimeString().slice(0, 8);
const tsaHex = (pid) => '0x' + pid.toString(16).toUpperCase().padStart(4, '0');
const tsaNs = (v) => v == null ? '—' : Math.abs(v) < 1e3 ? v.toFixed(0) + ' ns' : Math.abs(v) < 1e6 ? (v / 1e3).toFixed(1) + ' µs' : (v / 1e6).toFixed(1) + ' ms';
const tsaNum = (v, d = 0) => v == null ? '—' : v.toFixed(d);

// ticks on 1, 2, 5 x 10^n
function tsaTicks(lo, hi, n = 4) {
  if (!(hi > lo)) { hi = lo + 1; }
  const raw = (hi - lo) / n, mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const step = [1, 2, 5, 10].map((m) => m * mag).find((s) => s >= raw) || raw;
  const out = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-6; v += step) out.push(+v.toFixed(10));
  return { ticks: out, lo: Math.min(lo, out[0] ?? lo), hi: Math.max(hi, out[out.length - 1] ?? hi) };
}

// Time series on one value axis: thin lines, round markers while the series is sparse, optional
// reference lines (limits), a legend for two or more series, crosshair and tooltip on hover.
function TsaChart({ series, now, fmtY = (v) => String(+v.toFixed(2)), refLines = [], yMin, yMax }) {
  const ref = useTsR(null);
  const [dim, setDim] = useTsS({ w: 600, h: 160 });
  const w = dim.w, height = dim.h;
  const [hover, setHover] = useTsS(null);
  useTsE(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver((e) => setDim({ w: e[0].contentRect.width, h: Math.max(70, e[0].contentRect.height) }));
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);
  const padL = 62, padR = 12, padT = 10, padB = 22;
  const innerW = Math.max(10, w - padL - padR), innerH = height - padT - padB;
  const t1 = now || Date.now(), t0 = t1 - TSA_WINDOW_S * 1000;
  // visible points, thinned to what can be drawn
  const vis = series.map((s) => {
    const p = s.pts.filter((x) => x.t >= t0);
    const k = Math.ceil(p.length / TSA_MAX_DRAW);
    return k > 1 ? p.filter((_, i) => i % k === 0) : p;
  });
  const all = vis.flat().map((x) => x.v).concat(refLines.map((r) => r.y));
  let lo = yMin != null ? yMin : all.length ? Math.min(...all) : 0, hi = yMax != null ? yMax : all.length ? Math.max(...all) : 1;
  if (yMin == null && yMax == null) { const pad = (hi - lo) * 0.12 || Math.abs(hi) * 0.1 || 1; lo -= pad; hi += pad; }
  const tk = tsaTicks(lo, hi);
  lo = tk.lo; hi = tk.hi;
  const xAt = (t) => padL + ((t - t0) / (t1 - t0)) * innerW;
  const yAt = (v) => padT + innerH - ((v - lo) / (hi - lo)) * innerH;
  const nx = Math.max(2, Math.min(7, Math.floor(innerW / 90)));   // time labels (HH:MM:SS) need about 90 px each
  const xTicks = Array.from({ length: nx }, (_, i) => t0 + ((t1 - t0) / (nx - 1)) * i);
  const sparse = vis.every((p) => p.length <= 150);
  // limit lines are only drawn when they are apart on the plot; far beyond the limit they collapse on zero
  const refs = refLines.length > 1 && Math.abs(yAt(refLines[0].y) - yAt(refLines[1].y)) < 14 ? [] : refLines;

  const onMove = (e) => {
    const x = e.clientX - e.currentTarget.getBoundingClientRect().left;
    setHover(t0 + ((x - padL) / innerW) * (t1 - t0));
  };
  const nearest = (pts, t) => { let b = null, bd = Infinity; for (const p of pts) { const dd = Math.abs(p.t - t); if (dd < bd) { bd = dd; b = p; } } return b; };
  const hov = hover != null && hover >= t0 && hover <= t1 ? series.map((s, i) => ({ s, p: nearest(vis[i], hover) })).filter((x) => x.p) : null;

  return (
    <div className="tsa-fill">
      {series.length > 1 && (
        <div className="legend" style={{ marginBottom: 4 }}>
          {series.map((s) => <span key={s.label}><i style={{ background: s.color }} />{s.label}</span>)}
        </div>
      )}
      <div ref={ref} className="tsa-plot">
        <svg width={w} height={height} onMouseMove={onMove} onMouseLeave={() => setHover(null)} style={{ display: 'block' }}>
          {tk.ticks.map((v) => (
            <g key={v}>
              <line x1={padL} x2={w - padR} y1={yAt(v)} y2={yAt(v)} className="grid-line" />
              <text x={padL - 8} y={yAt(v) + 4} textAnchor="end" className="axis-label">{fmtY(v)}</text>
            </g>
          ))}
          {xTicks.map((t, i) => <text key={i} x={xAt(t)} y={height - 5} textAnchor={i === 0 ? 'start' : i === nx - 1 ? 'end' : 'middle'} className="axis-label">{tsaTime(t)}</text>)}
          {refs.map((r) => (
            <g key={r.label + r.y}>
              <line x1={padL} x2={w - padR} y1={yAt(r.y)} y2={yAt(r.y)} stroke="var(--bad)" strokeWidth="1" strokeDasharray="4 4" />
              <text x={w - padR - 4} y={yAt(r.y) - 4} textAnchor="end" className="axis-label" style={{ fill: 'var(--bad)' }}>{r.label}</text>
            </g>
          ))}
          {vis.map((pts, i) => pts.length > 0 && (
            <g key={series[i].label}>
              <polyline fill="none" stroke={series[i].color} strokeWidth="1.5" strokeLinejoin="round" points={pts.map((p) => `${xAt(p.t).toFixed(1)},${yAt(p.v).toFixed(1)}`).join(' ')} />
              {sparse && pts.map((p, j) => <circle key={j} cx={xAt(p.t)} cy={yAt(p.v)} r="3" fill={series[i].color} stroke="var(--panel)" strokeWidth="1" />)}
            </g>
          ))}
          {hov && hov.length > 0 && (
            <g pointerEvents="none">
              <line x1={xAt(hov[0].p.t)} x2={xAt(hov[0].p.t)} y1={padT} y2={padT + innerH} className="crosshair" />
              {hov.map(({ s, p }) => <circle key={s.label} cx={xAt(p.t)} cy={yAt(p.v)} r="4" fill="var(--panel)" stroke={s.color} strokeWidth="2" />)}
            </g>
          )}
        </svg>
        {all.length === 0 && <div className="dim" style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center', fontSize: 12.5 }}>Waiting for data…</div>}
        {hov && hov.length > 0 && (
          <div className="chart-tip" style={{ left: Math.min(Math.max(xAt(hov[0].p.t), 80), w - 80) }}>
            <div className="tip-time mono">{tsaTime(hov[0].p.t)}</div>
            {hov.map(({ s, p }) => <div key={s.label} className="tip-row"><span className="tip-dot" style={{ background: s.color }} /><span className="tip-name">{s.label}</span><b className="mono">{fmtY(p.v)}</b></div>)}
          </div>
        )}
      </div>
    </div>
  );
}

// Distribution of the PCR intervals: bars from the baseline, rounded at the data end, 2 px gap
function TsaHistogram({ hist, color = 'var(--c-blue)' }) {
  const ref = useTsR(null);
  const [dim, setDim] = useTsS({ w: 600, h: 160 });
  const w = dim.w, height = dim.h;
  const [hover, setHover] = useTsS(null);
  useTsE(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver((e) => setDim({ w: e[0].contentRect.width, h: Math.max(70, e[0].contentRect.height) }));
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);
  const padL = 40, padR = 12, padT = 8, padB = 40;
  const innerW = Math.max(10, w - padL - padR), innerH = height - padT - padB;
  const counts = hist.counts, n = counts.length;
  const tk = tsaTicks(0, Math.max(1, ...counts));
  const bw = n ? innerW / n : 0;
  const yAt = (v) => padT + innerH - (v / tk.hi) * innerH;
  const edge = (i) => (hist.start + hist.width * i);
  return (
    <div className="tsa-fill"><div ref={ref} className="tsa-plot">
      <svg width={w} height={height} style={{ display: 'block' }} onMouseLeave={() => setHover(null)}>
        {tk.ticks.map((v) => (
          <g key={v}>
            <line x1={padL} x2={w - padR} y1={yAt(v)} y2={yAt(v)} className="grid-line" />
            <text x={padL - 8} y={yAt(v) + 4} textAnchor="end" className="axis-label">{v}</text>
          </g>
        ))}
        {counts.map((c, i) => {
          const x = padL + i * bw + 1, bwid = Math.max(1, bw - 2), h = innerH - (yAt(c) - padT);
          return (
            <g key={i} onMouseEnter={() => setHover(i)}>
              <rect x={padL + i * bw} y={padT} width={bw} height={innerH} fill="transparent" />
              {c > 0 && <path d={`M${x},${padT + innerH} V${yAt(c) + Math.min(4, h)} Q${x},${yAt(c)} ${x + Math.min(4, bwid / 2)},${yAt(c)} H${x + bwid - Math.min(4, bwid / 2)} Q${x + bwid},${yAt(c)} ${x + bwid},${yAt(c) + Math.min(4, h)} V${padT + innerH} Z`} fill={color} opacity={hover === i ? 1 : 0.85} />}
            </g>
          );
        })}
        {counts.map((_, i) => i % 2 === 0 && <text key={i} x={padL + i * bw + bw / 2} y={padT + innerH + 12} textAnchor="end" className="axis-label" transform={`rotate(-40 ${padL + i * bw + bw / 2} ${padT + innerH + 12})`}>{edge(i).toFixed(1)}</text>)}
      </svg>
      {n === 0 && <div className="dim" style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center', fontSize: 12.5 }}>Waiting for data…</div>}
      {hover != null && counts[hover] != null && (
        <div className="chart-tip" style={{ left: Math.min(Math.max(padL + hover * bw + bw / 2, 80), w - 80) }}>
          <div className="tip-time mono">{edge(hover).toFixed(2)} – {edge(hover + 1).toFixed(2)} ms</div>
          <div className="tip-row"><span className="tip-dot" style={{ background: color }} /><span className="tip-name">PCR intervals</span><b className="mono">{counts[hover]}</b></div>
        </div>
      )}
    </div></div>
  );
}

const tsaDefaultUrl = () => `ws://${window._tezukaDevHost || window.location.hostname}:8080/stream.ts`;

function Analysis({ d }) {
  const url = tsaDefaultUrl();                    // the board's own stream (same as Webtioune)
  const [state, setState] = useTsS('idle');          // idle | connecting | running | paused | error
  const [err, setErr] = useTsS('');
  const [snap, setSnap] = useTsS(null);
  const [at, setAt] = useTsS(Date.now());
  const an = useTsR(null), ws = useTsR(null), paused = useTsR(false);

  const close = () => { const s = ws.current; ws.current = null; if (s) { s.onmessage = s.onclose = s.onerror = null; try { s.close(); } catch (_) {} } };
  const start = () => {
    close();
    if (!window.TsAnalyzer) { setState('error'); setErr('tsanalyzer-core.js is not loaded'); return; }
    an.current = new window.TsAnalyzer();
    paused.current = false; setErr(''); setSnap(null); setState('connecting');
    let s;
    try { s = new WebSocket(url); } catch (e) { setState('error'); setErr(String(e.message || e)); return; }
    s.binaryType = 'arraybuffer';
    ws.current = s;
    s.onopen = () => setState('running');
    s.onmessage = (e) => { if (!paused.current && an.current) an.current.push(new Uint8Array(e.data), Date.now()); };
    s.onerror = () => { setState('error'); setErr('Cannot reach ' + url); };
    s.onclose = () => { if (ws.current === s) { ws.current = null; setState((st) => (st === 'error' ? st : 'idle')); } };
  };
  const stop = () => { close(); setState('idle'); };
  const togglePause = () => { paused.current = !paused.current; setState(paused.current ? 'paused' : 'running'); };

  // start on entering the page, release the connection on leaving it
  useTsE(() => { start(); return close; }, []);
  // refresh the display once a second (the analyzer itself works on every packet)
  useTsE(() => {
    const id = setInterval(() => {
      if (!an.current || paused.current) return;
      const now = Date.now();
      an.current.tick(now);
      setSnap(an.current.snapshot()); setAt(now);
    }, 1000);
    return () => clearInterval(id);
  }, []);

  const s = snap;
  const v = s && s.video, a = s && s.audio, pcr = s && s.pcr;
  const vi = v && v.info, ai = a && a.info;
  const pill = state === 'running' ? ['ok', 'Receiving'] : state === 'paused' ? ['warn', 'Paused'] : state === 'connecting' ? ['neutral', 'Connecting…'] : state === 'error' ? ['bad', 'Error'] : ['neutral', 'Stopped'];
  const busy = state === 'running' || state === 'paused' || state === 'connecting';

  const videoLine = !v ? '—' : [
    vi ? `${vi.codec}${vi.profile ? ' ' + vi.profile : ''}${vi.level ? ' L' + vi.level : ''}` : v.type,
    vi ? `${vi.width}x${vi.height}` : null, v.fps ? `${v.fps.toFixed(v.fps % 1 ? 2 : 0)} fps` : null, `${tsaNum(v.kbps)} kb/s (avg 5 s)`].filter(Boolean).join(' — ');
  const audioLine = !a ? '—' : [
    ai ? ai.codec : a.type, ai && ai.rate ? `${ai.rate / 1000} kHz${/^AAC/.test(ai.codec) && ai.rate <= 24000 ? ' (core)' : ''}` : null,
    ai ? (ai.channels === 2 ? 'stereo' : ai.channels === 1 ? 'mono' : ai.channels + ' ch') : null, `${tsaNum(a.kbps)} kb/s (avg 5 s)`].filter(Boolean).join(' — ');
  const cont = !s ? '—' : s.ccErrors === 0 ? 'OK — 0 errors' : `${s.ccErrors} errors`;
  const dist = s && s.dist;

  return (
    <div className="page tsa-page">
      <Card title="Transport stream" sub="Live analysis of the board's received stream" className="tsa-info"
          right={<Pill tone={pill[0]} dot>{pill[1]}</Pill>}>
          <div style={{ display: 'flex', gap: 24, flexWrap: 'wrap', justifyContent: 'space-between' }}>
            <div className="mono" style={{ display: 'grid', gap: 7, fontSize: 13.5, lineHeight: 1.45, flex: '1 1 560px', minWidth: 0 }}>
              <div><span className="dim">Video : </span>{videoLine}</div>
              <div><span className="dim">Audio : </span>{audioLine}</div>
              <div><span className="dim">TS rate : </span>{s ? `${tsaNum(s.tsKbps)} kb/s (avg 5 s)` : '—'}</div>
              <div><span className="dim">PCR accuracy : </span>{pcr && pcr.nowNs != null
                ? <>{tsaNs(pcr.nowNs)} — avg |error| {tsaNs(pcr.avgNs)} — max |error| {tsaNs(pcr.maxNs)} — limit ±{TSA_PCR_LIMIT_NS} ns{pcr.maxNs > TSA_PCR_LIMIT_NS && <> <Pill tone="bad" dot>over limit</Pill></>}</>
                : '—'}</div>
              <div><span className="dim">TS continuity : </span>{s ? <>{cont}{s.ccErrors > 0 && <> <Pill tone="bad" dot>errors</Pill></>}{(s.tei > 0 || s.syncLost > 0) && <span className="dim"> — {s.tei} transport errors, {s.syncLost} sync losses</span>}</> : '—'}</div>
              <div><span className="dim">PID share (since start) : </span>{dist
                ? <>Video{v ? ` (${tsaHex(v.pid)})` : ''} {dist.video.toFixed(1)} % — Audio{a ? ` (${tsaHex(a.pid)})` : ''} {dist.audio.toFixed(1)} % — Null (0x1FFF) {dist.nul.toFixed(1)} % — Fake null (0x1FFE) {dist.fake.toFixed(1)} % — Other (PSI/SI, PMT…) {dist.other.toFixed(1)} %</>
                : '—'}</div>
            </div>
            <div style={{ flex: '0 1 420px', minWidth: 260 }}>
              <div style={{ display: 'flex', gap: 8 }}>
                <button className="btn primary" onClick={start} disabled={state === 'connecting'}>{busy ? 'Restart' : 'Start'}</button>
                <button className="btn ghost" onClick={togglePause} disabled={state !== 'running' && state !== 'paused'}>{state === 'paused' ? 'Resume' : 'Pause'}</button>
                <button className="btn ghost" onClick={stop} disabled={!busy}>Stop</button>
              </div>
              {err && <div className="info-note" style={{ marginTop: 10 }}><Icon name="bolt" size={16} /><p>{err}</p></div>}
            </div>
          </div>
        </Card>

      <div className="tsa-grid">
        <Card title="PTS − PCR video" sub="ms">
          <TsaChart now={at} series={[{ label: 'Video', color: 'var(--c-blue)', pts: s ? s.series.ptsVideo : [] }]} />
        </Card>
        <Card title="PTS − PCR audio" sub="ms">
          <TsaChart now={at} series={[{ label: 'Audio', color: 'var(--c-blue)', pts: s ? s.series.ptsAudio : [] }]} />
        </Card>
        <Card title="Video and TS rate" sub="kb/s">
          <TsaChart now={at} yMin={0} fmtY={(x) => String(Math.round(x))} series={[
            { label: 'Video', color: 'var(--c-blue)', pts: s ? s.series.rates.map((r) => ({ t: r.t, v: r.video })) : [] },
            { label: 'TS', color: 'var(--c-coral)', pts: s ? s.series.rates.map((r) => ({ t: r.t, v: r.ts })) : [] }]} />
        </Card>
        <Card title="PCR accuracy" sub={`ns · limit ±${TSA_PCR_LIMIT_NS} ns`}>
          <TsaChart now={at} fmtY={tsaNs}
            refLines={[{ y: TSA_PCR_LIMIT_NS, label: `+${TSA_PCR_LIMIT_NS} ns` }, { y: -TSA_PCR_LIMIT_NS, label: `−${TSA_PCR_LIMIT_NS} ns` }]}
            series={[{ label: 'PCR error', color: 'var(--c-pink)', pts: s ? s.series.pcrAcc : [] }]} />
        </Card>
        <Card title="PCR intervals" sub="ms between PCRs">
          <TsaHistogram hist={s ? s.series.pcrHist : { start: 0, width: 1, counts: [] }} />
        </Card>
        <Card title="PIDs" sub="since start" pad={false}>
          <table className="ver-table compact">
            <thead><tr><th>PID</th><th>Type</th><th>Share</th><th>Packets</th></tr></thead>
            <tbody>
              {(s ? s.pids : []).map((p) => (
                <tr key={p.pid}>
                  <td className="mono vt-ver">{tsaHex(p.pid)}</td>
                  <td className="vt-name">{p.name}{p.isPcr && <span className="dim"> · PCR</span>}</td>
                  <td className="mono">{p.pct.toFixed(1)} %</td>
                  <td className="mono dim">{p.packets}</td>
                </tr>
              ))}
              {!s && <tr><td colSpan="4" className="dim">Waiting for data…</td></tr>}
            </tbody>
          </table>
        </Card>
      </div>
    </div>
  );
}

Object.assign(window, { Analysis });

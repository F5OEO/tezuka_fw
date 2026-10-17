// complexiq.jsx — Complex I/Q Frequency Data Stream + raw complex capture
//
// Two features of maia-httpd (maia-sdr PR #12): the continuous /iq-waterfall
// channel, which carries the complex FFT of the last frame of each integration
// (one 32-bit word per bin: I in the high 16 bits, Q in the low 16 bits, same
// bin order as /waterfall, bin 2048 = DC), and a one-shot raw capture over a
// small REST API.
//
// The stream is shown as one complex spectrum X[f] = I + jQ in four views:
//   Magnitude / Phase   dB over frequency, phase in rad (-pi..+pi) over frequency
//   Real / Imaginary    I and Q in LSB over frequency
//   Bode plot           dB and unwrapped phase in degrees over log frequency offset
//   Smith chart         Gamma = X[f] / X[peak], frequency colour coded
// Every axis carries its unit. The level is 20*log10 of the int16 bin value
// ("dB re 1 LSB"), not dBm: there is no absolute calibration.

const { useState: useSCx, useEffect: useECx, useRef: useRCx } = React;

const CIQ_N = 4096;
const CIQ_MODES = [
  ['magphase', 'Magnitude / Phase'],
  ['reim',     'Real / Imaginary'],
  ['bode',     'Bode plot'],
  ['smith',    'Smith chart'],
];
const CIQ_COL = { mag: '#5BB1F5', re: '#5BB1F5', im: '#FF8A5B', ok: '#7bd88f' };
const CIQ_LEVEL_LABEL = 'Magnitude (dB re 1 LSB)';

const ciqPhaseColor = (ph) => `hsl(${((ph + Math.PI) / (2 * Math.PI)) * 360},90%,62%)`;
const ciqFreqColor  = (frac) => `hsl(${240 - 240 * frac},90%,60%)`;

function ciqAnalyse(I, Q) {
  const n = I.length;
  const db = new Float32Array(n), ph = new Float32Array(n);
  let pk = -1, pkDb = -Infinity;
  for (let k = 0; k < n; k++) {
    const m = Math.hypot(I[k], Q[k]);
    db[k] = 20 * Math.log10(Math.max(m, 0.5));
    ph[k] = Math.atan2(Q[k], I[k]);
    if (Math.abs(k - n / 2) > 2 && db[k] > pkDb) { pkDb = db[k]; pk = k; }
  }
  // second strongest peak: at least 6 bins from the first one and from DC
  let pk2 = -1, pk2Db = -Infinity;
  for (let k = 0; k < n; k++) {
    if (Math.abs(k - n / 2) > 2 && Math.abs(k - pk) > 6 && db[k] > pk2Db) { pk2Db = db[k]; pk2 = k; }
  }
  return { db, ph, pk, pkDb, pk2, pk2Db };
}

const ciqFreq = (st, k) => st.centerHz + ((k - CIQ_N / 2) / CIQ_N) * st.spanHz;
const ciqNiceCeil = (v) => {
  const m = Math.pow(10, Math.floor(Math.log10(v)));
  const f = v / m;
  return (f <= 1 ? 1 : f <= 2 ? 2 : f <= 5 ? 5 : 10) * m;
};

function ciqLinX(st, r) {
  // frequency axis in MHz (absolute), or bin number if the span is unknown
  const known = st.centerHz != null && st.spanHz != null;
  const lo = known ? (st.centerHz - st.spanHz / 2) / 1e6 : 0;
  const hi = known ? (st.centerHz + st.spanHz / 2) / 1e6 : CIQ_N;
  const t = paTicks(lo, hi, Math.max(4, Math.floor(r.w / 90)));
  return {
    x0: lo, x1: hi,
    xTicks: t.ticks.map((v) => ({ v, label: v.toFixed(t.dec) })),
    xLabel: known ? 'Frequency (MHz)' : 'Bin number (span unknown)',
    topTicks: [0, 512, 1024, 1536, 2048, 2560, 3072, 3584, 4096].map((k) => ({ v: lo + (k / CIQ_N) * (hi - lo), label: String(k) })),
    topLabel: 'Bin number (2048 = LO, DC)',
    toBin: (v) => Math.round(((v - lo) / (hi - lo)) * CIQ_N),
    fromBin: (k) => lo + (k / CIQ_N) * (hi - lo),
  };
}

const CIQ_MK = ['#ffd24a', '#7ee0ff'];   // peak 1 and peak 2 marker colours

// Marker on a trace: a triangle above the point and a label (lines of text) beside it
function ciqMarker(ctx, r, x, y, lines, col) {
  if (!(x >= r.x && x <= r.x + r.w)) return;
  y = Math.max(r.y + 14, Math.min(r.y + r.h - 4, y));
  ctx.save();
  ctx.fillStyle = col; ctx.strokeStyle = 'rgba(11,15,20,0.9)'; ctx.lineWidth = 1.5;
  ctx.beginPath(); ctx.moveTo(x, y - 2); ctx.lineTo(x - 5, y - 10); ctx.lineTo(x + 5, y - 10); ctx.closePath();
  ctx.stroke(); ctx.fill();
  const right = x > r.x + r.w - 200;
  const y0 = Math.max(r.y + 9, y - 13);
  lines.forEach((t, i) => paText(ctx, t, right ? x - 10 : x + 10, y0 + i * 13,
    { color: col, align: right ? 'right' : 'left', halo: 'rgba(11,15,20,0.9)' }));
  ctx.restore();
}

// the two strongest bins as marker descriptions: { k, tag }
function ciqPeaks(an) {
  const p = [{ k: an.pk, tag: '1' }];
  if (an.pk2 >= 0) p.push({ k: an.pk2, tag: '2' });
  return p.filter((q) => q.k >= 0);
}
const ciqFmtF = (st, k) => (st.centerHz != null ? (ciqFreq(st, k) / 1e6).toFixed(6) + ' MHz' : 'bin ' + k);

function ciqLegend(ctx, r, items) {
  let x = r.x + 8;
  for (const [col, txt] of items) {
    ctx.fillStyle = col; ctx.fillRect(x, r.y + 7, 10, 10);
    paText(ctx, txt, x + 14, r.y + 12, { halo: 'rgba(11,15,20,0.9)' });
    ctx.font = PA_FONT;
    x += 14 + ctx.measureText(txt).width + 16;
  }
}

function ciqNoData(ctx, w, h, msg) {
  paText(ctx, msg, w / 2, h / 2, { align: 'center', color: PA_DIM });
}

// Magnitude trace; the threshold only affects what is drawn as phase
function ciqDrawMag(ctx, r, st, ax, an, first) {
  const top = Math.ceil((an.pkDb + 3) / 10) * 10, bot = top - 80;
  const yt = paTicks(bot, top, 8);
  const { X, Y } = paPanel(ctx, r, {
    ...ax, y0: bot, y1: top,
    yTicks: yt.ticks.map((v) => ({ v, label: String(Math.round(v)) })),
    yLabel: CIQ_LEVEL_LABEL, showX: !first,
  });
  ctx.save(); ctx.beginPath(); ctx.rect(r.x, r.y, r.w, r.h); ctx.clip();
  ctx.strokeStyle = CIQ_COL.mag; ctx.lineWidth = 1.2; ctx.beginPath();
  for (let k = 0; k < CIQ_N; k++) {
    const x = X(ax.fromBin(k)), y = Y(an.db[k]);
    if (k === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke(); ctx.restore();
  return { X, Y };
}

function ciqDrawPhase(ctx, r, st, ax, an, thrDb) {
  const P = Math.PI;
  const { X, Y } = paPanel(ctx, r, {
    ...ax, topTicks: null, topLabel: null, y0: -P, y1: P,
    yTicks: [-P, -P / 2, 0, P / 2, P].map((v, i) => ({ v, label: ['-π', '-π/2', '0', '+π/2', '+π'][i] })),
    yLabel: 'Phase (rad)', showX: true,
  });
  ctx.save(); ctx.beginPath(); ctx.rect(r.x, r.y, r.w, r.h); ctx.clip();
  const min = an.pkDb - thrDb;
  for (let k = 0; k < CIQ_N; k++) {
    if (an.db[k] < min) continue;
    ctx.fillStyle = ciqPhaseColor(an.ph[k]);
    ctx.fillRect(X(ax.fromBin(k)) - 2, Y(an.ph[k]) - 2, 4, 4);
  }
  ctx.restore();
  return { X, Y };
}

function ciqDrawMagPhase(ctx, w, h, st, an, thrDb) {
  const L = 70, R = 34, T = 38, GAP = 16, B = 40;
  const ph = (h - T - B - GAP) / 2;
  const r1 = { x: L, y: T, w: w - L - R, h: ph }, r2 = { x: L, y: T + ph + GAP, w: w - L - R, h: ph };
  const ax = ciqLinX(st, r1);
  const m = ciqDrawMag(ctx, r1, st, ax, an, true);
  ciqLegend(ctx, r1, [[CIQ_COL.mag, '|X| in dB']]);
  const p = ciqDrawPhase(ctx, r2, st, ax, an, thrDb);
  ciqPeaks(an).forEach(({ k, tag }, i) => {
    ciqMarker(ctx, r1, m.X(ax.fromBin(k)), m.Y(an.db[k]), [tag + ': ' + ciqFmtF(st, k), an.db[k].toFixed(1) + ' dB'], CIQ_MK[i]);
    ciqMarker(ctx, r2, p.X(ax.fromBin(k)), p.Y(an.ph[k]), [tag + ': ' + an.ph[k].toFixed(3) + ' rad'], CIQ_MK[i]);
  });
  // hue key for the phase dots
  for (let i = 0; i < 60; i++) {
    ctx.fillStyle = ciqPhaseColor(-Math.PI + (i / 59) * 2 * Math.PI);
    ctx.fillRect(r2.x + 8 + i * 2, r2.y + 8, 2, 8);
  }
  paText(ctx, 'phase -π → +π, dots shown within ' + thrDb + ' dB of the peak',
         r2.x + 8 + 126, r2.y + 12, { halo: 'rgba(11,15,20,0.9)' });
  return { kind: 'lin', r: r1, ax, rects: [r1, r2], X: m.X };
}

function ciqDrawReIm(ctx, w, h, st, an, norm) {
  const L = 70, R = 34, T = 38, GAP = 16, B = 40;
  const ph = (h - T - B - GAP) / 2;
  const r1 = { x: L, y: T, w: w - L - R, h: ph }, r2 = { x: L, y: T + ph + GAP, w: w - L - R, h: ph };
  const ax = ciqLinX(st, r1);
  // norm: 'raw' = LSB of the int16 FFT output (autoscaled); 'unit' = divided by the
  // magnitude of the strongest bin, so the peak lies on the unit circle; 'unitrot' =
  // additionally rotated so the peak is exactly 1 + j0 (removes the frame to frame phase)
  let I = st.I, Q = st.Q, unit = 'LSB';
  if (norm !== 'raw' && an.pk >= 0) {
    const a = st.I[an.pk], b = st.Q[an.pk], m2 = a * a + b * b;
    I = new Float32Array(CIQ_N); Q = new Float32Array(CIQ_N);
    for (let k = 0; k < CIQ_N; k++) {
      if (norm === 'unitrot') { I[k] = (st.I[k] * a + st.Q[k] * b) / m2; Q[k] = (st.Q[k] * a - st.I[k] * b) / m2; }
      else { const m = Math.sqrt(m2); I[k] = st.I[k] / m; Q[k] = st.Q[k] / m; }
    }
    unit = norm === 'unitrot' ? 'normalised, peak at 0 rad' : 'normalised to the peak';
  }
  let sc, ticks;
  if (unit === 'LSB') {
    let mx = 1;
    for (let k = 0; k < CIQ_N; k++) mx = Math.max(mx, Math.abs(I[k]), Math.abs(Q[k]));
    sc = ciqNiceCeil(mx * 1.1);
    ticks = paTicks(-sc, sc, 6).ticks.map((v) => ({ v, label: String(v) }));
  } else {
    sc = 1.1;   // the peak is on the unit circle; a fixed scale, so the plot does not breathe
    ticks = [-1, -0.5, 0, 0.5, 1].map((v) => ({ v, label: v.toFixed(1) }));
  }
  const draw = (r, arr, col, name, showX) => {
    const yl = unit === 'LSB' ? name + ' (LSB)' : name + ' (normalised, unitless)';
    const { X, Y } = paPanel(ctx, r, { ...ax, topTicks: showX ? null : ax.topTicks, topLabel: showX ? null : ax.topLabel, y0: -sc, y1: sc, yTicks: ticks, yLabel: yl, showX });
    ctx.save(); ctx.beginPath(); ctx.rect(r.x, r.y, r.w, r.h); ctx.clip();
    ctx.strokeStyle = col; ctx.lineWidth = 1.2; ctx.beginPath();
    for (let k = 0; k < CIQ_N; k++) {
      const x = X(ax.fromBin(k)), y = Y(arr[k]);
      if (k === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }
    ctx.stroke(); ctx.restore();
    ciqLegend(ctx, r, [[col, name === 'Real' ? 'Real part I' : 'Imaginary part Q']]);
    ciqPeaks(an).forEach(({ k, tag }, i) => {
      const v = arr[k], txt = unit === 'LSB' ? v.toFixed(0) + ' LSB' : v.toFixed(3);
      ciqMarker(ctx, r, X(ax.fromBin(k)), Y(v),
        [tag + ': ' + ciqFmtF(st, k), (name === 'Real' ? 'Re ' : 'Im ') + txt + ',  |X| ' + an.db[k].toFixed(1) + ' dB'], CIQ_MK[i]);
    });
  };
  draw(r1, I, CIQ_COL.re, 'Real', false);
  draw(r2, Q, CIQ_COL.im, 'Imaginary', true);
  return { kind: 'lin', r: r1, ax, rects: [r1, r2] };
}

function ciqDrawBode(ctx, w, h, st, an, thrDb, side) {
  const L = 70, R = 34, T = 38, GAP = 16, B = 40;
  const ph = (h - T - B - GAP) / 2;
  const r1 = { x: L, y: T, w: w - L - R, h: ph }, r2 = { x: L, y: T + ph + GAP, w: w - L - R, h: ph };
  const binHz = st.spanHz != null ? st.spanHz / CIQ_N : 1;
  const unitHz = st.spanHz != null;
  const kMax = CIQ_N / 2 - 1;                 // offset in bins, 1 .. kMax
  const lo = Math.log10(binHz), hi = Math.log10(binHz * kMax);
  const xTicks = [];
  for (let e = Math.floor(lo); e <= Math.ceil(hi); e++) {
    for (const m of [1, 2, 5]) {
      const v = m * Math.pow(10, e);
      if (Math.log10(v) >= lo - 1e-9 && Math.log10(v) <= hi + 1e-9)
        xTicks.push({ v: Math.log10(v), label: unitHz ? paFmtHz(v, 0) : String(v) });
    }
  }
  const topTicks = [];
  for (let e = 0; e <= 3; e++) for (const m of [1, 2, 5]) { const kk = m * Math.pow(10, e); if (kk <= kMax) topTicks.push({ v: Math.log10(binHz * kk), label: String(kk) }); }
  const ax = { x0: lo, x1: hi, xTicks, topTicks, topLabel: 'Offset from the LO in bins (bin number = 2048 ' + (side > 0 ? '+' : '-') + ' offset)',
               xLabel: unitHz ? 'Frequency offset ' + (side > 0 ? 'above' : 'below') + ' the LO (log scale, Hz)' : 'Offset (bins, log scale)' };
  const idx = (m) => CIQ_N / 2 + side * m;         // offset bins -> bin
  const top = Math.ceil((an.pkDb + 3) / 10) * 10, bot = top - 80;
  const yt = paTicks(bot, top, 8);
  const p1 = paPanel(ctx, r1, { ...ax, y0: bot, y1: top,
    yTicks: yt.ticks.map((v) => ({ v, label: String(Math.round(v)) })), yLabel: CIQ_LEVEL_LABEL, showX: false });
  ctx.save(); ctx.beginPath(); ctx.rect(r1.x, r1.y, r1.w, r1.h); ctx.clip();
  ctx.strokeStyle = CIQ_COL.mag; ctx.lineWidth = 1.2; ctx.beginPath();
  for (let m = 1; m <= kMax; m++) {
    const x = p1.X(Math.log10(binHz * m)), y = p1.Y(an.db[idx(m)]);
    if (m === 1) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke(); ctx.restore();
  ciqLegend(ctx, r1, [[CIQ_COL.mag, '|X| in dB']]);
  ciqPeaks(an).forEach(({ k, tag }, i) => {
    const m = side * (k - CIQ_N / 2);
    if (m >= 1 && m <= kMax) ciqMarker(ctx, r1, p1.X(Math.log10(binHz * m)), p1.Y(an.db[k]),
      [tag + ': ' + ciqFmtF(st, k) + ' (offset ' + (st.spanHz != null ? paFmtHz(binHz * m, 1) : m + ' bins') + ')', an.db[k].toFixed(1) + ' dB'], CIQ_MK[i]);
  });
  // unwrapped phase in degrees, per contiguous run of bins within thrDb of the peak
  const min = an.pkDb - thrDb;
  const runs = []; let cur = null; let prev = 0, acc = 0;
  for (let m = 1; m <= kMax; m++) {
    const k = idx(m);
    if (an.db[k] < min) { cur = null; continue; }
    const p = an.ph[k];
    if (!cur) { cur = []; runs.push(cur); acc = p; }
    else {
      let d = p - prev; d -= 2 * Math.PI * Math.round(d / (2 * Math.PI)); acc += d;
    }
    prev = p; cur.push([m, acc * 180 / Math.PI]);
  }
  let pmin = -180, pmax = 180;
  for (const run of runs) for (const [, v] of run) { pmin = Math.min(pmin, v); pmax = Math.max(pmax, v); }
  const ps = paTicks(Math.floor(pmin / 90) * 90, Math.ceil(pmax / 90) * 90, 6);
  const y0 = ps.ticks[0], y1 = ps.ticks[ps.ticks.length - 1];
  const p2 = paPanel(ctx, r2, { ...ax, topTicks: null, topLabel: null, y0, y1,
    yTicks: ps.ticks.map((v) => ({ v, label: String(Math.round(v)) })), yLabel: 'Phase (deg, unwrapped)', showX: true });
  ctx.save(); ctx.beginPath(); ctx.rect(r2.x, r2.y, r2.w, r2.h); ctx.clip();
  ctx.strokeStyle = CIQ_COL.im; ctx.fillStyle = CIQ_COL.im; ctx.lineWidth = 1.2;
  for (const run of runs) {
    ctx.beginPath();
    run.forEach(([m, v], i) => {
      const x = p2.X(Math.log10(binHz * m)), y = p2.Y(v);
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    });
    ctx.stroke();
    for (const [m, v] of run) ctx.fillRect(p2.X(Math.log10(binHz * m)) - 2, p2.Y(v) - 2, 4, 4);
  }
  ctx.restore();
  ciqLegend(ctx, r2, [[CIQ_COL.im, 'phase of X, runs within ' + thrDb + ' dB of the peak']]);
  return { kind: 'log', r: r1, ax, rects: [r1, r2], binHz, side };
}

function ciqDrawSmith(ctx, w, h, st, an, thrDb) {
  const cbW = 150;
  const S = Math.min(h - 70, w - cbW - 120);
  const R = S / 2, x0 = Math.max(0, (w - (80 + 2 * R + 10 + 70 + 110)) / 2), cx = x0 + 80 + R + 10, cy = 14 + R;
  const U = (u) => cx + u * R, V = (v) => cy - v * R;
  ctx.save();
  ctx.fillStyle = '#0b0f14'; ctx.beginPath(); ctx.arc(cx, cy, R, 0, 2 * Math.PI); ctx.fill();
  ctx.save(); ctx.beginPath(); ctx.arc(cx, cy, R, 0, 2 * Math.PI); ctx.clip();
  ctx.strokeStyle = PA_GRID; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(U(-1), V(0)); ctx.lineTo(U(1), V(0)); ctx.stroke();
  for (const rr of [0.2, 0.5, 1, 2, 5]) {
    ctx.beginPath(); ctx.arc(U(rr / (1 + rr)), V(0), R / (1 + rr), 0, 2 * Math.PI); ctx.stroke();
  }
  for (const x of [0.2, 0.5, 1, 2, 5]) for (const sg of [1, -1]) {
    ctx.beginPath(); ctx.arc(U(1), V(sg / x), R / x, 0, 2 * Math.PI); ctx.stroke();
  }
  ctx.restore();
  ctx.strokeStyle = PA_FRAME; ctx.beginPath(); ctx.arc(cx, cy, R, 0, 2 * Math.PI); ctx.stroke();
  // chart labels: normalised resistance on the real axis, reactance on the rim
  for (const rr of [0.2, 0.5, 1, 2, 5]) {
    paText(ctx, String(rr), U((rr - 1) / (rr + 1)) - 2, V(0) + 9, { font: PA_FONT_S, color: PA_DIM, align: 'right' });
  }
  paText(ctx, 'r=0', U(-1) + 3, V(0) - 8, { font: PA_FONT_S, color: PA_DIM });
  for (const x of [0.2, 0.5, 1, 2, 5]) for (const sg of [1, -1]) {
    const xx = sg * x, u = (xx * xx - 1) / (xx * xx + 1), v = (2 * xx) / (xx * xx + 1);
    const ox = u * (R + 18), oy = v * (R + 10);
    paText(ctx, (sg > 0 ? '+j' : '-j') + x, cx + ox, cy - oy, { font: PA_FONT_S, color: PA_DIM, align: 'center' });
  }
  // points: Gamma = X / X[peak]
  const ref = [st.I[an.pk], st.Q[an.pk]], rn = ref[0] * ref[0] + ref[1] * ref[1];
  const G = (k) => {
    const a = st.I[k], b = st.Q[k];
    return [(a * ref[0] + b * ref[1]) / rn, (b * ref[0] - a * ref[1]) / rn];
  };
  const min = an.pkDb - thrDb;
  let n = 0;
  ctx.lineWidth = 1.2;
  for (let k = 0; k < CIQ_N; k++) {
    if (an.db[k] < min) continue;
    const g = G(k), col = ciqFreqColor(k / (CIQ_N - 1));
    if (k + 1 < CIQ_N && an.db[k + 1] >= min) {
      const g2 = G(k + 1);
      ctx.strokeStyle = col; ctx.beginPath(); ctx.moveTo(U(g[0]), V(g[1])); ctx.lineTo(U(g2[0]), V(g2[1])); ctx.stroke();
    }
    ctx.fillStyle = col; ctx.beginPath(); ctx.arc(U(g[0]), V(g[1]), 2.2, 0, 2 * Math.PI); ctx.fill();
    n++;
  }
  // the strongest bin is the reference, Gamma = 1; the second peak is plotted relative to it
  ciqPeaks(an).forEach(({ k, tag }, i) => {
    const gg = G(k);
    ciqMarker(ctx, { x: cx - R, y: cy - R, w: 2 * R, h: 2 * R }, U(gg[0]), V(gg[1]) - 2,
      [tag + ': ' + ciqFmtF(st, k), an.db[k].toFixed(1) + ' dB, \u0393 ' + gg[0].toFixed(2) + (gg[1] < 0 ? '-' : '+') + 'j' + Math.abs(gg[1]).toFixed(2)], CIQ_MK[i]);
  });
  // outside scales: Re(Gamma) below, Im(Gamma) at the left
  for (const t of [-1, -0.5, 0, 0.5, 1]) {
    paText(ctx, t.toFixed(1), U(t), cy + R + 14, { align: 'center' });
    paText(ctx, t.toFixed(1), cx - R - 8, V(t), { align: 'right' });
  }
  paText(ctx, 'Re(Γ), unitless', cx, cy + R + 32, { align: 'center', color: PA_DIM });
  ctx.translate(cx - R - 52, cy); ctx.rotate(-Math.PI / 2);
  paText(ctx, 'Im(Γ), unitless', 0, 0, { align: 'center', color: PA_DIM });
  ctx.restore();
  // frequency colour bar
  const bx = cx + R + 70, by = 14, bh = S;
  const g = ctx.createLinearGradient(0, by + bh, 0, by);
  for (let i = 0; i <= 8; i++) g.addColorStop(i / 8, ciqFreqColor(i / 8));
  ctx.fillStyle = g; ctx.fillRect(bx, by, 14, bh);
  ctx.strokeStyle = PA_FRAME; ctx.strokeRect(bx + 0.5, by + 0.5, 13, bh - 1);
  const known = st.centerHz != null && st.spanHz != null;
  const lo = known ? st.centerHz - st.spanHz / 2 : 0, hi = known ? st.centerHz + st.spanHz / 2 : CIQ_N;
  const tk = paTicks(lo / (known ? 1e6 : 1), hi / (known ? 1e6 : 1), 5);
  for (const v of tk.ticks) {
    const frac = (v * (known ? 1e6 : 1) - lo) / (hi - lo);
    paText(ctx, v.toFixed(tk.dec), bx + 20, by + bh - frac * bh);
  }
  paText(ctx, known ? 'Frequency (MHz)' : 'Bin number', bx - 4, by + bh + 14, { color: PA_DIM });
  paText(ctx, n + ' bins within ' + thrDb + ' dB of the peak', cx, cy + R + 50, { color: PA_DIM, align: 'center' });
  return { kind: 'smith' };
}

function ComplexIqPage({ d }) {
  const canvasRef = useRCx(null);
  const wsRef = useRCx(null);
  const frameRef = useRCx(null);      // { I, Q } Float32Array
  const dirtyRef = useRCx(true);
  const hoverRef = useRCx(null);      // css px x of the cursor
  const geoRef = useRCx(null);
  const modeRef = useRCx('magphase');
  const thrRef = useRCx(40);
  const normRef = useRCx('unitrot');
  const sideRef = useRCx(1);
  const holdRef = useRCx(false);
  const cfgRef = useRCx({ centerHz: null, spanHz: null });
  const [mode, setMode] = useSCx('magphase');
  const [thr, setThr] = useSCx(40);
  const [norm, setNorm] = useSCx('unitrot');
  const [side, setSide] = useSCx(1);
  const [hold, setHold] = useSCx(false);
  const [wsState, setWsState] = useSCx('connecting');
  const [peak, setPeak] = useSCx(null);
  const [mk, setMk] = useSCx(null);
  const [captureState, setCaptureState] = useSCx('idle');
  const [captureError, setCaptureError] = useSCx(null);

  const centerHz = d.rxFreq ?? null;
  const spanHz = d.span ?? d.rxSampling ?? null;
  cfgRef.current = { centerHz, spanHz };
  useECx(() => { modeRef.current = mode; dirtyRef.current = true; }, [mode]);
  useECx(() => { thrRef.current = thr; dirtyRef.current = true; }, [thr]);
  useECx(() => { normRef.current = norm; dirtyRef.current = true; }, [norm]);
  useECx(() => { sideRef.current = side; dirtyRef.current = true; }, [side]);
  useECx(() => { holdRef.current = hold; }, [hold]);
  useECx(() => { dirtyRef.current = true; }, [centerHz, spanHz]);

  // WebSocket to the continuous I/Q channel
  useECx(() => {
    let destroyed = false;
    const host = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    function connect() {
      if (destroyed) return;
      const ws = new WebSocket(`${proto}//${host}/iq-waterfall`);
      ws.binaryType = 'arraybuffer';
      wsRef.current = ws;
      setWsState('connecting');
      ws.onopen = () => setWsState('connected');
      ws.onclose = () => { setWsState('disconnected'); if (!destroyed) setTimeout(connect, 2000); };
      ws.onerror = () => {};
      ws.onmessage = (evt) => {
        if (!(evt.data instanceof ArrayBuffer) || holdRef.current) return;
        // one little-endian u32 per bin: I (signed) in the high 16 bits, Q in the low 16 bits
        const view = new DataView(evt.data);
        const n = evt.data.byteLength >> 2;
        const I = new Float32Array(n), Q = new Float32Array(n);
        for (let i = 0; i < n; i++) {
          Q[i] = view.getInt16(i * 4, true);
          I[i] = view.getInt16(i * 4 + 2, true);
        }
        frameRef.current = { I, Q };
        dirtyRef.current = true;
      };
    }
    connect();
    return () => { destroyed = true; try { wsRef.current?.close(); } catch (_) {} };
  }, []);

  // draw loop
  useECx(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    let running = true, raf = 0, lastPeak = 0, lastMk = 0;
    const draw = () => {
      if (!running) return;
      raf = requestAnimationFrame(draw);
      const cw = canvas.offsetWidth, ch = canvas.offsetHeight, dpr = window.devicePixelRatio || 1;
      if (cw <= 0 || ch <= 0) return;
      if (canvas.width !== Math.round(cw * dpr) || canvas.height !== Math.round(ch * dpr)) {
        canvas.width = Math.round(cw * dpr); canvas.height = Math.round(ch * dpr); dirtyRef.current = true;
      }
      if (!dirtyRef.current) return;
      dirtyRef.current = false;
      const ctx = canvas.getContext('2d');
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      ctx.fillStyle = '#0b0f14'; ctx.fillRect(0, 0, cw, ch);
      const fr = frameRef.current;
      if (!fr || fr.I.length !== CIQ_N) { ciqNoData(ctx, cw, ch, 'waiting for /iq-waterfall data'); return; }
      const st = { ...cfgRef.current, I: fr.I, Q: fr.Q };
      const an = ciqAnalyse(fr.I, fr.Q);
      const m = modeRef.current, thrDb = thrRef.current;
      let geo;
      if (m === 'magphase') geo = ciqDrawMagPhase(ctx, cw, ch, st, an, thrDb);
      else if (m === 'reim') geo = ciqDrawReIm(ctx, cw, ch, st, an, normRef.current);
      else if (m === 'bode') geo = ciqDrawBode(ctx, cw, ch, st, an, thrDb, sideRef.current);
      else geo = ciqDrawSmith(ctx, cw, ch, st, an, thrDb);
      geoRef.current = { ...geo, an, st };
      // cursor
      const hx = hoverRef.current;
      if (hx != null && geo.rects) {
        ctx.save(); ctx.strokeStyle = 'rgba(255,255,255,0.5)'; ctx.setLineDash([4, 4]);
        for (const r of geo.rects) { ctx.beginPath(); ctx.moveTo(hx + 0.5, r.y); ctx.lineTo(hx + 0.5, r.y + r.h); ctx.stroke(); }
        ctx.restore();
      }
      const now = performance.now();
      if (now - lastPeak > 250 && an.pk >= 0) {
        lastPeak = now;
        const pkInfo = (k) => ({ f: st.centerHz != null ? ciqFreq(st, k) : null, db: an.db[k], ph: an.ph[k],
                                 i: fr.I[k], q: fr.Q[k], k });
        setPeak({ ...pkInfo(an.pk), second: an.pk2 >= 0 ? pkInfo(an.pk2) : null });
      }
      if (hx != null && geo.rects && now - lastMk > 100) {
        lastMk = now;
        const r = geo.rects[0];
        let k = null;
        if (geo.kind === 'lin') k = Math.round(((hx - r.x) / r.w) * CIQ_N);
        else if (geo.kind === 'log') {
          const e = geo.ax.x0 + ((hx - r.x) / r.w) * (geo.ax.x1 - geo.ax.x0);
          k = Math.round(CIQ_N / 2 + geo.side * Math.pow(10, e) / geo.binHz);
        }
        if (k != null && k >= 0 && k < CIQ_N) {
          let nrm = null;
          if (m === 'reim' && normRef.current !== 'raw' && an.pk >= 0) {
            const a0 = fr.I[an.pk], b0 = fr.Q[an.pk], m2 = a0 * a0 + b0 * b0;
            nrm = normRef.current === 'unitrot'
              ? [(fr.I[k] * a0 + fr.Q[k] * b0) / m2, (fr.Q[k] * a0 - fr.I[k] * b0) / m2]
              : [fr.I[k] / Math.sqrt(m2), fr.Q[k] / Math.sqrt(m2)];
          }
          setMk({ k, f: st.centerHz != null ? ciqFreq(st, k) : null, db: an.db[k], ph: an.ph[k], i: fr.I[k], q: fr.Q[k], nrm });
        } else setMk(null);
      }
    };
    raf = requestAnimationFrame(draw);
    return () => { running = false; cancelAnimationFrame(raf); };
  }, []);

  const onMove = (e) => {
    const rect = canvasRef.current.getBoundingClientRect();
    hoverRef.current = e.clientX - rect.left;
    dirtyRef.current = true;
  };
  const onLeave = () => { hoverRef.current = null; setMk(null); dirtyRef.current = true; };

  async function startRawCapture() {
    setCaptureState('capturing');
    setCaptureError(null);
    try {
      const patchResp = await fetch('/api/raw-capture', {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ state_change: 'start' }),
      });
      if (!patchResp.ok) throw new Error(`start failed: HTTP ${patchResp.status}`);
      let finished = false;
      for (let attempt = 0; attempt < 20 && !finished; attempt++) {
        await new Promise((r) => setTimeout(r, 100));
        const statusResp = await fetch('/api/raw-capture');
        if (!statusResp.ok) throw new Error(`status check failed: HTTP ${statusResp.status}`);
        const status = await statusResp.json();
        if (status.state === 'Stopped') finished = true;
      }
      if (!finished) throw new Error('capture did not finish in time');
      const dataResp = await fetch('/raw-capture');
      if (!dataResp.ok) throw new Error(`fetching capture failed: HTTP ${dataResp.status}`);
      const blob = await dataResp.blob();
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `raw-capture-${Date.now()}.iq16`;
      a.click();
      setTimeout(() => URL.revokeObjectURL(url), 30000);
      setCaptureState('idle');
    } catch (err) {
      setCaptureState('error');
      setCaptureError(String(err && err.message || err));
    }
  }

  const fmtF = (f) => f == null ? 'n/a' : (f / 1e6).toFixed(6) + ' MHz';
  const ro = (p) => p == null ? '' :
    `${fmtF(p.f)}   |X| ${p.db.toFixed(1)} dB   phase ${p.ph.toFixed(3)} rad (${(p.ph * 180 / Math.PI).toFixed(1)}°)   I ${p.i}   Q ${p.q}   bin ${p.k}` +
    (p.nrm ? `   normalised Re ${p.nrm[0].toFixed(3)} Im ${p.nrm[1].toFixed(3)}` : '');
  const hasThr = mode !== 'reim';
  return (
    <>
      <Card
        title="Complex I/Q Frequency Data Stream"
        sub="Complex FFT bins I + jQ of the last frame of each integration, same bin order as the magnitude waterfall"
        right={<Pill tone={wsState === 'connected' ? 'ok' : 'warn'}>{wsState}</Pill>}
      >
        <div style={{ display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap', marginBottom: 10 }}>
          <div className="seg">
            {CIQ_MODES.map(([id, label]) => (
              <button key={id} className={mode === id ? 'seg-on' : ''} onClick={() => setMode(id)}>{label}</button>
            ))}
          </div>
          {mode === 'reim' && (
            <label className="dim" style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
              scale
              <Select value={norm} onChange={setNorm} options={[
                { v: 'unitrot', l: 'unit circle, peak at 0 rad' },
                { v: 'unit', l: 'unit circle (peak magnitude = 1)' },
                { v: 'raw', l: 'raw LSB (autoscale)' }]} />
            </label>
          )}
          {hasThr && (
            <label className="dim" style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
              show phase within
              <Select value={String(thr)} onChange={(v) => setThr(Number(v))}
                options={['20', '30', '40', '60', '80'].map((v) => ({ v, l: v + ' dB' }))} />
              of the peak
            </label>
          )}
          {mode === 'bode' && (
            <div className="seg">
              <button className={side > 0 ? 'seg-on' : ''} onClick={() => setSide(1)}>+ offsets</button>
              <button className={side < 0 ? 'seg-on' : ''} onClick={() => setSide(-1)}>- offsets</button>
            </div>
          )}
          <button className="btn ghost" onClick={() => setHold(!hold)}>{hold ? 'Resume' : 'Hold'}</button>
        </div>
        <div style={{ position: 'relative', width: '100%', height: 600 }}>
          <canvas ref={canvasRef} onMouseMove={onMove} onMouseLeave={onLeave}
            style={{ width: '100%', height: '100%', display: 'block', borderRadius: 8, cursor: 'crosshair' }} />
        </div>
        <div className="mono" style={{ marginTop: 8, fontSize: 12, minHeight: 50 }}>
          <div id="ciq-peak">{peak ? 'Peak (excl. DC): ' + ro(peak) : 'Peak: waiting for data'}</div>
          <div id="ciq-peak2">{peak && peak.second ? '2nd peak: ' + ro(peak.second) + '   (' + (peak.db - peak.second.db).toFixed(1) + ' dB below the peak)' : ''}</div>
          <div id="ciq-cursor" className="dim">{mk ? 'Cursor: ' + ro(mk) : 'Cursor: move the mouse over a plot'}</div>
        </div>
        <p className="dim" style={{ marginTop: 8 }}>
          {mode === 'magphase' && 'Top: magnitude 20·log10(|X|) in dB re 1 LSB (uncalibrated, not dBm). Bottom: phase atan2(Q, I) in rad from -π to +π, colour = phase.'}
          {mode === 'reim' && (norm === 'raw' ? 'Real part I (top) and imaginary part Q (bottom) of every bin in LSB of the int16 FFT output (autoscaled).' : (norm === 'unitrot' ? 'Real part (top) and imaginary part (bottom) of every bin divided by the complex value of the strongest bin (so the peak is exactly 1 + j0)' : 'Real part (top) and imaginary part (bottom) of every bin divided by the magnitude of the strongest bin (the peak lies on the unit circle, its phase still changes from frame to frame)') + '. Fixed scale -1.1 to +1.1. The readout line below stays in LSB.')}
          {mode === 'bode' && 'Magnitude in dB and phase in degrees (unwrapped inside each run of bins) over the frequency offset from the LO on a log axis, one side at a time. The phase is only meaningful where a signal is present.'}
          {mode === 'smith' && 'Γ = X[f] / X[peak]: every bin normalised to the strongest one (|Γ| ≤ 1), colour = frequency. Constant-r circles and constant-x arcs are those of a normalised Smith chart.'}
        </p>
      </Card>

      <Card
        title="Raw Complex Capture"
        sub="Single-shot burst of raw I/Q bins, tapped before magnitude/phase are computed"
      >
        <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
          <button className="btn primary" disabled={captureState === 'capturing'} onClick={startRawCapture}>
            {captureState === 'capturing' ? 'Capturing…' : 'Start Capture & Download'}
          </button>
          {captureState === 'error' && <span style={{ color: 'var(--bad)' }}>{captureError}</span>}
        </div>
      </Card>
    </>
  );
}

Object.assign(window, { ComplexIqPage });

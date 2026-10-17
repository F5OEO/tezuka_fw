// plotaxes.jsx — axis scales for the canvas plots
//
// Shared helpers so every spectrum-type plot carries labelled axes with units:
//   * spDrawAxes(): overlay for the HP-CRT style screens of the Spectrum,
//     Radio Astronomy and Signal Classifier pages (frequency in MHz along the
//     bottom, level in dB at the left edge, axis titles with units). It is an
//     overlay on the existing full-canvas graticule, so the geometry that the
//     cursor, zoom and touch handlers rely on does not change.
//   * paTicks / paPanel / paFmtHz: round ticks and a framed plot panel with
//     outside tick labels and axis titles, used by the Complex I/Q page.
//
// The level axis is "dB (uncal.)": 20*log10 of the FFT bin amplitude. It is not
// dBm, there is no absolute calibration yet.

const PA_FONT  = '600 11px "IBM Plex Mono", ui-monospace, monospace';
const PA_FONT_S = '600 10px "IBM Plex Mono", ui-monospace, monospace';
const PA_TEXT  = '#c9d4e0';
const PA_DIM   = 'rgba(201,212,224,0.55)';
const PA_GRID  = 'rgba(255,255,255,0.10)';
const PA_FRAME = 'rgba(255,255,255,0.35)';

// Round tick positions covering [lo, hi]: about `target` ticks with a
// 1-2-5 step. Returns { ticks, step, dec } (dec = decimals for labels).
function paTicks(lo, hi, target = 8) {
  const span = hi - lo;
  if (!(span > 0)) return { ticks: [lo], step: 1, dec: 0 };
  const raw = span / target;
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const f = raw / mag;
  const step = (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * mag;
  const ticks = [];
  for (let v = Math.ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9; v += step) {
    ticks.push(+v.toPrecision(12));
  }
  const dec = Math.max(0, -Math.floor(Math.log10(step) + 1e-9));
  return { ticks, step, dec };
}

// "12.5 kHz" style label for a frequency (offset) in Hz.
function paFmtHz(hz, dec) {
  const a = Math.abs(hz);
  if (a >= 1e9) return (hz / 1e9).toFixed(dec ?? 3) + ' GHz';
  if (a >= 1e6) return (hz / 1e6).toFixed(dec ?? 3) + ' MHz';
  if (a >= 1e3) return (hz / 1e3).toFixed(dec ?? 1) + ' kHz';
  return hz.toFixed(dec ?? 0) + ' Hz';
}

// Fewest decimals that show every value exactly (up to 6)
function paDecimals(vals) {
  let dec = 0;
  while (dec < 6 && vals.some((v) => Math.abs(v * Math.pow(10, dec) - Math.round(v * Math.pow(10, dec))) > 1e-6)) dec++;
  return dec;
}

function paText(ctx, s, x, y, o = {}) {
  ctx.font = o.font || PA_FONT;
  ctx.textAlign = o.align || 'left';
  ctx.textBaseline = o.base || 'middle';
  if (o.halo) {
    ctx.lineWidth = 3;
    ctx.lineJoin = 'round';
    ctx.strokeStyle = o.halo;
    ctx.strokeText(s, x, y);
  }
  ctx.fillStyle = o.color || PA_TEXT;
  ctx.fillText(s, x, y);
}

// Axis overlay for the HP-CRT screens. ax = { centerHz, spanHz, vs, ve,
// refDb, range, cols, rows, bottomInset }. vs/ve is the visible fraction of
// the span (zoom), bottomInset reserves room for a strip along the bottom.
function spDrawAxes(ctx, W, H, ax) {
  if (!ax) return;
  const cols = ax.cols || 10, rows = ax.rows || 8;
  const inset = ax.bottomInset || 0;
  const vs = ax.vs ?? 0, ve = ax.ve ?? 1;
  const halo = 'rgba(10,6,0,0.85)';
  const col = '#ffe7a8', dim = 'rgba(255,225,160,0.75)';
  ctx.save();

  // level axis: value at every horizontal gridline
  for (let i = 0; i <= rows; i++) {
    const y = (i / rows) * H;
    const db = ax.refDb - (i / rows) * ax.range;
    const lbl = Math.round(db) + '';
    if (i === 0) paText(ctx, lbl, 4, y + 8, { color: col, halo });
    else if (i === rows) paText(ctx, lbl, 4, y - 18 - inset, { color: col, halo });
    else paText(ctx, lbl, 4, y, { color: col, halo });
  }
  // level axis title and unit
  paText(ctx, 'dB (uncal.)', 30, 8, { color: dim, halo, font: PA_FONT_S });

  // frequency axis: value at every vertical gridline
  if (ax.centerHz != null && ax.spanHz != null) {
    const fMHz = (i) => (ax.centerHz + (vs + (i / cols) * (ve - vs) - 0.5) * ax.spanHz) / 1e6;
    const dec = paDecimals(Array.from({ length: cols + 1 }, (_, i) => fMHz(i)));
    for (let i = 0; i <= cols; i++) {
      const x = (i / cols) * W;
      const frac = vs + (i / cols) * (ve - vs);
      const f = ax.centerHz + (frac - 0.5) * ax.spanHz;
      const lbl = (f / 1e6).toFixed(dec);
      const align = i === 0 ? 'left' : i === cols ? 'right' : 'center';
      const x0 = i === 0 ? Math.max(x + 3, 36) : i === cols ? x - 3 : x;
      paText(ctx, lbl, x0, H - inset - 8, { color: col, halo, align });
    }
    paText(ctx, 'Frequency (MHz)', W - 4, H - inset - 22, { color: dim, halo, align: 'right', font: PA_FONT_S });
  } else {
    paText(ctx, 'Frequency (MHz): span unknown', W - 4, H - inset - 8, { color: dim, halo, align: 'right', font: PA_FONT_S });
  }
  ctx.restore();
}

// Framed plot panel. r = { x, y, w, h } in canvas px; o describes the axes:
//   x0,x1,y0,y1       data ranges (xLog: x0,x1 are log10 values)
//   xTicks,yTicks     [{ v, label }]
//   xLabel,yLabel     axis titles with units
//   showX             draw x tick labels and title below the panel
// Returns { X(v), Y(v) } mapping data to canvas px.
function paPanel(ctx, r, o) {
  const X = (v) => r.x + ((v - o.x0) / (o.x1 - o.x0)) * r.w;
  const Y = (v) => r.y + r.h - ((v - o.y0) / (o.y1 - o.y0)) * r.h;
  ctx.save();
  ctx.fillStyle = '#0b0f14';
  ctx.fillRect(r.x, r.y, r.w, r.h);
  ctx.lineWidth = 1;
  ctx.strokeStyle = PA_GRID;
  for (const t of o.xTicks || []) {
    const x = Math.round(X(t.v)) + 0.5;
    if (x < r.x || x > r.x + r.w) continue;
    ctx.beginPath(); ctx.moveTo(x, r.y); ctx.lineTo(x, r.y + r.h); ctx.stroke();
  }
  for (const t of o.yTicks || []) {
    const y = Math.round(Y(t.v)) + 0.5;
    if (y < r.y || y > r.y + r.h) continue;
    ctx.beginPath(); ctx.moveTo(r.x, y); ctx.lineTo(r.x + r.w, y); ctx.stroke();
  }
  // zero line, if inside
  if (o.y0 < 0 && o.y1 > 0) {
    ctx.strokeStyle = 'rgba(255,255,255,0.28)';
    const y = Math.round(Y(0)) + 0.5;
    ctx.beginPath(); ctx.moveTo(r.x, y); ctx.lineTo(r.x + r.w, y); ctx.stroke();
  }
  ctx.strokeStyle = PA_FRAME;
  ctx.strokeRect(r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1);
  // tick labels
  for (const t of o.yTicks || []) {
    const y = Y(t.v);
    if (y < r.y - 1 || y > r.y + r.h + 1) continue;
    paText(ctx, t.label, r.x - 6, y, { align: 'right' });
  }
  if (o.showX) {
    for (const t of o.xTicks || []) {
      const x = X(t.v);
      if (x < r.x - 1 || x > r.x + r.w + 1) continue;
      paText(ctx, t.label, x, r.y + r.h + 12, { align: 'center' });
    }
    if (o.xLabel) paText(ctx, o.xLabel, r.x + r.w / 2, r.y + r.h + 29, { align: 'center', color: PA_DIM });
  }
  if (o.yLabel) {
    ctx.translate(r.x - 46, r.y + r.h / 2);
    ctx.rotate(-Math.PI / 2);
    paText(ctx, o.yLabel, 0, 0, { align: 'center', color: PA_DIM });
  }
  ctx.restore();
  return { X, Y };
}

// Axis overlay for a normalised-shape plot: y runs -1..+1 (unitless), x is the
// frequency span in MHz. ax = { centerHz, spanHz }.
function paDrawNormAxes(ctx, W, H, ax) {
  const halo = 'rgba(11,15,20,0.9)';
  ctx.save();
  ctx.strokeStyle = PA_GRID; ctx.lineWidth = 1;
  for (const v of [1, 0, -1]) {
    const y = Math.round(H - ((v + 1) / 2) * H) + 0.5;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
    paText(ctx, (v > 0 ? '+' : '') + v, W - 4, Math.min(H - 8, Math.max(8, y)), { align: 'right', halo, color: PA_DIM, font: PA_FONT_S });
  }
  paText(ctx, 'normalised level (unitless)', W - 30, 8, { align: 'right', halo, color: PA_DIM, font: PA_FONT_S });
  if (ax && ax.centerHz != null && ax.spanHz != null) {
    const cols = 5;
    const dec = paDecimals(Array.from({ length: cols + 1 }, (_, i) => (ax.centerHz + (i / cols - 0.5) * ax.spanHz) / 1e6));
    for (let i = 0; i <= cols; i++) {
      const x = (i / cols) * W, f = ax.centerHz + (i / cols - 0.5) * ax.spanHz;
      ctx.strokeStyle = PA_GRID; ctx.beginPath(); ctx.moveTo(Math.round(x) + 0.5, 0); ctx.lineTo(Math.round(x) + 0.5, H); ctx.stroke();
      paText(ctx, (f / 1e6).toFixed(dec), i === 0 ? x + 3 : i === cols ? x - 3 : x, H - 24,
             { align: i === 0 ? 'left' : i === cols ? 'right' : 'center', halo });
    }
    paText(ctx, 'Frequency (MHz)', W / 2, 8, { align: 'center', halo, color: PA_DIM, font: PA_FONT_S });
  }
  ctx.restore();
}

Object.assign(window, { paDrawNormAxes, paTicks, paFmtHz, paText, paPanel, spDrawAxes, PA_TEXT, PA_DIM, PA_GRID, PA_FRAME, PA_FONT, PA_FONT_S });

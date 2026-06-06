// charts.jsx — gauges + streaming charts for Tezuka Dashboard
const { useState, useEffect, useRef } = React;

// ---- Donut gauge ----------------------------------------------------------
function Donut({ value, max = 100, label, unit = "%", size = 132, stroke = 11, color }) {
  const r = (size - stroke) / 2;
  const c = 2 * Math.PI * r;
  const pct = Math.max(0, Math.min(1, value / max));
  const dash = c * pct;
  const accent = color || "var(--accent)";
  return (
    <div className="gauge">
      <svg width={size} height={size} viewBox={`0 0 ${size} ${size}`} style={{ transform: "rotate(-90deg)" }}>
        <circle cx={size / 2} cy={size / 2} r={r} fill="none" stroke="var(--track)" strokeWidth={stroke} />
        <circle cx={size / 2} cy={size / 2} r={r} fill="none" stroke={accent} strokeWidth={stroke}
          strokeLinecap="round" strokeDasharray={`${dash} ${c}`}
          style={{ transition: "stroke-dasharray 0.6s cubic-bezier(.4,0,.2,1)" }} />
      </svg>
      <div className="gauge-center">
        <span className="gauge-val">{Math.round(value)}<i>{unit}</i></span>
      </div>
      <div className="gauge-label">{label}</div>
    </div>
  );
}

// ---- Vertical bar gauge (temperature) -------------------------------------
function BarGauge({ value, max = 100, label, unit = "°C", color }) {
  const pct = Math.max(0, Math.min(1, value / max));
  const accent = color || "var(--accent)";
  return (
    <div className="gauge">
      <div className="bar-wrap">
        <div className="bar-track">
          <div className="bar-fill" style={{ height: `${pct * 100}%`, background: accent }} />
          <div className="bar-readout">{Math.round(value)}<i>{unit}</i></div>
        </div>
      </div>
      <div className="gauge-label">{label}</div>
    </div>
  );
}

// ---- Half radial gauge (signal/level) -------------------------------------
function RadialGauge({ value, max = 100, label, unit = "", color }) {
  const size = 150, stroke = 12;
  const r = (size - stroke) / 2;
  const cx = size / 2, cy = size / 2;
  const semi = Math.PI * r;
  const pct = Math.max(0, Math.min(1, value / max));
  const accent = color || "var(--accent)";
  const arc = (frac) => {
    const a = Math.PI + Math.PI * frac;
    return `${cx + r * Math.cos(a)} ${cy + r * Math.sin(a)}`;
  };
  return (
    <div className="gauge">
      <svg width={size} height={size / 2 + 14} viewBox={`0 0 ${size} ${size / 2 + 14}`}>
        <path d={`M ${cx - r} ${cy} A ${r} ${r} 0 0 1 ${cx + r} ${cy}`} fill="none" stroke="var(--track)" strokeWidth={stroke} strokeLinecap="round" />
        <path d={`M ${cx - r} ${cy} A ${r} ${r} 0 0 1 ${arc(pct)}`} fill="none" stroke={accent} strokeWidth={stroke} strokeLinecap="round"
          style={{ transition: "all .5s cubic-bezier(.4,0,.2,1)" }} />
        <text x={cx} y={cy - 6} textAnchor="middle" className="radial-val">{value.toFixed(value < 10 ? 1 : 0)}<tspan className="radial-unit">{unit}</tspan></text>
      </svg>
      <div className="gauge-label">{label}</div>
    </div>
  );
}

// ---- Streaming line chart (SVG) -------------------------------------------
function StreamChart({ series, height = 230, maxY, rightMax, unit = "", grid = 5, fmt = (v) => v.toFixed(2) }) {
  const ref = useRef(null);
  const [w, setW] = useState(600);
  const [hover, setHover] = useState(null);
  useEffect(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver((e) => setW(e[0].contentRect.width));
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);
  const hasRight = rightMax != null;
  const padL = 48, padB = 22, padT = 12, padR = hasRight ? 48 : 12;
  const leftVals = series.filter((s) => s.axis !== "right").flatMap((s) => s.data);
  const leftMax = maxY || Math.max(1, Math.ceil(Math.max(...leftVals, 0.001) * 1.25));
  const innerW = w - padL - padR, innerH = height - padT - padB;
  const n = series[0] ? series[0].data.length : 0;
  const xAt = (i) => padL + (n <= 1 ? 0 : (i / (n - 1)) * innerW);
  const yAt = (v, right) => padT + innerH - (v / (right ? rightMax : leftMax)) * innerH;
  const linePath = (s) => s.data.map((v, i) => `${i === 0 ? "M" : "L"} ${xAt(i).toFixed(1)} ${yAt(v, s.axis === "right").toFixed(1)}`).join(" ");
  const areaPath = (s) => `${linePath(s)} L ${xAt(n - 1)} ${padT + innerH} L ${xAt(0)} ${padT + innerH} Z`;
  const ticks = Array.from({ length: grid + 1 }, (_, i) => (leftMax / grid) * i);
  const rTicks = hasRight ? Array.from({ length: grid + 1 }, (_, i) => (rightMax / grid) * i) : [];

  const onMove = (e) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    if (n <= 1) return;
    let idx = Math.round(((x - padL) / innerW) * (n - 1));
    idx = Math.max(0, Math.min(n - 1, idx));
    setHover(idx);
  };
  return (
    <div ref={ref} className="stream-wrap" style={{ width: "100%", position: "relative" }}>
      <svg width={w} height={height} className="stream" onMouseMove={onMove} onMouseLeave={() => setHover(null)}>
        {ticks.map((t, i) => (
          <g key={i}>
            <line x1={padL} x2={w - padR} y1={yAt(t)} y2={yAt(t)} className="grid-line" />
            <text x={padL - 8} y={yAt(t) + 4} textAnchor="end" className="axis-label">{fmt(t)}{unit}</text>
          </g>
        ))}
        {rTicks.map((t, i) => (
          <text key={"r" + i} x={w - padR + 8} y={yAt(t, true) + 4} textAnchor="start" className="axis-label">{t.toFixed(1)}</text>
        ))}
        {series.map((s, si) => (
          <g key={si}>
            {!hasRight && <path d={areaPath(s)} fill={s.color} opacity="0.10" />}
            <path d={linePath(s)} fill="none" stroke={s.color} strokeWidth="2" strokeLinejoin="round" />
          </g>
        ))}
        {hover != null && (
          <g pointerEvents="none">
            <line x1={xAt(hover)} x2={xAt(hover)} y1={padT} y2={padT + innerH} className="crosshair" />
            {series.map((s, si) => (
              <circle key={si} cx={xAt(hover)} cy={yAt(s.data[hover], s.axis === "right")} r="3.5" fill="var(--panel)" stroke={s.color} strokeWidth="2" />
            ))}
          </g>
        )}
      </svg>
      {hover != null && (
        <div className="chart-tip" style={{ left: Math.min(Math.max(xAt(hover), 70), w - 70) }}>
          <div className="tip-time mono">−{n - 1 - hover}s</div>
          {series.map((s, si) => (
            <div key={si} className="tip-row"><span className="tip-dot" style={{ background: s.color }} /><span className="tip-name">{s.label || "series"}</span><b className="mono">{(s.data[hover]).toFixed(s.axis === "right" ? 2 : 0)}{s.unit || ""}</b></div>
          ))}
        </div>
      )}
    </div>
  );
}

// ---- Tiny sparkline -------------------------------------------------------
function Sparkline({ data, color, width = 120, height = 36 }) {
  const max = Math.max(...data, 0.001), min = Math.min(...data, 0);
  const range = max - min || 1;
  const pts = data.map((v, i) => `${(i / (data.length - 1)) * width},${height - ((v - min) / range) * (height - 4) - 2}`).join(" ");
  return (
    <svg width={width} height={height} className="spark">
      <polyline points={pts} fill="none" stroke={color || "var(--accent)"} strokeWidth="1.8" strokeLinejoin="round" strokeLinecap="round" />
    </svg>
  );
}

// ---- Analog dial gauge with overheat zones (temperature) ------------------
function DialGauge({ value, min = 0, max = 65, label, unit = "°C", warnFrac = 0.78, dangerFrac = 0.90, color = "var(--ok)" }) {
  const W = 232, H = 196, cx = W / 2, cy = 104;
  const R = 80, BW = 12;                 // band center radius, band width
  const A0 = 135, SWEEP = 270;           // start angle (deg, screen-cw from east) + sweep
  const rad = (d) => (d * Math.PI) / 180;
  const ang = (f) => A0 + f * SWEEP;
  const pt = (r, f) => { const a = rad(ang(f)); return [cx + r * Math.cos(a), cy + r * Math.sin(a)]; };
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const vF = clamp((value - min) / (max - min), 0, 1);

  const arc = (f1, f2, r) => {
    const [x1, y1] = pt(r, f1), [x2, y2] = pt(r, f2);
    const large = (f2 - f1) * SWEEP > 180 ? 1 : 0;
    return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
  };

  const majors = 10, perMajor = 5, total = majors * perMajor;
  const ticks = Array.from({ length: total + 1 }, (_, i) => {
    const f = i / total, big = i % perMajor === 0;
    const [ix, iy] = pt(R - BW / 2, f);
    const [ox, oy] = pt(R - BW / 2 - (big ? 13 : 7), f);
    return <line key={i} x1={ix} y1={iy} x2={ox} y2={oy} stroke="var(--text)" strokeWidth={big ? 1.8 : 1} opacity={big ? 0.8 : 0.42} />;
  });
  const labels = Array.from({ length: majors + 1 }, (_, i) => {
    const f = i / majors, v = min + f * (max - min);
    const [lx, ly] = pt(R + BW / 2 + 15, f);
    return <text key={i} x={lx} y={ly} textAnchor="middle" dominantBaseline="middle" className="dial-lbl">{v.toFixed(2)}</text>;
  });

  // needle (tapered) + small tail
  const [tx, ty] = pt(R - BW / 2 - 5, vF);
  const ba = rad(ang(vF) + 90), bw = 4.5;
  const b1 = [cx + bw * Math.cos(ba), cy + bw * Math.sin(ba)];
  const b2 = [cx - bw * Math.cos(ba), cy - bw * Math.sin(ba)];
  const tail = pt(-15, vF);

  return (
    <div className="gauge">
      <svg width={W} height={H} viewBox={`0 0 ${W} ${H}`} className="dial">
        <path d={arc(0, 1, R)} fill="none" stroke="var(--track)" strokeWidth={BW} />
        <path d={arc(0, warnFrac, R)} fill="none" stroke={color} strokeWidth={BW} />
        <path d={arc(warnFrac, dangerFrac, R)} fill="none" stroke="var(--warn)" strokeWidth={BW} />
        <path d={arc(dangerFrac, 1, R)} fill="none" stroke="var(--bad)" strokeWidth={BW} />
        {ticks}
        {labels}
        <g style={{ transition: "transform .6s cubic-bezier(.34,1.2,.5,1)" }}>
          <polygon points={`${b1[0]},${b1[1]} ${tx},${ty} ${b2[0]},${b2[1]} ${tail[0]},${tail[1]}`} fill="var(--text)" />
        </g>
        <circle cx={cx} cy={cy} r="8" fill="var(--panel-2)" stroke="var(--text)" strokeWidth="2" />
        <text x={cx} y={cy + 40} textAnchor="middle" className="dial-name">{label}</text>
        <text x={cx} y={cy + 60} textAnchor="middle" className="dial-val mono">{value.toFixed(2)} {unit}</text>
      </svg>
    </div>
  );
}

// ---- Static XY line chart (e.g. gain vs frequency) ------------------------
function XYChart({ points, height = 280, color = "var(--accent)", xTicks = 6, yTicks = 5, fmtX = (v) => v, fmtY = (v) => v, xUnit = "", yUnit = "", editable = false, onPointChange, yMin: yMinProp, yMax: yMaxProp }) {
  const ref = useRef(null);
  const [w, setW] = useState(640);
  const [hover, setHover] = useState(null);
  const dragRef = useRef(null);
  useEffect(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver((e) => setW(e[0].contentRect.width));
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);
  const padL = 56, padB = 30, padT = 14, padR = 18;
  const xs = points.map((p) => p.x), ys = points.map((p) => p.y);
  const xMin = Math.min(...xs), xMax = Math.max(...xs);
  const yMin = yMinProp != null ? yMinProp : Math.floor(Math.min(...ys) - 1);
  const yMax = yMaxProp != null ? yMaxProp : Math.ceil(Math.max(...ys) + 1);
  const innerW = w - padL - padR, innerH = height - padT - padB;
  const xAt = (x) => padL + ((x - xMin) / (xMax - xMin || 1)) * innerW;
  const yAt = (y) => padT + innerH - ((y - yMin) / (yMax - yMin || 1)) * innerH;
  const yToVal = (py) => Math.max(yMin, Math.min(yMax, yMin + ((padT + innerH - py) / innerH) * (yMax - yMin)));
  const linePath = points.map((p, i) => `${i === 0 ? "M" : "L"} ${xAt(p.x).toFixed(1)} ${yAt(p.y).toFixed(1)}`).join(" ");
  const areaPath = `${linePath} L ${xAt(xMax)} ${padT + innerH} L ${xAt(xMin)} ${padT + innerH} Z`;
  const yTickVals = Array.from({ length: yTicks + 1 }, (_, i) => yMin + ((yMax - yMin) / yTicks) * i);
  const xTickVals = Array.from({ length: xTicks + 1 }, (_, i) => xMin + ((xMax - xMin) / xTicks) * i);

  const nearestIdx = (x) => {
    let best = 0, bestD = Infinity;
    points.forEach((p, i) => { const d = Math.abs(xAt(p.x) - x); if (d < bestD) { bestD = d; best = i; } });
    return best;
  };
  const onDown = (e) => {
    if (!editable || !onPointChange) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const idx = nearestIdx(e.clientX - rect.left);
    dragRef.current = idx;
    setHover(idx);
    onPointChange(idx, yToVal(e.clientY - rect.top));
  };
  const onMove = (e) => {
    const rect = e.currentTarget.getBoundingClientRect();
    if (dragRef.current != null) {
      onPointChange(dragRef.current, yToVal(e.clientY - rect.top));
      return;
    }
    setHover(nearestIdx(e.clientX - rect.left));
  };
  const endDrag = () => { dragRef.current = null; };
  return (
    <div ref={ref} className="stream-wrap" style={{ width: "100%", position: "relative" }}>
      <svg width={w} height={height} className="stream" style={{ cursor: editable ? (hover != null ? "ns-resize" : "crosshair") : "default", touchAction: "none" }}
        onMouseDown={onDown} onMouseMove={onMove} onMouseUp={endDrag}
        onMouseLeave={() => { endDrag(); setHover(null); }}>
        {yTickVals.map((t, i) => (
          <g key={i}>
            <line x1={padL} x2={w - padR} y1={yAt(t)} y2={yAt(t)} className="grid-line" />
            <text x={padL - 8} y={yAt(t) + 4} textAnchor="end" className="axis-label">{fmtY(t)}{yUnit}</text>
          </g>
        ))}
        {xTickVals.map((t, i) => (
          <text key={"x" + i} x={xAt(t)} y={height - padB + 18} textAnchor="middle" className="axis-label">{fmtX(t)}</text>
        ))}
        <path d={areaPath} fill={color} opacity="0.10" />
        <path d={linePath} fill="none" stroke={color} strokeWidth="2" strokeLinejoin="round" />
        {editable && points.map((p, i) => (
          <circle key={"h" + i} cx={xAt(p.x)} cy={yAt(p.y)} r={hover === i ? 4.5 : 2.5}
            fill={hover === i ? "var(--panel)" : color} stroke={color} strokeWidth={hover === i ? 2 : 0} />
        ))}
        {hover != null && (
          <g pointerEvents="none">
            <line x1={xAt(points[hover].x)} x2={xAt(points[hover].x)} y1={padT} y2={padT + innerH} className="crosshair" />
            {!editable && <circle cx={xAt(points[hover].x)} cy={yAt(points[hover].y)} r="3.5" fill="var(--panel)" stroke={color} strokeWidth="2" />}
          </g>
        )}
      </svg>
      {hover != null && (
        <div className="chart-tip" style={{ left: Math.min(Math.max(xAt(points[hover].x), 80), w - 80) }}>
          <div className="tip-time mono">{fmtX(points[hover].x)} {xUnit}</div>
          <div className="tip-row"><span className="tip-dot" style={{ background: color }} /><span className="tip-name">Gain</span><b className="mono">{points[hover].y.toFixed(2)}{yUnit}</b></div>
        </div>
      )}
    </div>
  );
}

Object.assign(window, { Donut, BarGauge, RadialGauge, DialGauge, StreamChart, Sparkline, XYChart });

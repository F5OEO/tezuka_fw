// tuner.jsx — SDR++-style per-digit segmented frequency / sample-rate tuner
const { useState: useTu, useRef: useTuRef, useEffect: useTuEffect } = React;

function FreqTuner({ value, onChange, digits = 12, min = 0, max = Infinity, unit = "Hz", sub }) {
  const [hover, setHover] = useTu(null);
  const ref = useTuRef(null);
  // Refs so the non-passive wheel listener always sees current values without re-attaching
  const hoverRef = useTuRef(null);
  const bumpRef = useTuRef(null);

  const clamp = (v) => Math.max(min, Math.min(max, Math.round(v)));
  const str = String(Math.max(0, Math.round(value))).padStart(digits, "0").slice(-digits);
  const firstSig = (() => { const m = str.search(/[1-9]/); return m === -1 ? digits - 1 : m; })();

  bumpRef.current = (i, dir) => onChange(clamp(value + dir * Math.pow(10, digits - 1 - i)));
  hoverRef.current = hover;

  // Non-passive wheel listener — attached once, reads current values via refs
  useTuEffect(() => {
    const el = ref.current;
    if (!el) return;
    const handler = (e) => {
      if (hoverRef.current == null) return;
      e.preventDefault();
      bumpRef.current(hoverRef.current, e.deltaY < 0 ? 1 : -1);
    };
    el.addEventListener('wheel', handler, { passive: false });
    return () => el.removeEventListener('wheel', handler);
  }, []);

  const onClick = (e, i) => { const r = e.currentTarget.getBoundingClientRect(); bumpRef.current(i, (e.clientY - r.top) < r.height / 2 ? 1 : -1); };
  const onKey = (e) => {
    if (hover == null) return;
    if (e.key === "ArrowUp") { e.preventDefault(); bumpRef.current(hover, 1); }
    else if (e.key === "ArrowDown") { e.preventDefault(); bumpRef.current(hover, -1); }
    else if (e.key === "ArrowLeft") { e.preventDefault(); setHover(Math.max(0, hover - 1)); }
    else if (e.key === "ArrowRight") { e.preventDefault(); setHover(Math.min(digits - 1, hover + 1)); }
    else if (/^[0-9]$/.test(e.key)) { e.preventDefault(); const arr = str.split(""); arr[hover] = e.key; onChange(clamp(parseInt(arr.join(""), 10))); setHover(Math.min(digits - 1, hover + 1)); }
  };

  return (
    <div className="ftuner-wrap">
      <div className="ftuner" ref={ref} tabIndex={0} onKeyDown={onKey}>
        {str.split("").map((ch, i) => {
          const dim = i < firstSig;
          const hot = hover === i;
          const dot = i > 0 && (digits - i) % 3 === 0;
          return (
            <React.Fragment key={i}>
              {dot && <span className={`fdot ${i <= firstSig ? "dim" : ""}`}>.</span>}
              <span className={`fd ${dim ? "dim" : ""} ${hot ? "hot" : ""}`}
                onClick={(e) => onClick(e, i)}
                onMouseEnter={() => setHover(i)} onMouseLeave={() => setHover(null)}>
                <b className="fcaret up">▲</b>
                <span className="fdig">{ch}</span>
                <b className="fcaret dn">▼</b>
                <i className="fund" />
              </span>
            </React.Fragment>
          );
        })}
        <span className="funit">{unit}</span>
      </div>
      {sub && <div className="ftuner-sub mono">{sub(value)}</div>}
    </div>
  );
}

window.FreqTuner = FreqTuner;

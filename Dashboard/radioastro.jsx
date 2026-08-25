// radioastro.jsx — Radio Astronomy page: integrated (moving-average) maia-sdr FFT spectrum
//
// maia-sdr is put in "Average" spectrometer mode at a fixed 1 fps — each
// incoming /waterfall frame is therefore already an on-device average over
// ~1s. This page keeps a sliding window of the last INTEG seconds of those
// frames (in linear domain, before the dB conversion) and displays their
// mean, boosting SNR on weak / narrowband sources (e.g. the HI 21cm line).
// The window slides one frame per second — no periodic reset — so the trace
// updates every second and old frames age out continuously.
const { useState: useSRa, useEffect: useERa, useRef: useRRa } = React;

if (!window._ra) window._ra = {};

function RadioAstronomyPage({ d }) {
  const ra = window._ra;
  const canvasRef    = useRRa(null);
  const wsRef        = useRRa(null);
  const binsRef       = useRRa(null);   // Float32Array dB, current window average
  const ringRef       = useRRa([]);     // array of Float32Array raw linear frames currently in the window
  const sumRef        = useRRa(null);   // Float64Array running sum of frames in ringRef
  const ringLenRef    = useRRa(0);      // bin count of frames currently tracked (detects FFT size changes)
  const refBinsRef    = useRRa(null);   // Float64Array raw linear background reference, captured via CAL
  const holdRef        = useRRa(false);
  const calOnRef        = useRRa(false);
  const mousePosRef    = useRRa(null);
  const integSecRef    = useRRa(ra.integSec ?? 30);
  const centerHzRef    = useRRa(ra.centerHz ?? (d.rxFreq ?? 1420405751));
  const bandwidthRef   = useRRa(ra.bandwidth ?? 2.5e6);
  const refDbRef       = useRRa(ra.refDb ?? 130);
  const rangeRef       = useRRa(ra.range ?? SP_ROWS * 10);

  // Frequency switching (classical Dicke-style calibration): alternate the
  // LO between CENTER (SIG) and CENTER+OFS (REF) every INTEG seconds,
  // snapshotting each completed dwell and displaying SIG-REF continuously.
  const fswOnRef       = useRRa(false);
  const fswOffsetRef   = useRRa(ra.fswOffsetKHz ?? 500);
  const fswPhaseRef    = useRRa('sig');       // 'sig' | 'ref' — which frequency is currently tuned
  const fswSigRef      = useRRa(null);        // Float64Array raw linear, last completed SIG dwell
  const fswRefRef      = useRRa(null);        // Float64Array raw linear, last completed REF dwell
  const fswPhaseStartRef = useRRa(0);         // performance.now() of the current dwell's start

  const [bandwidth, setBandwidth] = useSRa(bandwidthRef.current);
  const [integSec,  setIntegSec]  = useSRa(integSecRef.current);
  const [gain,      setGain]      = useSRa(() => ra.gain ?? (d.rxGain ?? 40));
  const [centerHz,  setCenterHz]  = useSRa(centerHzRef.current);
  const [refDb,     setRefDb]     = useSRa(refDbRef.current);
  const [range,     setRange]     = useSRa(rangeRef.current);
  const [wsState,   setWsState]   = useSRa('disconnected');
  const [frames,    setFrames]    = useSRa(0);   // frames currently in the sliding window (caps at integSec)
  const [mkr,       setMkr]       = useSRa(null);
  const [hold,      setHold]      = useSRa(false);
  const [calOn,     setCalOn]     = useSRa(false);
  const [fswOn,       setFswOn]       = useSRa(false);
  const [fswOffsetKHz, setFswOffsetKHz] = useSRa(fswOffsetRef.current);
  const [fswPhase,    setFswPhase]    = useSRa('sig');

  function redraw() {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const w = canvas.offsetWidth, h = canvas.offsetHeight;
    if (w <= 0 || h <= 0) return;
    if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
    const ctx = canvas.getContext('2d');
    spDraw(ctx, w, h, binsRef.current, refDbRef.current, rangeRef.current, 0, 1, 'off');
    spDrawCursor(ctx, w, h, mousePosRef.current, binsRef.current,
                 centerHzRef.current, bandwidthRef.current, refDbRef.current, rangeRef.current, 0, 1);
  }

  // Keep refs current for the WS handler (mounted once) + persist to session store
  useERa(() => { bandwidthRef.current = bandwidth; ra.bandwidth = bandwidth; redraw(); }, [bandwidth]);
  useERa(() => { integSecRef.current  = integSec;  ra.integSec  = integSec;  }, [integSec]);
  useERa(() => { centerHzRef.current  = centerHz;  ra.centerHz  = centerHz;  redraw(); }, [centerHz]);
  useERa(() => { refDbRef.current     = refDb;     ra.refDb     = refDb;     redraw(); }, [refDb]);
  useERa(() => { rangeRef.current     = range;     ra.range     = range;     redraw(); }, [range]);
  useERa(() => { ra.gain = gain; }, [gain]);
  useERa(() => { holdRef.current = hold; }, [hold]);
  useERa(() => { calOnRef.current = calOn; }, [calOn]);
  useERa(() => { fswOnRef.current = fswOn; }, [fswOn]);
  useERa(() => { fswOffsetRef.current = fswOffsetKHz; ra.fswOffsetKHz = fswOffsetKHz; }, [fswOffsetKHz]);

  // Sync from MQTT state — mirrors whatever the device is actually set to.
  // rxFreq is skipped while FSW is running: CENTER must stay pinned to the
  // SIG frequency the user asked for, not whichever of SIG/REF the hardware
  // happens to be tuned to at that instant (the echo would otherwise stomp
  // it every switch, collapsing SIG and REF onto the same frequency).
  useERa(() => { if (d.rxGain  != null) setGain(d.rxGain); }, [d.rxGain]);
  useERa(() => { if (d.rxFreq  != null && !fswOnRef.current) setCenterHz(d.rxFreq); }, [d.rxFreq]);
  useERa(() => { const s = d.span ?? d.rxSampling; if (s != null) setBandwidth(s); }, [d.span, d.rxSampling]);

  // One-time device setup: manual gain, spectrometer Average mode @ 1 fps,
  // the default 2.5 MHz acquisition bandwidth (sets rx/sampling + bandwidth),
  // and the AD9361 DC/quadrature calibration loops — these null out LO
  // self-mixing leakage, which is what shows up as the big spike at the
  // exact center frequency on any zero-IF receiver (see the DC OFFSET hint
  // above the CRT for the other half of the fix).
  useERa(() => {
    d.publish('rx/gain_mode', 'manual');
    d.publish('rx/rf_dc_tracking', 1);
    d.publish('rx/bb_dc_tracking', 1);
    d.publish('rx/quad_tracking', 1);
    d.publish('spectro/mode', 'Average');
    d.publish('spectro/fps', 1);
    d.publish('rx/span', Math.round(bandwidth));
  }, []);

  // Canvas resize
  useERa(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver(() => redraw());
    ro.observe(canvas);
    redraw();
    return () => ro.disconnect();
  }, []);

  // WebSocket: same /waterfall feed used by the Spectrum page. f[0] is a
  // structural step index (unused outside sweep mode); f[1..] are the FFT
  // bins as raw linear amplitude. Frames feed a sliding window: each new
  // frame is added to the running sum, frames older than INTEG seconds
  // (fps is fixed at 1, so 1 frame ~= 1s) age out and are subtracted back
  // out, and the mean of whatever's currently in the window is redrawn on
  // every incoming frame (~1/s) — a continuously updating moving average.
  useERa(() => {
    let destroyed = false;
    const host  = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const toDB  = (v) => 20 * Math.log10(v > 0 ? v : 1e-10);

    function resetWindow(len) {
      ringRef.current = [];
      sumRef.current  = new Float64Array(len);
      ringLenRef.current = len;
      refBinsRef.current = null; // FFT size changed — any captured background is stale
      setFrames(0);
      setCalOn(false);
    }

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
        if (!(evt.data instanceof ArrayBuffer) || holdRef.current) return;
        const f = new Float32Array(evt.data);
        if (f.length < 2) return;
        const len = f.length - 1;
        if (!sumRef.current || ringLenRef.current !== len) resetWindow(len);

        // FSW: if the current dwell has run its course, snapshot it (raw
        // linear average of whatever accumulated), flip SIG<->REF, retune
        // the hardware, and start the new dwell's window fresh — all before
        // this frame is folded in, so it belongs to the new phase.
        if (fswOnRef.current && ringRef.current.length > 0) {
          const now = performance.now();
          const periodMs = Math.max(1, integSecRef.current) * 1000;
          if (now - fswPhaseStartRef.current >= periodMs) {
            const count0 = ringRef.current.length;
            const snap = new Float64Array(len);
            for (let i = 0; i < len; i++) snap[i] = sumRef.current[i] / count0;
            if (fswPhaseRef.current === 'sig') fswSigRef.current = snap; else fswRefRef.current = snap;
            fswPhaseRef.current = fswPhaseRef.current === 'sig' ? 'ref' : 'sig';
            fswPhaseStartRef.current = now;
            setFswPhase(fswPhaseRef.current);
            const targetHz = fswPhaseRef.current === 'sig'
              ? centerHzRef.current
              : centerHzRef.current + fswOffsetRef.current * 1000;
            d.publish('rx/frequency', Math.round(targetHz));
            resetWindow(len);
          }
        }

        const sum = sumRef.current;
        const frame = new Float32Array(len);
        for (let i = 0; i < len; i++) { frame[i] = f[i + 1]; sum[i] += frame[i]; }
        ringRef.current.push(frame);

        // Drop frames older than the current window size (integ time can
        // change live; the while loop catches up in one tick either way).
        const maxWindow = Math.max(1, Math.round(integSecRef.current));
        while (ringRef.current.length > maxWindow) {
          const old = ringRef.current.shift();
          for (let i = 0; i < len; i++) sum[i] -= old[i];
        }

        const count = ringRef.current.length;
        const db = new Float32Array(len);
        // With CAL or FSW on, display a dB *ratio* against the reference,
        // not a raw subtraction — a straight subtraction can land arbitrarily
        // close to zero (noise fluctuating right around the reference) and
        // log() of that blows up to huge negative dB. A ratio of two always-
        // positive power sums stays naturally bounded around 0 dB (matches
        // background), with genuine excursions (the line, RFI) standing out.
        if (fswOnRef.current) {
          const other = fswPhaseRef.current === 'sig' ? fswRefRef.current : fswSigRef.current;
          for (let i = 0; i < len; i++) {
            const cur = toDB(sum[i] / count);
            if (!other) { db[i] = cur; continue; }
            const oth = toDB(other[i]);
            db[i] = fswPhaseRef.current === 'sig' ? cur - oth : oth - cur; // always SIG - REF
          }
        } else {
          const ref = (calOnRef.current && refBinsRef.current && refBinsRef.current.length === len) ? refBinsRef.current : null;
          for (let i = 0; i < len; i++) {
            const avg = sum[i] / count;
            db[i] = ref ? toDB(avg) - toDB(ref[i]) : toDB(avg);
          }
        }
        binsRef.current = db;
        setFrames(count);
        redraw();
      };
    }
    connect();
    return () => { destroyed = true; try { wsRef.current?.close(); } catch (_) {} };
  }, []);

  // Retuning invalidates any captured background — a reference taken at a
  // different bandwidth/frequency/gain no longer lines up bin-for-bin.
  const invalidateCal = () => { refBinsRef.current = null; setCalOn(false); };
  const invalidateFsw = () => { fswSigRef.current = null; fswRefRef.current = null; };

  const pubBandwidth = (kHz) => {
    const hz = kHz * 1000;
    setBandwidth(hz);
    sumRef.current = null; // invalidate the window — sample rate is changing
    invalidateCal();
    invalidateFsw();
    d.publish('rx/span', Math.round(hz));
  };
  const pubCenter = (kHz) => {
    const hz = kHz * 1000;
    setCenterHz(hz);
    sumRef.current = null;
    invalidateCal();
    d.publish('rx/frequency', Math.round(hz));
  };
  const pubGain = (v) => {
    setGain(v);
    sumRef.current = null;
    invalidateCal();
    invalidateFsw();
    d.publish('rx/gain', v);
  };

  // CAL: capture the current window average (raw linear, pre-dB) as a
  // background reference and subtract it from every subsequent frame — the
  // standard radio-astronomy "acquire background" technique. Cancels the
  // DC/LO-leakage spike and any fixed bandpass ripple, as long as bandwidth/
  // center/gain don't change afterward (those auto-clear it, see above).
  // Click again to turn subtraction off; turning it back on always captures
  // a fresh reference rather than reusing whatever was there before.
  // Mutually exclusive with FSW (FSW owns CENTER while it runs) — switching
  // off FSW here just returns to plain mode; it does NOT also capture in the
  // same click, since disabling FSW resets the window (nothing to capture
  // yet). Click CAL again once WIN has refilled.
  const toggleCal = () => {
    if (calOn) { setCalOn(false); return; }
    if (fswOn) { disableFsw(); return; }
    if (!sumRef.current || ringRef.current.length === 0) return;
    const len = sumRef.current.length, count = ringRef.current.length;
    const ref = new Float64Array(len);
    for (let i = 0; i < len; i++) ref[i] = sumRef.current[i] / count;
    refBinsRef.current = ref;
    setCalOn(true);
  };

  // FSW: classical frequency-switching / Dicke calibration — alternate the
  // LO between CENTER (SIG) and CENTER+OFS (REF) every INTEG seconds and
  // continuously display SIG-REF (see the WS handler above). Mutually
  // exclusive with CAL (both want ownership of what "the reference" means).
  const disableFsw = () => {
    setFswOn(false);
    fswPhaseRef.current = 'sig';
    setFswPhase('sig');
    sumRef.current = null; // fresh plain window next frame
    d.publish('rx/frequency', Math.round(centerHzRef.current)); // park back on SIG
  };
  const toggleFsw = () => {
    if (fswOn) { disableFsw(); return; }
    if (calOn) invalidateCal();
    invalidateFsw();
    fswPhaseRef.current = 'sig';
    setFswPhase('sig');
    fswPhaseStartRef.current = performance.now();
    sumRef.current = null;
    setFswOn(true);
  };

  // toDB() clamps non-positive input to -200 dB. In CAL (ratio) mode a bin
  // where either the current average or the reference individually hit that
  // floor produces a difference around ±200 dB, not just -200 — so filter on
  // magnitude, not just a low-side cutoff. 150 dB is well outside any real
  // reading in either mode; only floor-clamp artifacts land past it.
  const AUTO_OUTLIER_DB = 150;

  const autoScale = () => {
    const bins = binsRef.current;
    if (!bins || bins.length === 0) return;
    let lo = Infinity, hi = -Infinity;
    for (let i = 0; i < bins.length; i++) {
      const v = bins[i];
      if (!isFinite(v) || Math.abs(v) > AUTO_OUTLIER_DB) continue;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    if (!isFinite(lo) || !isFinite(hi)) { lo = -SP_ROWS / 2; hi = SP_ROWS / 2; }
    const margin = Math.max(3, (hi - lo) * 0.15);
    setRefDb(Math.ceil(hi + margin));
    setRange(Math.max(SP_ROWS, Math.ceil((hi - lo) + margin * 2)));
  };

  const onCanvasMouseMove = (e) => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const px = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    mousePosRef.current = { px };
    const bins = binsRef.current;
    if (bins && bins.length > 0) {
      const binI = Math.max(0, Math.min(bins.length - 1, Math.round(px * (bins.length - 1))));
      setMkr({ freq: centerHz + (px - 0.5) * bandwidth, db: bins[binI] });
    }
    redraw();
  };
  const onCanvasMouseLeave = () => { mousePosRef.current = null; setMkr(null); redraw(); };

  const fmtMHz = (hz) => (hz / 1e6).toFixed(4);

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>Radio Astronomy</h1>
          <span className="datv-sub mono">Integrated FFT spectrum · maia-sdr Average mode @ 1 fps, moving-window mean over the last INTEG seconds</span>
        </div>
      </div>

      <div className="hp-crt">
        <div className="hp-rows">
          <div className="hp-row">
            <span className="hp-fld">
              <span className="hp-pfx">REF</span>
              <DbTuner value={Math.round(refDb)} digits={3} unit="dB" onChange={setRefDb} />
            </span>
            <span style={{ display: 'inline-flex', alignItems: 'baseline', gap: '1ch' }}>
              <CrtField pfx="MKR" val={fmtMHz(mkr ? mkr.freq : centerHz)} sfx="MHz" readOnly />
              <CrtField val={mkr ? mkr.db.toFixed(1) : '—'} sfx="dB" readOnly />
            </span>
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }} onClick={autoScale}>
              <span className="hp-pfx">AUTO</span>
            </span>
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }} onClick={() => setHold((h) => !h)}>
              <span className="hp-pfx">HOLD</span>
              <span style={{ color: '#fff3d6' }}>{hold ? 'ON' : 'OFF'}</span>
            </span>
          </div>
          <div className="hp-row">
            <span style={{ display: 'inline-flex', alignItems: 'baseline', gap: '1ch' }}>
              <span className="hp-fld">
                <span className="hp-pfx">RANGE</span>
                <div className="hp-tuner">
                  <FreqTuner value={range} digits={3} min={SP_ROWS} max={800} unit="dB" onChange={setRange} />
                </div>
              </span>
              <CrtField val={(range / SP_ROWS).toFixed(0)} sfx="dB/DIV" readOnly />
            </span>
            <span className="hp-fld">
              <span className="hp-pfx">MODE</span>
              <span style={{ color: '#fff3d6' }}>AVG · 1fps</span>
            </span>
            <span className="hp-fld">
              <span className="hp-pfx">GAIN</span>
              <DbTuner value={gain} digits={2} unit="dB" onChange={pubGain} />
            </span>
          </div>
        </div>

        <div className="hp-screen">
          <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: '100%', cursor: 'crosshair' }}
            onMouseMove={onCanvasMouseMove} onMouseLeave={onCanvasMouseLeave} />
        </div>

        <div className="hp-rows">
          <div className="hp-row">
            <div className="hp-fld">
              <span className="hp-pfx">CENTER</span>
              <div className={fswOn ? "hp-tuner ftuner-disabled" : "hp-tuner"}>
                <FreqTuner value={Math.round(centerHz / 1000)} digits={7} min={47000} max={6000000} unit="MHz"
                  onChange={fswOn ? () => {} : pubCenter} />
              </div>
            </div>
            <div className="hp-fld">
              <span className="hp-pfx">INTEG</span>
              <div className="hp-tuner">
                <FreqTuner value={integSec} digits={4} min={1} max={3600} unit="s" onChange={setIntegSec} />
              </div>
            </div>
            <div className="hp-fld">
              <span className="hp-pfx">BW</span>
              <div className="hp-tuner">
                <FreqTuner value={Math.round(bandwidth / 1000)} digits={5} min={100} max={10000} unit="MHz" onChange={pubBandwidth} />
              </div>
            </div>
            <div className="hp-fld">
              <span className="hp-pfx">OFS</span>
              <div className="hp-tuner">
                <FreqTuner value={fswOffsetKHz} digits={4} min={100} max={5000} unit="kHz" onChange={setFswOffsetKHz} />
              </div>
            </div>
          </div>
          <div className="hp-row sm">
            <CrtField pfx="WIN" val={frames.toString()} sfx={`/ ${integSec}s`} readOnly />
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }} onClick={toggleCal}>
              <span className="hp-pfx">CAL</span>
              <span style={{ color: calOn ? '#fff3d6' : undefined }}>{calOn ? 'ON' : (refBinsRef.current ? 'OFF' : '—')}</span>
            </span>
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }} onClick={toggleFsw}>
              <span className="hp-pfx">FSW</span>
              <span style={{ color: fswOn ? '#fff3d6' : undefined }}>{fswOn ? fswPhase.toUpperCase() : 'OFF'}</span>
            </span>
            <CrtField pfx="WS" val={wsState} readOnly />
          </div>
        </div>
      </div>

      <div className="info-note">
        <Icon name="target" size={16} />
        <p>
          Big spike at the exact CENTER frequency? That's LO self-mixing (DC offset), inherent to this
          zero-IF receiver — RF/BB DC and quadrature tracking are enabled automatically to minimize it, but
          it never fully disappears. Three fixes, in order of effort: detune CENTER a few hundred kHz away
          from your target line so the spike lands outside the region of interest, then hover the trace to
          read the true line frequency off MKR — or hit CAL once WIN has filled to capture the current
          spectrum as a one-time background and subtract it going forward — or hit FSW for the classical
          radio-astronomy approach: it automatically alternates CENTER between SIG (your tuned frequency)
          and SIG+OFS (a reference band OFS kHz away) every INTEG seconds and continuously displays
          SIG−REF, re-measuring the background on every cycle instead of once. CAL and FSW are mutually
          exclusive and both invalidate on any BW/GAIN change (FSW also owns CENTER while it runs).
        </p>
      </div>
    </div>
  );
}

Object.assign(window, { RadioAstronomyPage });

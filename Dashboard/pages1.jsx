// pages1.jsx — Dashboard + RF Parameters
const { useState: useS1, useEffect: useE1 } = React;

const fmtBytes = (b) => {
  if (b == null) return null;
  if (b >= 1048576) return (b / 1048576).toFixed(1) + ' MB';
  if (b >= 1024)    return (b / 1024).toFixed(1) + ' KB';
  return b + ' B';
};

function StatTile({ label, value, unit, spark, color, tone }) {
  return (
    <div className="stat-tile">
      <div className="stat-top">
        <span className="stat-label">{label}</span>
        {tone && <Pill tone={tone} dot>{tone === "ok" ? "LOCK" : "—"}</Pill>}
      </div>
      <div className="stat-val mono">{value}<i>{unit}</i></div>
      {spark && <Sparkline data={spark} color={color} width={150} height={32} />}
    </div>);

}

function BigStat({ label, value, unit, options, onCycle, color }) {
  return (
    <div className={`bigstat ${options ? "switchable" : ""} ${color ? "active" : ""}`}
    style={color ? { "--ac": color } : undefined}
    onClick={options ? onCycle : undefined}>
      <div className="bigstat-label">{label}{options && <Icon name="refresh" size={13} />}</div>
      <div className="bigstat-val mono">{value}{unit && <i>{unit}</i>}</div>
    </div>);

}

function Led({ label, alarm }) {
  return (
    <div className="led-item">
      <span className={`led ${alarm ? "led-red" : "led-green"}`} />
      <span className="led-label">{label}</span>
    </div>
  );
}

function MiniSelect({ label, value, onChange, options }) {
  return (
    <label className="mini-sel" title={label}>
      <span>{label}</span>
      <select value={value} onChange={(e) => onChange(e.target.value)}>
        {options.map((o) => <option key={o} value={o}>{o}</option>)}
      </select>
    </label>
  );
}

const AGC_TO_UI = { manual: "manual", slow_attack: "slow", fast_attack: "fast", hybrid: "slow" };
const UI_TO_AGC = { manual: "manual", slow: "slow_attack", fast: "fast_attack" };

function Dashboard({ d, ver }) {
  const [rxFreqHz, setRxFreqHz] = useS1(null);
  const [txFreqHz, setTxFreqHz] = useS1(null);
  const [srHz, setSrHz] = useS1(null);
  const [bwHz, setBwHz] = useS1(null);
  const [bwAuto, setBwAuto] = useS1(true);
  const [input, setInput] = useS1(0);
  const [output, setOutput] = useS1(0);
  const [rxGain, setRxGain] = useS1(null);
  const [txGain, setTxGain] = useS1(null);
  const [agc, setAgc] = useS1("manual");
  const [dDecim, setDDecim] = useS1("None");
  const [dInterp, setDInterp] = useS1("None");

  useE1(() => { if (d.rxFreq     != null) setRxFreqHz(d.rxFreq); },     [d.rxFreq]);
  useE1(() => { if (d.txFreq     != null) setTxFreqHz(d.txFreq); },     [d.txFreq]);
  useE1(() => { if (d.rxSampling != null) setSrHz(d.rxSampling); },     [d.rxSampling]);
  useE1(() => { if (d.rxBandwidth!= null) setBwHz(d.rxBandwidth); },    [d.rxBandwidth]);
  useE1(() => { if (d.rxGain     != null) setRxGain(d.rxGain); },       [d.rxGain]);
  useE1(() => { if (d.txGain     != null) setTxGain(d.txGain); },       [d.txGain]);
  useE1(() => { if (d.rxGainMode)         setAgc(AGC_TO_UI[d.rxGainMode] || "manual"); }, [d.rxGainMode]);
  useE1(() => { if (d.rxRfinput != null)  setInput(d.rxRfinput - 1); },  [d.rxRfinput]);
  useE1(() => {
    if (d.rxFirEnable === false)        setDDecim("None");
    else if (d.rxFirDecim === 4)        setDDecim("×4");
    else if (d.rxFirDecim === 2)        setDDecim("×2");
    if (d.rxFirEnable === false)        setDInterp("None");
    else if (d.txFirInterp === 4)       setDInterp("×4");
    else if (d.txFirInterp === 2)       setDInterp("×2");
  }, [d.rxFirEnable, d.rxFirDecim, d.txFirInterp]);
  useE1(() => { if (d.txRfoutput != null)  setOutput(d.txRfoutput - 1); }, [d.txRfoutput]);

  const RX = ["RX1", "RX2"], TX = ["TX1", "TX2"];
  const mhz = (v) => (v / 1e6).toFixed(3) + " MHz", ms = (v) => (v / 1e6).toFixed(3) + " MS/s";
  const autoBw    = (sr) => Math.min(Math.round(sr * 1.5), 56000000);
  const bwShown   = bwAuto ? d.rxBandwidth : bwHz;

  const onRxFreq  = (v) => { setRxFreqHz(v); d.publish("rx/frequency", v); };
  const onTxFreq  = (v) => { setTxFreqHz(v); d.publish("tx/frequency", v); };
  const onSr      = (v) => {
    setSrHz(v); d.publish("rx/sampling", v); d.publish("tx/sampling", v);
    if (bwAuto) { const bw = autoBw(v); d.publish("rx/bandwidth", bw); d.publish("tx/bandwidth", bw); }
  };
  const onBw      = (v) => { setBwHz(v); d.publish("rx/bandwidth", v); d.publish("tx/bandwidth", v); };
  const onRxGain  = (v) => { setRxGain(v); d.publish("rx/gain", v); };
  const onTxGain  = (v) => { setTxGain(v); d.publish("tx/gain", v); };
  const onAgc     = (m) => { setAgc(m); d.publish("rx/gain_mode", UI_TO_AGC[m]); };
  return (
    <div className="page">
      <div className="grid-12">
        <Card className="span-12">
          <div className="ds-shared">
            <div className="ds-shared-head"><span className="bigstat-label" style={{ margin: 0 }}>Baseband</span><span className={`pill ${d.sys2r2t ? 'pill-ok' : 'pill-neutral'}`}>{d.sys2r2t ? '2R2T' : '1R1T'}</span></div>
            <div className="bw-grid">
              <div>
                <div className="bw-field-head"><span className="field-label">Sample rate</span></div>
                <div className="rate-row">
                  {srHz != null && <FreqTuner value={srHz} digits={9} min={520833} max={61440000} unit="S/s" onChange={onSr} />}
                  <div className="rate-factors">
                    <MiniSelect label="Decim" value={dDecim} onChange={(v) => { setDDecim(v); if (v === "None") d.publish("rx/fir_enable", 0); }} options={["None", "×2", "×4"]} />
                    <MiniSelect label="Interp" value={dInterp} onChange={setDInterp} options={["None", "×2", "×4"]} />
                  </div>
                </div>
              </div>
              <div>
                <div className="bw-field-head"><span className="field-label">Bandwidth</span><Checkbox checked={bwAuto} onChange={setBwAuto} label="Auto" /></div>
                <div className={bwAuto ? "ftuner-disabled" : ""}>
                  {bwShown != null && <FreqTuner value={bwShown} digits={8} min={200000} max={56000000} unit="Hz" onChange={bwAuto ? () => {} : onBw} />}
                </div>
              </div>
            </div>
          </div>
          <div className="rf-panels">
            <div className="rf-panel rx">
              <div className="rf-panel-head"><span className="rf-tag">RX</span>Receive path</div>
              <div className="ds-tuner"><span className="bigstat-label">Frequency</span>{rxFreqHz != null && <FreqTuner value={rxFreqHz} digits={12} min={47e6} max={6e9} unit="Hz" sub={mhz} onChange={onRxFreq} />}</div>
              <Field label="Gain control">
                <div style={{ display: "flex", alignItems: "center", gap: 12 }}>
                  <div className="seg">
                    {["manual", "slow", "fast"].map((m) =>
                    <button key={m} className={agc === m ? "seg-on" : ""} onClick={() => onAgc(m)}>{m === "manual" ? "Manual" : m === "slow" ? "Slow AGC" : "Fast AGC"}</button>
                    )}
                  </div>
                  <div className="rssi-panel">
                    <span className="field-label">RSSI</span>
                    <span className="mono" style={{ marginLeft: 6 }}>{d.rfLevel != null ? `${d.rfLevel.toFixed(1)} dB` : "—"}</span>
                  </div>
                </div>
              </Field>
              <Field label="RX gain" hint={agc !== "manual" ? "Set automatically by AGC" : undefined}>
                <div className={agc !== "manual" ? "ftuner-disabled" : ""}>
                  {rxGain != null && <Slider value={rxGain} min={0} max={73} step={1} onChange={agc !== "manual" ? () => {} : onRxGain} unit=" dB" />}
                </div>
              </Field>
              <div className="led-row">
                <Led label="Clipping" alarm={d.rxOverload} />
                <Led label="Underflow" alarm={d.rxUnderflow} />
                {d.rxBufferSize != null && <span className="mono" style={{fontSize:'0.75em',opacity:0.7}}>{fmtBytes(d.rxBufferSize)}</span>}
                {d.rxDmaTransfer != null && d.rxBufferSize != null && <HBarGauge value={d.rxDmaTransfer} max={500} color="var(--accent)" label="DMA" fmt={fmtBytes} />}
              </div>
              <div className="bigstat-grid">
                <BigStat label="Input" value={RX[input]} options color={d.rxDmaTransfer > 0 ? "var(--accent)" : undefined} onCycle={() => { const next = (input + 1) % 2; setInput(next); d.publish("rx/rfinput", next + 1); }} />
              </div>
            </div>
            <div className="rf-panel tx">
              <div className="rf-panel-head"><span className="rf-tag tx">TX</span>Transmit path</div>
              <div className="ds-tuner"><span className="bigstat-label">Frequency</span>{txFreqHz != null && <FreqTuner value={txFreqHz} digits={12} min={47e6} max={6e9} unit="Hz" sub={mhz} onChange={onTxFreq} />}</div>
              <div className="field" aria-hidden="true" style={{ visibility: "hidden" }}>
                <span className="field-label">Gain control</span>
                <div className="seg"><button>Manual</button></div>
              </div>
              <Field label="TX gain">{txGain != null && <Slider value={txGain} min={-89} max={0} step={0.25} onChange={onTxGain} unit=" dB" fmt={(v) => v.toFixed(2)} />}</Field>
              <div className="led-row">
                <Led label="Clipping" alarm={d.txOverload} />
                <Led label="Underflow" alarm={d.txUnderflow} />
                {d.txBufferSize != null && <span className="mono" style={{fontSize:'0.75em',opacity:0.7}}>{fmtBytes(d.txBufferSize)}</span>}
                {d.txDmaTransfer != null && d.txBufferSize != null && <HBarGauge value={d.txDmaTransfer} max={500} color="var(--c-pink)" label="DMA" fmt={fmtBytes} />}
              </div>
              <div className="bigstat-grid">
                <BigStat label="Output" value={TX[output]} options color={d.txDmaTransfer > 0 ? "var(--c-pink)" : undefined} onCycle={() => { const next = (output + 1) % 2; setOutput(next); d.publish("tx/rfoutput", next + 1); }} />
              </div>
            </div>
          </div>
        </Card>

        <Card title="Temperature" sub="On-die sensors · live" className="span-12">
          <div className="gauge-row">
            <DialGauge value={d.fpgaTemp} max={65} label="FPGA" unit="°C" color="var(--ok)" />
            <DialGauge value={d.temp} max={65} label="AD9361" unit="°C" color="var(--ok)" />
          </div>
        </Card>

        <Card title="Device" className="span-12" pad={false}>
          <div className="device-strip">
            <div className="ds-item"><Icon name="chip" size={18} /><div><span>Model</span><b className="mono">{ver.model}</b></div></div>
            <div className="ds-item"><Icon name="clock" size={18} /><div><span>Uptime</span><b className="mono">{fmtUptime(d.uptime)}</b></div></div>
            <div className="ds-item"><Icon name="wave" size={18} /><div><span>Tezuka core</span><b className="mono">{ver.tezuka}</b></div></div>
            <div className="ds-item"><span className={`conn ${d.mqtt ? "up" : "down"}`} /><div><span>MQTT broker</span><b className="mono">{d.mqtt ? `connected · ${d.mqttHost}` : "offline"}</b></div></div>
          </div>
        </Card>
      </div>
    </div>);

}

// ---- RF Parameters --------------------------------------------------------
function RFParams() {
  const [freqHz, setFreqHz] = useS1(437000000);
  const [srHz, setSrHz] = useS1(2400000);
  const [bwHz, setBwHz] = useS1(1920000);
  const [bwAuto, setBwAuto] = useS1(true);
  const [rxGain, setRxGain] = useS1(38);
  const [txGain, setTxGain] = useS1(-11);
  const [txFreqHz, setTxFreqHz] = useS1(437000000);
  const [linked, setLinked] = useS1(true);
  const [agc, setAgc] = useS1("manual");
  const [decim, setDecim] = useS1("None");
  const [interp, setInterp] = useS1("None");
  const [dirty, setDirty] = useS1(false);
  const facMap = { None: 1, "×2": 2, "×4": 4 };
  const hostRate = (f) => (srHz / 1e6 / facMap[f]).toFixed(3) + " MS/s";
  const mk = (fn) => (v) => {fn(v);setDirty(true);};
  const txF = linked ? freqHz : txFreqHz;
  const bwShown = bwAuto ? srHz : bwHz;

  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Baseband" sub="Sample rate & RF bandwidth — shared by RX and TX" className="span-12"
        right={<span className="pill pill-neutral">RX + TX</span>}>
          <div className="bw-grid">
            <div>
              <div className="bw-field-head"><span className="field-label">Sample rate</span></div>
              <FreqTuner value={srHz} digits={9} min={520833} max={61440000} unit="S/s" sub={(v) => (v / 1e6).toFixed(3) + " MS/s"} onChange={mk(setSrHz)} />
            </div>
            <div>
              <div className="bw-field-head"><span className="field-label">Bandwidth</span><Checkbox checked={bwAuto} onChange={(v) => {setBwAuto(v);setDirty(true);}} label="Auto" /></div>
              <div className={bwAuto ? "ftuner-disabled" : ""}>
                <FreqTuner value={bwShown} digits={8} min={200000} max={56000000} unit="Hz" sub={(v) => (v / 1e6).toFixed(3) + " MHz"} onChange={bwAuto ? () => {} : mk(setBwHz)} />
              </div>
            </div>
          </div>
          <div className="ftuner-hint">{bwAuto ? "Bandwidth follows the sample rate automatically." : "Scroll or click a digit to set the RF channel bandwidth."}</div>
        </Card>

        <Card title="RX path" sub="AD9363 receive chain" className="span-6">
          <Field label="Center frequency" hint="47 MHz – 6 GHz · scroll or click a digit to tune">
            <FreqTuner value={freqHz} digits={12} min={47e6} max={6e9} unit="Hz" sub={(v) => (v / 1e6).toFixed(3) + " MHz"} onChange={mk(setFreqHz)} />
          </Field>
          <Field label="Gain control">
            <div className="seg">
              {["manual", "slow", "fast"].map((m) =>
              <button key={m} className={agc === m ? "seg-on" : ""} onClick={() => {setAgc(m);setDirty(true);}}>{m === "manual" ? "Manual" : m === "slow" ? "Slow AGC" : "Fast AGC"}</button>
              )}
            </div>
          </Field>
          <Field label="RX gain" hint={agc !== "manual" ? "Disabled while AGC active" : "0 – 73 dB"}>
            <Slider value={rxGain} min={0} max={73} step={1} onChange={mk(setRxGain)} unit=" dB" />
          </Field>
          <Field label="AD9361 decimation" hint={`RX FIR · ${hostRate(decim)} to host`}>
            <Select value={decim} onChange={(v) => {setDecim(v);setDirty(true);}} options={["None", "×2", "×4"]} />
          </Field>
        </Card>

        <Card title="TX path" sub="AD9363 transmit chain" className="span-6">
          <Field label="Follow RX" hint={linked ? "TX frequency tracks the RX path" : "TX tuned independently of RX"}>
            <Toggle on={linked} onChange={(v) => {setLinked(v);setDirty(true);}} labels={["Independent", "Linked"]} />
          </Field>
          <Field label="Center frequency" hint="47 MHz – 6 GHz · scroll or click a digit to tune">
            <FreqTuner value={txF} digits={12} min={47e6} max={6e9} unit="Hz" sub={(v) => (v / 1e6).toFixed(3) + " MHz"} onChange={linked ? mk(setFreqHz) : mk(setTxFreqHz)} />
          </Field>
          <Field label="TX attenuation" hint="0 to −89.75 dB">
            <Slider value={txGain} min={-89} max={0} step={0.25} onChange={mk(setTxGain)} unit=" dB" fmt={(v) => v.toFixed(2)} />
          </Field>
          <Field label="AD9361 interpolation" hint={`TX FIR · ${hostRate(interp)} from host`}>
            <Select value={interp} onChange={(v) => {setInterp(v);setDirty(true);}} options={["None", "×2", "×4"]} />
          </Field>
          <div className="info-note">
            <Icon name="versions" size={16} />
            <p>Changes are staged and applied to the SDR in one transaction. The DATV modulator inherits the TX frequency set here unless overridden.</p>
          </div>
        </Card>
      </div>

      <div className={`action-bar ${dirty ? "show" : ""}`}>
        <span>{dirty ? "Unsaved parameter changes" : "All changes applied"}</span>
        <div className="ab-btns">
          <button className="btn ghost" onClick={() => setDirty(false)}>Reset</button>
          <button className="btn primary" onClick={() => setDirty(false)}><Icon name="check" size={16} />Apply</button>
        </div>
      </div>
    </div>);

}

function CrtField({ pfx, val, sfx, onChange, center, readOnly }) {
  return (
    <span className={`hp-fld ${center ? "ctr" : ""}`}>
      {pfx ? <span className="hp-pfx">{pfx}</span> : null}
      {readOnly
        ? <span className="hp-val">{val}</span>
        : <input
            className="hp-inp" value={val} onChange={onChange} spellCheck={false}
            onFocus={(e) => e.target.select()}
            style={{ width: `calc(${Math.max(String(val).length, 1)}ch + 2px)` }} />}
      {sfx ? <span className="hp-sfx">{sfx}</span> : null}
    </span>
  );
}

function SpectrumStub({ freq, bw }) {
  const [tick, setTick] = useS1(0);
  React.useEffect(() => {const id = setInterval(() => setTick((t) => t + 1), 420);return () => clearInterval(id);}, []);

  // editable analyzer fields
  const [f, setF] = useS1({
    ref: "-10.0", div: "10", range: "-10.0", mkrF: freq.toFixed(3), mkrL: "-10.3",
    center: freq.toFixed(3), span: bw.toFixed(3), rbw: "30", vbw: "10", st: "104",
  });
  const upd = (k) => (e) => setF((s) => ({ ...s, [k]: e.target.value }));

  // ---- screen geometry (graticule fills the SVG) ----
  const GW = 1340, GH = 544, cols = 10, rows = 8;

  // ---- flat-top band-pass response; width tracks the SPAN field ----
  const spanNum = parseFloat(f.span) || bw;
  const c = 0.5, hw = Math.max(0.045, Math.min(0.46, 0.165 * (bw / spanNum)));
  const level = (t, rnd) => {
    const d = Math.abs(t - c);
    const floor = 0.13 + (rnd ? (Math.random() - 0.5) * 0.035 : 0);
    if (d <= hw) return 0.90 + 0.022 * Math.sin(t * 71) + 0.010 * Math.sin(t * 26);
    const o = d - hw;
    const skirt = 0.90 * Math.exp(-Math.pow(o / 0.012, 2)); // steep edge
    const lobe = 0.34 * Math.exp(-Math.pow((o - 0.055) / 0.045, 2)); // shoulder bump
    return Math.max(floor, skirt, lobe + floor * 0.45);
  };
  const n = 280;
  const X = (t) => t * GW;
  const Y = (v) => GH - Math.max(0.02, Math.min(0.98, v)) * GH;
  const trace = Array.from({ length: n }, (_, i) => {
    const t = i / (n - 1);
    return `${i === 0 ? "M" : "L"} ${X(t).toFixed(1)} ${Y(level(t, true)).toFixed(1)}`;
  }).join(" ");
  const area = `${trace} L ${GW} ${GH} L 0 ${GH} Z`;
  const mk = [c - hw, c + hw, c + hw + 0.009];
  const markers = mk.map((t) => ({ x: X(t), y: Y(level(t, false)) }));

  return (
    <div className="hp-crt">
      <div className="hp-rows">
        <div className="hp-row">
          <CrtField pfx="REF" val={f.ref} sfx="dBm" onChange={upd("ref")} />
          <CrtField pfx="MKR" val={f.mkrF} sfx="MHz" onChange={upd("mkrF")} />
        </div>
        <div className="hp-row">
          <CrtField val={f.div} sfx="dB/DIV" onChange={upd("div")} />
          <CrtField pfx="RANGE" val={f.range} sfx="dBm" onChange={upd("range")} center />
          <CrtField val={f.mkrL} sfx="dB" onChange={upd("mkrL")} />
        </div>
      </div>

      <div className="hp-screen">
        <svg viewBox={`0 0 ${GW} ${GH}`} preserveAspectRatio="none" role="img" aria-label="Spectrum analyzer display">
          <g>
            {Array.from({ length: cols + 1 }, (_, i) => {
              const x = (i * GW) / cols, ctr = i === cols / 2;
              return <line key={"v" + i} x1={x} x2={x} y1={0} y2={GH} className={ctr ? "hp-grat-c" : "hp-grat"} />;
            })}
            {Array.from({ length: rows + 1 }, (_, i) => {
              const y = (i * GH) / rows, ctr = i === rows / 2;
              return <line key={"h" + i} x1={0} x2={GW} y1={y} y2={y} className={ctr ? "hp-grat-c" : "hp-grat"} />;
            })}
            <rect x={1} y={1} width={GW - 2} height={GH - 2} className="hp-frame" />
          </g>
          <path d={area} className="hp-fill" />
          <path d={trace} className="hp-trace" />
          {markers.map((m, i) => <circle key={i} cx={m.x} cy={m.y} r="5" className="hp-mkr" />)}
        </svg>
      </div>

      <div className="hp-rows">
        <div className="hp-row">
          <CrtField pfx="CENTER" val={f.center} sfx="MHz" onChange={upd("center")} />
          <CrtField pfx="SPAN" val={f.span} sfx="MHz" onChange={upd("span")} />
        </div>
        <div className="hp-row sm">
          <CrtField pfx="RBW" val={f.rbw} sfx="kHz" onChange={upd("rbw")} />
          <CrtField pfx="VBW" val={f.vbw} sfx="kHz" onChange={upd("vbw")} center />
          <CrtField pfx="ST" val={f.st} sfx="ms" onChange={upd("st")} />
        </div>
      </div>
    </div>);

}

// ---- Spectrum Page — HP CRT style, fresh canvas implementation ---------------
const { useRef: useSpR, useEffect: useSpE, useState: useSpS, useCallback: useSpCb } = React;

// Signed dB tuner: clickable '−' sign prefix + FreqTuner for magnitude (integer dBm steps)
function DbTuner({ value, onChange, digits = 3, unit = 'dBm' }) {
  const abs = Math.abs(Math.round(value));
  const neg = value < 0;
  // Clicking the sign toggles positive/negative; prevents getting stuck at ±0
  const flip = () => onChange(neg ? abs : -(abs || 1));
  return (
    <span style={{ display: 'inline-flex', alignItems: 'baseline' }}>
      <span className="hp-pfx"
        style={{ cursor: 'pointer', userSelect: 'none', minWidth: '0.6ch', textAlign: 'right' }}
        onClick={flip}>
        {neg ? '−' : ' '}
      </span>
      <div className="hp-tuner">
        <FreqTuner value={abs} digits={digits} min={0} max={999} unit={unit}
          onChange={(v) => onChange(neg ? -(v || 1) : v)} />
      </div>
    </span>
  );
}

// Phosphor palette — matches CSS --phos variables in .hp-crt
const SP_PHOS      = '#FFC21A';
const SP_PHOS_GRID = 'rgba(245,179,1,0.17)';
const SP_PHOS_DIM  = 'rgba(245,179,1,0.34)';
const SP_BG        = '#0a0600';
const SP_COLS = 10, SP_ROWS = 8;

// range = total dB span visible (bottom = refDb - range)
function spDrawGraticule(ctx, W, H) {
  ctx.lineWidth = 1;
  for (let i = 0; i <= SP_COLS; i++) {
    const x = (i / SP_COLS) * W;
    const ctr = i === SP_COLS / 2;
    ctx.strokeStyle = ctr ? SP_PHOS_DIM : SP_PHOS_GRID;
    ctx.setLineDash(ctr ? [7, 6] : []);
    ctx.beginPath(); ctx.moveTo(x + 0.5, 0); ctx.lineTo(x + 0.5, H); ctx.stroke();
  }
  for (let i = 0; i <= SP_ROWS; i++) {
    const y = (i / SP_ROWS) * H;
    const ctr = i === SP_ROWS / 2;
    ctx.strokeStyle = ctr ? SP_PHOS_DIM : SP_PHOS_GRID;
    ctx.setLineDash(ctr ? [7, 6] : []);
    ctx.beginPath(); ctx.moveTo(0, y + 0.5); ctx.lineTo(W, y + 0.5); ctx.stroke();
  }
  ctx.setLineDash([]);
  ctx.strokeStyle = 'rgba(255,205,70,0.45)';
  ctx.lineWidth = 1.6;
  ctx.strokeRect(0.5, 0.5, W - 1, H - 1);
}

const FOSFOR_DECAY = { off: 0, light: 0.04, medium: 0.015, high: 0.005 };

function spDraw(ctx, W, H, bins, refDb, range, vs = 0, ve = 1, fosfor = 'off') {
  const decay = FOSFOR_DECAY[fosfor] ?? 0;
  if (decay > 0) {
    // Fade previous frame instead of clearing — trace accumulates with decay
    ctx.fillStyle = `rgba(0,0,0,${decay})`;
    ctx.fillRect(0, 0, W, H);
  } else {
    ctx.fillStyle = SP_BG;
    ctx.fillRect(0, 0, W, H);
    spDrawGraticule(ctx, W, H);
  }

  if (!bins || bins.length < 2) {
    if (decay > 0) spDrawGraticule(ctx, W, H);
    return;
  }

  const n   = bins.length;
  const top = refDb;
  const bot = refDb - range;

  const i0  = Math.max(0,     Math.floor(vs * (n - 1)));
  const i1  = Math.min(n - 1, Math.ceil (ve * (n - 1)));
  const toY = (db) => H - Math.max(0, Math.min(H, ((db - bot) / (top - bot)) * H));
  const toX = (i)  => ((i / (n - 1) - vs) / (ve - vs)) * W;

  if (decay === 0) {
    // Gradient fill under trace (skip in fosfor mode — fill would obscure the decay)
    const grad = ctx.createLinearGradient(0, 0, 0, H);
    grad.addColorStop(0,    'rgba(255,190,30,0.16)');
    grad.addColorStop(0.55, 'rgba(180,120,0,0.07)');
    grad.addColorStop(1,    'rgba(0,0,0,0)');
    ctx.beginPath();
    ctx.moveTo(toX(i0), toY(bins[i0]));
    for (let i = i0 + 1; i <= i1; i++) ctx.lineTo(toX(i), toY(bins[i]));
    ctx.lineTo(toX(i1), H);
    ctx.lineTo(toX(i0), H);
    ctx.closePath();
    ctx.fillStyle = grad;
    ctx.fill();
  }

  // Phosphor trace with glow
  ctx.shadowColor = 'rgba(255,200,40,0.80)';
  ctx.shadowBlur  = 4;
  ctx.beginPath();
  ctx.moveTo(toX(i0), toY(bins[i0]));
  for (let i = i0 + 1; i <= i1; i++) ctx.lineTo(toX(i), toY(bins[i]));
  ctx.strokeStyle = SP_PHOS;
  ctx.lineWidth   = 2;
  ctx.lineJoin    = 'round';
  ctx.lineCap     = 'round';
  ctx.stroke();
  ctx.shadowBlur = 0;

  // In fosfor mode redraw graticule on top so it stays crisp
  if (decay > 0) spDrawGraticule(ctx, W, H);
}

function spDrawCursor(ctx, W, H, mp, bins, centerHz, spanHz, refDb, range, vs = 0, ve = 1) {
  if (!mp || !bins || bins.length === 0) return;
  const { px } = mp;
  const x = px * W;
  const n = bins.length;

  const binFrac = vs + px * (ve - vs);
  const freq  = centerHz + (binFrac - 0.5) * spanHz;
  const binI  = Math.max(0, Math.min(n - 1, Math.round(binFrac * (n - 1))));
  const db    = bins[binI];
  const top   = refDb, bot = refDb - range;
  const traceY = H - Math.max(0, Math.min(H, ((db - bot) / (top - bot)) * H));

  ctx.save();

  // Vertical crosshair
  ctx.strokeStyle = 'rgba(255,215,80,0.55)';
  ctx.lineWidth   = 1;
  ctx.setLineDash([4, 4]);
  ctx.beginPath(); ctx.moveTo(x + 0.5, 0); ctx.lineTo(x + 0.5, H); ctx.stroke();
  ctx.setLineDash([]);

  // Dot at trace intersection
  ctx.shadowColor = 'rgba(255,200,40,0.95)';
  ctx.shadowBlur  = 8;
  ctx.fillStyle   = '#fff';
  ctx.beginPath(); ctx.arc(x, traceY, 3.5, 0, Math.PI * 2); ctx.fill();
  ctx.shadowBlur  = 0;

  // Labels
  const fStr = freq >= 1e9  ? (freq / 1e9).toFixed(4) + ' GHz'
             : freq >= 1e6  ? (freq / 1e6).toFixed(3) + ' MHz'
             :                 (freq / 1e3).toFixed(1) + ' kHz';
  const dStr = db.toFixed(1) + ' dB';

  ctx.font         = '600 12px "IBM Plex Mono", ui-monospace, monospace';
  ctx.fillStyle    = '#fff3d6';
  ctx.textBaseline = 'middle';
  const right      = px > 0.72;
  ctx.textAlign    = right ? 'right' : 'left';
  const lx         = right ? x - 8 : x + 8;
  ctx.fillText(fStr, lx, 14);
  ctx.fillText(dStr, lx, Math.max(20, Math.min(H - 14, traceY - 14)));

  ctx.restore();
}

// Spectrum UI state — persists across route navigation within the session
if (!window._sp) window._sp = {};

// Standard 1-2-2.5-5 test-equipment span ladder (used by Ctrl+wheel SPAN stepping).
const NICE_SPANS = [50e3,100e3,250e3,500e3,1e6,2.5e6,5e6,10e6,20e6,40e6,100e6,150e6,300e6];
const snapSpan  = (hz) => NICE_SPANS.reduce((b, v) => Math.abs(v - hz) < Math.abs(b - hz) ? v : b);
const stepSpan  = (s, out) => out
  ? (NICE_SPANS.find(v => v > s)    ?? NICE_SPANS[NICE_SPANS.length - 1])
  : ([...NICE_SPANS].reverse().find(v => v < s) ?? NICE_SPANS[0]);

function SpectrumPage({ d }) {
  const sp = window._sp;
  const canvasRef  = useSpR(null);
  const wsRef      = useSpR(null);
  const rafRef     = useSpR(null);
  const binsRef    = useSpR(null);   // normal-mode dB bins (Float32Array)
  const sweepBuf   = useSpR(null);   // sweep stitching buffer (Float32Array, 8× frame length)
  const sweepLenR  = useSpR(0);
  const sweepMaskR = useSpR(0);     // bitmask of steps received since last buffer init
  const dirtyRef    = useSpR(true);
  const refDbRef    = useSpR(sp.refDb  ?? 130);
  const rangeRef    = useSpR(sp.range  ?? SP_ROWS * 10);
  const isSweepR    = useSpR(false);
  const mousePosRef = useSpR(null);   // {px} normalised 0-1 when cursor is over canvas
  const mouseDragRef    = useSpR(null);  // {startX, startY, startRefDb, startCenterHz} while dragging
  const wheelDebounceRef  = useSpR(0);  // setTimeout id for post-wheel flush
  const spanPubTimerRef   = useSpR(0);  // last MQTT publish timestamp for span tuner
  const spanThrottleRef   = useSpR(0);  // 500 ms gate for rx/span publishes
  const spanDebounceRef   = useSpR(0);  // setTimeout id for post-span-change flush
  const freqThrottleRef   = useSpR(0);  // 200 ms gate for rx/sweep/frequency publishes while sweeping
  const freqDebounceRef   = useSpR(0);  // setTimeout id for post-frequency-change flush while sweeping
  const midiGainThrottleRef = useSpR(0);  // 100 ms gate for rx/gain publishes from MIDI VOL_DA (fader floods CC otherwise)
  const midiGainDebounceRef = useSpR(0);  // setTimeout id for post-fader-move flush
  const midiFreqThrottleRef = useSpR(0);  // 100 ms gate for rx/frequency publishes from MIDI JOG_DA (jog floods CC otherwise)
  const midiFreqDebounceRef = useSpR(0);  // setTimeout id for post-jog-move flush
  const midiFreqRef         = useSpR(null); // synchronous running accumulator — avoids stale reads between rapid jog messages before React commits centerHzRef
  const midiJogSuppressUntilRef = useSpR(0); // ignore d.rxFreq telemetry echoes until this timestamp — see JOG_DA handler below
  const midiSpanThrottleRef = useSpR(0);  // 500 ms gate for rx/span publishes from MIDI XFADER — do_sweep_start/stop is a real hardware reinit, not a sysfs write
  const midiSpanDebounceRef = useSpR(0);  // setTimeout id for post-fader-move flush
  const midiVfwThrottleRef  = useSpR(0);  // 500 ms gate for spectro/fps publishes from MIDI VOL_DB — spectro_fps() is a curl PATCH to the spectrometer HTTP API, not a sysfs write
  const midiVfwDebounceRef  = useSpR(0);  // setTimeout id for post-fader-move flush
  const midiStepFreqRef     = useSpR(1000e6 / 256); // Hz per BASS_DA raw-value unit — set by CUE_KP1-4_DA (GHz/MHz/kHz/Hz), default MHz
  const bassDaLastValRef    = useSpR(null); // previous raw CC value for BASS_DA, to synthesize a delta from its absolute stream
  const kpSelectedRef       = useSpR(null); // note number of the currently-selected KP1-4 step — guards LED sendNote so repeat presses of the same key are a no-op
  // Note number -> step size, in KP1-4 order. Declared inside the component
  // (not module scope) since MIDI_NOTE comes from midi.jsx, which loads
  // after pages1.jsx — fine here because it's only read at render/callback
  // time, long after every script has parsed.
  // Step sizes: a full BASS_DA knob sweep (raw 0..255) covers 1000 of the
  // selected unit — except GHz, capped to 0..6 to match the tunable
  // frequency range (47 MHz..6 GHz), so that mode never overshoots.
  const CUE_KP_STEPS = [
    [MIDI_NOTE.CUE_KP1_DA, 6e9 / 256],    // GHz mode: full sweep = 0..6 GHz
    [MIDI_NOTE.CUE_KP2_DA, 1000e6 / 256], // MHz mode: full sweep = 1000 MHz
    [MIDI_NOTE.CUE_KP3_DA, 1000e3 / 256], // kHz mode: full sweep = 1000 kHz
    [MIDI_NOTE.CUE_KP4_DA, 1000 / 256],   // Hz mode: full sweep = 1000 Hz
  ];
  const centerHzRef = useSpR(sp.centerHz ?? (d.rxFreq ?? 437e6));
  const spanHzRef      = useSpR(sp.spanHz   ?? (d.span ?? d.rxSampling ?? null));
  const vfwRef          = useSpR(40);  // kept current via the sync effect below; used by span-change handlers
  const spanDefaultSet = useSpR(false);
  const dRef           = useSpR(d);
  const viewZoomR      = useSpR(1);    // graphical zoom factor (1 = full view)
  const viewCenterR    = useSpR(0.5);  // center of view window as bin fraction 0-1
  const dispZoomRef    = useSpR(1);    // last rendered zoom, guards against redundant setDispZoom
  const fosforRef      = useSpR('off');

  const [refDb,    setRefDb]    = useSpS(() => sp.refDb    ?? 130);
  const [range,    setRange]    = useSpS(() => sp.range    ?? SP_ROWS * 10);
  const [centerHz, setCenterHz] = useSpS(() => sp.centerHz ?? (d.rxFreq    ?? 437e6));
  const [spanHz,   setSpanHz]   = useSpS(() => sp.spanHz   ?? (d.span ?? d.rxSampling ?? null));
  const [gain,     setGain]     = useSpS(() => sp.gain     ?? (d.rxGain    ?? 50));
  const [rxInput,  setRxInput]  = useSpS(() => sp.rxInput  ?? (d.rxRfinput === 2 ? 'rx2' : 'rx1'));
  const [wsState,  setWsState]  = useSpS('disconnected');
  const [fps,      setFps]      = useSpS(0);
  const [vfw,      setVfw]      = useSpS(40);
  const [mkr,      setMkr]      = useSpS(null);   // {freq, db} when cursor is over canvas
  const [clipBlink,   setClipBlink]   = useSpS(false);
  const [dispZoom,    setDispZoom]    = useSpS(1);
  const [fosfor,      setFosfor]      = useSpS('off'); // 'off'|'light'|'medium'|'high'
  // Two distinct mechanisms, because requestFullscreen() requires transient
  // user activation — a trusted synchronous gesture (click, keydown,
  // pointerup, ...) — which a MIDI SYNC_DA press/release never has, so it
  // reliably throws NotAllowedError when triggered from MIDI:
  //  - osFullscreen: real Fullscreen API, mouse/keyboard only (hides
  //    browser chrome + OS taskbar). Tracked from the browser's own
  //    fullscreenchange event, since exiting via Esc bypasses our button.
  //  - cssMaximized: plain CSS position:fixed "maximize", triggered by
  //    MIDI (or anything else) — works from any code path since it's not
  //    a gated browser API, but doesn't hide browser/OS chrome.
  // The FULL indicator and .maximized/:fullscreen styling reflect whichever
  // is currently active.
  const [osFullscreen, setOsFullscreen] = useSpS(false);
  const [cssMaximized, setCssMaximized] = useSpS(false);
  const isFullscreen = osFullscreen || cssMaximized;
  const spPageRef = useSpR(null);
  const syncPressedRef = useSpR(false); // tracks SYNC_DA press state for the 127→0 (release) edge toggle
  const toggleOsFullscreen = () => {
    const p = !document.fullscreenElement
      ? spPageRef.current?.requestFullscreen()
      : document.exitFullscreen();
    p?.catch?.((e) => console.warn('[fullscreen] toggle failed:', e.name, e.message));
  };
  const toggleCssMaximize = () => setCssMaximized(v => !v);

  dRef.current = d;
  if (fosforRef.current !== fosfor) { fosforRef.current = fosfor; dirtyRef.current = true; }

  // Shared RX-frequency step logic — used by both JOG_DA (relative encoder)
  // and BASS_DA (absolute knob, stepped via applyMidiFreqStep below). Same
  // anti-flood throttle+debounce and anti-revert telemetry suppression for
  // both, since both can generate CC messages fast enough to need it.
  const applyMidiFreqStep = (step) => {
    midiJogSuppressUntilRef.current = performance.now() + 400;
    const base = midiFreqRef.current ?? centerHzRef.current;
    const hz = Math.max(47e6, Math.min(6e9, base + step));
    midiFreqRef.current = hz;
    setCenterHz(hz);
    const topic = d.sweepActive ? 'rx/sweep/frequency' : 'rx/frequency';
    const now = performance.now();
    if (now - midiFreqThrottleRef.current >= 100) {
      midiFreqThrottleRef.current = now;
      d.publish(topic, Math.round(hz));
    }
    clearTimeout(midiFreqDebounceRef.current);
    midiFreqDebounceRef.current = setTimeout(() => {
      if (midiFreqRef.current != null) d.publish(topic, Math.round(midiFreqRef.current));
    }, 120);
  };

  // MIDI: added one control at a time, via this page's own onCC hook rather
  // than midi.jsx's d.publish dispatch table (still fully disabled) — keeps
  // each binding local and easy to reason about while we build these up.
  // MEDIUM_DA/DB set REF/RANGE directly (local UI state, never published).
  // VOL_DA sets RX gain: the fader sends CC updates fast enough to flood
  // cmd/rx/gain if published on every message (same reasoning as the
  // span/frequency throttles above), so the local slider still tracks every
  // message instantly but the actual publish is throttled to 100ms with a
  // trailing flush so the final position always lands.
  const midi = useMidiController(d, (num, val) => {
    if (num === MIDI_CC.MEDIUM_DA) setRefDb(192 + (64 - val)*3);
    if (num === MIDI_CC.MEDIUM_DB) setRange(192 + (val - 64) *3);
    if (num === MIDI_CC.VOL_DA) {
      const g = val ;
      setGain(g);
      const now = performance.now();
      if (now - midiGainThrottleRef.current >= 100) {
        midiGainThrottleRef.current = now;
        d.publish('rx/gain', g);
      }
      clearTimeout(midiGainDebounceRef.current);
      midiGainDebounceRef.current = setTimeout(() => d.publish('rx/gain', g), 120);
    }
    if (num === MIDI_CC.JOG_DA) {
      // Step size scales with the current span (±5% per relative unit) so
      // the jog feels proportional whether you're zoomed in or way out.
      applyMidiFreqStep(decodeRelative(val) * spanHzRef.current * 0.01);
    }
    if (num === MIDI_CC.BASS_DA) {
      // BASS_DA is an absolute knob (0-127), not a relative encoder like
      // JOG_DA — so "step" is synthesized from the delta between successive
      // raw values (how far the knob moved since the last message), scaled
      // by midiStepFreqRef (set by the CUE_KP1-4_DA buttons: GHz/MHz/kHz/Hz
      // per step). First message after (re)connect has no prior value to
      // diff against, so it's ignored rather than producing a bogus jump.
      const prev = bassDaLastValRef.current;
      bassDaLastValRef.current = val;
      if (prev != null) applyMidiFreqStep((val - prev) * midiStepFreqRef.current);
    }
    if (num === MIDI_CC.XFADER) {
      // Log-scale across the full span range (80 kHz .. 300 MHz — a linear
      // fader over a linear Hz range would spend almost its whole travel on
      // spans above 50 MHz), snapped to the same NICE_SPANS ladder the
      // wheel/pinch scroll controls step through so it always lands on a
      // "real" span value, same as scrolling would. XFADER is absolute (no
      // accumulation across messages, unlike JOG_DA), so the debounce below
      // can safely close over the plain local value instead of needing a
      // ref — nothing to race against.
      const ratio = val / 127;
      const raw = 80e3 * Math.pow(300e6 / 80e3, ratio);
      const snapped = snapSpan(raw);
      setSpanHz(snapped);
      // rx/span (api_controller.sh) can trigger do_sweep_start/stop — real
      // hardware sweep (re)init, not a cheap sysfs write — so this uses the
      // same 500ms/220ms throttle-and-flush timing as the non-MIDI span
      // controls in this file (spanThrottleRef/spanDebounceRef), not the
      // 100ms used for plain register writes like gain.
      const now = performance.now();
      if (now - midiSpanThrottleRef.current >= 500) {
        midiSpanThrottleRef.current = now;
        d.publish('rx/span', Math.round(snapped));
        publishFps(d);
      }
      clearTimeout(midiSpanDebounceRef.current);
      midiSpanDebounceRef.current = setTimeout(() => {
        d.publish('rx/span', Math.round(snapped));
        publishFps(d);
      }, 220);
    }
    if (num === MIDI_CC.VOL_DB) {
      // vfw (video frame width, ms) drives spectro/fps via publishFps(),
      // which reads vfwRef — set it synchronously so both the throttled and
      // debounced calls below always see the latest fader position.
      // spectro_fps() (api_controller.sh) does a curl PATCH to the
      // spectrometer's HTTP API — a real network + backend-reconfig call,
      // not a cheap sysfs write — so this needs the same 500ms/220ms timing
      // as span above, not the 100ms used for gain/frequency register
      // writes. 100ms here was firing PATCH requests faster than the
      // backend could apply them, so most of them effectively got lost.
      const v = 10 + val * 10;
      setVfw(v);
      vfwRef.current = v;
      const now = performance.now();
      if (now - midiVfwThrottleRef.current >= 500) {
        midiVfwThrottleRef.current = now;
        publishFps(d);
      }
      clearTimeout(midiVfwDebounceRef.current);
      midiVfwDebounceRef.current = setTimeout(() => publishFps(d), 220);
    }
  }, (num, pressed) => {
    // Momentary button, no dedicated Note Off — the device sends Note On
    // val=127 on push and Note On val=0 on release. Toggle specifically on
    // the release (127→0) that follows a real press, not on push itself.
    if (num === MIDI_NOTE.SYNC_DA) {
      if (pressed) {
        syncPressedRef.current = true;
      } else if (syncPressedRef.current) {
        syncPressedRef.current = false;
        toggleCssMaximize(); // not toggleOsFullscreen — see the comment by osFullscreen/cssMaximized above
      }
    }
    // KP1-4 select the BASS_DA step size (GHz/MHz/kHz/Hz per unit of knob
    // movement) — press edge only, matching the other momentary buttons.
    // LED feedback: light the pressed key full-on (127), the other three off
    // (0) — a physical "radio button" showing which step size is selected.
    if (pressed) {
      const kpEntry = CUE_KP_STEPS.find(([n]) => n === num);
      if (kpEntry && kpSelectedRef.current !== num) {
        kpSelectedRef.current = num;
        midiStepFreqRef.current = kpEntry[1];
        CUE_KP_STEPS.forEach(([n]) => midi.sendNote(n, n === num ? 127 : 0));
      }
    }
  });

  // Keep refs current for RAF loop (avoids stale closures) + persist to session store
  useSpE(() => { refDbRef.current = refDb;  dirtyRef.current = true; sp.refDb    = refDb;    }, [refDb]);
  useSpE(() => { rangeRef.current = range;  dirtyRef.current = true; sp.range    = range;    }, [range]);
  useSpE(() => { centerHzRef.current = centerHz; sp.centerHz = centerHz; }, [centerHz]);
  useSpE(() => { spanHzRef.current = spanHz; sp.spanHz = spanHz; }, [spanHz]);
  useSpE(() => { vfwRef.current = vfw; }, [vfw]);
  useSpE(() => { sp.gain     = gain;     }, [gain]);
  useSpE(() => { sp.rxInput  = rxInput;  }, [rxInput]);

  // Sync from MQTT state
  useSpE(() => { if (d.rxGain    != null) setGain(d.rxGain); },    [d.rxGain]);
  useSpE(() => {
    // Ignore telemetry that arrives while (or just after) actively jogging —
    // see midiJogSuppressUntilRef in the JOG_DA handler above. Outside that
    // window this is the normal external-change sync (other pages, initial
    // mount, etc).
    if (d.rxFreq != null && performance.now() >= midiJogSuppressUntilRef.current) setCenterHz(d.rxFreq);
  }, [d.rxFreq]);
  // Re-seed the MIDI jog accumulator once telemetry is trusted again after
  // a jog session settles — never mid-jog, or a stale echo would reset the
  // accumulator and the next jog tick would resume from a backward-jumped base.
  useSpE(() => {
    if (performance.now() >= midiJogSuppressUntilRef.current) midiFreqRef.current = null;
  }, [d.rxFreq]);
  useSpE(() => {
    // Prefer an explicit span echo (d.span) over the raw sampling rate.
    // Never publish cmd/rx/span at startup — only reflect what the device reports.
    const s = d.span ?? d.rxSampling;
    if (s != null) { spanDefaultSet.current = true; setSpanHz(s); }
  }, [d.span, d.rxSampling]);
  useSpE(() => {
    if (d.rxRfinput != null) setRxInput(d.rxRfinput === 2 ? 'rx2' : 'rx1');
  }, [d.rxRfinput]);
  useSpE(() => {
    isSweepR.current = !!d.sweepActive;
    if (!d.sweepActive) { sweepBuf.current = null; sweepLenR.current = 0; }
  }, [d.sweepActive]);
  useSpE(() => { d.publish('rx/gain_mode', 'manual'); }, []);
  useSpE(() => {
    // Real fullscreen can be exited via Esc (or other OS/browser UI) without
    // going through our button, so track it from the browser's own event
    // rather than only setting it optimistically on click.
    const handler = () => setOsFullscreen(!!document.fullscreenElement);
    document.addEventListener('fullscreenchange', handler);
    return () => document.removeEventListener('fullscreenchange', handler);
  }, []);
  useSpE(() => {
    if (!d.rxOverload) { setClipBlink(false); return; }
    const t = setInterval(() => setClipBlink(v => !v), 500);
    return () => clearInterval(t);
  }, [d.rxOverload]);

  // Canvas RAF render loop
  useSpE(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    const syncSize = () => {
      const w = canvas.offsetWidth, h = canvas.offsetHeight;
      if (w > 0 && h > 0 && (canvas.width !== w || canvas.height !== h)) {
        canvas.width  = w;
        canvas.height = h;
        dirtyRef.current = true;
      }
    };
    syncSize();
    const ro = new ResizeObserver(syncSize);
    ro.observe(canvas);

    let running = true, fCount = 0, fTimer = 0;
    const frame = (ts) => {
      if (!running) return;
      syncSize();
      if ((dirtyRef.current || fosforRef.current !== 'off') && canvas.width > 0 && canvas.height > 0) {
        const bins = isSweepR.current ? sweepBuf.current : binsRef.current;
        const vz = viewZoomR.current, vc = viewCenterR.current;
        const vs = vc - 0.5 / vz, ve = vc + 0.5 / vz;
        spDraw(ctx, canvas.width, canvas.height, bins, refDbRef.current, rangeRef.current, vs, ve, fosforRef.current);
        spDrawCursor(ctx, canvas.width, canvas.height, mousePosRef.current, bins,
                     centerHzRef.current, spanHzRef.current, refDbRef.current, rangeRef.current, vs, ve);
        // Keep header MKR in sync with every new frame
        const mp = mousePosRef.current;
        if (mp && bins && bins.length > 0) {
          const binFrac = vs + mp.px * (ve - vs);
          const binI = Math.max(0, Math.min(bins.length - 1, Math.round(binFrac * (bins.length - 1))));
          setMkr({ freq: centerHzRef.current + (binFrac - 0.5) * spanHzRef.current, db: bins[binI] });
        }
        if (vz !== dispZoomRef.current) { dispZoomRef.current = vz; setDispZoom(vz); }
        dirtyRef.current = false;
        fCount++;
        if (ts - fTimer >= 1000) { setFps(fCount); fCount = 0; fTimer = ts; }
      }
      rafRef.current = requestAnimationFrame(frame);
    };
    rafRef.current = requestAnimationFrame(frame);

    return () => {
      running = false;
      cancelAnimationFrame(rafRef.current);
      ro.disconnect();
    };
  }, []);

  // WebSocket connection
  useSpE(() => {
    let destroyed = false;
    const host  = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';

    // Convert raw float amplitude to dBFS
    const toDB = (v) => 20 * Math.log10(v > 0 ? v : 1e-10);

    function connect() {
      if (destroyed) return;
      const ws = new WebSocket(`${proto}//${host}/waterfall`);
      ws.binaryType = 'arraybuffer';
      wsRef.current = ws;
      setWsState('connecting');

      ws.onopen  = () => setWsState('connected');
      ws.onclose = () => {
        setWsState('disconnected');
        if (!destroyed) setTimeout(connect, 2000);
      };
      ws.onerror = () => {};

      ws.onmessage = (evt) => {
        if (!(evt.data instanceof ArrayBuffer)) {
          try {
            const msg = JSON.parse(evt.data);
            if (msg.center != null) setCenterHz(msg.center);
            if (msg.span   != null) setSpanHz(msg.span);
            if (msg.gain   != null) setGain(msg.gain);
          } catch(_) {}
          return;
        }
        const f = new Float32Array(evt.data);
        if (f.length < 2) return;

        // f[0] is always a step index (structural, even outside sweep mode — firmware bug)
        const step = Math.round(f[0]) & 7;
        const len  = f.length - 1;

        if (isSweepR.current) {
          // Sweep mode: stitch 8 sub-frames; discard 15% of each edge to hide filter rolloff.
          // Backend spaces bands by SR*0.70, so the inner 70% of each sub-frame tiles seamlessly.
          const edge   = Math.floor(len * 0.15);
          const usable = len - 2 * edge;
          if (len !== sweepLenR.current || !sweepBuf.current) {
            sweepLenR.current = len;
            sweepBuf.current  = new Float32Array(usable * 8);
            sweepMaskR.current = 0;
          }
          const off = step * usable;
          for (let i = 0; i < usable; i++) sweepBuf.current[off + i] = toDB(f[i + 1 + edge]);
          sweepMaskR.current |= (1 << step);
          if (step === 7 && sweepMaskR.current === 0xFF) dirtyRef.current = true;
        } else {
          // Normal mode: f[1..] are the FFT bins; f[0] is discarded
          const db = new Float32Array(len);
          for (let i = 0; i < len; i++) db[i] = toDB(f[i + 1]);
          binsRef.current = db;
          dirtyRef.current = true;
        }
      };
    }
    connect();
    return () => { destroyed = true; try { wsRef.current?.close(); } catch(_) {} };
  }, []);

  // Publish rx/frequency, or rx/sweep/frequency while sweeping — throttled
  // to at most one publish per 200ms in sweep mode only. do_sweep_start
  // needs real time to (re)initialize the sweep on the device; firing
  // retunes faster than that (e.g. a drag gesture or fast tuner scroll)
  // just queues up stale ones behind each other. Non-sweep single-band
  // retuning is cheap on the device, so it stays unthrottled.
  const publishFreq = (dv, hz) => {
    const topic = dv.sweepActive ? 'rx/sweep/frequency' : 'rx/frequency';
    if (!dv.sweepActive) { dv.publish(topic, Math.round(hz)); return; }
    const now = performance.now();
    if (now - freqThrottleRef.current >= 200) {
      freqThrottleRef.current = now;
      dv.publish(topic, Math.round(hz));
    }
    clearTimeout(freqDebounceRef.current);
    freqDebounceRef.current = setTimeout(() => {
      dv.publish(topic, Math.round(centerHzRef.current));
    }, 220);
  };

  // Publish spectro/fps from the current VFW setting. While sweeping, the
  // device needs 8x the requested fps — a full sweep is built from 8
  // sub-band captures, so hitting the same perceived sweep-update rate
  // needs 8x the raw per-capture rate.
  const publishFps = (dv) => {
    const base = Math.round(1000 / vfwRef.current);
    dv.publish('spectro/fps', dv.sweepActive ? base * 8 : base);
  };

  // Step the hardware SPAN one ladder entry (see NICE_SPANS), keeping the
  // frequency under `px` (0-1 fraction across canvas width) fixed. No
  // graphical zoom layer involved — this directly re-tunes the device.
  // Shared by wheel (plain + Ctrl) and pinch, so `dv` is passed explicitly
  // rather than closed over (the pinch handler uses dRef.current).
  const stepSpanCentered = (dv, px, wider) => {
    const s  = spanHzRef.current;
    const ns = stepSpan(s, wider);
    const markerFreq = centerHzRef.current + (px - 0.5) * s;
    const nc = Math.max(47e6 + ns / 2, Math.min(6e9 - ns / 2, markerFreq));
    setCenterHz(nc);
    setSpanHz(ns);
    publishFreq(dv, nc);
    const now = performance.now();
    if (now - spanThrottleRef.current >= 500) {
      spanThrottleRef.current = now;
      dv.publish('rx/span', Math.round(ns));
      publishFps(dv);
    }
    clearTimeout(wheelDebounceRef.current);
    wheelDebounceRef.current = setTimeout(() => {
      dv.publish('rx/span', Math.round(spanHzRef.current));
      publishFps(dv);
    }, 220);
  };

  // Mouse wheel on canvas: zoom span centred on cursor, or adjust RANGE (Shift)
  const onCanvasWheel = useSpCb((e) => {
    e.preventDefault();

    if (e.shiftKey) {
      // Shift+wheel → RANGE (dB per division)
      setRange(r => Math.max(SP_ROWS, r + (e.deltaY > 0 ? SP_ROWS : -SP_ROWS)));
      return;
    }

    // Wheel → SPAN (hardware), centred on cursor. Ctrl is accepted too for
    // muscle memory but no longer required.
    const canvas = canvasRef.current;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const px   = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    stepSpanCentered(d, px, e.deltaY > 0);
  }, [d]);

  // Keyboard shortcuts: +/- shift REF, [/] change RANGE (1 step = SP_ROWS dB)
  useSpE(() => {
    const onKey = (e) => {
      if (e.target.tagName === 'INPUT') return;
      if (e.key === '+' || e.key === '=') setRefDb(r => r + 5);
      if (e.key === '-' || e.key === '_') setRefDb(r => r - 5);
      if (e.key === '[') setRange(v => Math.max(SP_ROWS, v - SP_ROWS));
      if (e.key === ']') setRange(v => v + SP_ROWS);
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, []);

  // Release drag — flush a final MQTT publish so device lands on exact position
  useSpE(() => {
    const onUp = () => {
      if (mouseDragRef.current) {
        publishFreq(d, centerHzRef.current);
        mouseDragRef.current = null;
      }
    };
    window.addEventListener('mouseup', onUp);
    return () => window.removeEventListener('mouseup', onUp);
  }, [d]);

  // Touch support: single-finger pan + two-finger pinch zoom
  useSpE(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    let startDist = null, startVdist = null, startRange = null, lastStepDist = null;
    const PINCH_STEP_RATIO = 1.3; // finger-distance ratio that triggers one SPAN step

    const pdist = (t) => Math.hypot(t[0].clientX - t[1].clientX, t[0].clientY - t[1].clientY);

    const onTouchStart = (e) => {
      e.preventDefault();
      if (e.touches.length === 1) {
        const t = e.touches[0];
        mouseDragRef.current = {
          startX: t.clientX, startY: t.clientY,
          startRefDb: refDbRef.current, startCenterHz: centerHzRef.current,
        };
      } else if (e.touches.length === 2) {
        mouseDragRef.current = null;
        startDist    = pdist(e.touches);
        lastStepDist = startDist;
        startVdist   = Math.abs(e.touches[0].clientY - e.touches[1].clientY) + 1;
        startRange   = rangeRef.current;
      }
    };

    const onTouchMove = (e) => {
      e.preventDefault();
      if (e.touches.length === 2) {
        if (startDist) {
          // Vertical pinch → RANGE
          const vdist = Math.abs(e.touches[0].clientY - e.touches[1].clientY) + 1;
          const vFactor = Math.sqrt(startVdist / vdist);
          setRange(Math.max(SP_ROWS, startRange + Math.round((vFactor - 1) * 10) * SP_ROWS));

          // Horizontal pinch → SPAN (hardware), one discrete step per
          // PINCH_STEP_RATIO change in finger distance. No graphical zoom
          // layer — each step directly re-tunes the hardware span, same as
          // a wheel tick.
          const d2 = pdist(e.touches);
          const ratio = d2 / lastStepDist;
          if (ratio > PINCH_STEP_RATIO || ratio < 1 / PINCH_STEP_RATIO) {
            const mx   = (e.touches[0].clientX + e.touches[1].clientX) / 2;
            const rect = canvas.getBoundingClientRect();
            const px   = Math.max(0, Math.min(1, (mx - rect.left) / rect.width));
            stepSpanCentered(dRef.current, px, ratio < 1);
            lastStepDist = d2;
          }
          dirtyRef.current = true;
        }
      } else if (e.touches.length === 1 && mouseDragRef.current) {
        const t = e.touches[0];
        const W = canvas.offsetWidth, H = canvas.offsetHeight;
        const rect = canvas.getBoundingClientRect();
        mousePosRef.current = { px: Math.max(0, Math.min(1, (t.clientX - rect.left) / rect.width)) };
        dirtyRef.current = true;
        const dx = t.clientX - mouseDragRef.current.startX;
        const dy = t.clientY - mouseDragRef.current.startY;
        const steps = Math.round((dx / W) * 10);
        const step = spanHzRef.current / 10;
        const nc = Math.abs(dx) >= W * 0.05
          ? mouseDragRef.current.startCenterHz - steps * step
          : mouseDragRef.current.startCenterHz;
        setCenterHz(nc);
        setRefDb(mouseDragRef.current.startRefDb + (dy / H) * rangeRef.current);
        publishFreq(dRef.current, nc);
      }
    };

    const onTouchEnd = (e) => {
      if (e.touches.length < 2) { startDist = null; startVdist = null; startRange = null; lastStepDist = null; }
      if (e.touches.length === 0 && mouseDragRef.current) {
        publishFreq(dRef.current, centerHzRef.current);
        mouseDragRef.current = null;
        mousePosRef.current = null;
        setMkr(null);
        dirtyRef.current = true;
      }
    };

    canvas.addEventListener('touchstart', onTouchStart, { passive: false });
    canvas.addEventListener('touchmove',  onTouchMove,  { passive: false });
    canvas.addEventListener('touchend',   onTouchEnd,   { passive: false });
    return () => {
      canvas.removeEventListener('touchstart', onTouchStart);
      canvas.removeEventListener('touchmove',  onTouchMove);
      canvas.removeEventListener('touchend',   onTouchEnd);
    };
  }, []);

  const onCanvasMouseDown = useSpCb((e) => {
    if (e.button !== 0) return;
    mouseDragRef.current = {
      startX: e.clientX, startY: e.clientY,
      startRefDb: refDbRef.current, startCenterHz: centerHzRef.current,
    };
    e.preventDefault();
  }, []);

  const onCanvasMouseMove = useSpCb((e) => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    // 2-D drag: horizontal → center frequency, vertical → REF level
    if (mouseDragRef.current) {
      const W = canvas.offsetWidth, H = canvas.offsetHeight;
      const dx = e.clientX - mouseDragRef.current.startX;
      const dy = e.clientY - mouseDragRef.current.startY;
      const steps = Math.round((dx / W) * 10);
      const step = spanHzRef.current / 10;
      const nc = Math.abs(dx) >= W * 0.05
        ? mouseDragRef.current.startCenterHz - steps * step
        : mouseDragRef.current.startCenterHz;
      setCenterHz(nc);
      publishFreq(d, nc);
      // Vertical: drag down raises REF
      const deltaDb = (dy / H) * rangeRef.current;
      setRefDb(mouseDragRef.current.startRefDb + deltaDb);
      dirtyRef.current = true;
      return;
    }

    const rect = canvas.getBoundingClientRect();
    const px = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    mousePosRef.current = { px };
    dirtyRef.current = true;
    // Update header MKR readout
    const bins = isSweepR.current ? sweepBuf.current : binsRef.current;
    if (bins && bins.length > 0) {
      const binI = Math.max(0, Math.min(bins.length - 1, Math.round(px * (bins.length - 1))));
      setMkr({ freq: centerHzRef.current + (px - 0.5) * spanHzRef.current, db: bins[binI] });
    }
  }, []);

  const onCanvasMouseLeave = useSpCb(() => {
    mousePosRef.current = null;
    dirtyRef.current = true;
    setMkr(null);
  }, []);

  const fmtMHz  = (hz) => (hz / 1e6).toFixed(3);
  const stMs    = fps > 0 ? Math.round(1000 / fps) : 0;
  const wsTone  = wsState === 'connected' ? 'ok' : wsState === 'connecting' ? 'info' : 'warn';

  const pubGain     = (v) => { setGain(v);    d.publish('rx/gain',    v); };
  const pubInput    = (v) => {
    setRxInput(v);
    d.publish('rx/rfinput', v === 'rx2' ? 2 : 1);
    const savedGain = gain;
    d.publish('rx/gain_mode', 'fast_attack');
    setTimeout(() => {
      d.publish('rx/gain_mode', 'manual');
      d.publish('rx/gain', savedGain);
    }, 100);
  };
  const onCenterChg = useSpCb((kHz) => { const hz = kHz * 1000; setCenterHz(hz); publishFreq(d, hz); }, [d]);
  const onVfwChg    = useSpCb((v) => { setVfw(v); vfwRef.current = v; publishFps(d); }, [d]);
  const onSpanChg   = useSpCb((kHz) => {
    const hz = kHz * 1000;
    setSpanHz(hz);
    const now = performance.now();
    if (now - spanThrottleRef.current >= 500) {
      spanThrottleRef.current = now;
      d.publish('rx/span', hz);
      publishFps(d);
    }
    clearTimeout(spanDebounceRef.current);
    spanDebounceRef.current = setTimeout(() => {
      d.publish('rx/span', Math.round(spanHzRef.current));
      publishFps(d);
    }, 220);
  }, [d]);

  return (
    <div className={`sp-page${cssMaximized ? ' maximized' : ''}`} ref={spPageRef}>
      <div className="hp-crt">
        <div className="hp-rows">
          <div className="hp-row">
            <span className="hp-fld">
              <span className="hp-pfx">REF</span>
              <DbTuner value={Math.round(refDb)} digits={3} unit="dBm" onChange={setRefDb} />
            </span>
            <span style={{ display: 'inline-flex', alignItems: 'baseline', gap: '1ch' }}>
              <CrtField pfx="MKR" val={fmtMHz(mkr ? mkr.freq : centerHz)} sfx="MHz" readOnly />
              <CrtField val={mkr ? mkr.db.toFixed(1) : (refDb - range).toFixed(1)} sfx="dB" readOnly />
            </span>
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }}
              onClick={toggleOsFullscreen}>
              <span className="hp-pfx">FULL</span>
              <span style={{ color: '#fff3d6' }}>{isFullscreen ? 'ON' : 'OFF'}</span>
            </span>
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }}
              onClick={() => pubInput(rxInput === 'rx1' ? 'rx2' : 'rx1')}>
              <span className="hp-pfx">ANT</span>
              <span style={{ color: '#fff3d6' }}>{rxInput === 'rx1' ? 'RX1' : 'RX2'}</span>
            </span>
          </div>
          <div className="hp-row">
            <span style={{ display: 'inline-flex', alignItems: 'baseline', gap: '1ch' }}>
              <span className="hp-fld">
                <span className="hp-pfx">RANGE</span>
                <div className="hp-tuner">
                  <FreqTuner value={range} digits={3} min={SP_ROWS} max={800} unit="dB"
                    onChange={setRange} />
                </div>
              </span>
              <CrtField val={(range / SP_ROWS).toFixed(0)} sfx="dB/DIV" readOnly />
            </span>
            <span className="hp-fld" style={{ cursor: 'pointer', userSelect: 'none' }}
              onClick={() => setFosfor(f => ({ off: 'light', light: 'medium', medium: 'high', high: 'off' })[f])}>
              <span className="hp-pfx">FOSFOR</span>
              <span style={{ color: '#fff3d6' }}>{fosfor.toUpperCase()}</span>
            </span>
            <span className="hp-fld" style={clipBlink ? { background: 'var(--phos)', color: '#000', borderRadius: 2, padding: '0 3px', textShadow: 'none' } : {}}>
              <span className="hp-pfx">GAIN</span>
              <DbTuner value={gain} digits={2} unit="dB" onChange={pubGain} />
            </span>
          </div>
        </div>

        <div className="hp-screen">
          <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: '100%', cursor: 'crosshair' }}
            onWheel={onCanvasWheel} onMouseDown={onCanvasMouseDown}
            onMouseMove={onCanvasMouseMove} onMouseLeave={onCanvasMouseLeave} />
        </div>

        <div className="hp-rows">
          <div className="hp-row">
            <div className="hp-fld">
              <span className="hp-pfx">CENTER</span>
              <div className="hp-tuner">
                <FreqTuner value={Math.round(centerHz / 1000)} digits={7} min={47000} max={6000000} unit="MHz" onChange={onCenterChg} />
              </div>
            </div>
            <div className="hp-fld">
              <span className="hp-pfx">VFW</span>
              <div className="hp-tuner">
                <FreqTuner value={vfw} digits={4} min={20} max={1000} unit="ms" onChange={onVfwChg} />
              </div>
            </div>
            <div className="hp-fld">
              <span className="hp-pfx">SPAN</span>
              <div className="hp-tuner">
                {spanHz != null && <FreqTuner value={Math.round(spanHz / 1000)} digits={6} min={80} max={300000} unit="MHz" onChange={onSpanChg} />}
              </div>
            </div>
          </div>
          <div className="hp-row sm">
            <CrtField pfx="ST" val={stMs.toString()} sfx="ms" readOnly />
            {dispZoom > 1 && <CrtField pfx="ZOOM" val={`×${dispZoom % 1 === 0 ? dispZoom : dispZoom.toFixed(1)}`} readOnly />}
          </div>
        </div>
      </div>

    </div>
  );
}

Object.assign(window, { Dashboard, RFParams, SpectrumPage, NICE_SPANS, stepSpan });
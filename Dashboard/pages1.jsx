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
  useE1(() => { if (d.txRfinput != null)  setOutput(d.txRfinput - 1); }, [d.txRfinput]);

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
            <div className="ds-shared-head"><span className="bigstat-label" style={{ margin: 0 }}>Baseband</span><span className="pill pill-neutral">RX + TX · common</span></div>
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
              </div>
              <div className="bigstat-grid">
                <BigStat label="Output" value={TX[output]} options color={d.txDmaTransfer > 0 ? "var(--c-pink)" : undefined} onCycle={() => { const next = (output + 1) % 2; setOutput(next); d.publish("tx/rfinput", next + 1); }} />
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

function CrtField({ pfx, val, sfx, onChange, center }) {
  return (
    <span className={`hp-fld ${center ? "ctr" : ""}`}>
      {pfx ? <span className="hp-pfx">{pfx}</span> : null}
      <input
        className="hp-inp" value={val} onChange={onChange} spellCheck={false}
        onFocus={(e) => e.target.select()}
        style={{ width: `calc(${Math.max(String(val).length, 1)}ch + 2px)` }} />
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

function SpectrumPage() {
  const freq = 437.000, bw = 2.400;
  return (
    <div className="page">
      <Card className="span-12" pad={false}>
        <SpectrumStub freq={freq} bw={bw} />
      </Card>
    </div>
  );
}

Object.assign(window, { Dashboard, RFParams, SpectrumPage });
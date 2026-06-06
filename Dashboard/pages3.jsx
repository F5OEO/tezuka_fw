// pages3.jsx — Architecture page: themed AD9361 block diagram (clickable, editable)
const { useState: useS3, useEffect: useE3 } = React;

const ARCH_GAIN_MODE = { manual: 'Manual', slow_attack: 'Slow AGC', fast_attack: 'Fast AGC', hybrid: 'Slow AGC' };
const ARCH_GAIN_MODE_INV = { Manual: 'manual', 'Slow AGC': 'slow_attack', 'Fast AGC': 'fast_attack' };

const GCOL = { rx: "var(--accent)", tx: "var(--c-pink)", lo: "var(--c-blue)", sys: "var(--dim)", port: "var(--ok)", fpga: "var(--c-purple)" };
const GNAME = { rx: "Receive chain", tx: "Transmit chain", lo: "Synthesizer", sys: "System", port: "RF port", fpga: "Processing system", overview: "Device" };

// geometry: [x, y, w, h, group, label, sub]
const BLK = {
  rxport: [24, 80, 84, 56, "port", "RX1/RX2", "RF input"],
  lna: [120, 80, 84, 56, "rx", "LNA", "+ Att"],
  rxmix: [216, 80, 84, 56, "rx", "RX Mixer", null],
  rxbb: [312, 80, 84, 56, "rx", "BB Filter", null],
  rxadc: [408, 80, 84, 56, "rx", "ADC", null],
  rxhb: [504, 80, 84, 56, "rx", "Dec / HB", null],
  txport: [24, 400, 84, 56, "port", "TX1/TX2", "RF output"],
  txpa: [120, 400, 84, 56, "tx", "Driver", "+ Att"],
  txmix: [216, 400, 84, 56, "tx", "TX Mixer", null],
  txbb: [312, 400, 84, 56, "tx", "BB Filter", null],
  txdac: [408, 400, 84, 56, "tx", "DAC", null],
  txhb: [504, 400, 84, 56, "tx", "Int / HB", null],
  rxlo: [216, 180, 84, 44, "lo", "RX PLL", null],
  txlo: [216, 316, 84, 44, "lo", "TX PLL", null],
  aux: [312, 250, 84, 44, "sys", "AuxADC", null],
  bbpll: [408, 250, 84, 44, "sys", "BB PLL", null],
  ctrl: [504, 250, 84, 44, "sys", "SPI / ENSM", null],
  ddi: [606, 210, 72, 120, "sys", "Data Port", null],
  fpga: [714, 190, 110, 160, "fpga", "Zynq PL", "Tezuka HDL"],
};

const CONN = [
  ["rx", "M108,108 H120"], ["rx", "M204,108 H216"], ["rx", "M300,108 H312"], ["rx", "M396,108 H408"], ["rx", "M492,108 H504"],
  ["rx", "M588,108 H597 V250 H606"],
  ["tx", "M606,290 H597 V428 H588"], ["tx", "M504,428 H492"], ["tx", "M408,428 H396"], ["tx", "M312,428 H300"], ["tx", "M216,428 H204"], ["tx", "M120,428 H108"],
  ["lo", "M258,180 V136"], ["lo", "M258,360 V400"],
  ["lo", "M450,250 V136"], ["lo", "M450,294 V400"],
  ["sys", "M588,272 H606"],
  ["data", "M678,270 H714"],
];

const DEFAULTS = {
  rxInput: "RX1", txOutput: "TX1", gainMode: "Manual", rxGain: 38,
  rxFreqHz: 437000000, txFreqHz: 437000000, rxBw: 2.00, txBw: 2.00,
  rxSrHz: 2400000, txSrHz: 2400000, rxDec: "÷4", txInt: "×4", txAtten: -11,
  ensm: "FDD", ddiIf: "LVDS", bbRef: "40 MHz TCXO",
};

// field kinds: ro | num | slider | select
const META = {
  overview: { desc: "Agile RF transceiver — wideband 2×2 MIMO front-end paired with the Zynq programmable logic.", fields: [
    { kind: "ro", label: "Part", get: () => "AD9361" }, { kind: "ro", label: "Configuration", get: () => "2×2 MIMO" },
    { kind: "ro", label: "Tuning range", get: () => "70 MHz – 6 GHz" }, { kind: "ro", label: "Max channel BW", get: () => "56 MHz" }] },
  rxport: { desc: "Differential RF receive ports. Two independent inputs can be active simultaneously.", fields: [
    { kind: "select", key: "rxInput", label: "Active input", options: ["RX1", "RX2", "RX1 + RX2"] },
    { kind: "ro", label: "Impedance", get: () => "50 Ω" },
    { kind: "ro", label: "RSSI", get: (d) => d.rfLevel != null ? `${d.rfLevel.toFixed(1)} dBm` : '—' }] },
  lna: { desc: "Low-noise amplifier with stepped attenuator. Gain is set manually or by the AGC.", fields: [
    { kind: "select", key: "gainMode", label: "Gain mode", options: ["Manual", "Slow AGC", "Fast AGC"] },
    { kind: "slider", key: "rxGain", label: "RX gain", min: 0, max: 73, step: 1, unit: " dB" },
    { kind: "ro", label: "RSSI", get: () => "−62.4 dBm" }] },
  rxmix: { desc: "Zero-IF quadrature down-conversion mixer driven by the RX synthesizer.", fields: [
    { kind: "tuner", key: "rxFreqHz", label: "RX LO frequency", digits: 12, min: 70e6, max: 6e9, unit: "Hz", sub: (v) => (v / 1e6).toFixed(3) + " MHz" },
    { kind: "ro", label: "Architecture", get: () => "Zero-IF" }] },
  rxbb: { desc: "Trans-impedance amplifier and programmable baseband low-pass filter.", fields: [
    { kind: "slider", key: "rxBw", label: "Bandwidth", min: 0.2, max: 18, step: 0.1, unit: " MHz", fmt: (v) => v.toFixed(2) },
    { kind: "ro", label: "Type", get: () => "TIA + LPF" }] },
  rxadc: { desc: "Σ-Δ analog-to-digital converter sampling the baseband I/Q.", fields: [
    { kind: "tuner", key: "rxSrHz", label: "Sample rate", digits: 9, min: 520833, max: 61440000, unit: "S/s", sub: (v) => (v / 1e6).toFixed(3) + " MS/s" },
    { kind: "ro", label: "Resolution", get: () => "12-bit" }, { kind: "ro", label: "Σ-Δ clock", get: () => "640 MHz" }] },
  rxhb: { desc: "Half-band decimation filter chain reducing the data rate to the requested sample rate.", fields: [
    { kind: "select", key: "rxDec", label: "Decimation", options: ["÷1", "÷2", "÷4", "÷8"] },
    { kind: "ro", label: "Filters", get: () => "HB1 / HB2 / HB3" }] },
  txhb: { desc: "Interpolation / half-band filter chain raising the baseband rate for the DAC.", fields: [
    { kind: "select", key: "txInt", label: "Interpolation", options: ["×1", "×2", "×4", "×8"] },
    { kind: "ro", label: "Filters", get: () => "HB1 / HB2 / HB3" }] },
  txdac: { desc: "Digital-to-analog converter generating the transmit baseband I/Q.", fields: [
    { kind: "tuner", key: "txSrHz", label: "Sample rate", digits: 9, min: 520833, max: 61440000, unit: "S/s", sub: (v) => (v / 1e6).toFixed(3) + " MS/s" },
    { kind: "ro", label: "Resolution", get: () => "12-bit" }] },
  txbb: { desc: "Programmable baseband low-pass reconstruction filter.", fields: [
    { kind: "slider", key: "txBw", label: "Bandwidth", min: 0.2, max: 18, step: 0.1, unit: " MHz", fmt: (v) => v.toFixed(2) },
    { kind: "ro", label: "Type", get: () => "LPF" }] },
  txmix: { desc: "Direct-conversion quadrature up-conversion mixer driven by the TX synthesizer.", fields: [
    { kind: "tuner", key: "txFreqHz", label: "TX LO frequency", digits: 12, min: 70e6, max: 6e9, unit: "Hz", sub: (v) => (v / 1e6).toFixed(3) + " MHz" },
    { kind: "ro", label: "Architecture", get: () => "Direct conversion" }] },
  txpa: { desc: "PA driver with digital attenuator setting the output level.", fields: [
    { kind: "slider", key: "txAtten", label: "TX attenuation", min: -89, max: 0, step: 0.25, unit: " dB", fmt: (v) => v.toFixed(2) },
    { kind: "ro", label: "Linearity", get: () => "calibrated" }] },
  txport: { desc: "Differential RF transmit ports.", fields: [
    { kind: "select", key: "txOutput", label: "Active output", options: ["TX1", "TX2", "TX1 + TX2"] },
    { kind: "ro", label: "Impedance", get: () => "50 Ω" }] },
  rxlo: { desc: "RX fractional-N PLL synthesizer generating the receive local oscillator.", fields: [
    { kind: "tuner", key: "rxFreqHz", label: "Frequency", digits: 12, min: 70e6, max: 6e9, unit: "Hz", sub: (v) => (v / 1e6).toFixed(3) + " MHz" },
    { kind: "ro", label: "Type", get: () => "Fractional-N" }, { kind: "ro", label: "Lock", get: () => "locked" }] },
  txlo: { desc: "TX fractional-N PLL synthesizer generating the transmit local oscillator.", fields: [
    { kind: "tuner", key: "txFreqHz", label: "Frequency", digits: 12, min: 70e6, max: 6e9, unit: "Hz", sub: (v) => (v / 1e6).toFixed(3) + " MHz" },
    { kind: "ro", label: "Type", get: () => "Fractional-N" }, { kind: "ro", label: "Lock", get: () => "locked" }] },
  aux: { desc: "Auxiliary ADC and internal temperature sensor.", fields: [
    { kind: "ro", label: "SoC temp", get: (d) => `${d.temp.toFixed(1)} °C` }, { kind: "ro", label: "FPGA temp", get: (d) => `${d.fpgaTemp.toFixed(1)} °C` },
    { kind: "ro", label: "AuxADC", get: () => "0.82 V" }] },
  bbpll: { desc: "Baseband PLL deriving all converter and data clocks from the reference.", fields: [
    { kind: "select", key: "bbRef", label: "Reference", options: ["40 MHz TCXO", "30.72 MHz", "10 MHz ext"] },
    { kind: "ro", label: "Clock", get: () => "983.04 MHz" }, { kind: "ro", label: "Lock", get: () => "locked" }] },
  ctrl: { desc: "4-wire SPI control bus and the Enable State Machine (ENSM).", fields: [
    { kind: "select", key: "ensm", label: "ENSM state", options: ["FDD", "TDD", "Alert", "Sleep"] },
    { kind: "ro", label: "Interface", get: () => "SPI 4-wire" }, { kind: "ro", label: "GPO", get: () => "0x0" }] },
  ddi: { desc: "Digital data interface carrying I/Q samples to and from the FPGA.", fields: [
    { kind: "select", key: "ddiIf", label: "Interface", options: ["LVDS", "CMOS"] },
    { kind: "ro", label: "RX data rate", get: (d) => `${d.rxRate.toFixed(2)} Mb/s` }, { kind: "ro", label: "Bus", get: () => "12-bit I/Q" }] },
  fpga: { desc: "Zynq-7020 programmable logic running the Tezuka HDL core and AXI DMA.", fields: [
    { kind: "ro", label: "Device", get: () => "XC7Z020" }, { kind: "ro", label: "Core", get: () => "Tezuka HDL" }, { kind: "ro", label: "PL clock", get: () => "100 MHz" }] },
};

function ArchBlock({ id, sel, onClick }) {
  const [x, y, w, h, g, label, sub] = BLK[id];
  const cx = x + w / 2, cy = y + h / 2;
  return (
    <g className={`blk ${sel ? "sel" : ""}`} style={{ "--bc": GCOL[g] }} onClick={() => onClick(id)}>
      <rect x={x} y={y} width={w} height={h} rx="7" />
      <text className="t" x={cx} y={sub ? cy - 3 : cy + 4} textAnchor="middle">{label}</text>
      {sub && <text className="s" x={cx} y={cy + 12} textAnchor="middle">{sub}</text>}
    </g>
  );
}

function ArchField({ f, vals, set, d }) {
  if (f.kind === "ro") return <div className="arch-param"><span>{f.label}</span><b className="mono">{f.get(d)}</b></div>;
  if (f.kind === "select") return <Field label={f.label}><Select value={vals[f.key]} onChange={(v) => set(f.key, v)} options={f.options} /></Field>;
  if (f.kind === "num") return <Field label={f.label}><TextInput value={String(vals[f.key])} onChange={(v) => set(f.key, v)} suffix={f.suffix} /></Field>;
  if (f.kind === "tuner") return <Field label={f.label}><FreqTuner value={vals[f.key]} digits={f.digits} min={f.min} max={f.max} unit={f.unit} sub={f.sub} onChange={(v) => set(f.key, v)} /></Field>;
  if (f.kind === "slider") return <Field label={f.label}><Slider value={vals[f.key]} min={f.min} max={f.max} step={f.step} unit={f.unit} fmt={f.fmt} onChange={(v) => set(f.key, v)} /></Field>;
  return null;
}

function Architecture({ d }) {
  const [sel, setSel] = useS3("overview");
  const [vals, setVals] = useS3(DEFAULTS);
  const [applied, setApplied] = useS3(DEFAULTS);
  const set = (k, v) => setVals((p) => ({ ...p, [k]: v }));
  const dirty = JSON.stringify(vals) !== JSON.stringify(applied);

  const sync = (patch) => {
    setVals((p) => ({ ...p, ...patch }));
    setApplied((p) => ({ ...p, ...patch }));
  };
  useE3(() => { if (d.rxFreq      != null) sync({ rxFreqHz: d.rxFreq }); },                         [d.rxFreq]);
  useE3(() => { if (d.txFreq      != null) sync({ txFreqHz: d.txFreq }); },                         [d.txFreq]);
  useE3(() => { if (d.rxSampling  != null) sync({ rxSrHz: d.rxSampling }); },                       [d.rxSampling]);
  useE3(() => { if (d.txSampling  != null) sync({ txSrHz: d.txSampling }); },                       [d.txSampling]);
  useE3(() => { if (d.rxBandwidth != null) sync({ rxBw: d.rxBandwidth / 1e6 }); },                  [d.rxBandwidth]);
  useE3(() => { if (d.txBandwidth != null) sync({ txBw: d.txBandwidth / 1e6 }); },                  [d.txBandwidth]);
  useE3(() => { if (d.rxGain      != null) sync({ rxGain: d.rxGain }); },                           [d.rxGain]);
  useE3(() => { if (d.txGain      != null) sync({ txAtten: d.txGain }); },                          [d.txGain]);
  useE3(() => { if (d.rxGainMode)          sync({ gainMode: ARCH_GAIN_MODE[d.rxGainMode] || 'Manual' }); }, [d.rxGainMode]);

  const applyToDevice = () => {
    if (vals.rxFreqHz !== applied.rxFreqHz)   d.publish('rx/frequency',    vals.rxFreqHz);
    if (vals.txFreqHz !== applied.txFreqHz)   d.publish('tx/frequency',    vals.txFreqHz);
    if (vals.rxBw     !== applied.rxBw)       d.publish('rx/bandwidth',    Math.round(vals.rxBw * 1e6));
    if (vals.txBw     !== applied.txBw)       d.publish('tx/bandwidth',    Math.round(vals.txBw * 1e6));
    if (vals.rxSrHz   !== applied.rxSrHz)   { d.publish('rx/sampling',     vals.rxSrHz); d.publish('tx/sampling', vals.rxSrHz); }
    if (vals.rxGain   !== applied.rxGain)     d.publish('rx/gain',         vals.rxGain);
    if (vals.txAtten  !== applied.txAtten)    d.publish('tx/gain',         vals.txAtten);
    if (vals.gainMode !== applied.gainMode)   d.publish('rx/gain_mode',    ARCH_GAIN_MODE_INV[vals.gainMode] || 'manual');
    setApplied(vals);
  };
  const m = META[sel];
  const g = sel === "overview" ? "overview" : BLK[sel][4];
  const editable = m.fields.some((f) => f.kind !== "ro");
  const title = sel === "overview" ? "AD9361 transceiver" : BLK[sel][5] + (BLK[sel][6] ? " " + BLK[sel][6] : "");
  return (
    <div className="page">
      <div className="grid-12">
        <Card title="AD9361 RF transceiver" sub="Functional block diagram · click a block to edit" className="span-8">
          <svg viewBox="0 0 848 500" className="arch-svg">
            <defs>
              <marker id="arr" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
                <path d="M0,1 L9,5 L0,9 Z" fill="context-stroke" />
              </marker>
            </defs>
            <rect x="110" y="64" width="580" height="408" rx="14" className="chip" />
            <text x="120" y="56" className="chip-label">AD9361 · 2×2 MIMO</text>
            <text x="769" y="180" className="chip-label" textAnchor="middle">Zynq-7020</text>
            {CONN.map(([cls, dpath], i) => (
              <path key={i} d={dpath} className={`conn ${cls}`} markerEnd="url(#arr)" markerStart={cls === "data" ? "url(#arr)" : undefined} />
            ))}
            <text x="268" y="162" className="wire-lbl">LO</text>
            <text x="268" y="392" className="wire-lbl">LO</text>
            <text x="460" y="162" className="wire-lbl">CLK</text>
            <text x="460" y="392" className="wire-lbl">CLK</text>
            <text x="645" y="265" className="wire-lbl">I/Q</text>
            {Object.keys(BLK).map((id) => <ArchBlock key={id} id={id} sel={sel === id} onClick={setSel} />)}
          </svg>
          <div className="arch-legend">
            {Object.entries(GNAME).filter(([k]) => k !== "overview").map(([k, v]) => (
              <span key={k}><i style={{ background: GCOL[k] }} />{v}</span>
            ))}
          </div>
        </Card>

        <Card title={title} sub={GNAME[g]} className="span-4">
          <div className="arch-tag-row"><span className="rf-tag" style={{ "--ac": GCOL[g], background: "color-mix(in oklab, var(--ac) 18%, transparent)", color: GCOL[g] }}>{g.toUpperCase()}</span>{sel !== "overview" && <button className="btn ghost sm" onClick={() => setSel("overview")}>Overview</button>}</div>
          <p className="arch-desc">{m.desc}</p>
          <div className="arch-fields">
            {m.fields.map((f, i) => <ArchField key={i} f={f} vals={vals} set={set} d={d} />)}
          </div>
          {editable
            ? <div className="arch-hint"><Icon name="check" size={15} />Edit parameters above, then apply to the device.</div>
            : <div className="arch-hint"><Icon name="chip" size={15} />Read-only — values stream live over MQTT.</div>}
        </Card>
      </div>

      <div className={`action-bar ${dirty ? "show" : ""}`}>
        <span>{dirty ? "Unsaved parameter changes" : "All changes applied"}</span>
        <div className="ab-btns">
          <button className="btn ghost" onClick={() => setVals(applied)}>Reset</button>
          <button className="btn primary" onClick={applyToDevice}><Icon name="check" size={16} />Apply</button>
        </div>
      </div>
    </div>
  );
}

window.Architecture = Architecture;

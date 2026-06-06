// pages2.jsx — DATV Controller, Versions, Analysis, Network
const { useState: useS2, useEffect: useE2 } = React;

function DATV({ d, callsign }) {
  const dv = d.datv || {};
  const call = callsign || 'F5OEO';
  const pub = (path, val) => d.publish('pluto/' + call + '/' + path, String(val));

  const [onAir, setOnAir] = useS2(false);
  const [freqHz, setFreqHz] = useS2(437000000);
  const [gain, setGain] = useS2(-11);
  const [mode, setMode] = useS2('dvbs2-ts');
  const [modu, setModu] = useS2('qpsk');
  const [sr, setSr] = useS2(250000);
  const [fec, setFec] = useS2('2/3');
  const [pilots, setPilots] = useS2('0');
  const [frame, setFrame] = useS2('long');
  const [firFilter, setFirFilter] = useS2('0');
  const [tsSource, setTsSource] = useS2('0');
  const [tsAddr, setTsAddr] = useS2('239.0.0.1:5004');
  // Sync from MQTT retained values on first arrival
  useE2(() => { if (dv['tx/frequency']             != null) setFreqHz(parseFloat(dv['tx/frequency'])); },           [dv['tx/frequency']]);
  useE2(() => { if (dv['tx/gain']                  != null) setGain(parseFloat(dv['tx/gain'])); },                  [dv['tx/gain']]);
  useE2(() => { if (dv['tx/mute']                  != null) setOnAir(dv['tx/mute'] === '0'); },                     [dv['tx/mute']]);
  useE2(() => { if (dv['tx/stream/mode']           != null) setMode(dv['tx/stream/mode']); },                       [dv['tx/stream/mode']]);
  useE2(() => { if (dv['tx/dvbs2/constel']         != null) setModu(dv['tx/dvbs2/constel']); },                     [dv['tx/dvbs2/constel']]);
  useE2(() => { if (dv['tx/dvbs2/sr']              != null) setSr(parseInt(dv['tx/dvbs2/sr'])); },                  [dv['tx/dvbs2/sr']]);
  useE2(() => {
    if (dv['tx/dvbs2/fecmode'] === 'variable') { setFec('auto'); return; }
    if (dv['tx/dvbs2/fec'] != null) setFec(dv['tx/dvbs2/fec']);
  }, [dv['tx/dvbs2/fecmode'], dv['tx/dvbs2/fec']]);
  useE2(() => { if (dv['tx/dvbs2/pilots']          != null) setPilots(dv['tx/dvbs2/pilots']); },                    [dv['tx/dvbs2/pilots']]);
  useE2(() => { if (dv['tx/dvbs2/frame']           != null) setFrame(dv['tx/dvbs2/frame']); },                      [dv['tx/dvbs2/frame']]);
  useE2(() => { if (dv['tx/dvbs2/firfilter']       != null) setFirFilter(dv['tx/dvbs2/firfilter']); },              [dv['tx/dvbs2/firfilter']]);
  useE2(() => { if (dv['tx/dvbs2/tssourcemode']    != null) setTsSource(dv['tx/dvbs2/tssourcemode']); },            [dv['tx/dvbs2/tssourcemode']]);
  useE2(() => { if (dv['tx/dvbs2/tssourceaddress'] != null) setTsAddr(dv['tx/dvbs2/tssourceaddress']); },           [dv['tx/dvbs2/tssourceaddress']]);

  const isDvbs2 = mode === 'dvbs2-ts' || mode === 'dvbs2-gse';
  const hasSr = isDvbs2 || mode === 'dvbs';

  const fecNum = (s) => { if (s === 'auto') return 0.5; const [a, b] = s.split('/'); return parseFloat(a) / parseFloat(b); };
  const tsBitrateStr = dv['tx/dvbs2/ts/bitrate']
    ? (parseFloat(dv['tx/dvbs2/ts/bitrate']) / 1000).toFixed(1) + ' Kb/s'
    : isDvbs2 ? (sr * 2 * fecNum(fec) * 0.88 / 1000).toFixed(1) + ' Kb/s (est.)' : '—';
  const queue = dv['tx/dvbs2/queue'] ? parseInt(dv['tx/dvbs2/queue']) : 0;
  const queueWarn = queue > 100;

  const modeLabels = { test: 'Test tone', pass: 'Passthrough', 'dvbs2-ts': 'DVB-S2/TS', 'dvbs2-gse': 'DVB-S2/GSE', dvbs: 'DVB-S' };
  const modeOpts = Object.entries(modeLabels).map(([v, l]) => ({ v, l }));

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>DATV Controller</h1>
          <span className="datv-sub mono">{call} · {modeLabels[mode] || mode}{isDvbs2 ? ' · ' + modu.toUpperCase() : ''}</span>
        </div>
        <div className={`onair-box ${onAir ? 'live' : ''}`}>
          <div className="onair-lamp"><span /></div>
          <div className="onair-text"><span>{onAir ? 'ON AIR' : 'STANDBY'}</span><small className="mono">PTT</small></div>
          <Toggle on={onAir} onChange={(v) => { setOnAir(v); pub('tx/mute', v ? '0' : '1'); }} labels={['TX OFF', 'TX ON']} />
        </div>
      </div>

      <div className="grid-12">
        <Card title="Modulator" sub="PlutoSDR DVB-S / DVB-S2 transmitter" className="span-7">
          <div className="form-grid">
            <div style={{ gridColumn: '1 / -1' }}>
              <Field label="Frequency" hint="47 MHz – 6 GHz">
                <FreqTuner value={freqHz} digits={12} min={47e6} max={6e9} unit="Hz"
                  sub={(v) => (v / 1e6).toFixed(3) + ' MHz'}
                  onChange={(v) => { setFreqHz(v); pub('tx/frequency', v); }} />
              </Field>
            </div>
            <Field label="TX gain" hint="−80 to 0 dB · step 0.25">
              <Slider value={gain} min={-80} max={0} step={0.25}
                onChange={(v) => { setGain(v); pub('tx/gain', v); }} unit=" dB" fmt={(v) => v.toFixed(2)} />
            </Field>
            <Field label="Stream mode">
              <Select value={mode} onChange={(v) => { setMode(v); pub('tx/stream/mode', v); }} options={modeOpts} />
            </Field>
            {hasSr && (
              <Field label="Symbol rate" hint="25 000 – 4 000 000 Bd">
                <TextInput value={sr} onChange={(v) => { const n = parseInt(v); if (!isNaN(n)) { setSr(n); pub('tx/dvbs2/sr', n); } }} suffix="Bd" />
              </Field>
            )}
            {isDvbs2 && <>
              <Field label="Constellation">
                <Select value={modu} onChange={(v) => { setModu(v); pub('tx/dvbs2/constel', v); }} options={['qpsk', '8psk', '16apsk', '32apsk']} />
              </Field>
              <Field label="FEC">
                <Select value={fec} onChange={(v) => {
                  setFec(v);
                  if (v === 'auto') { pub('tx/dvbs2/fecmode', 'variable'); }
                  else { pub('tx/dvbs2/fecmode', 'fixed'); pub('tx/dvbs2/fec', v); }
                }} options={[{ v: 'auto', l: 'Auto' }, '1/4', '1/3', '2/5', '3/5', '4/5', '5/6', '8/9', '9/10']} />
              </Field>
              <Field label="Pilots">
                <Select value={pilots} onChange={(v) => { setPilots(v); pub('tx/dvbs2/pilots', v); }} options={[{ v: '0', l: 'Off' }, { v: '1', l: 'On' }]} />
              </Field>
              <Field label="Frame">
                <Select value={frame} onChange={(v) => { setFrame(v); pub('tx/dvbs2/frame', v); }} options={[{ v: 'long', l: 'Long frame' }, { v: 'short', l: 'Short frame' }]} />
              </Field>
              <Field label="FIR rolloff">
                <Select value={firFilter} onChange={(v) => { setFirFilter(v); pub('tx/dvbs2/firfilter', v); }} options={[{ v: '0', l: '0.20 (standard)' }, { v: '1', l: '0.15 (narrow)' }]} />
              </Field>
            </>}
          </div>
        </Card>

        <div className="span-5 tile-stack">
          {isDvbs2 && (
            <Card title="TS source">
              <Field label="Input mode">
                <Select value={tsSource}
                  onChange={(v) => { setTsSource(v); pub('tx/dvbs2/tssourcemode', v); }}
                  options={[{ v: '0', l: 'UDP' }, { v: '1', l: 'File' }, { v: '2', l: 'Internal pattern' }]} />
              </Field>
              {tsSource === '0' && (
                <Field label="UDP address:port">
                  <TextInput value={tsAddr} onChange={(v) => { setTsAddr(v); pub('tx/dvbs2/tssourceaddress', v); }} />
                </Field>
              )}
            </Card>
          )}
          <Card title="Stream status">
            <div className="budget">
              <div className="budget-main"><span>TS bitrate</span><b className="mono">{tsBitrateStr}</b></div>
              <div className="budget-row" style={queueWarn ? { color: 'var(--c-coral)' } : {}}>
                <span>Buffer queue</span><b className="mono">{queue} BBframes{queueWarn ? ' ↑' : ''}</b>
              </div>
              {dv['tx/dvbs2/ts/fecvariable'] && (
                <div className="budget-row"><span>Current FEC</span><b className="mono">{dv['tx/dvbs2/ts/fecvariable']}</b></div>
              )}
              {dv['tx/dvbs2/ts/ccerror'] && (
                <div className="budget-row" style={{ color: 'var(--c-coral)' }}>
                  <span>CC error PID</span><b className="mono">{dv['tx/dvbs2/ts/ccerror']}</b>
                </div>
              )}
              <div className="budget-row"><span>Firmware</span><b className="mono">{dv['system/version'] || '—'}</b></div>
            </div>
          </Card>
        </div>
      </div>
    </div>
  );
}

// ---- Versions -------------------------------------------------------------
function Versions({ ver }) {
  const rows = [
    ["Tezuka", ver.tezuka, "Application firmware", "ok"],
    ["Linux kernel", ver.linux, "armv7l GNU/Linux · 2 cores", "ok"],
    ["U-Boot", ver.uboot, "Bootloader", "ok"],
    ["FPGA bitstream", ver.fpga, "AD9363 RF fabric", "ok"],
    ["Root FS", ver.rootfs, "Read-only squashfs", "ok"],
    ["libiio", ver.iio, "IIO library", "neutral"],
  ];
  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Platform" className="span-5">
          <div className="platform">
            <div className="plat-badge"><Icon name="chip" size={26} /></div>
            <div>
              <h3>{ver.model}</h3>
              <span className="mono dim">{ver.serial}</span>
            </div>
          </div>
          <div className="kv-grid mt">
            <div className="kv"><span>SoC</span><b className="mono">Zynq-7020</b></div>
            <div className="kv"><span>RF transceiver</span><b className="mono">AD9363</b></div>
            <div className="kv"><span>GCC target</span><b className="mono">arm-linux-gnueabihf</b></div>
            <div className="kv"><span>Build date</span><b className="mono">Mar 9 2026</b></div>
          </div>
        </Card>

        <Card title="Firmware components" sub="Each part carries its own version" className="span-7" pad={false}>
          <table className="ver-table">
            <thead><tr><th>Component</th><th>Version</th><th>Notes</th><th></th></tr></thead>
            <tbody>
              {rows.map(([n, v, note, tone]) => (
                <tr key={n}>
                  <td className="vt-name">{n}</td>
                  <td className="mono vt-ver">{v}</td>
                  <td className="dim">{note}</td>
                  <td><Pill tone={tone} dot>{tone === "ok" ? "current" : "info"}</Pill></td>
                </tr>
              ))}
            </tbody>
          </table>
        </Card>

        <Card title="Update" sub="Check for newer Tezuka releases" className="span-12">
          <div className="update-row">
            <div className="up-status"><Pill tone="ok" dot>Up to date</Pill><span className="dim">Last checked 2 min ago · channel <b className="mono">stable</b></span></div>
            <div className="ab-btns">
              <button className="btn ghost"><Icon name="refresh" size={15} />Check for updates</button>
              <button className="btn ghost"><Icon name="upload" size={15} />Install from file</button>
            </div>
          </div>
        </Card>
      </div>
    </div>
  );
}

// ---- Analysis -------------------------------------------------------------
function Analysis({ d }) {
  const [bufH, setBufH] = useS2(() => Array.from({ length: 60 }, () => 60 + Math.random() * 20));
  useE2(() => {
    const id = setInterval(() => setBufH((p) => [...p.slice(1), Math.max(20, Math.min(98, p[p.length - 1] + (Math.random() - 0.5) * 14))]), 1000);
    return () => clearInterval(id);
  }, []);
  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Transport stream rate" sub="DVB output bitrate" className="span-6"
          right={<span className="big-readout mono">{d.tsBitrate.toFixed(2)}<i>Mb/s</i></span>}>
          <StreamChart maxY={3} unit="" fmt={(v) => v.toFixed(1)} series={[{ data: d.tsH, color: "var(--c-blue)" }]} />
        </Card>
        <Card title="Video buffer fill" sub="Encoder output buffer" className="span-6"
          right={<span className="big-readout mono">{bufH[bufH.length - 1].toFixed(0)}<i>%</i></span>}>
          <StreamChart maxY={100} unit="%" grid={5} fmt={(v) => v.toFixed(0)} series={[{ data: bufH, color: "var(--accent-2)" }]} />
        </Card>
        <Card title="PID table" sub="Active program identifiers" className="span-12" pad={false}>
          <table className="ver-table">
            <thead><tr><th>PID</th><th>Type</th><th>Codec</th><th>Bitrate</th><th>Continuity</th></tr></thead>
            <tbody>
              {[["0x0000", "PAT", "—", "—", "ok"], ["0x1000", "PMT", "—", "—", "ok"], ["0x0100", "Video", "H.265", "256 Kb/s", "ok"], ["0x0101", "Audio", "AAC", "32 Kb/s", "ok"], ["0x1FFF", "Null", "—", "43 Kb/s", "ok"]].map((r) => (
                <tr key={r[0]}><td className="mono vt-ver">{r[0]}</td><td className="vt-name">{r[1]}</td><td className="dim">{r[2]}</td><td className="mono">{r[3]}</td><td><Pill tone="ok" dot>no errors</Pill></td></tr>
              ))}
            </tbody>
          </table>
        </Card>
      </div>
    </div>
  );
}

// ---- Network --------------------------------------------------------------
function Network({ d }) {
  const [dhcp, setDhcp] = useS2(true);
  const [tab, setTab] = useS2("lan");
  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Network interface" className="span-7">
          <div className="tabs">
            {[["lan", "LAN"], ["wifi", "Wi-Fi"], ["usb", "USB"]].map(([k, l]) => (
              <button key={k} className={tab === k ? "tab-on" : ""} onClick={() => setTab(k)}>{l}</button>
            ))}
          </div>
          <div className="form-grid mt">
            <Field label="DHCP"><Toggle on={dhcp} onChange={setDhcp} /></Field>
            <div />
            <Field label="IP address"><TextInput value="10.0.0.70" onChange={() => {}} /></Field>
            <Field label="Subnet mask"><TextInput value="255.255.255.0" onChange={() => {}} /></Field>
            <Field label="Gateway"><TextInput value="10.0.0.1" onChange={() => {}} /></Field>
            <Field label="DNS"><TextInput value="8.8.8.8" onChange={() => {}} /></Field>
            <Field label="MAC"><TextInput value="88:33:0B:5A:98:5D" onChange={() => {}} /></Field>
            <Field label="Hostname"><TextInput value="tezuka" onChange={() => {}} /></Field>
          </div>
        </Card>

        <div className="span-5 tile-stack">
          <Card title="MQTT broker">
            <div className="form-grid">
              <Field label="Host"><TextInput value="10.0.0.70" onChange={() => {}} /></Field>
              <Field label="Port"><TextInput value="1883" onChange={() => {}} /></Field>
            </div>
            <Field label="Base topic"><TextInput value="tezuka/+/status" onChange={() => {}} /></Field>
            <div className="mqtt-status"><Pill tone="ok" dot>Connected</Pill><span className="dim mono">42 topics · 1.2 msg/s</span></div>
          </Card>
          <Card title="Service ports" pad={false}>
            <table className="ver-table compact">
              <tbody>
                {[["HTTP", "80"], ["RTSP", "554"], ["RTMP", "1935"], ["SSH", "22"], ["MQTT", "1883"]].map(([n, p]) => (
                  <tr key={n}><td className="vt-name">{n}</td><td className="mono" style={{ textAlign: "right" }}>{p}</td></tr>
                ))}
              </tbody>
            </table>
          </Card>
        </div>

        <Card title="System &amp; throughput" sub="Load &amp; temperature (left) · data rate (right)" className="span-12"
          right={<div className="legend"><span><i style={{ background: "var(--accent)" }} />CPU %</span><span><i style={{ background: "var(--c-blue)" }} />Mem %</span><span><i style={{ background: "var(--ok)" }} />SoC °C</span><span><i style={{ background: "var(--c-coral)" }} />FPGA °C</span><span><i style={{ background: "var(--c-purple)" }} />RX B/s</span><span><i style={{ background: "var(--c-pink)" }} />TX B/s</span></div>}>
          <StreamChart maxY={100} rightMax={5000000} unit="" grid={5} height={260} fmt={(v) => v.toFixed(0)}
            series={[
              { data: d.cpuH, color: "var(--accent)", label: "CPU", unit: "%" },
              { data: d.memH, color: "var(--c-blue)", label: "Memory", unit: "%" },
              { data: d.tempH, color: "var(--ok)", label: "SoC temp", unit: "°C" },
              { data: d.fpgaH, color: "var(--c-coral)", label: "FPGA temp", unit: "°C" },
              { data: d.rxH, color: "var(--c-purple)", label: "RX rate", unit: " B/s", axis: "right" },
              { data: d.txH, color: "var(--c-pink)", label: "TX rate", unit: " B/s", axis: "right" },
            ]} />
        </Card>
      </div>
      <div className="action-bar">
        <span>All changes applied</span>
        <div className="ab-btns"><button className="btn ghost">Reset</button><button className="btn primary"><Icon name="check" size={16} />Save</button></div>
      </div>
    </div>
  );
}

// ---- Transverter ----------------------------------------------------------
function Transverter({ d }) {
  const [rxFreq, setRxFreq] = useS2(null);
  const [rxBw, setRxBw] = useS2(null);
  const [txFreq, setTxFreq] = useS2(null);
  const [txBw, setTxBw] = useS2(null);
  const [enabled, setEnabled] = useS2(true);
  const [dirty, setDirty] = useS2(false);
  useE2(() => { if (d.rxFreq      != null) setRxFreq(d.rxFreq); },      [d.rxFreq]);
  useE2(() => { if (d.txFreq      != null) setTxFreq(d.txFreq); },      [d.txFreq]);
  useE2(() => { if (d.rxBandwidth != null) setRxBw(d.rxBandwidth); },   [d.rxBandwidth]);
  useE2(() => { if (d.txBandwidth != null) setTxBw(d.txBandwidth); },   [d.txBandwidth]);
  const mk = (fn) => (v) => { fn(v); setDirty(true); };
  const mhz = (v) => (v / 1e6).toFixed(3) + " MHz";

  return (
    <div className="page">
      <div className="grid-12">
        <Card className="span-12">
          <label className="chk transverter-enable">
            <input type="checkbox" checked={enabled} onChange={(e) => {setEnabled(e.target.checked);setDirty(true);}} />
            <span className="chk-box"><Icon name="check" size={12} /></span>
            <span className="chk-lbl">Transverter Enable</span>
          </label>
        </Card>

        <Card title="RX path" sub="Down-converter · receive" className={`span-6 ${enabled ? "" : "ftuner-disabled"}`}>
          <Field label="RX frequency" hint="47 MHz – 6 GHz · scroll or click a digit to tune">
            {rxFreq != null && <FreqTuner value={rxFreq} digits={12} min={47000000} max={6000000000} unit="Hz" sub={mhz} onChange={mk(setRxFreq)} />}
          </Field>
          <Field label="RX bandwidth" hint="200 kHz – 56 MHz">
            {rxBw != null && <FreqTuner value={rxBw} digits={8} min={200000} max={56000000} unit="Hz" sub={mhz} onChange={mk(setRxBw)} />}
          </Field>
        </Card>

        <Card title="TX path" sub="Up-converter · transmit" className={`span-6 ${enabled ? "" : "ftuner-disabled"}`}>
          <Field label="TX frequency" hint="47 MHz – 6 GHz · scroll or click a digit to tune">
            {txFreq != null && <FreqTuner value={txFreq} digits={12} min={47000000} max={6000000000} unit="Hz" sub={mhz} onChange={mk(setTxFreq)} />}
          </Field>
          <Field label="TX bandwidth" hint="200 kHz – 56 MHz">
            {txBw != null && <FreqTuner value={txBw} digits={8} min={200000} max={56000000} unit="Hz" sub={mhz} onChange={mk(setTxBw)} />}
          </Field>
        </Card>
      </div>

      <div className={`action-bar ${dirty ? "show" : ""}`}>
        <span>{dirty ? "Unsaved transverter changes" : "All changes applied"}</span>
        <div className="ab-btns">
          <button className="btn ghost" onClick={() => { setRxFreq(d.rxFreq); setTxFreq(d.txFreq); setRxBw(d.rxBandwidth); setTxBw(d.txBandwidth); setDirty(false); }}>Reset</button>
          <button className="btn primary" onClick={() => {
            if (rxFreq != null) d.publish('rx/frequency', rxFreq);
            if (txFreq != null) d.publish('tx/frequency', txFreq);
            if (rxBw != null)   d.publish('rx/bandwidth', rxBw);
            if (txBw != null)   d.publish('tx/bandwidth', txBw);
            setDirty(false);
          }}><Icon name="check" size={16} />Apply</button>
        </div>
      </div>
    </div>
  );
}

// ---- IQ Tape --------------------------------------------------------------
function IQTape({ d }) {
  const [sr, setSr] = useS2(null);
  const [rxFreq, setRxFreq] = useS2(null);
  const [txFreq, setTxFreq] = useS2(null);
  const [rec, setRec] = useS2(false);
  useE2(() => { if (d.rxSampling != null) setSr(d.rxSampling); },    [d.rxSampling]);
  useE2(() => { if (d.rxFreq    != null) setRxFreq(d.rxFreq); },    [d.rxFreq]);
  useE2(() => { if (d.txFreq    != null) setTxFreq(d.txFreq); },    [d.txFreq]);
  const [file, setFile] = useS2("capture_2026-06-05_143012.iq");
  const [playing, setPlaying] = useS2(false);
  const mhz = (v) => (v / 1e6).toFixed(3) + " MHz";
  const files = [
    "capture_2026-06-05_143012.iq",
    "qo100_beacon_10489.iq",
    "fm_bcast_98M3.iq",
    "adsb_1090_test.iq",
  ];
  const meta = { "capture_2026-06-05_143012.iq": "2.40 MS/s · CF32 · 184 MB · 19.2 s", "qo100_beacon_10489.iq": "1.00 MS/s · CF32 · 76 MB · 19.0 s", "fm_bcast_98M3.iq": "2.40 MS/s · CS16 · 92 MB · 9.6 s", "adsb_1090_test.iq": "2.00 MS/s · CS8 · 40 MB · 10.0 s" };

  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Capture" sub="Record baseband I/Q to file" className="span-6">
          <Field label="Sample rate">
            {sr != null && <FreqTuner value={sr} digits={9} min={520833} max={61440000} unit="S/s" sub={(v) => (v / 1e6).toFixed(3) + " MS/s"} onChange={(v) => { setSr(v); d.publish('rx/sampling', v); d.publish('tx/sampling', v); }} />}
          </Field>
          <Field label="RX frequency" hint="47 MHz – 6 GHz">
            {rxFreq != null && <FreqTuner value={rxFreq} digits={12} min={47e6} max={6e9} unit="Hz" sub={mhz} onChange={(v) => { setRxFreq(v); d.publish('rx/frequency', v); }} />}
          </Field>
          <Field label="TX frequency" hint="47 MHz – 6 GHz">
            {txFreq != null && <FreqTuner value={txFreq} digits={12} min={47e6} max={6e9} unit="Hz" sub={mhz} onChange={(v) => { setTxFreq(v); d.publish('tx/frequency', v); }} />}
          </Field>
          <button className={`btn block iq-rec ${rec ? "on" : ""}`} onClick={() => setRec((r) => !r)}>
            <span className="iq-dot" />{rec ? "Stop recording" : "Record"}
          </button>
        </Card>

        <Card title="Playback" sub="Replay a stored I/Q capture" className="span-6">
          <Field label="File selection">
            <Select value={file} onChange={setFile} options={files} />
          </Field>
          <div className="iq-meta mono">{meta[file]}</div>
          <div className="btn-col">
            <button className="btn primary block" onClick={() => setPlaying((p) => !p)}>
              <Icon name={playing ? "check" : "play"} size={15} />{playing ? "Stop playback" : "Play file"}
            </button>
            <button className="btn ghost block"><Icon name="upload" size={15} />Load capture…</button>
          </div>
        </Card>
      </div>
    </div>
  );
}

// ---- Signal generator -----------------------------------------------------
function SigGen({ d }) {
  const [txFreq, setTxFreq] = useS2(null);
  const [txGain, setTxGain] = useS2(null);
  const [type, setType] = useS2("CW");
  const [on, setOn] = useS2(false);
  useE2(() => { if (d.txFreq != null) setTxFreq(d.txFreq); }, [d.txFreq]);
  useE2(() => { if (d.txGain != null) setTxGain(d.txGain); }, [d.txGain]);
  const mhz = (v) => (v / 1e6).toFixed(3) + " MHz";
  const types = ["CW", "DC", "FM-W", "FM-N", "AM", "EXT", "ASK", "FSK", "NPR"];

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>Signal generator</h1>
          <span className="datv-sub mono">Continuous-wave &amp; modulated test source · {type}</span>
        </div>
      </div>

      <div className="grid-12">
        <Card title="Output" sub="Carrier frequency &amp; level" className="span-12">
          <Field label="TX frequency" hint="47 MHz – 6 GHz · scroll or click a digit to tune">
            {txFreq != null && <FreqTuner value={txFreq} digits={12} min={47e6} max={6e9} unit="Hz" sub={mhz} onChange={(v) => { setTxFreq(v); d.publish('tx/frequency', v); }} />}
          </Field>
          <Field label="TX gain" hint="0 to −89.75 dB">
            {txGain != null && <Slider value={txGain} min={-89} max={0} step={0.25} onChange={(v) => { setTxGain(v); d.publish('tx/gain', v); }} unit=" dB" fmt={(v) => v.toFixed(2)} />}
          </Field>
          <Field label="Signal type" hint="Waveform / modulation">
            <Select value={type} onChange={setType} options={types} />
          </Field>
          <div className={`onair-box ${on ? "live" : ""}`}>
            <div className="onair-lamp"><span /></div>
            <div className="onair-text"><span>{on ? "TX ON" : "TX OFF"}</span><small className="mono">OUTPUT</small></div>
            <Toggle on={on} onChange={setOn} labels={["OFF", "ON"]} />
          </div>
        </Card>
      </div>
    </div>
  );
}

// ---- Calibration ----------------------------------------------------------
const GAIN_CURVE = (() => {
  const pts = [];
  const f0 = 47, f1 = 6000, N = 56;
  for (let i = 0; i <= N; i++) {
    const f = f0 + ((f1 - f0) / N) * i;
    const norm = (f - f0) / (f1 - f0);
    const g = 13.5 - 4.2 * norm - 3.6 * norm * norm + 1.3 * Math.sin(norm * 8.5) + 0.7 * Math.sin(norm * 21) + (Math.random() - 0.5) * 0.35;
    pts.push({ x: f, y: g });
  }
  return pts;
})();

const DAC_CURVE = (() => {
  const pts = [];
  const N = 40;
  for (let i = 0; i <= N; i++) {
    const a = (100 / N) * i;          // amplitude, % of full scale
    const norm = a / 100;
    // near-linear gain that compresses as amplitude approaches full scale
    const g = 9.6 - 3.4 * Math.pow(norm, 3.2) - 0.6 * norm + 0.25 * Math.sin(norm * 16) + (Math.random() - 0.5) * 0.18;
    pts.push({ x: a, y: g });
  }
  return pts;
})();

const TCXO_HZ = 40000000;
const ppmToFreqCorr = (ppm) => Math.round(TCXO_HZ + ppm * TCXO_HZ / 1e6);
const freqCorrToPpm = (hz) => (hz - TCXO_HZ) / (TCXO_HZ / 1e6);

function Calibrate({ d }) {
  const [freqCalOn, setFreqCalOn] = useS2(true);
  const [ppm, setPpm] = useS2(0);
  const [curve, setCurve] = useS2(GAIN_CURVE);
  const [dac, setDac] = useS2(DAC_CURVE);
  const [calRun, setCalRun] = useS2(false);
  useE2(() => {
    if (d.freqCorrection != null)
      setPpm(parseFloat(freqCorrToPpm(d.freqCorrection).toFixed(2)));
  }, [d.freqCorrection]);
  const fmtMHz = (v) => (v >= 1000 ? (v / 1000).toFixed(2) + "G" : Math.round(v) + "M");
  const setPoint = (i, y) => setCurve((c) => c.map((p, j) => (j === i ? { ...p, y } : p)));
  const setDacPoint = (i, y) => setDac((c) => c.map((p, j) => (j === i ? { ...p, y } : p)));
  const launchCal = () => {
    if (calRun) return;
    setCalRun(true);
    setFreqCalOn(true);
    setTimeout(() => {
      setPpm(Math.round((Math.random() * 1.2 - 0.6) * 100) / 100);
      setCalRun(false);
    }, 2600);
  };

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>Calibrate</h1>
          <span className="datv-sub mono">Frequency &amp; gain calibration · TCXO {ppm >= 0 ? "+" : ""}{ppm.toFixed(2)} ppm</span>
        </div>
        <button className="btn primary" onClick={launchCal} disabled={calRun}>
          <span className={calRun ? "spin" : ""} style={{ display: "inline-flex" }}><Icon name={calRun ? "refresh" : "target"} size={16} /></span>
          {calRun ? "Calibrating…" : "Launch frequency calibration"}
        </button>
      </div>

      <div className="grid-12">
        <Card title="Frequency calibration" sub="Reference oscillator trim" className="span-12"
          right={<Toggle on={freqCalOn} onChange={setFreqCalOn} labels={["OFF", "ON"]} />}>
          <Field label="Oscillator PPM" hint="−20 to +20 ppm · TCXO offset against reference">
            <Slider value={ppm} min={-20} max={20} step={0.05}
              onChange={(v) => { setPpm(v); d.publish('main/freq_correction', ppmToFreqCorr(v)); }}
              unit=" ppm" fmt={(v) => (v >= 0 ? "+" : "") + v.toFixed(2)} />
          </Field>
          <div className={`cal-status ${freqCalOn ? "on" : ""}`}>
            <span className="cal-dot" />
            <span className="mono">{freqCalOn ? "Auto-discipline active · locked to 10 MHz ref" : "Manual trim · calibration paused"}</span>
          </div>
        </Card>

        <Card title="Gain vs frequency" sub="Drag any point up or down to set its gain" className="span-12">
          <XYChart points={curve} height={300} xUnit="MHz" editable onPointChange={setPoint}
            yMin={0} yMax={20} fmtX={fmtMHz} fmtY={(v) => v.toFixed(0)} yUnit=" dB" />
        </Card>

        <Card title="DAC gain vs amplitude" sub="Drag any point up or down to set its gain" className="span-12">
          <XYChart points={dac} height={300} xUnit="FS" editable onPointChange={setDacPoint}
            yMin={0} yMax={14} fmtX={(v) => Math.round(v) + "%"} fmtY={(v) => v.toFixed(0)} yUnit=" dB" />
        </Card>
      </div>
    </div>
  );
}

// ---- Diagnostic -----------------------------------------------------------
const clockStr = (off = 0) => new Date(Date.now() + off * 1000).toTimeString().slice(0, 8);
const SEED_LOGS = [
  ["INFO", "System boot complete · Tezuka v2.4.1-g9a3f"],
  ["INFO", "AD9363 transceiver initialized @ 2.400 MS/s"],
  ["OK", "PLL locked · reference 10 MHz external"],
  ["INFO", "MQTT broker connected · 192.168.1.10:1883"],
  ["WARN", "FPGA core temperature 58.2 °C approaching soft limit"],
  ["INFO", "DATV encoder idle · codec H.265"],
  ["OK", "IIO context healthy · libiio 0.25 (git tag v0.25)"],
];

function Diagnostic() {
  const [logs, setLogs] = useS2(() => SEED_LOGS.map((l, i) => ({ t: clockStr(-(SEED_LOGS.length - i) * 7), lvl: l[0], msg: l[1] })));
  const [running, setRunning] = useS2(false);
  const winRef = React.useRef(null);
  useE2(() => { const el = winRef.current; if (el) el.scrollTop = el.scrollHeight; }, [logs]);

  const checkPerf = () => {
    if (running) return;
    setRunning(true);
    const steps = [
      ["INFO", "Performance check started"],
      ["INFO", "CPU load test · 4 threads · 5 s"],
      ["OK", `CPU score ${8900 + Math.floor(Math.random() * 600)} · peak load ${(18 + Math.random() * 22).toFixed(0)}%`],
      ["INFO", "Memory bandwidth test"],
      ["OK", `Memory ${(38 + Math.random() * 8).toFixed(0)}% used · ${(2.6 + Math.random() * 0.6).toFixed(1)} GB/s throughput`],
      ["INFO", "RF datapath loopback · RX→TX"],
      ["OK", `Latency ${(0.8 + Math.random()).toFixed(2)} ms · 0 dropped samples`],
      ["WARN", `FPGA temperature ${(54 + Math.random() * 6).toFixed(1)} °C under load`],
      ["OK", "Performance check complete · all subsystems nominal"],
    ];
    steps.forEach((s, i) => setTimeout(() => {
      setLogs((l) => [...l, { t: clockStr(0), lvl: s[0], msg: s[1] }]);
      if (i === steps.length - 1) setRunning(false);
    }, (i + 1) * 480));
  };

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>Diagnostic</h1>
          <span className="datv-sub mono">System logs &amp; performance checks</span>
        </div>
        <button className="btn primary" onClick={checkPerf} disabled={running}>
          <span className={running ? "spin" : ""} style={{ display: "inline-flex" }}><Icon name={running ? "refresh" : "pulse"} size={16} /></span>
          {running ? "Checking…" : "Check performance"}
        </button>
      </div>

      <div className="grid-12">
        <Card title="System log" sub="Live diagnostic output" className="span-12" pad={false}
          right={<button className="btn ghost btn-sm" onClick={() => setLogs([])}>Clear</button>}>
          <div className="logwin" ref={winRef}>
            {logs.length === 0 && <div className="logline log-empty mono">— log cleared —</div>}
            {logs.map((l, i) => (
              <div key={i} className="logline">
                <span className="log-time mono">{l.t}</span>
                <span className={`log-lvl lvl-${l.lvl.toLowerCase()}`}>{l.lvl}</span>
                <span className="log-msg mono">{l.msg}</span>
              </div>
            ))}
          </div>
        </Card>
      </div>
    </div>
  );
}

// ---- Reboot ---------------------------------------------------------------
function Reboot({ d, ver }) {
  const [mode, setMode] = useS2("Normal");
  const [pending, setPending] = useS2(null);   // null | "reboot" | "shutdown"
  const [phase, setPhase] = useS2("idle");      // idle | busy | done
  const [action, setAction] = useS2("reboot");
  const [secs, setSecs] = useS2(0);
  useE2(() => {
    if (phase !== "busy") return;
    if (secs <= 0) { setPhase("done"); return; }
    const id = setTimeout(() => setSecs((s) => s - 1), 1000);
    return () => clearTimeout(id);
  }, [phase, secs]);
  const start = (kind) => { setPending(null); setAction(kind); setPhase("busy"); setSecs(kind === "shutdown" ? 6 : 18); };

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>Reboot</h1>
          <span className="datv-sub mono">Uptime {fmtUptime(d.uptime)} · {mode} mode</span>
        </div>
      </div>

      <div className="grid-12">
        {phase === "idle" ? (
          <Card title="Restart device" sub="Active sessions will be disconnected" className="span-7">
            <Field label="Reboot mode" hint="Normal performs a clean restart of all services">
              <Select value={mode} onChange={setMode} options={["Normal", "Safe mode", "Bootloader"]} />
            </Field>
            <div className="warn-note">
              <Icon name="bell" size={16} />
              <p>Rebooting interrupts any active TX, recording, or stream. The unit is unreachable for roughly 20 seconds.</p>
            </div>
            {pending ? (
              <div className="confirm-bar">
                <span className="mono">{pending === "reboot" ? `Reboot now in ${mode} mode?` : "Power off the device?"}</span>
                <div className="ab-btns">
                  <button className="btn ghost" onClick={() => setPending(null)}>Cancel</button>
                  <button className="btn primary" onClick={() => start(pending)}>Confirm</button>
                </div>
              </div>
            ) : (
              <div className="ab-btns">
                <button className="btn primary" onClick={() => setPending("reboot")}><Icon name="refresh" size={16} />Reboot now</button>
                <button className="btn ghost iq-rec" onClick={() => setPending("shutdown")}><Icon name="power" size={16} />Power off</button>
              </div>
            )}
          </Card>
        ) : (
          <Card className="span-7">
            <div className="reboot-status">
              {phase === "busy" ? (
                <>
                  <span className="spin reboot-spin"><Icon name="refresh" size={30} /></span>
                  <h2>{action === "shutdown" ? "Shutting down…" : "Rebooting…"}</h2>
                  <p className="mono">{action === "shutdown" ? "Powering off subsystems" : `Reconnecting in ${secs}s`}</p>
                </>
              ) : (
                <>
                  <span className="reboot-check"><Icon name="check" size={28} /></span>
                  <h2>{action === "shutdown" ? "Device powered off" : "Back online"}</h2>
                  <p className="mono">{action === "shutdown" ? "All subsystems halted" : "All services restarted · MQTT reconnected"}</p>
                  <button className="btn primary" onClick={() => setPhase("idle")}>{action === "shutdown" ? "Power on" : "Done"}</button>
                </>
              )}
            </div>
          </Card>
        )}

        <Card title="System" sub="Current image" className="span-5" pad={false}>
          <table className="ver-table compact">
            <tbody>
              <tr><td className="dim">Model</td><td className="vt-name">{ver.model}</td></tr>
              <tr><td className="dim">Firmware</td><td className="vt-ver">{ver.tezuka}</td></tr>
              <tr><td className="dim">Linux</td><td className="mono">{ver.linux}</td></tr>
              <tr><td className="dim">Uptime</td><td className="mono">{fmtUptime(d.uptime)}</td></tr>
              <tr><td className="dim">Serial</td><td className="mono">{ver.serial}</td></tr>
            </tbody>
          </table>
        </Card>
      </div>
    </div>
  );
}

// ---- Operator -------------------------------------------------------------
function Operator({ operator, onSave }) {
  const [name, setName] = useS2(operator.name);
  const [callsign, setCallsign] = useS2(operator.callsign);
  const [locator, setLocator] = useS2(operator.locator);
  const [saved, setSaved] = useS2(false);
  const dirty = name !== operator.name || callsign !== operator.callsign || locator !== operator.locator;
  const save = () => { onSave({ name, callsign: callsign.toUpperCase(), locator }); setSaved(true); setTimeout(() => setSaved(false), 1800); };
  const reset = () => { setName(operator.name); setCallsign(operator.callsign); setLocator(operator.locator); };

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>Operator</h1>
          <span className="datv-sub mono">Station identity · used for logging &amp; beacon ID</span>
        </div>
      </div>

      <div className="grid-12">
        <Card title="Operator profile" sub="Edit your station details" className="span-7">
          <div className="form-grid">
            <Field label="Operator name"><TextInput value={name} onChange={setName} mono={false} /></Field>
            <Field label="Callsign"><TextInput value={callsign} onChange={setCallsign} /></Field>
            <div style={{ gridColumn: "1 / -1" }}>
              <Field label="Grid locator" hint="Maidenhead locator · e.g. JN18cv">
                <TextInput value={locator} onChange={setLocator} />
              </Field>
            </div>
          </div>
          <div className="ab-btns" style={{ marginTop: 20 }}>
            <button className="btn primary" disabled={!dirty} onClick={save}>{saved ? "Saved" : "Save changes"}</button>
            <button className="btn ghost" disabled={!dirty} onClick={reset}>Reset</button>
          </div>
        </Card>

        <Card title="Identity" sub="As broadcast" className="span-5">
          <div className="op-card">
            <div className="op-avatar"><Icon name="user" size={28} /></div>
            <div className="op-id">
              <b>{operator.name}</b>
              <span className="mono">{operator.callsign}</span>
              <span className="mono dim">Locator {operator.locator}</span>
            </div>
          </div>
        </Card>
      </div>
    </div>
  );
}

Object.assign(window, { DATV, Versions, Analysis, Network, Transverter, IQTape, SigGen, Calibrate, Diagnostic, Reboot, Operator });

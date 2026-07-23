// data.jsx — MQTT live data + shared UI primitives
const { useState: useStateD, useEffect: useEffectD, useRef: useRefD, useCallback: useCBD } = React;

// Set to the device IP for local development; leave null to use window.location.hostname (on-device)
const MQTT_DEV_HOST = '10.0.0.56';
// Exposed for use in other pages (e.g. spectrum WebSocket)
window._tezukaDevHost = MQTT_DEV_HOST || window.location.hostname;

const HIST = 60;

// ---- MQTT state/# → d fields ---------------------------------------------
function applyMqtt(prev, path, raw) {
  if (path.startsWith('caps/')) {
    return { ...prev, caps: { ...prev.caps, [path.slice(5)]: raw } };
  }
  if (path.startsWith('net/')) {
    return { ...prev, net: { ...prev.net, [path.slice(4)]: raw } };
  }
  if (path.startsWith('gpio/')) {
    const pin = path.slice(5);
    return { ...prev, gpio: { ...prev.gpio, [pin]: raw === '1' } };
  }
  if (path === 'system/env_count') {
    return { ...prev, envCount: parseInt(raw) };
  }
  if (path.startsWith('system/env/')) {
    const key = path.slice('system/env/'.length);
    if (key) return { ...prev, envVars: { ...prev.envVars, [key]: raw } };
    return prev;
  }
  if (path.startsWith('system/debugiio/')) {
    const key = path.slice('system/debugiio/'.length);
    if (key) return { ...prev, debugIio: { ...prev.debugIio, [key]: raw } };
    return prev;
  }
  switch (path) {
    case 'rx/frequency':        return { ...prev, rxFreq: parseFloat(raw) };
    case 'tx/frequency':        return { ...prev, txFreq: parseFloat(raw) };
    case 'rx/sampling':         return { ...prev, rxSampling: parseFloat(raw) };
    case 'tx/sampling':         return { ...prev, txSampling: parseFloat(raw) };
    case 'rx/bandwidth':        return { ...prev, rxBandwidth: parseFloat(raw) };
    case 'tx/bandwidth':        return { ...prev, txBandwidth: parseFloat(raw) };
    case 'rx/gain':             return { ...prev, rxGain: parseFloat(raw), agc: parseFloat(raw) };
    case 'tx/gain':             return { ...prev, txGain: parseFloat(raw), txPower: parseFloat(raw) };
    case 'rx/gain_mode':        return { ...prev, rxGainMode: raw };
    case 'rx/ant':              return { ...prev, rxAnt: raw };
    case 'tx/ant':              return { ...prev, txAnt: raw };
    case 'rx/active':           return { ...prev, rxActive: raw === '0' };
    case 'tx/active':           return { ...prev, txActive: raw === '0' };
    case 'rx/rfinput':          return { ...prev, rxRfinput: parseInt(raw) };
    case 'tx/rfoutput':         return { ...prev, txRfoutput: parseInt(raw) };
    case 'rx/loopback':         return { ...prev, loopback: parseInt(raw) };
    case 'rx/fir_enable':       return { ...prev, rxFirEnable: raw === '1' };
    case 'rx/overload':         return { ...prev, rxOverload: raw === '1' };
    case 'tx/overload':         return { ...prev, txOverload: raw === '1' };
    case 'rx/underflow':        return { ...prev, rxUnderflow: raw === '1' };
    case 'tx/underflow':        return { ...prev, txUnderflow: raw === '1' };
    case 'rx/buffer_size':      return { ...prev, rxBufferSize: parseInt(raw) };
    case 'tx/buffer_size':      return { ...prev, txBufferSize: parseInt(raw) };
    case 'rx/sweep/activate':   return { ...prev, sweepActive: raw === '1' };
    case 'rx/sweep/frequency':  return { ...prev, sweepFreq: parseFloat(raw) };
    case 'rx/span':             return { ...prev, span: parseFloat(raw) };
    case 'main/serial':         return { ...prev, serial: raw };
    case 'main/hw_model':       return { ...prev, hwModel: raw };
    case 'main/fw_version':     return { ...prev, fwVersion: raw };
    case 'main/freq_correction':    { const v = parseFloat(raw); return { ...prev, freqCorrection: isNaN(v) ? null : v }; }
    case 'system/xo_correction':   { const v = parseFloat(raw); return { ...prev, systemXoCorrection: isNaN(v) ? null : v }; }
    case 'system/ppb_correction':  { const v = parseFloat(raw); return { ...prev, ppbCorrection: isNaN(v) ? null : v }; }
    case 'main/ensm_mode':         return { ...prev, ensmMode: raw };
    case 'main/rx_path_rates':     return { ...prev, rxPathRates: raw };
    case 'main/tx_path_rates':     return { ...prev, txPathRates: raw };
    case 'main/firmware_version':  return { ...prev, firmwareVersion: raw };
    case 'main/fir_config': {
      const rx = raw.match(/Rx:\s*\d+,(\d+)/);
      const tx = raw.match(/Tx:\s*\d+,(\d+)/);
      return { ...prev, firConfig: raw,
        rxFirDecim:  rx ? parseInt(rx[1]) : 0,
        txFirInterp: tx ? parseInt(tx[1]) : 0 };
    }
    case 'main/linux':             return { ...prev, linuxVersion: raw };
    case 'main/uboot':             return { ...prev, ubootVersion: raw };
    case 'main/buildroot':         return { ...prev, buildrootVersion: raw };
    case 'main/iio':               return { ...prev, iioVersion: raw };
    case 'main/fpga':              return { ...prev, fpgaVersion: raw };
    case 'rx/rssi': { const v = parseFloat(raw); return { ...prev, rfLevel: v }; }
    case 'rx/bb_dc_tracking':      return { ...prev, rxBbDcTracking: raw === '1' };
    case 'rx/quad_tracking':       return { ...prev, rxQuadTracking: raw === '1' };
    case 'rx/rf_dc_tracking':      return { ...prev, rxRfDcTracking: raw === '1' };
    case 'main/cpu':  { const v = parseFloat(raw); return { ...prev, cpu: v,       cpuH:  [...prev.cpuH.slice(1),  v] }; }
    case 'main/mem':  { const v = parseFloat(raw); return { ...prev, mem: v,       memH:  [...prev.memH.slice(1),  v] }; }
    case 'main/temp': { const v = parseFloat(raw); return { ...prev, temp: v,      tempH: [...prev.tempH.slice(1), v] }; }
    case 'main/fpga_temp': { const v = parseFloat(raw); return { ...prev, fpgaTemp: v, fpgaH: [...prev.fpgaH.slice(1), v] }; }
    case 'main/uptime':  return { ...prev, uptime: parseInt(raw) };
    case 'rx/rate': { const v = parseFloat(raw); return { ...prev, rxRate: v, rxH: [...prev.rxH.slice(1), v] }; }
    case 'tx/rate':         { const v = parseFloat(raw); return { ...prev, txRate: v, txH: [...prev.txH.slice(1), v] }; }
    case 'tx/dma_transfer':  return { ...prev, txDmaTransfer: parseInt(raw) };
    case 'rx/dma_transfer':  return { ...prev, rxDmaTransfer: parseInt(raw) };
    case 'usb/rx_rate':      return { ...prev, usbRxRate: parseInt(raw) };
    case 'usb/tx_rate':      return { ...prev, usbTxRate: parseInt(raw) };
    case 'system/2r2t':          return { ...prev, sys2r2t: raw === '1' };
    case 'system/iqtape':        return { ...prev, iqtape: raw };
    case 'system/siggen':        return { ...prev, siggen: raw };
    case 'system/overclock':     return { ...prev, overclock: raw };
    case 'system/overclock_cap': try { return { ...prev, overclockCap: JSON.parse(raw) }; } catch { return prev; }
    case 'system/log':                  return { ...prev, systemLog: [...prev.systemLog, raw] };
    case 'system/kalibrate/status':     return { ...prev, kalibrateStatus: raw };
    case 'main/gain_table_config':      try { return { ...prev, gainTableConfig: JSON.parse(raw) }; } catch { return prev; }
    case 'system/kalibrate/channels':   try { return { ...prev, kalibrateChannels: JSON.parse(raw) }; } catch { return prev; }
    case 'system/kalibrate/result_ppm': return { ...prev, kalibrateResultPpm: parseFloat(raw) };
    case 'system/kalibrate/result_ppb': return { ...prev, kalibrateResultPpb: parseFloat(raw) };
    case 'system/kalibrate/log':        return { ...prev, kalibrateLog: [...prev.kalibrateLog, raw] };
    case 'system/gps/fix':             return { ...prev, gpsfix: raw };
    case 'system/gps/locator':         return { ...prev, gpsLocator: raw };
    default:                    return prev;
  }
}

// ---- Live data hook -------------------------------------------------------
function useLiveData(running = true) {
  const mqttRef = useRefD(null);

  const zeros = Array(HIST).fill(0);
  const [d, setD] = useStateD(() => ({
    cpu: 0, mem: 0, temp: 0, fpgaTemp: 0,
    rfLevel: 0, lock: false, ber: 0,
    tsBitrate: 0, rxRate: 0, txRate: 0,
    cpuH: zeros, memH: zeros, tempH: zeros, fpgaH: zeros,
    tsH: zeros, rxH: zeros, txH: zeros, rfH: zeros,
    uptime: 0, mqtt: false, datv: {},
    rxFreq: null, txFreq: null,
    rxSampling: null, txSampling: null,
    rxBandwidth: null, txBandwidth: null,
    rxGain: null, txGain: null,
    agc: null, txPower: null,
    rxGainMode: null,
    rxAnt: null, txAnt: null,
    rxActive: null, txActive: null,
    rxRfinput: null, rxFirEnable: null, loopback: null, rxOverload: false, txOverload: false, rxUnderflow: false, txUnderflow: false, rxBufferSize: null, txBufferSize: null,
    sweepActive: false, sweepFreq: null, span: null,
    serial: null, hwModel: null, fwVersion: null, freqCorrection: null, ppbCorrection: null,
    caps: {}, net: {}, gpio: {}, envVars: {}, envCount: null, systemLog: [], debugIio: {},
    overclock: null, overclockCap: [],
    gainTableConfig: null,
    kalibrateStatus: '', kalibrateChannels: [], kalibrateResultPpm: null, kalibrateResultPpb: null, kalibrateLog: [],
    gpsfix: null, gpsLocator: null,
  }));

  // MQTT connection — ws://[hostname]:9001/mqtt  or  wss://[hostname]:9002/mqtt over HTTPS
  useEffectD(() => {
    if (!running || typeof Paho === 'undefined') return;
    const host = MQTT_DEV_HOST || window.location.hostname;
    const useSSL = window.location.protocol === 'https:';
    const clientId = 'tezuka_dash_' + Math.random().toString(16).slice(2, 8);
    const client = new Paho.MQTT.Client(host, useSSL ? 9002 : 9001, '/mqtt', clientId);
    mqttRef.current = client;
    let offlineTimer = null;

    client.onMessageArrived = (msg) => {
      let payload;
      try { payload = msg.payloadString; } catch (_) { return; }
      const dest = msg.destinationName;
      if (dest.startsWith('dt/pluto/')) {
        const subPath = dest.split('/').slice(3).join('/');
        setD((p) => ({ ...p, datv: { ...p.datv, [subPath]: payload } }));
      } else {
        const path = dest.replace(/^state\//, '');
        setD((p) => applyMqtt(p, path, payload));
      }
    };

    client.onConnectionLost = () => {
      clearTimeout(offlineTimer);
      offlineTimer = setTimeout(() => setD((p) => ({ ...p, mqtt: false })), 4000);
    };

    client.connect({
      onSuccess: () => {
        clearTimeout(offlineTimer);
        client.subscribe('state/#');
        client.subscribe('dt/pluto/#');
        setD((p) => ({ ...p, mqtt: true, mqttHost: `${host}:9001` }));
      },
      onFailure: () => {
        clearTimeout(offlineTimer);
        setD((p) => ({ ...p, mqtt: false }));
      },
      reconnect: true,
      keepAliveInterval: 30,
      useSSL,
    });

    return () => { clearTimeout(offlineTimer); try { client.disconnect(); } catch (_) {} mqttRef.current = null; };
  }, []);

  const publish = useCBD((path, value) => {
    const client = mqttRef.current;
    if (client && client.isConnected()) {
      const msg = new Paho.MQTT.Message(String(value));
      msg.destinationName = 'cmd/' + path;
      client.send(msg);
    }
  }, []);

  const clearEnvVars = useCBD(() => {
    setD(p => ({ ...p, envVars: {}, envCount: null }));
  }, []);

  const clearDebugIio = useCBD(() => {
    setD(p => ({ ...p, debugIio: {} }));
  }, []);

  return { ...d, publish, clearEnvVars, clearDebugIio };
}

function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return `${d}d ${String(h).padStart(2, "0")}h ${String(m).padStart(2, "0")}m`;
}

// ---- Shared UI primitives -------------------------------------------------
function Card({ title, sub, right, children, className = "", pad = true }) {
  return (
    <section className={`card ${className}`}>
      {(title || right) && (
        <header className="card-head">
          <div className="card-titles">
            {title && <h2>{title}</h2>}
            {sub && <span className="card-sub">{sub}</span>}
          </div>
          {right && <div className="card-right">{right}</div>}
        </header>
      )}
      <div className={pad ? "card-body" : ""}>{children}</div>
    </section>
  );
}

function Pill({ tone = "neutral", children, dot }) {
  return <span className={`pill pill-${tone}`}>{dot && <i className="pill-dot" />}{children}</span>;
}

function Toggle({ on, onChange, labels = ["OFF", "ON"] }) {
  return (
    <button className={`toggle ${on ? "on" : ""}`} onClick={() => onChange(!on)} type="button">
      <span className="toggle-track"><span className="toggle-knob" /></span>
      <span className="toggle-text">{on ? labels[1] : labels[0]}</span>
    </button>
  );
}

function Slider({ value, min, max, step = 1, onChange, unit = "", fmt }) {
  const pct = ((value - min) / (max - min)) * 100;
  return (
    <div className="slider-row">
      <input type="range" min={min} max={max} step={step} value={value}
        onChange={(e) => onChange(parseFloat(e.target.value))}
        style={{ "--fill": `${pct}%` }} className="slider" />
      <span className="slider-val">{fmt ? fmt(value) : value}{unit}</span>
    </div>
  );
}

function Field({ label, children, hint }) {
  return (
    <div className="field">
      <label className="field-label">{label}</label>
      <div className="field-control">{children}</div>
      {hint && <span className="field-hint">{hint}</span>}
    </div>
  );
}

function Select({ value, onChange, options }) {
  return (
    <div className="select-wrap">
      <select value={value} onChange={(e) => onChange(e.target.value)} className="select">
        {options.map((o) => (typeof o === "string" ? <option key={o} value={o}>{o}</option> : <option key={o.v} value={o.v}>{o.l}</option>))}
      </select>
      <svg className="select-chev" width="12" height="12" viewBox="0 0 12 12"><path d="M2 4l4 4 4-4" stroke="currentColor" strokeWidth="1.5" fill="none" strokeLinecap="round" strokeLinejoin="round" /></svg>
    </div>
  );
}

function TextInput({ value, onChange, suffix, mono = true, invalid = false, ...rest }) {
  return (
    <div className={"text-input" + (invalid ? " invalid" : "")}>
      <input value={value} onChange={(e) => onChange(e.target.value)} className={mono ? "mono" : ""} {...rest} />
      {suffix && <span className="input-suffix">{suffix}</span>}
    </div>
  );
}

function Checkbox({ checked, onChange, label }) {
  return (
    <label className="chk">
      <input type="checkbox" checked={checked} onChange={(e) => onChange(e.target.checked)} />
      <span className="chk-box"><svg viewBox="0 0 12 12" width="11" height="11"><path d="M2.5 6.5l2.5 2.5 4.5-5" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" /></svg></span>
      {label && <span className="chk-lbl">{label}</span>}
    </label>
  );
}

Object.assign(window, { useLiveData, fmtUptime, Card, Pill, Toggle, Slider, Field, Select, TextInput, Checkbox });

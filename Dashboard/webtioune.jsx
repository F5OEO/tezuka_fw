// webtioune.jsx — DATV Controller sub-page: live transport-stream preview
// via mpegts.js (https://github.com/xqq/mpegts.js, Apache-2.0).
//
// Source: the page's own board, which serves the decoded transport stream
// over WebSocket (ws://<board>:8080/stream.ts) — this page is purely a player.
//
// mpegts.js remuxes the TS to fragmented MP4 and plays it through Media Source
// Extensions, so the *browser* decodes the video. The stream is HEVC, which
// only plays where the browser/OS can decode it (Safari, Edge on Windows with
// the HEVC extension, Chrome with hardware decode — on Linux that needs VA-API
// and Chrome started with --enable-features=VaapiVideoDecoder,
// PlatformHEVCDecoderSupport). The page checks this up front and says so.
//
// The runtime is lazy-loaded on first visit rather than included in every page
// load.
//
// The page also controls the receiver itself: frequency / symbol rate and
// start / stop go to zynqrx over MQTT in longmynd's scheme (cmd/zynqrx/
// frequency [kHz], sr [kS], process start|stop), and its status comes back on
// dt/zynqrx/... (d.lm, filled by useLiveData in data.jsx).
const { useState: useWtS, useEffect: useWtE, useRef: useWtR } = React;

// Relative to wherever Tezuka Dashboard.html is served from — mirrors the
// Dashboard/vendor/ convention used for react.js/babel.js. On-device the files
// come from overlay_datv (root/vendor/mpegts/).
const MPEGTS_SCRIPT = './vendor/mpegts/mpegts.js';

let mpegtsLoadPromise = null;
function loadMpegts() {
  if (window.mpegts) return Promise.resolve();
  if (mpegtsLoadPromise) return mpegtsLoadPromise;
  mpegtsLoadPromise = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = MPEGTS_SCRIPT;
    s.onload = () => resolve();
    s.onerror = () => { mpegtsLoadPromise = null; reject(new Error(`Failed to load ${MPEGTS_SCRIPT} — is the mpegts.js runtime installed there?`)); };
    document.head.appendChild(s);
  });
  return mpegtsLoadPromise;
}

// The board's zynqrx serves the live TS over WebSocket on :8080.
// _tezukaDevHost honours the MQTT_DEV_HOST override in data.jsx, else the
// page's own hostname.
const WS_TS_PORT = 8080;
const wsTsUrl = () => `ws://${window._tezukaDevHost || window.location.hostname}:${WS_TS_PORT}/stream.ts`;

// zynqrx's limits (cmd/zynqrx/frequency in kHz, sr in kS)
const RX_FREQ_MIN_HZ = 70e6, RX_FREQ_MAX_HZ = 6e9;
const RX_SR_MIN_KS = 33, RX_SR_MAX_KS = 7680;
const RX_SR_PRESETS = [125, 250, 333, 500, 1000];

// The QO-100 wideband spectrum from the BATC feed (frame: 16-bit power per bin over 10490.5 - 10499.5 MHz),
// drawn on a canvas at 10 Hz from the latest frame. Click: on a detected signal it tunes to its centre and
// symbol rate, elsewhere to the clicked frequency at the current symbol rate. The receiver's own tuning is
// drawn as a band.
const SPEC_F0 = 10490.5, SPEC_SPAN = 9.0, SPEC_NOISE = 11000;
function Qo100Spectrum({ frameRef, signals, rxDlMHz, rxSrKs, namesRef, onPick }) {
  const cvRef = useWtR(null);
  const live = useWtR({});
  live.current = { signals, rxDlMHz, rxSrKs };
  const yMax = useWtR(30000);
  const H = 150;
  useWtE(() => {
    const draw = () => {
      const cv = cvRef.current; if (!cv) return;
      const w = cv.clientWidth, dpr = window.devicePixelRatio || 1;
      if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(H * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(H * dpr); }
      const g = cv.getContext('2d'); g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, w, H);
      const css = getComputedStyle(cv);
      const accent = css.getPropertyValue('--accent').trim() || '#f5a623';
      const ink = css.getPropertyValue('--text-dim').trim() || css.getPropertyValue('--muted').trim() || '#8a8f98';
      const grid = 'rgba(128,128,128,.22)';
      const padB = 16, ph = H - padB, X = (mhz) => ((mhz - SPEC_F0) / SPEC_SPAN) * w;
      const { signals: sg, rxDlMHz: rx, rxSrKs: rs } = live.current;
      const nameOf = (s) => { const n = namesRef.current.find((q) => q.freq >= s.start - 0.03 && q.freq <= s.end + 0.03); return n ? n.name : ''; };
      g.font = '10px sans-serif'; g.textBaseline = 'alphabetic';
      // Labels above the signals (a decoded name, else the symbol rate). A label that would overlap one already placed in a row goes
      // to the next row down; the plot starts under the last row.
      const labels = [];
      for (const s of sg) {
        const nm = nameOf(s);
        g.font = nm ? 'bold 12px sans-serif' : '10px sans-serif';
        const text = nm || (s.beacon ? 'beacon' : (s.sr >= 1000 ? s.sr / 1000 + 'M' : s.sr + 'k'));
        const hw = g.measureText(text).width / 2;
        labels.push({ s, nm, text, hw, x: Math.min(w - hw - 2, Math.max(hw + 2, X(s.freq))), row: 0 });
      }
      labels.sort((a, b) => a.x - b.x);
      const rowEnd = [];
      for (const l of labels) {
        let r = 0;
        while (r < rowEnd.length && l.x - l.hw < rowEnd[r] + 6) r++;
        rowEnd[r] = l.x + l.hw; l.row = r;
      }
      const padT = 4 + 13 * Math.max(1, rowEnd.length);
      for (let f = 10491; f <= 10499; f++) { const x = Math.round(X(f)) + .5; g.strokeStyle = grid; g.beginPath(); g.moveTo(x, 0); g.lineTo(x, ph); g.stroke(); g.fillStyle = ink; g.textAlign = 'center'; g.fillText(String(f), x, H - 3); }
      if (rx > 0 && rs > 0) { g.fillStyle = 'rgba(255,255,255,.10)'; const x0 = X(rx - rs / 2000); g.fillRect(x0, 0, Math.max(2, X(rx + rs / 2000) - x0), ph); g.fillStyle = accent; g.fillRect(Math.round(X(rx)), 0, 1, ph); }
      const fr = frameRef.current;
      if (!fr || fr.length < 16) { g.fillStyle = ink; g.textAlign = 'center'; g.fillText('waiting for the BATC spectrum feed…', w / 2, ph / 2); return; }
      let peak = 0; for (let i = 0; i < fr.length; i++) if (fr[i] > peak) peak = fr[i];
      yMax.current += (Math.max(30000, peak * 1.08) - yMax.current) * 0.2;
      const Y = (v) => ph - Math.max(0, Math.min(1, (v - 9000) / (yMax.current - 9000))) * (ph - padT);
      g.beginPath(); g.moveTo(0, ph);
      for (let px = 0; px <= w; px++) {                           // max of the bins under each pixel
        const a = Math.floor((px / w) * fr.length), b = Math.max(a + 1, Math.floor(((px + 1) / w) * fr.length));
        let m = 0; for (let i = a; i < b && i < fr.length; i++) if (fr[i] > m) m = fr[i];
        g.lineTo(px, Y(m));
      }
      g.lineTo(w, ph); g.closePath();
      g.fillStyle = 'rgba(80,160,255,.25)'; g.fill();
      g.strokeStyle = 'rgba(110,180,255,.9)'; g.lineWidth = 1; g.stroke();
      for (const l of labels) {                                   // a decoded service name stays on its signal until the signal leaves the spectrum
        const sg1 = l.s;
        g.fillStyle = sg1.beacon ? 'rgba(255,120,60,.85)' : accent;
        g.fillRect(X(sg1.start), ph - 4, Math.max(2, X(sg1.end) - X(sg1.start)), 4);
        g.font = l.nm ? 'bold 12px sans-serif' : '10px sans-serif'; g.fillStyle = l.nm ? accent : ink; g.textAlign = 'center';
        g.fillText(l.text, l.x, 11 + 13 * l.row);
        if (l.row > 0) { g.strokeStyle = 'rgba(128,128,128,.35)'; g.beginPath(); g.moveTo(Math.round(X(sg1.freq)) + .5, 13 * l.row + 14); g.lineTo(Math.round(X(sg1.freq)) + .5, padT); g.stroke(); }
      }
    };
    const id = setInterval(draw, 100); draw();
    return () => clearInterval(id);
  }, []);
  const pick = (e) => {
    const r = cvRef.current.getBoundingClientRect();
    const mhz = SPEC_F0 + ((e.clientX - r.left) / r.width) * SPEC_SPAN;
    const hit = live.current.signals.find((s) => mhz >= s.start - 0.03 && mhz <= s.end + 0.03);
    if (hit) onPick(hit.freq, hit.sr); else onPick(Math.round(mhz * 1000) / 1000, 0);
  };
  return <canvas ref={cvRef} onClick={pick} style={{ display: 'block', width: '100%', height: H, cursor: 'crosshair', borderRadius: 6 }} />;
}

// Expert receiver options: every setting zynqrx takes over MQTT besides frequency / symbol rate (cmd/zynqrx/<name>, the same names as its command
// line options). The value shown is what the receiver reports it has (dt/zynqrx/set/<name>); a change is sent and the receiver restarts for a few
// seconds. Numbers and text are sent on Enter or when the box loses focus.
const EXPERT_MODS = ['qpsk', '8psk', '16apsk', '32apsk'];
const numIn = (lo, hi) => (v) => { const n = parseFloat(v); return !isNaN(n) && n >= lo && n <= hi; };
const EXPERT_TEXT = [
  { k: 'gain', label: 'RX gain', suffix: 'dB', hint: 'auto = the radio AGC, or 0 – 73 dB', ok: (v) => v === 'auto' || numIn(0, 73)(v) },
  { k: 'bw', label: 'Analog bandwidth', suffix: 'kHz', hint: '0 = 3 x the symbol rate', ok: numIn(0, 56000) },
];
function ExpertText({ d, spec, reported }) {
  const [draft, setDraft] = useWtS(null);
  const shown = draft != null ? draft : (reported != null ? reported : '');
  const valid = draft == null || spec.ok(draft.trim());
  const commit = () => {
    if (draft == null) return;
    const v = draft.trim();
    if (spec.ok(v) && v !== reported) d.publish('zynqrx/' + spec.k, v);
    setDraft(null);
  };
  return (
    <div title={spec.hint}><Field label={spec.label}>
      <TextInput value={shown} suffix={spec.suffix} invalid={!valid} disabled={!d.mqtt}
        onChange={setDraft} onBlur={commit} onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); else if (e.key === 'Escape') setDraft(null); }} />
    </Field></div>
  );
}
// Max lag as a slider, 1 – 32 s: sent when the slider is released (a change restarts the receiver, so not on every step)
function ExpertLag({ d, reported }) {
  const [draft, setDraft] = useWtS(null);
  const rep = reported != null ? Math.min(32, Math.max(1, Math.round(parseFloat(reported)) || 1)) : 4;
  const shown = draft != null ? draft : rep;
  const commit = () => {
    if (draft != null && draft !== rep) d.publish('zynqrx/maxlag', String(draft));
    setDraft(null);
  };
  return (
    <div className="slider-compact" title="drop the oldest samples when the receiver is this far behind" onPointerUp={commit} onKeyUp={commit}>
      <Field label="Max lag">
        <Slider value={shown} min={1} max={32} step={1} onChange={setDraft} unit=" s" />
      </Field>
    </div>
  );
}
function ExpertOptions({ d, lm, open, setOpen }) {
  const set = (k) => lm['set/' + k];
  const send = (k, v) => d.publish('zynqrx/' + k, v);
  const flag = (k, label, hint) => (
    <div title={hint}><Field label={label}>
      <Toggle on={set(k) === '1'} onChange={(v) => send(k, v ? '1' : '0')} />
    </Field></div>
  );
  const modsOn = set('mods') == null || set('mods') === 'all' ? EXPERT_MODS : set('mods').split(',');
  const toggleMod = (m, on) => {
    const next = EXPERT_MODS.filter((x) => (x === m ? on : modsOn.includes(x)));
    if (next.length) send('mods', next.length === EXPERT_MODS.length ? 'qpsk,8psk,16apsk,32apsk' : next.join(','));
  };
  const known = set('std') != null;
  return (
    <Card title="Expert options" sub="zynqrx settings over MQTT · a change restarts the receiver"
      right={<button className="btn ghost" onClick={() => setOpen(open === '1' ? '0' : '1')}>{open === '1' ? 'Hide' : 'Show'}</button>}>
      {open === '1' && (
        <>
          {!known && (
            <div className="info-note" style={{ marginBottom: 10 }}>
              <Icon name="bolt" size={16} />
              <p>The receiver does not report its settings — it may run an older zynqrx without these commands.</p>
            </div>
          )}
          <div className="form-grid" style={{ gridTemplateColumns: 'repeat(5, minmax(0, 1fr))', gap: '8px 12px' }}>
            <div title="--std"><Field label="Standard">
              <Select value={set('std') != null ? set('std') : '2'} onChange={(v) => send('std', v)}
                options={[{ v: '0', l: 'DVB-S and DVB-S2' }, { v: '1', l: 'DVB-S only' }, { v: '2', l: 'DVB-S2 only' }]} />
            </Field></div>
            <div title="cheaper symbol timing, auto = above 0.5 MS/s"><Field label="Fast timing">
              <Select value={set('fasttiming') || 'auto'} onChange={(v) => send('fasttiming', v)}
                options={[{ v: 'auto', l: 'Auto' }, { v: 'on', l: 'On' }, { v: 'off', l: 'Off' }]} />
            </Field></div>
            {EXPERT_TEXT.map((spec) => <ExpertText key={spec.k} d={d} spec={spec} reported={set(spec.k)} />)}
            <ExpertLag d={d} reported={set('maxlag')} />
            {flag('ccm', 'Constant coding (CCM)', 'search only the MODCOD found first')}
            {flag('presence', 'Presence gate', 'look for a signal only while one is seen')}
            {flag('ldpc8', '8-bit LDPC', 'cheaper, about 2 dB less sensitive at low code rates')}
            <div title="Tezuka firmware: 8-bit samples from the FPGA, half the memory traffic, less dynamic range. Off = 12-bit samples in 16 bits"><Field label="CS8 samples">
              <Toggle on={(set('iqformat') || 'cs8') === 'cs8'} onChange={(v) => send('iqformat', v ? 'cs8' : 'cs16')} />
            </Field></div>
            {flag('nobch', 'Bypass BCH', 'skip the BCH decoder (saves CPU, the LDPC output is taken as is)')}
            {flag('wskeyframe', 'Wait for keyframe', 'a new viewer starts at a keyframe, applies at once')}
            <div style={{ gridColumn: 'span 3' }}>
              <div title="fewer = a cheaper search"><Field label="Modulations searched">
                <div style={{ display: 'flex', gap: 16, flexWrap: 'wrap' }}>
                  {EXPERT_MODS.map((m) => <Checkbox key={m} checked={modsOn.includes(m)} label={m.toUpperCase()} onChange={(on) => toggleMod(m, on)} />)}
                </div>
              </Field></div>
            </div>
          </div>
        </>
      )}
    </Card>
  );
}

function Webtioune({ d }) {
  const lm = (d && d.lm) || {};
  const [freqHz, setFreqHz] = useLS2('webtioune_rx_frequency', 437e6, parseFloat);
  const [srKs, setSrKs] = useLS2('webtioune_rx_sr', 333, (x) => parseInt(x));
  const [expertOpen, setExpertOpen] = useLS2('webtioune_expert', '0');
  const sendTimers = useWtR({});
  // The tuner and the text box change on every key: send once the value has settled
  // (each change restarts the receiver for a few seconds).
  const sendLater = (key, topic, value) => {
    clearTimeout(sendTimers.current[key]);
    sendTimers.current[key] = setTimeout(() => d.publish(topic, value), 400);
  };
  // What zynqrx reports it was asked for (dt/zynqrx/set/..., kHz and kS) wins over the local copy
  useWtE(() => { if (lm['set/frequency'] != null) { const v = parseInt(lm['set/frequency']); if (v > 0) setFreqHz(v * 1000); } }, [lm['set/frequency']]);
  useWtE(() => { if (lm['set/sr'] != null) { const v = parseInt(lm['set/sr']); if (v > 0) setSrKs(v); } }, [lm['set/sr']]);
  useWtE(() => () => Object.values(sendTimers.current).forEach(clearTimeout), []);

  const rxKnown = lm.process != null;
  const rxRunning = lm.process === 'start';
  const setProcess = (v) => d.publish('zynqrx/process', v);

  // QO-100 WB bandplan, the DATV Controller's own component: a slot gives the downlink frequency and the
  // symbol rate; the receiver (radio) frequency is the downlink minus the LNB LO. The LO is stored under
  // the same key as the DATV Controller's, so both pages use one value.
  const [lnbLo, setLnbLo] = useLS2('datv_lnb_lo', 9750e6, parseFloat);
  const [planMsg, setPlanMsg] = useWtS('');

  // Live signals on the transponder, from the BATC spectrum monitor (qo100wb.js). A signal is kept lit for
  // 1.5 s after it was last seen, so the highlight does not flicker with the noise.
  const [wbOn, setWbOn] = useLS2('webtioune_wb_live', '1');
  const [wb, setWb] = useWtS({ signals: [], state: 'idle' });
  const wbFrame = useWtR(null);
  useWtE(() => {
    if (wbOn !== '1' || !window.Qo100Wb) { wbFrame.current = null; setWb({ signals: [], state: 'idle' }); return; }
    const seen = new Map();                       // key -> { sig, t }
    let state = 'connecting', lastKey = '';
    const mon = new window.Qo100Wb((sigs, st) => {
      state = st;
      const now = Date.now();
      for (const g of sigs) seen.set(`${g.beacon ? 'b' : Math.round(g.freq * 50)}|${g.sr}`, { sig: g, t: now });
    }, (frame) => { wbFrame.current = frame; });
    mon.start();
    const id = setInterval(() => {
      const now = Date.now(), list = [];
      for (const [k, v] of seen) { if (now - v.t > 1500) seen.delete(k); else list.push(v.sig); }
      list.sort((a, b) => a.freq - b.freq);
      const key = state + '|' + list.map((g) => `${g.freq.toFixed(2)}:${g.sr}:${g.level.toFixed(0)}`).join(',');
      if (key !== lastKey) { lastKey = key; setWb({ signals: list, state }); }
    }, 500);
    return () => { clearInterval(id); mon.stop(); wbFrame.current = null; };
  }, [wbOn]);
  const onSpecPick = (dlMHz, sr) => {                   // sr 0 = keep the current symbol rate
    const ifHz = dlMHz * 1e6 - lnbLo;                    // downlink minus the LNB LO
    if (!(ifHz >= RX_FREQ_MIN_HZ && ifHz <= RX_FREQ_MAX_HZ)) { setPlanMsg(`IF ${(ifHz / 1e6).toFixed(1)} MHz is outside 70 MHz - 6 GHz with this LNB LO`); return; }
    setPlanMsg('');
    setFreqHz(ifHz);
    d.publish('zynqrx/frequency', Math.round(ifHz / 1000));
    if (sr > 0) { setSrKs(sr); d.publish('zynqrx/sr', sr); }
  };

  const [status, setStatus] = useWtS('idle'); // idle | loading | connected | error
  const [errorMsg, setErrorMsg] = useWtS('');
  // this browser cannot play HEVC through Media Source Extensions (addSourceBuffer refuses hvc1/hev1): shown as an H265 warning, not the raw error
  const hevcUnsupported = /(hvc1|hev1|hevc)/i.test(errorMsg) && /addSourceBuffer|unsupported/i.test(errorMsg);
  const [mediaInfo, setMediaInfo] = useWtS(null);
  const playerRef = useWtR(null);
  const videoRef = useWtR(null);
  // Info: what mpegts.js and the video element report, polled twice a second while the box is ticked
  const [infoOn, setInfoOn] = useLS2('webtioune_info', '0');
  const statsRef = useWtR(null);
  const [live, setLive] = useWtS(null);
  useWtE(() => {
    if (infoOn !== '1') { setLive(null); return; }
    const id = setInterval(() => {
      const v = videoRef.current;
      if (!v) return;
      const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality() : null;
      setLive({ stats: statsRef.current, t: v.currentTime, paused: v.paused, ready: v.readyState,
        lat: v.buffered && v.buffered.length ? v.buffered.end(v.buffered.length - 1) - v.currentTime : null,
        dropped: q ? q.droppedVideoFrames : null, shown: q ? q.totalVideoFrames : null });
    }, 500);
    return () => clearInterval(id);
  }, [infoOn]);

  const stop = () => {
    const p = playerRef.current;
    if (p) {
      playerRef.current = null;
      try { p.pause(); p.unload(); p.detachMediaElement(); p.destroy(); } catch (_) {}
    }
    setStatus('idle');
    setMediaInfo(null);
    statsRef.current = null;
  };

  const connect = async () => {
    setErrorMsg('');
    setStatus('loading');
    try {
      await loadMpegts();
    } catch (e) {
      setStatus('error');
      setErrorMsg(e.message);
      return;
    }
    stop();
    setStatus('loading');
    const mp = window.mpegts;
    if (!mp.getFeatureList().mseLivePlayback) {
      setStatus('error');
      setErrorMsg('This browser has no Media Source Extensions live playback.');
      return;
    }
    const player = mp.createPlayer(
      { type: 'mpegts', isLive: true, url: wsTsUrl(), hasAudio: true },
      { enableWorker: true, liveBufferLatencyChasing: true, liveBufferLatencyMaxLatency: 3, liveBufferLatencyMinRemain: 0.5 }
    );
    playerRef.current = player;
    // a player that was stopped meanwhile (lock lost) must not report anything
    player.on(mp.Events.ERROR, (type, detail, info) => {
      if (playerRef.current !== player) return;
      setStatus('error');
      setErrorMsg(`${type}: ${detail}${info && info.msg ? ' — ' + info.msg : ''}`);
    });
    player.on(mp.Events.STATISTICS_INFO, (st) => { if (playerRef.current === player) statsRef.current = st; });
    player.on(mp.Events.MEDIA_INFO, (info) => {
      setMediaInfo({ video: info.videoCodec, size: info.width && `${info.width}x${info.height}`, fps: info.fps && +info.fps.toFixed(2), mime: info.mimeType, profile: info.profile, level: info.level, chroma: info.chromaFormat, kbps: info.videoDataRate });
    });
    player.attachMediaElement(videoRef.current);
    player.load();
    // With sound when the browser allows it (it blocks autoplay with sound until the user has used the page);
    // otherwise muted, and the video's own speaker button turns the sound on.
    const v = videoRef.current;
    const mayHearIt = !!(navigator.userActivation && navigator.userActivation.hasBeenActive);
    v.muted = !mayHearIt;
    const onStarted = () => setStatus('connected');
    const onFailed = (e) => { if (playerRef.current !== player) return; setStatus('error'); setErrorMsg('play(): ' + e.message); };
    const started = player.play();
    if (started && started.then) {
      started.then(onStarted).catch((e) => {
        if (playerRef.current !== player) return;
        if (e && e.name === 'NotAllowedError' && !v.muted) { v.muted = true; player.play().then(onStarted).catch(onFailed); }   // autoplay with sound refused
        else onFailed(e);
      });
    } else onStarted();
  };

  // Release the player when navigating away from this page.
  useWtE(() => () => stop(), []);

  // The player follows the demodulator: connected while it is locked (header found / demod_s / demod_s2),
  // stopped when it is not. The service name comes from the SDT, which zynqrx decodes and publishes.
  const locked = rxRunning && (lm.rx_state === 'demod_s' || lm.rx_state === 'demod_s2');
  // dt/zynqrx/service_name is retained, so after a retune it still holds the previous station's name. A name is
  // only trusted once the receiver has left lock and locked again since the tuning changed.
  const tuneKey = `${Math.round(freqHz / 1000)}|${srKs}`;
  const [nameArmed, setNameArmed] = useWtS(true);
  const wasUnlocked = useWtR(false);
  const lockedNow = useWtR(locked); lockedNow.current = locked;
  const firstKey = useWtR(true);
  useWtE(() => { if (firstKey.current) { firstKey.current = false; return; } wasUnlocked.current = !lockedNow.current; setNameArmed(wasUnlocked.current); }, [tuneKey]);
  useWtE(() => { if (!locked) wasUnlocked.current = true; else if (wasUnlocked.current) setNameArmed(true); }, [locked]);
  const serviceName = locked && nameArmed && lm.service_name ? lm.service_name : '';
  // Names decoded so far, each at the downlink frequency it was received on; the spectrum keeps a name on its signal
  // until that signal is gone from the spectrum.
  const namesRef = useWtR([]);
  const rxDlRef = useWtR(0); rxDlRef.current = (freqHz + lnbLo) / 1e6;
  useWtE(() => {
    if (!serviceName) return;
    const f = rxDlRef.current;
    namesRef.current = namesRef.current.filter((n) => Math.abs(n.freq - f) > 0.05).concat({ freq: f, name: serviceName });
  }, [serviceName]);
  useWtE(() => {
    if (wb.state !== 'live') return;
    namesRef.current = namesRef.current.filter((n) => wb.signals.some((g) => n.freq >= g.start - 0.03 && n.freq <= g.end + 0.03));
  }, [wb.signals, wb.state]);
  useWtE(() => {
    if (locked) connect(); else stop();
    setErrorMsg('');
  }, [locked]);

  // Info overlay: only what the player has reported so far; nothing at all until something has arrived
  const infoLines = [];
  {
    const st = (live && live.stats) || null;
    const n = (v, d = 0) => (+v).toFixed(d);
    if (status === 'connected' || mediaInfo || st) infoLines.push(`player ${status}${live && live.paused && status === 'connected' ? ' (paused)' : ''}`);
    if (mediaInfo && (mediaInfo.video || mediaInfo.size || mediaInfo.fps)) infoLines.push([mediaInfo.video, mediaInfo.size, mediaInfo.fps && mediaInfo.fps + ' fps'].filter(Boolean).join('  '));
    if (mediaInfo && (mediaInfo.profile || mediaInfo.level || mediaInfo.chroma || mediaInfo.kbps)) {
      infoLines.push([mediaInfo.profile && 'profile ' + mediaInfo.profile, mediaInfo.level && 'level ' + mediaInfo.level, mediaInfo.chroma,
        mediaInfo.kbps && 'video ' + n(mediaInfo.kbps) + ' kb/s'].filter(Boolean).join('  '));
    }
    if (st) {
      const dropped = live && live.dropped != null ? live.dropped : st.droppedFrames;
      infoLines.push([st.speed != null && 'speed ' + n(st.speed, 1) + ' KB/s', st.decodedFrames != null && 'decoded ' + n(st.decodedFrames), dropped != null && 'dropped ' + n(dropped)].filter(Boolean).join('   '));
    }
    const lat = live && live.lat != null && !isNaN(live.lat) ? 'latency ' + n(live.lat, 2) + ' s' : null;
    if (lat || (st && st.loaderType)) infoLines.push([lat, st && st.loaderType && 'via ' + st.loaderType].filter(Boolean).join('   '));
  }
  const showInfo = infoOn === '1' && infoLines.length > 0;

  // Two columns so the controls and the picture fit on one screen without scrolling
  // (they stack below 1100 px, see .grid-12 in styles.css). The video box is
  // 16:9 and capped to what is left of the window height after the cards' chrome.
  return (
    <div className="page">
      <div className="grid-12">
        <div className="span-5 tile-stack">
          <Card title="Receiver" sub="zynqrx DVB-S2"
            right={<div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Pill tone={!rxKnown ? 'neutral' : rxRunning ? 'ok' : 'warn'} dot>{!rxKnown ? 'No status' : rxRunning ? 'Running' : 'Stopped'}</Pill>
              <button className="btn primary" onClick={() => setProcess('start')} disabled={rxRunning || !d.mqtt}>Start</button>
              <button className="btn ghost" onClick={() => setProcess('stop')} disabled={!rxRunning || !d.mqtt}>Stop</button>
            </div>}>
            <div className="form-grid" style={{ gridTemplateColumns: '1fr', gap: 14 }}>
              <Field label="Frequency" hint="70 MHz – 6 GHz">
                <FreqTuner value={freqHz} digits={12} min={RX_FREQ_MIN_HZ} max={RX_FREQ_MAX_HZ} unit="Hz"
                  sub={(v) => (v / 1e6).toFixed(3) + ' MHz'}
                  onChange={(v) => { setFreqHz(v); sendLater('freq', 'zynqrx/frequency', Math.round(v / 1000)); }} />
              </Field>
              <Field label="Symbol rate" hint="33 – 7 680 kS/s">
                <div style={{ display: 'flex', gap: 6 }}>
                  <Select value={RX_SR_PRESETS.includes(srKs) ? srKs : 'custom'}
                    onChange={(v) => { if (v === 'custom') return; const n = parseInt(v); setSrKs(n); sendLater('sr', 'zynqrx/sr', n); }}
                    options={[{ v: 125, l: '125 kS' }, { v: 250, l: '250 kS' }, { v: 333, l: '333 kS' }, { v: 500, l: '500 kS' }, { v: 1000, l: '1 MS' }, { v: 'custom', l: 'Custom' }]} />
                  <TextInput value={String(srKs)} suffix="kS"
                    invalid={!(srKs >= RX_SR_MIN_KS && srKs <= RX_SR_MAX_KS)}
                    onChange={(v) => { const n = Math.round(parseFloat(v)); if (!isNaN(n)) { setSrKs(n); if (n >= RX_SR_MIN_KS && n <= RX_SR_MAX_KS) sendLater('sr', 'zynqrx/sr', n); } else if (v === '') setSrKs(0); }} />
                </div>
              </Field>
            </div>
            <div className="kv-grid" style={{ marginTop: 14, gridTemplateColumns: 'repeat(4, 1fr)' }}>
              <div className="kv"><span>State</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning ? (lm.rx_state || '—') : rxKnown ? 'stopped' : '—'}</b></div>
              <div className="kv"><span>Signal</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.modulation ? `${lm.modulation} ${lm.fec || ''}` : '—'}</b></div>
              <div className="kv"><span>MER</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.mer ? lm.mer + ' dB' : '—'}</b></div>
              <div className="kv"><span>Margin</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.margin_db != null ? lm.margin_db + ' dB' : '—'}</b></div>
              <div className="kv"><span>LDPC errors</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.ldpc_errors != null ? lm.ldpc_errors : '—'}</b></div>
              <div className="kv"><span>Type</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.short_frame != null && lm.modulation ? (lm.short_frame === '1' ? 'Short' : 'Long') : '—'}</b></div>
              <div className="kv" style={{ gridColumn: 'span 2' }}><span>Pilots</span><b className="mono" style={{ fontSize: 12.5 }}>{rxRunning && lm.pilots != null && lm.modulation ? (lm.pilots === '1' ? 'On' : 'Off') : '—'}</b></div>
            </div>
            {!rxKnown && (
              <div className="info-note" style={{ marginTop: 10 }}>
                <Icon name="bolt" size={16} />
                <p>No status from zynqrx yet — is it running on the board (start_zynqrx)?</p>
              </div>
            )}
          </Card>

          <Card title="QO-100 WB spectrum" sub="click a signal or the trace to tune"
            right={<div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
              <Pill tone={wb.state === 'live' ? 'ok' : wb.state === 'idle' ? 'neutral' : 'warn'} dot>{wb.state === 'live' ? 'WB live' : wb.state === 'idle' ? 'WB off' : wb.state === 'offline' ? 'WB offline' : 'WB …'}</Pill>
              <Toggle on={wbOn === '1'} onChange={(v) => setWbOn(v ? '1' : '0')} labels={['LIVE', 'LIVE']} />
              <div style={{ width: 170 }}><TextInput value={String(lnbLo)} suffix="LO Hz"
                onChange={(v) => { const n = parseFloat(v); if (!isNaN(n)) setLnbLo(n); }} /></div>
            </div>}>
            <Qo100Spectrum frameRef={wbFrame} signals={wb.signals} rxDlMHz={(freqHz + lnbLo) / 1e6} rxSrKs={srKs} namesRef={namesRef} onPick={onSpecPick} />
            {planMsg && (
              <div className="info-note" style={{ marginTop: 8 }}>
                <Icon name="bolt" size={16} />
                <p>{planMsg}</p>
              </div>
            )}
          </Card>
        </div>

        <div className="span-7 tile-stack">
        <Card title="Webtioune" sub="Live TS preview · mpegts.js"
          right={hevcUnsupported && <Pill tone="warn" dot>H265</Pill>}>
          {errorMsg && !hevcUnsupported && (
            <div className="info-note" style={{ marginTop: 10 }}>
              <Icon name="bolt" size={16} />
              <p>{errorMsg}</p>
            </div>
          )}
          <div style={{ marginTop: 12 }}>
            <Checkbox checked={infoOn === '1'} onChange={(on) => setInfoOn(on ? '1' : '0')} label="Info (mpegts.js status and statistics)" />
          </div>
          <div style={{ position: 'relative', marginTop: 8 }}>
            <video ref={videoRef} controls playsInline
              style={{ display: 'block', width: '100%', aspectRatio: '16 / 9', maxHeight: `calc(100vh - ${(expertOpen === '1' ? 560 : 300) + 30}px)`, minHeight: expertOpen === '1' ? 150 : 200, background: '#000' }} />
            {locked && lm.margin_db != null && !isNaN(parseInt(lm.margin_db)) && (
              <div title={`margin ${lm.margin_db} dB`} style={{ position: 'absolute', top: 14, right: 18, pointerEvents: 'none', color: '#fff', fontSize: 'clamp(22px, 3.2vw, 44px)', fontWeight: 700, lineHeight: 1.1, textShadow: '0 2px 6px rgba(0,0,0,.9), 0 0 2px #000' }}>D{parseInt(lm.margin_db)}</div>
            )}
            {(serviceName || showInfo) && (
              <div style={{ position: 'absolute', top: 14, left: 18, right: 'clamp(70px, 8vw, 120px)', pointerEvents: 'none', color: '#fff', textShadow: '0 2px 6px rgba(0,0,0,.9), 0 0 2px #000', overflow: 'hidden' }}>
                {serviceName && <div style={{ fontSize: 'clamp(22px, 3.2vw, 44px)', fontWeight: 700, lineHeight: 1.1, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{serviceName}</div>}
                {showInfo && <div className="mono" style={{ marginTop: 6, fontSize: 'clamp(11px, 1.1vw, 14px)', lineHeight: 1.35 }}>{infoLines.map((l, k) => <div key={k}>{l}</div>)}</div>}
              </div>
            )}
          </div>
        </Card>

        <ExpertOptions d={d} lm={lm} open={expertOpen} setOpen={setExpertOpen} />
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { Webtioune });

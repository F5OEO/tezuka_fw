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
// start / stop go to zynqrx over MQTT in longmynd's scheme (cmd/longmynd/
// frequency [kHz], sr [kS], process start|stop), and its status comes back on
// dt/longmynd/... (d.lm, filled by useLiveData in data.jsx).
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

// Can this browser decode HEVC through MSE? (Main profile, level 3.1 is enough
// as a probe — decoders either do HEVC or they don't.)
const hevcMseSupported = () =>
  !!(window.MediaSource && MediaSource.isTypeSupported('video/mp4; codecs="hvc1.1.6.L93.B0"'));

// zynqrx's limits (cmd/longmynd/frequency in kHz, sr in kS)
const RX_FREQ_MIN_HZ = 70e6, RX_FREQ_MAX_HZ = 6e9;
const RX_SR_MIN_KS = 33, RX_SR_MAX_KS = 7680;
const RX_SR_PRESETS = [125, 250, 333, 500, 1000];

function Webtioune({ d }) {
  const lm = (d && d.lm) || {};
  const [freqHz, setFreqHz] = useLS2('webtioune_rx_frequency', 437e6, parseFloat);
  const [srKs, setSrKs] = useLS2('webtioune_rx_sr', 333, (x) => parseInt(x));
  const sendTimers = useWtR({});
  // The tuner and the text box change on every key: send once the value has settled
  // (each change restarts the receiver for a few seconds).
  const sendLater = (key, topic, value) => {
    clearTimeout(sendTimers.current[key]);
    sendTimers.current[key] = setTimeout(() => d.publish(topic, value), 400);
  };
  // What zynqrx reports it was asked for (dt/longmynd/set/..., kHz and kS) wins over the local copy
  useWtE(() => { if (lm['set/frequency'] != null) { const v = parseInt(lm['set/frequency']); if (v > 0) setFreqHz(v * 1000); } }, [lm['set/frequency']]);
  useWtE(() => { if (lm['set/sr'] != null) { const v = parseInt(lm['set/sr']); if (v > 0) setSrKs(v); } }, [lm['set/sr']]);
  useWtE(() => () => Object.values(sendTimers.current).forEach(clearTimeout), []);

  const rxKnown = lm.process != null;
  const rxRunning = lm.process === 'start';
  const setProcess = (v) => d.publish('longmynd/process', v);

  const [status, setStatus] = useWtS('idle'); // idle | loading | connected | error
  const [errorMsg, setErrorMsg] = useWtS('');
  const [mediaInfo, setMediaInfo] = useWtS(null);
  const [hevcOk] = useWtS(hevcMseSupported);
  const playerRef = useWtR(null);
  const videoRef = useWtR(null);

  const stop = () => {
    const p = playerRef.current;
    if (p) {
      playerRef.current = null;
      try { p.pause(); p.unload(); p.detachMediaElement(); p.destroy(); } catch (_) {}
    }
    setStatus('idle');
    setMediaInfo(null);
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
      { type: 'mpegts', isLive: true, url: wsTsUrl(), hasAudio: false },
      { enableWorker: true, liveBufferLatencyChasing: true, liveBufferLatencyMaxLatency: 3, liveBufferLatencyMinRemain: 0.5 }
    );
    playerRef.current = player;
    player.on(mp.Events.ERROR, (type, detail, info) => {
      setStatus('error');
      setErrorMsg(`${type}: ${detail}${info && info.msg ? ' — ' + info.msg : ''}`);
    });
    player.on(mp.Events.MEDIA_INFO, (info) => {
      setMediaInfo({ video: info.videoCodec, size: info.width && `${info.width}x${info.height}`, fps: info.fps && +info.fps.toFixed(2), mime: info.mimeType });
    });
    player.attachMediaElement(videoRef.current);
    player.load();
    // Muted: the browser blocks autoplay with sound, and audio is off anyway.
    videoRef.current.muted = true;
    const started = player.play();
    if (started && started.then) started.then(() => setStatus('connected')).catch((e) => { setStatus('error'); setErrorMsg('play(): ' + e.message); });
    else setStatus('connected');
  };

  // Release the player when navigating away from this page.
  useWtE(() => () => stop(), []);

  const statusTone = status === 'connected' ? 'ok' : status === 'error' ? 'warn' : 'neutral';
  const statusLabel = status === 'connected' ? 'Connected' : status === 'loading' ? 'Connecting…' : status === 'error' ? 'Error' : 'Idle';

  // Two columns so the controls and the picture fit on one screen without scrolling
  // (they stack below 1100 px, see .grid-12 in styles.css). The video box is
  // 16:9 and capped to what is left of the window height after the cards' chrome.
  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Receiver" sub="zynqrx DVB-S2 demodulator" className="span-5"
          right={<Pill tone={!rxKnown ? 'neutral' : rxRunning ? 'ok' : 'warn'} dot>{!rxKnown ? 'No status' : rxRunning ? 'Running' : 'Stopped'}</Pill>}>
          <div className="form-grid" style={{ gridTemplateColumns: '1fr', gap: 14 }}>
            <Field label="Frequency" hint="70 MHz – 6 GHz">
              <FreqTuner value={freqHz} digits={12} min={RX_FREQ_MIN_HZ} max={RX_FREQ_MAX_HZ} unit="Hz"
                sub={(v) => (v / 1e6).toFixed(3) + ' MHz'}
                onChange={(v) => { setFreqHz(v); sendLater('freq', 'longmynd/frequency', Math.round(v / 1000)); }} />
            </Field>
            <Field label="Symbol rate" hint="33 – 7 680 kS/s">
              <div style={{ display: 'flex', gap: 6 }}>
                <Select value={RX_SR_PRESETS.includes(srKs) ? srKs : 'custom'}
                  onChange={(v) => { if (v === 'custom') return; const n = parseInt(v); setSrKs(n); sendLater('sr', 'longmynd/sr', n); }}
                  options={[{ v: 125, l: '125 kS' }, { v: 250, l: '250 kS' }, { v: 333, l: '333 kS' }, { v: 500, l: '500 kS' }, { v: 1000, l: '1 MS' }, { v: 'custom', l: 'Custom' }]} />
                <TextInput value={String(srKs)} suffix="kS"
                  invalid={!(srKs >= RX_SR_MIN_KS && srKs <= RX_SR_MAX_KS)}
                  onChange={(v) => { const n = Math.round(parseFloat(v)); if (!isNaN(n)) { setSrKs(n); if (n >= RX_SR_MIN_KS && n <= RX_SR_MAX_KS) sendLater('sr', 'longmynd/sr', n); } else if (v === '') setSrKs(0); }} />
              </div>
            </Field>
          </div>
          <div className="kv-grid" style={{ marginTop: 14 }}>
            <div className="kv"><span>State</span><b className="mono">{rxRunning ? (lm.rx_state || '—') : rxKnown ? 'stopped' : '—'}</b></div>
            <div className="kv"><span>Signal</span><b className="mono">{rxRunning && lm.modulation ? `${lm.modulation} ${lm.fec || ''}` : '—'}</b></div>
            <div className="kv"><span>MER</span><b className="mono">{rxRunning && lm.mer ? lm.mer + ' dB' : '—'}</b></div>
            <div className="kv"><span>Service</span><b className="mono">{rxRunning && lm.service_name ? lm.service_name : '—'}</b></div>
          </div>
          <div style={{ display: 'flex', gap: 10, marginTop: 14 }}>
            <button className="btn primary" onClick={() => setProcess('start')} disabled={rxRunning || !d.mqtt}>Start receiver</button>
            <button className="btn ghost" onClick={() => setProcess('stop')} disabled={!rxRunning || !d.mqtt}>Stop receiver</button>
          </div>
          {!rxKnown && (
            <div className="info-note" style={{ marginTop: 10 }}>
              <Icon name="bolt" size={16} />
              <p>No status from zynqrx yet — is it running on the board (start_zynqrx)?</p>
            </div>
          )}
        </Card>

        <Card title="Webtioune" sub="Live TS preview · mpegts.js" className="span-7"
          right={<div style={{ display: 'flex', gap: 8 }}>
            <button className="btn primary" onClick={connect} disabled={status === 'loading'}>Connect</button>
            <button className="btn ghost" onClick={stop} disabled={status === 'idle'}>Stop</button>
          </div>}>
          <div className="kv-grid" style={{ gridTemplateColumns: 'repeat(auto-fit, minmax(150px, 1fr))' }}>
            <div className="kv"><span>Status</span><b className="mono"><Pill tone={statusTone} dot>{statusLabel}</Pill></b></div>
            <div className="kv"><span>HEVC decode</span><b className="mono"><Pill tone={hevcOk ? 'ok' : 'warn'} dot>{hevcOk ? 'Supported' : 'Not supported'}</Pill></b></div>
            <div className="kv"><span>Source</span><b className="mono" style={{ fontSize: 12, wordBreak: 'break-all' }}>{wsTsUrl()}</b></div>
            {mediaInfo && (
              <div className="kv"><span>Media info</span><b className="mono" style={{ fontSize: 12 }}>{[mediaInfo.video, mediaInfo.size, mediaInfo.fps && mediaInfo.fps + ' fps'].filter(Boolean).join(' · ')}</b></div>
            )}
          </div>
          {!hevcOk && (
            <div className="info-note" style={{ marginTop: 10 }}>
              <Icon name="bolt" size={16} />
              <p>This browser cannot decode HEVC (H.265), so the preview will not play. Use Safari, Edge on Windows (HEVC extension), Chrome on Android, or a Chrome with hardware HEVC decoding.</p>
            </div>
          )}
          {errorMsg && (
            <div className="info-note" style={{ marginTop: 10 }}>
              <Icon name="bolt" size={16} />
              <p>{errorMsg}</p>
            </div>
          )}
          <video ref={videoRef} controls muted playsInline
            style={{ display: 'block', marginTop: 12, width: '100%', aspectRatio: '16 / 9', maxHeight: 'calc(100vh - 360px)', minHeight: 200, background: '#000' }} />
        </Card>
      </div>
    </div>
  );
}

Object.assign(window, { Webtioune });

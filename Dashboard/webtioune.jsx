// webtioune.jsx — DATV Controller sub-page: live transport-stream preview
// via h265web.js (https://github.com/numberwolf/h265web.js, CYL_Free-1.0
// license).
//
// Source: an external DVB-S2 receiver that already serves the decoded
// transport stream over WebSocket — this firmware does not produce or relay
// that stream itself, this page is purely a player pointed at a URL you give
// it.
//
// h265web.js's runtime (h265web.js, h265web_wasm.js, h265web_wasm.wasm,
// extjs.js, extwasm.js, extwasm.wasm) is NOT part of this repo — it's placed
// on-device separately (see H265WEB_BASE below). It's lazy-loaded on first
// visit to this page rather than included in every page load, since it's a
// large WASM decoder unrelated to the rest of the Dashboard.
const { useState: useWtS, useEffect: useWtE, useRef: useWtR } = React;

// Expected relative to wherever Tezuka Dashboard.html itself is served from
// — mirrors the existing Dashboard/vendor/ convention already used for
// react.js/babel.js/colormap.js.
const H265WEB_BASE = './vendor/h265web/';
const H265WEB_SCRIPT = H265WEB_BASE + 'h265web.js';

let h265webLoadPromise = null;
function loadH265Web() {
  if (window.H265webjsPlayer) return Promise.resolve();
  if (h265webLoadPromise) return h265webLoadPromise;
  h265webLoadPromise = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = H265WEB_SCRIPT;
    s.onload = () => resolve();
    s.onerror = () => { h265webLoadPromise = null; reject(new Error(`Failed to load ${H265WEB_SCRIPT} — is the h265web.js runtime installed there?`)); };
    document.head.appendChild(s);
  });
  return h265webLoadPromise;
}

function Webtioune() {
  const [wsUrl, setWsUrl] = useWtS(() => localStorage.getItem('webtioune_ws_url') || 'ws://192.168.1.100:8080/ts');
  const [status, setStatus] = useWtS('idle'); // idle | loading | connected | error
  const [errorMsg, setErrorMsg] = useWtS('');
  const [mediaInfo, setMediaInfo] = useWtS(null);
  const playerRef = useWtR(null);
  const containerIdRef = useWtR('webtioune-player-' + Math.random().toString(36).slice(2));

  useWtE(() => { localStorage.setItem('webtioune_ws_url', wsUrl); }, [wsUrl]);

  const stop = () => {
    if (playerRef.current) {
      try { playerRef.current.release(); } catch (_) {}
      playerRef.current = null;
    }
    setStatus('idle');
    setMediaInfo(null);
  };

  const connect = async () => {
    setErrorMsg('');
    setStatus('loading');
    try {
      await loadH265Web();
    } catch (e) {
      setStatus('error');
      setErrorMsg(e.message);
      return;
    }
    stop();
    const player = window.H265webjsPlayer();
    playerRef.current = player;
    player.on_ready_show_done_callback = () => setStatus('connected');
    player.video_probe_callback = (info) => setMediaInfo(info);
    player.on_play_finished = () => setStatus('idle');
    // NOTE: load_media() with a ws:// URL against a live "websocket-ts"
    // source is documented at the protocol-support level (README lists
    // "websocket-ts" as supported) but no exact worked example was available
    // at build time — this is the straightforward reading of the public API
    // (build() then load_media(url), same call used for every other source
    // type) and may need adjustment once tried against a real receiver.
    player.build({
      player_id: containerIdRef.current,
      base_url: H265WEB_BASE,
      wasm_js_uri: 'h265web_wasm.js',
      wasm_wasm_uri: 'h265web_wasm.wasm',
      ext_src_js_uri: 'extjs.js',
      ext_wasm_js_uri: 'extwasm.js',
      width: '100%',
      height: 480,
      color: '#0a0a0a',
      auto_play: true,
      ignore_audio: false,
    });
    player.load_media(wsUrl);
  };

  // Release the player/decoder when navigating away from this page.
  useWtE(() => () => stop(), []);

  const statusTone = status === 'connected' ? 'ok' : status === 'error' ? 'warn' : 'neutral';
  const statusLabel = status === 'connected' ? 'Connected' : status === 'loading' ? 'Connecting…' : status === 'error' ? 'Error' : 'Idle';

  return (
    <div className="page">
      <div className="grid-12">
        <Card title="Webtioune" sub="Live TS preview via h265web.js — source: external DVB-S2 receiver over WebSocket" className="span-12">
          <div className="kv-grid" style={{ marginBottom: 12 }}>
            <div className="kv"><span>Status</span><b className="mono"><Pill tone={statusTone} dot>{statusLabel}</Pill></b></div>
            {mediaInfo && (
              <div className="kv"><span>Media info</span><b className="mono">{JSON.stringify(mediaInfo)}</b></div>
            )}
          </div>
          <Field label="WebSocket TS URL" hint="From the DVB-S2 receiver, e.g. ws://192.168.1.100:8080/ts">
            <TextInput value={wsUrl} onChange={setWsUrl} />
          </Field>
          {errorMsg && (
            <div className="info-note" style={{ marginTop: 8 }}>
              <Icon name="bolt" size={16} />
              <p>{errorMsg}</p>
            </div>
          )}
          <div style={{ display: 'flex', gap: 10, marginTop: 12 }}>
            <button className="btn primary" onClick={connect} disabled={status === 'loading'}>Connect</button>
            <button className="btn ghost" onClick={stop} disabled={status === 'idle'}>Stop</button>
          </div>
          <div id={containerIdRef.current} style={{ marginTop: 16, background: '#000', minHeight: 480 }} />
        </Card>
      </div>
    </div>
  );
}

Object.assign(window, { Webtioune });

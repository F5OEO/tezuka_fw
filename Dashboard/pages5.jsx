// pages5.jsx — SDR Radio page (iio_ws_proxy + @jtarrio/signals demodulator)
const { useState: useS5, useEffect: useE5, useRef: useR5, useCallback: useCB5 } = React;

function RadioPage({ d }) {
  const { PushSource, SimpleProvider, Radio, Demodulator, getMode, getSchemes, modeParameters } = window.Signals;

  // ── Hardware parameters from MQTT ───────────────────────────────────────
  const mqttCenter = d.rxFreq     || 0;
  const mqttRate   = d.rxSampling || 0;

  // Lazy initializers: if MQTT data is already in `d` when the page mounts,
  // start with those values immediately. useEffects below keep them in sync
  // while the radio is not active (activeRef guards against mid-session stomps).
  const [centerFreq, setCenterFreq] = useS5(() => mqttCenter || 100e6);
  const [sampleRate, setSampleRate] = useS5(() => mqttRate   || 1024000);
  const [listenFreq, setListenFreq] = useS5(() => mqttCenter || 100e6);

  // ── Demodulator controls ────────────────────────────────────────────────
  const schemes = getSchemes();
  const [mode,    setMode]    = useS5('NBFM');
  const [volume,  setVolume]  = useS5(100);
  const [sqOn,    setSqOn]    = useS5(false);
  const [sqLevel, setSqLevel] = useS5(0.8);  // linear SNR ratio 0-6; was dB (broken)

  // ── Session state ───────────────────────────────────────────────────────
  const [status,      setStatus]      = useS5('idle');  // idle | starting | connecting | playing | error
  const [msg,         setMsg]         = useS5('');
  const [activeRate,  setActiveRate]  = useS5(null);    // sample rate the running radio was started with
  const [audioPower,  setAudioPower]  = useS5(null);    // dBFS, null when idle

  const pushRef      = useR5(null);
  const radioRef     = useR5(null);
  const demodRef     = useR5(null);
  const wsRef        = useR5(null);
  const reconnTimRef = useR5(null);
  const activeRef    = useR5(false);
  const levelRef     = useR5(-100);   // audio RMS in dBFS, updated by play() intercept
  const rafRef       = useR5(null);   // requestAnimationFrame handle for meter

  // Sync center/rate from MQTT while radio is not active.
  // setListenFreq resets to new center so the offset stays at 0 on retune.
  useE5(() => {
    if (activeRef.current || !mqttCenter) return;
    setCenterFreq(mqttCenter);
    setListenFreq(mqttCenter);
  }, [mqttCenter]);

  useE5(() => {
    if (activeRef.current || !mqttRate) return;
    setSampleRate(mqttRate);
  }, [mqttRate]);

  // ── Helpers ─────────────────────────────────────────────────────────────
  function wsUrl() {
    const host  = window._tezukaDevHost || window.location.hostname;
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${proto}//${host}:8765`;
  }

  function applySquelch(demod, on, level) {
    const p = modeParameters(demod.getMode());
    if (!p.hasSquelch()) return;
    demod.setMode(p.setSquelch(on ? level : -100).mode);
  }

  // ── Cleanup ──────────────────────────────────────────────────────────────
  const cleanup = useCB5(async (publishOff = true) => {
    activeRef.current = false;  // guards error handler from re-entering
    clearTimeout(reconnTimRef.current);
    const ws   = wsRef.current;
    const r    = radioRef.current;
    const push = pushRef.current;
    const dem  = demodRef.current;
    wsRef.current    = null;
    radioRef.current = null;
    pushRef.current  = null;
    demodRef.current = null;

    // Silence immediately so the user hears nothing while cleanup finishes.
    if (dem) dem.setVolume(0);

    // Stop WebSocket — no more pushSamples() calls after this.
    if (ws) { ws.onopen = ws.onclose = ws.onerror = ws.onmessage = null; try { ws.close(); } catch (_) {} }

    // Cancel pending readSamples() calls inside PushSource so that
    // Transfers.stopStream() can unblock and radio.stop() completes.
    // Without this, the two parallel reads block forever (no more data arrives)
    // and radio.stop() / stopStream() never return.
    if (push) try { await push.close(); } catch (_) {}

    if (rafRef.current) { cancelAnimationFrame(rafRef.current); rafRef.current = null; }
    if (r) await r.stop();
    if (publishOff) d.publish('system/iqtape', 'off');
    setActiveRate(null);
    setStatus('idle');
    setMsg('');
    setAudioPower(null);
  }, [d]);

  // ── WebSocket connect (with auto-reconnect while active) ─────────────────
  function connectWs() {
    if (!activeRef.current) return;
    setMsg('Connecting…');
    const url = wsUrl();
    const ws  = new WebSocket(url, 'iio-rx');
    ws.binaryType = 'arraybuffer';
    wsRef.current = ws;

    ws.onopen = () => {
      if (!activeRef.current) { ws.close(); return; }
      setStatus('playing');
      setMsg('');
    };

    ws.onerror = () => {};

    ws.onclose = () => {
      wsRef.current = null;
      if (!activeRef.current) return;
      setStatus('connecting');
      setMsg('Reconnecting…');
      reconnTimRef.current = setTimeout(connectWs, 2000);
    };

    ws.onmessage = (ev) => {
      const push = pushRef.current;
      if (!push || !(ev.data instanceof ArrayBuffer)) return;
      // AD9361 via libiio: interleaved int16 LE I/Q, 12-bit in 16-bit container
      const raw = new Int16Array(ev.data);
      const n   = raw.length >> 1;
      const I   = new Float32Array(n);
      const Q   = new Float32Array(n);
      const k   = 1 / 2048;
      for (let i = 0; i < n; i++) { I[i] = raw[2*i] * k; Q[i] = raw[2*i+1] * k; }
      push.pushSamples(I, Q);
    };
  }

  // ── Play ─────────────────────────────────────────────────────────────────
  async function handlePlay() {
    setStatus('starting');
    setMsg('Starting proxy…');
    try {
      const push  = new PushSource();
      const demod = new Demodulator();
      demod.setMode(getMode(mode));
      demod.setVolume(volume / 100);
      demod.setFrequencyOffset(listenFreq - centerFreq);
      applySquelch(demod, sqOn, sqLevel);

      // Intercept AudioPlayer.play() to measure audio RMS without touching AudioContext graph
      const origPlay = demod.player.play.bind(demod.player);
      demod.player.play = (left, right) => {
        let sum = 0;
        for (let i = 0; i < left.length; i++) sum += left[i] * left[i];
        const rms = Math.sqrt(sum / left.length);
        levelRef.current = rms > 1e-10 ? 20 * Math.log10(rms) : -100;
        origPlay(left, right);
      };

      // Scale buffers/sec so each buffer stays ~64 K samples regardless of rate.
      // samplesPerBuf = 512 * ceil(sampleRate / buffersPerSecond / 512) ≈ 65536
      const buffersPerSecond = Math.max(10, Math.ceil(sampleRate / 65536));
      const radio = new Radio(new SimpleProvider(push), demod, { buffersPerSecond });
      radio.setSampleRate(sampleRate);
      await radio.setFrequency(centerFreq);

      radio.addEventListener('radio', (e) => {
        // activeRef is set to false at the start of cleanup(), so error events
        // that fire because push.close() cancelled pending reads are ignored.
        if (e.detail.type === 'error' && activeRef.current) {
          setMsg('Radio error: ' + e.detail.exception);
          setStatus('error');
          cleanup();
        }
      });

      // Must call radio.start() inside the user-gesture stack to open AudioContext
      await radio.start();
      pushRef.current  = push;
      radioRef.current = radio;
      demodRef.current = demod;
      activeRef.current = true;
      setActiveRate(sampleRate);
      levelRef.current = -100;
      // RAF loop updates audio meter at ~30 fps without flooding React renders
      (function tick() {
        if (!activeRef.current) return;
        setAudioPower(levelRef.current);
        rafRef.current = requestAnimationFrame(tick);
      })();

      // Ask the device to start iio_ws_proxy, then connect
      d.publish('system/iqtape', 'on');
      setStatus('connecting');
      connectWs();
    } catch (e) {
      setMsg('Error: ' + e);
      setStatus('error');
      await cleanup(false);
    }
  }

  // ── Live demodulator updates ──────────────────────────────────────────────
  useE5(() => {
    const demod = demodRef.current;
    if (!demod) return;
    demod.setMode(getMode(mode));
    applySquelch(demod, sqOn, sqLevel);
  }, [mode]);

  useE5(() => { if (demodRef.current) demodRef.current.setVolume(volume / 100); }, [volume]);
  useE5(() => { if (demodRef.current) applySquelch(demodRef.current, sqOn, sqLevel); }, [sqOn, sqLevel]);
  useE5(() => {
    if (demodRef.current) demodRef.current.setFrequencyOffset(listenFreq - centerFreq);
    // Keep PushSource's reported center in sync so the demod offset is always correct
    if (radioRef.current) radioRef.current.setFrequency(centerFreq);
  }, [listenFreq, centerFreq]);

  useE5(() => () => { cleanup(); }, []);

  // ── Derived ──────────────────────────────────────────────────────────────
  const playing     = status === 'playing';
  const busy        = status !== 'idle' && status !== 'error';
  const halfBW      = sampleRate / 2;
  const offsetKHz   = ((listenFreq - centerFreq) / 1e3).toFixed(1);
  const proxyOn     = d.iqtape === 'on';
  const rateChanged = activeRate !== null && mqttRate > 0 && mqttRate !== activeRate;

  // ── Render ────────────────────────────────────────────────────────────────
  return (
    <div style={{ padding: '1rem', maxWidth: 520 }}>

      <Card title="Source">
        <Field label="Proxy (iio_ws_proxy)">
          <span style={{
            display: 'inline-block', padding: '0.15rem 0.55rem', borderRadius: 10,
            fontSize: '0.75rem', fontWeight: 600,
            background: proxyOn ? 'rgba(80,200,80,0.15)' : 'rgba(150,150,150,0.12)',
            color:      proxyOn ? '#6d6' : '#888',
          }}>
            {proxyOn ? '● on' : '○ off'}
          </span>
        </Field>
        <Field label="Center frequency">
          <FreqTuner
            value={centerFreq}
            onChange={(v) => {
              d.publish('rx/frequency', String(v));
              setCenterFreq(v);
              if (listenFreq < v - halfBW || listenFreq > v + halfBW) setListenFreq(v);
            }}
            digits={9}
          />
        </Field>
        <Field label="Sample rate">
          <FreqTuner value={sampleRate} onChange={(v) => { d.publish('rx/sampling', String(v)); setSampleRate(v); }} digits={7} />
        </Field>
      </Card>

      <Card title="Demodulator" style={{ marginTop: '0.75rem' }}>
        <Field label="Listen frequency">
          <FreqTuner
            value={listenFreq}
            onChange={setListenFreq}
            min={centerFreq - halfBW}
            max={centerFreq + halfBW}
            digits={9}
          />
        </Field>
        <Field label="">
          <span style={{ fontSize: '0.75rem', color: 'var(--fg-dim)' }}>
            Offset: {offsetKHz} kHz from center
          </span>
        </Field>
        <Field label="Mode">
          <Select value={mode} onChange={setMode} options={schemes} />
        </Field>
        <Field label={`Volume  ${volume}%`}>
          <Slider value={volume} min={0} max={200} onChange={setVolume} />
        </Field>
        <Field label="Squelch">
          <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
            <Toggle on={sqOn} onChange={setSqOn} labels={['off', 'on']} />
            <Slider value={sqLevel} min={0} max={2} step={0.05} onChange={setSqLevel} />
            <span style={{ fontSize: '0.75rem', color: 'var(--fg-dim)', minWidth: 40 }}>
              {sqOn ? sqLevel.toFixed(2) : '—'}
            </span>
          </div>
        </Field>
        {audioPower !== null && (
          <Field label="Audio level">
            <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
              <div style={{ flex: 1, height: 8, background: 'rgba(255,255,255,0.07)', borderRadius: 4, overflow: 'hidden' }}>
                <div style={{
                  height: '100%', borderRadius: 4,
                  width: Math.max(0, (audioPower + 60) / 60 * 100) + '%',
                  background: audioPower > -3 ? '#f55' : audioPower > -12 ? '#fa0' : '#5b8',
                  transition: 'width 50ms linear, background 80ms',
                }} />
              </div>
              <span style={{ fontSize: '0.75rem', color: 'var(--fg-dim)', minWidth: 60, textAlign: 'right', fontFamily: 'monospace' }}>
                {audioPower > -99 ? audioPower.toFixed(1) + ' dBFS' : '−∞'}
              </span>
            </div>
          </Field>
        )}
      </Card>

      {rateChanged && (
        <div style={{
          marginTop: '0.75rem', padding: '0.4rem 0.6rem', borderRadius: 4,
          background: 'rgba(220,160,40,0.15)', color: '#fa0',
          fontSize: '0.78rem', fontFamily: 'monospace',
        }}>
          ⚠ Sample rate changed ({(activeRate/1e6).toFixed(3)} → {(mqttRate/1e6).toFixed(3)} Msps) — restart to apply
        </div>
      )}

      <div style={{ display: 'flex', gap: '0.5rem', marginTop: '0.75rem' }}>
        <button className="btn btn-primary" onClick={handlePlay} disabled={busy} style={{ flex: 1 }}>
          {status === 'starting' ? '…' : status === 'connecting' ? 'Connecting…' : '▶  Play'}
        </button>
        <button className="btn" onClick={() => cleanup()} disabled={!busy} style={{ flex: 1 }}>
          ■  Stop
        </button>
      </div>

      {(msg || playing) && (
        <div style={{
          marginTop: '0.5rem', padding: '0.4rem 0.6rem', borderRadius: 4,
          background: status === 'error' ? 'rgba(220,80,80,0.15)' : 'rgba(80,180,80,0.12)',
          color:      status === 'error' ? '#f88' : '#8d8',
          fontSize: '0.78rem', fontFamily: 'monospace',
        }}>
          {playing && !msg
            ? '▶  ' + mode + '  ·  ' + (listenFreq/1e6).toFixed(6) + ' MHz'
            : msg}
        </div>
      )}
    </div>
  );
}

Object.assign(window, { RadioPage });

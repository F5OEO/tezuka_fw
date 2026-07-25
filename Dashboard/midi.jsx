// midi.jsx — Hercules DJControl Compact → SDR control via Web MIDI
//
// Mapping verified against the manufacturer's "DJControl Compact MIDI
// Mapping Version 1.1" document (Note On = buttons, Control Change =
// jogs/faders/knobs). See doc comments below per control.
const { useState: useM, useEffect: useME, useRef: useMR } = React;

// ---- Verified hardware CC / note numbers -----------------------------------
const MIDI_CC = {
  JOG_DA: 0x30, JOG_DB: 0x31,           // relative encoders
  SH_JOG_DA: 0x37, SH_JOG_DB: 0x38,     // relative, fired instead of JOG_D* while SH held
  XFADER: 0x36,                          // absolute 0-127
  VOL_DA: 0x39, VOL_DB: 0x3D,           // absolute 0-127 (channel faders)
  MEDIUM_DA: 0x3B, BASS_DA: 0x3C,       // absolute 0-127, unbound in v1
  MEDIUM_DB: 0x3F, BASS_DB: 0x40,       // absolute 0-127, unbound in v1
};
const MIDI_NOTE = {
  PLAY_DA: 0x21, CUE_DA: 0x22, SYNC_DA: 0x23,
  PLAY_DB: 0x51, CUE_DB: 0x52, SYNC_DB: 0x53,
  CUE_KP1_DA: 0x01, CUE_KP2_DA: 0x02, CUE_KP3_DA: 0x03, CUE_KP4_DA: 0x04,
};

// Relative encoder decode: 1..0x3F = CW (positive), 0x7F..0x40 = CCW
// (negative, magnitude = 128-value). Matches the PDF's "Incremental" spec.
const decodeRelative = (raw) => (raw < 64 ? raw : raw - 128);

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const lerp = (v, lo, hi) => lo + (v / 127) * (hi - lo);

// ---- Controller hook --------------------------------------------------------
// Runs globally (mounted once in App()) so the physical controller keeps
// working no matter which Dashboard page/route is currently showing — the
// only thing it touches is d.publish(...), same as any other page.
function useMidiController(d, onCC, onNote) {
  // Optional per-page hooks: onCC fires for every Control Change message,
  // onNote for every Note On/Off message — both independent of the internal
  // dispatch table below (which is currently all disabled). Lets a page
  // bind a control to local state/behavior it owns — e.g. SpectrumPage's
  // REF level or fullscreen toggle, neither of which is published over MQTT.
  const onCCRef = useMR(onCC);
  onCCRef.current = onCC;
  const onNoteRef = useMR(onNote);
  onNoteRef.current = onNote;
  // `supported` = the API exists at all (feature detection only — never
  // flipped by a later failed access request, so it can't be confused with
  // "exists but permission was denied" / "no devices").
  const supported = typeof navigator !== 'undefined' && !!navigator.requestMIDIAccess;
  const [secure, setSecure] = useM(typeof isSecureContext !== 'undefined' ? isSecureContext : true);
  const [deviceName, setDeviceName] = useM(null);
  const [lastMessage, setLastMessage] = useM(null);
  const [accessError, setAccessError] = useM(null); // e.g. "NotAllowedError: ..." when requestMIDIAccess() rejects
  const dRef = useMR(d);
  dRef.current = d;

  // Sweep-mode RX frequency throttle — same 200ms-while-sweeping idea as
  // pages1.jsx's publishFreq, reimplemented locally since this hook has no
  // access to SpectrumPage's refs.
  const freqThrottleRef = useMR(0);
  const freqDebounceRef = useMR(0);
  const rxFreqRef = useMR(null);
  useME(() => { rxFreqRef.current = d.rxFreq; }, [d.rxFreq]);

  const publishRxFreq = (hz) => {
    rxFreqRef.current = hz;
    const dv = dRef.current;
    const topic = dv.sweepActive ? 'rx/sweep/frequency' : 'rx/frequency';
    // MQTT publishes disabled for now — the real controller floods CC
    // messages fast enough to flood MQTT even through the sweep throttle.
    // Re-enable once handling is scoped to the Spectrum page (see App()/
    // SpectrumPage) and rate-limiting is revisited.
    if (!dv.sweepActive) { /* dv.publish(topic, Math.round(hz)); */ return; }
    const now = performance.now();
    if (now - freqThrottleRef.current >= 200) {
      freqThrottleRef.current = now;
      /* dv.publish(topic, Math.round(hz)); */
    }
    clearTimeout(freqDebounceRef.current);
    freqDebounceRef.current = setTimeout(() => {
      /* dv.publish(topic, Math.round(rxFreqRef.current)); */
    }, 220);
  };

  // Paired output port (LED feedback etc.) — matched by device name, same
  // heuristic as the input. Kept in a ref only; no need to re-render on
  // change, callers just call sendNote() and it's a no-op if unavailable.
  const outputRef = useMR(null);

  const sendNote = (num, val) => {
    const out = outputRef.current;
    if (out) out.send([0x90, num, val]);
  };

  useME(() => {
    if (!supported) return;
    let access = null, input = null, output = null;
    let destroyed = false;

    const attach = (midi) => {
      access = midi;
      const inputs = Array.from(midi.inputs.values());
      input = inputs.find(i => /hercules|djcontrol/i.test(i.name || '')) || inputs[0] || null;
      if (input) { input.onmidimessage = onMessage; setDeviceName(input.name); }
      else setDeviceName(null);
      const outputs = Array.from(midi.outputs.values());
      output = outputs.find(o => /hercules|djcontrol/i.test(o.name || '')) || outputs[0] || null;
      outputRef.current = output;
      midi.onstatechange = () => {
        if (destroyed) return;
        const list = Array.from(midi.inputs.values());
        const found = list.find(i => /hercules|djcontrol/i.test(i.name || '')) || list[0] || null;
        if (found !== input) {
          if (input) input.onmidimessage = null;
          input = found;
          if (input) { input.onmidimessage = onMessage; setDeviceName(input.name); }
          else setDeviceName(null);
        }
        const outList = Array.from(midi.outputs.values());
        const outFound = outList.find(o => /hercules|djcontrol/i.test(o.name || '')) || outList[0] || null;
        if (outFound !== output) {
          output = outFound;
          outputRef.current = output;
        }
      };
    };

    const onMessage = (ev) => {
      const [status, num, val] = ev.data;
      const type = status >> 4; // 0x9 = Note On, 0xB = Control Change
      setLastMessage({ type, num, val, t: Date.now() });

      const dv = dRef.current;

      if (type === 0xB && onCCRef.current) onCCRef.current(num, val);

      if (type === 0xB) {
        switch (num) {
          case MIDI_CC.JOG_DA: {
            const step = decodeRelative(val) * 100; // ~100 Hz per unit at coarse speed
            publishRxFreq(clamp((rxFreqRef.current ?? dv.rxFreq ?? 437e6) + step, 47e6, 6e9));
            return;
          }
          case MIDI_CC.SH_JOG_DA: {
            const step = decodeRelative(val) * 10; // fine tune, 10x smaller
            publishRxFreq(clamp((rxFreqRef.current ?? dv.rxFreq ?? 437e6) + step, 47e6, 6e9));
            return;
          }
          case MIDI_CC.JOG_DB: {
            const step = decodeRelative(val) * 100;
            const hz = clamp((dv.txFreq ?? 437e6) + step, 47e6, 6e9);
            /* dv.publish('tx/frequency', Math.round(hz)); */ // disabled — flooding MQTT, see publishRxFreq comment
            return;
          }
          case MIDI_CC.SH_JOG_DB: {
            const step = decodeRelative(val) * 10;
            const hz = clamp((dv.txFreq ?? 437e6) + step, 47e6, 6e9);
            /* dv.publish('tx/frequency', Math.round(hz)); */ // disabled — flooding MQTT
            return;
          }
          case MIDI_CC.VOL_DA:
            /* dv.publish('rx/gain', Math.round(lerp(val, 0, 73))); */ // disabled — flooding MQTT
            return;
          case MIDI_CC.VOL_DB:
            /* dv.publish('tx/gain', +(lerp(val, -89.75, 0)).toFixed(2)); */ // disabled — flooding MQTT
            return;
          case MIDI_CC.XFADER: {
            // Quantize crossfader position into the same span ladder the
            // Spectrum page's wheel/pinch controls step through.
            const spans = window.NICE_SPANS;
            const idx = clamp(Math.floor((val / 128) * spans.length), 0, spans.length - 1);
            /* dv.publish('rx/span', Math.round(spans[idx])); */ // disabled — flooding MQTT
            return;
          }
          default:
            return;
        }
      }

      if (type === 0x9) {
        // "7F" pressed / "00" released — these are momentary buttons, the
        // device doesn't latch state itself (per the PDF, "Button-Toggling
        // Output" names the message category, not toggle-on-press behavior).
        const pressed = val >= 0x40;

        if (onNoteRef.current) onNoteRef.current(num, pressed);

        switch (num) {
          case MIDI_NOTE.PLAY_DA:
            // Momentary hold-to-transmit, like a walkie-talkie PTT.
            /* dv.publish('tx/mute', pressed ? '0' : '1'); */ // disabled — flooding MQTT
            return;
          case MIDI_NOTE.CUE_DA:
            // Toggle on the press edge only — flips current live state.
            /* if (pressed) dv.publish('rx/active', dv.rxActive ? '1' : '0'); */ // disabled — flooding MQTT
            return;
          case MIDI_NOTE.SYNC_DA:
            /* if (pressed) dv.publish('rx/sweep/activate', dv.sweepActive ? '0' : '1'); */ // disabled — flooding MQTT
            return;
          default:
            return;
        }
      }
    };

    navigator.requestMIDIAccess({ sysex: false }).then(
      (midi) => { if (!destroyed) attach(midi); },
      (err) => { if (!destroyed) setAccessError(`${err.name}: ${err.message}`); }
    );

    return () => {
      destroyed = true;
      if (input) input.onmidimessage = null;
      if (access) access.onstatechange = null;
      outputRef.current = null;
      clearTimeout(freqDebounceRef.current);
    };
  }, [supported]);

  useME(() => { setSecure(typeof isSecureContext !== 'undefined' ? isSecureContext : true); }, []);

  return { supported, secure, deviceName, lastMessage, accessError, sendNote };
}

// ---- Status page -------------------------------------------------------------
function MidiPage({ d }) {
  // Self-contained: this hook instance is only mounted while this page is
  // open, same as SpectrumPage's own instance — App() no longer runs one
  // globally (see App()'s comment in app.jsx).
  const m = useMidiController(d);
  const typeLabel = (t) => t === 0x9 ? 'Note On' : t === 0xB ? 'CC' : t === 0x8 ? 'Note Off' : `0x${t.toString(16)}`;

  return (
    <div className="page">
      <div className="datv-head">
        <div className="datv-title">
          <h1>MIDI controller</h1>
          <span className="datv-sub mono">Hercules DJControl Compact · JOG/VOL/XFADER/PLAY/CUE/SYNC → RX/TX control</span>
        </div>
      </div>

      <div className="grid-12">
        <Card title="Status" className="span-6">
          <div className="kv-grid">
            <div className="kv"><span>Web MIDI API</span><b className="mono">
              <Pill tone={m.supported ? 'ok' : 'warn'} dot>{m.supported ? 'Supported' : 'Not available in this browser'}</Pill>
            </b></div>
            <div className="kv"><span>Secure context</span><b className="mono">
              <Pill tone={m.secure ? 'ok' : 'warn'} dot>{m.secure ? 'Yes' : 'No — MIDI will not work'}</Pill>
            </b></div>
            <div className="kv"><span>Access</span><b className="mono">
              <Pill tone={m.accessError ? 'warn' : m.deviceName ? 'ok' : 'neutral'} dot>
                {m.accessError ? 'Error' : m.deviceName ? 'Granted' : 'Pending…'}
              </Pill>
            </b></div>
            <div className="kv"><span>Device</span><b className="mono">{m.deviceName || '—'}</b></div>
          </div>
          {!m.secure && (
            <div className="info-note" style={{ marginTop: 12 }}>
              <Icon name="bolt" size={16} />
              <p>
                Web MIDI needs a secure context (HTTPS, or <code>localhost</code>). Reached over plain
                <code> http://&lt;device-ip&gt;</code>, browsers block <code>navigator.requestMIDIAccess</code> entirely.
                Workarounds: an SSH tunnel to <code>localhost</code>, self-signed HTTPS in front of the dashboard,
                or (dev only) launching Chrome with <code>--unsafely-treat-insecure-origin-as-secure=http://&lt;device-ip&gt;</code>.
              </p>
            </div>
          )}
          {m.accessError && (
            <div className="info-note" style={{ marginTop: 12 }}>
              <Icon name="bolt" size={16} />
              <p>
                <code>navigator.requestMIDIAccess()</code> rejected: <b className="mono">{m.accessError}</b>.
                Usually a browser permission prompt was denied or dismissed — reload the page and allow MIDI
                access when asked.
              </p>
            </div>
          )}
          {m.supported && m.secure && !m.accessError && !m.deviceName && (
            <div className="info-note" style={{ marginTop: 12 }}>
              <Icon name="search" size={16} />
              <p>No matching MIDI input found. Make sure the DJControl Compact is plugged in and in MIDI mode, then reload.</p>
            </div>
          )}
        </Card>

        <Card title="Last message" sub="Live monitor — move a jog/fader or press a button" className="span-6">
          {m.lastMessage ? (
            <div className="kv-grid">
              <div className="kv"><span>Type</span><b className="mono">{typeLabel(m.lastMessage.type)}</b></div>
              <div className="kv"><span>Number</span><b className="mono">0x{m.lastMessage.num.toString(16).toUpperCase().padStart(2, '0')}</b></div>
              <div className="kv"><span>Value</span><b className="mono">0x{m.lastMessage.val.toString(16).toUpperCase().padStart(2, '0')} ({m.lastMessage.val})</b></div>
            </div>
          ) : (
            <div className="dim" style={{ padding: '12px 0', fontSize: 13 }}>No messages received yet.</div>
          )}
        </Card>

        <Card title="Bindings" sub="Fixed for v1 — edit MIDI_CC / MIDI_NOTE in midi.jsx to change" className="span-12" pad={false}>
          <table className="ver-table compact">
            <thead><tr><th>Control</th><th>Action</th><th>Topic</th></tr></thead>
            <tbody>
              {[
                ['JOG_DA', 'RX frequency (coarse)', 'cmd/rx/frequency'],
                ['SH+JOG_DA', 'RX frequency (fine)', 'cmd/rx/frequency'],
                ['JOG_DB', 'TX frequency (coarse)', 'cmd/tx/frequency'],
                ['SH+JOG_DB', 'TX frequency (fine)', 'cmd/tx/frequency'],
                ['VOL_DA', 'RX gain 0-73 dB', 'cmd/rx/gain'],
                ['VOL_DB', 'TX attenuation −89.75-0 dB', 'cmd/tx/gain'],
                ['XFADER', 'RX span (ladder step)', 'cmd/rx/span'],
                ['PLAY_DA', 'TX PTT — hold to transmit', 'cmd/tx/mute'],
                ['CUE_DA', 'RX active — toggle on press', 'cmd/rx/active'],
                ['SYNC_DA', 'RX sweep mode — toggle on press', 'cmd/rx/sweep/activate'],
              ].map(([n, a, t]) => (
                <tr key={n}><td className="vt-name mono">{n}</td><td>{a}</td><td className="dim mono">{t}</td></tr>
              ))}
            </tbody>
          </table>
        </Card>
      </div>
    </div>
  );
}

Object.assign(window, { useMidiController, MidiPage, MIDI_CC, MIDI_NOTE, decodeRelative });

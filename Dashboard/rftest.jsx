// rftest.jsx — RF Test Frequency presets for receiver characterization
//
// Lab/bench convenience page: quick-select known reference frequencies
// (cellular, ISM, GNSS, Wi-Fi/Bluetooth/drone links) that simply set the
// SDR's TX frequency (cmd/tx/frequency) — the same shared field the Signal
// Generator page already reads/writes. This page has no transmit logic of
// its own; starting/stopping actual RF output is controlled entirely by the
// Signal Generator page.
//
// Intended use: receiver sensitivity/selectivity characterization inside a
// shielded/anechoic enclosure, on a receiver you own. Presets stay disabled
// until a one-time acknowledgment is given (persisted in localStorage).
const { useState: useRfS } = React;

const RF_TEST_ACK_KEY = 'rftest_ack';
const RF_TEST_ADDED_KEY = 'rftest_added';

const RF_TEST_PRESETS = [
  {
    group: 'Cellular / mobile networks',
    items: [
      { label: '700 MHz', hz: 700e6 },
      { label: '800 MHz', hz: 800e6 },
      { label: '900 MHz', hz: 900e6 },
      { label: '1800 MHz', hz: 1800e6 },
      { label: '2100 MHz', hz: 2100e6 },
      { label: '2600 MHz', hz: 2600e6 },
      { label: '3.5 GHz (5G mid-band)', hz: 3500e6 },
    ],
  },
  {
    group: 'ISM / remote controls',
    items: [
      { label: '315 MHz', hz: 315e6 },
      { label: '433.92 MHz', hz: 433.92e6 },
      { label: '868 MHz (EU)', hz: 868e6 },
      { label: '915 MHz (US)', hz: 915e6 },
    ],
  },
  {
    group: 'GNSS',
    items: [
      { label: 'GPS L1 / Galileo E1 / GLONASS L1 — 1575.42 MHz', hz: 1575.42e6 },
      { label: 'GPS L2 — 1227.60 MHz', hz: 1227.60e6 },
      { label: 'GPS L5 / Galileo E5a — 1176.45 MHz', hz: 1176.45e6 },
    ],
  },
  {
    group: 'Wi-Fi / Bluetooth / drone links',
    items: [
      { label: 'Wi-Fi ch1 — 2412 MHz', hz: 2412e6 },
      { label: 'Wi-Fi ch6 — 2437 MHz', hz: 2437e6 },
      { label: 'Wi-Fi ch11 — 2462 MHz', hz: 2462e6 },
      { label: '5.8 GHz FPV/Wi-Fi — 5800 MHz', hz: 5800e6 },
    ],
  },
];

function RfTest({ d }) {
  const [ack, setAck] = useRfS(() => localStorage.getItem(RF_TEST_ACK_KEY) === '1');
  const [lastSet, setLastSet] = useRfS(null);
  // Presets are independent toggles, not a single-select group — any number
  // of them can be marked "added" (green square) at once, persisted across
  // reloads. Marking is just a visual checklist; it doesn't reflect which
  // frequency is actually tuned right now (only one can be, on real hardware).
  const [added, setAdded] = useRfS(() => {
    try { return JSON.parse(localStorage.getItem(RF_TEST_ADDED_KEY)) || []; } catch (_) { return []; }
  });

  const acknowledge = () => {
    localStorage.setItem(RF_TEST_ACK_KEY, '1');
    setAck(true);
  };

  // True toggle: clicking an unmarked preset publishes the frequency and
  // marks it; clicking it again just un-marks it (no re-publish — the SDR's
  // TX frequency stays wherever it last was, same as toggling any of the
  // other independent marks off doesn't retune anything).
  const togglePreset = (hz, label) => {
    if (!ack) return;
    setAdded((prev) => {
      const isAdded = prev.includes(hz);
      let next;
      if (isAdded) {
        next = prev.filter((v) => v !== hz);
      } else {
        d.publish('tx/frequency', Math.round(hz));
        setLastSet({ label, t: new Date() });
        next = [...prev, hz];
      }
      localStorage.setItem(RF_TEST_ADDED_KEY, JSON.stringify(next));
      return next;
    });
  };

  return (
    <div className="page">
      <div className="grid-12">
        <Card title="RF Test Frequencies" sub="Reference frequencies for receiver characterization — sets TX frequency only" className="span-12">
          <div className="info-note">
            <Icon name="bolt" size={16} />
            <p>
              For use only inside a shielded/anechoic enclosure, with a receiver you own. Selecting a preset sets the SDR's TX
              frequency (the same field the Signal Generator page uses) — actual transmission is started and stopped from the
              Signal Generator page itself, not here. Transmitting these frequencies outside a shielded environment may violate
              radio regulations in your jurisdiction.
            </p>
          </div>
          {!ack ? (
            <div style={{ marginTop: 12 }}>
              <Checkbox checked={ack} onChange={(v) => { if (v) acknowledge(); }}
                label="I acknowledge this will be used only in a shielded/anechoic environment, on a receiver I own" />
            </div>
          ) : (
            <div className="info-note" style={{ marginTop: 12 }}>
              <Icon name="check" size={16} />
              <p>
                Acknowledged — presets below are active.
                {lastSet && <> Last set: <b className="mono">{lastSet.label}</b> at {lastSet.t.toLocaleTimeString()}</>}
              </p>
            </div>
          )}
          <button className="btn ghost" disabled style={{ marginTop: 12 }}
            title="This page never transmits — start/stop TX from the Signal Generator page">
            TX
          </button>
        </Card>

        {RF_TEST_PRESETS.map((grp) => (
          <Card key={grp.group} title={grp.group} className="span-6">
            <div className={ack ? undefined : 'ftuner-disabled'} style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
              {grp.items.map((it) => (
                <button key={it.label} className="btn ghost" onClick={() => togglePreset(it.hz, it.label)}
                  title={added.includes(it.hz) ? 'Click to un-mark' : 'Click to set frequency and mark'}>
                  {added.includes(it.hz) && (
                    <span style={{ display: 'inline-block', width: 8, height: 8, marginRight: 6, background: 'var(--ok)', borderRadius: 2 }} />
                  )}
                  {it.label}
                </button>
              ))}
            </div>
          </Card>
        ))}

        <div className="span-12" style={{
          marginTop: 8, padding: '18px 22px', borderRadius: 'var(--r-sm)',
          background: 'color-mix(in oklab, var(--c-pink) 16%, var(--panel))',
          border: '1px solid var(--c-pink)', color: 'var(--c-pink)',
          fontSize: 15, fontWeight: 700, textAlign: 'center',
        }}>
          Not available : Only Authorized Personnel Are Allowed To Enter This Area
        </div>
      </div>
    </div>
  );
}

Object.assign(window, { RfTest });

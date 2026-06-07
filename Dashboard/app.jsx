// app.jsx — shell, routing, tweaks
const { useState: useA, useEffect: useAE } = React;

const VER = {
  model: "Tezuka SDR · Zynq-7020 / AD9363",
  serial: "104473ce6991-0006ecff2800",
  tezuka: "v2.4.1-g9a3f",
  linux: "5.15.0 #2 SMP PREEMPT",
  uboot: "v2023.01-Tezuka-0043",
  fpga: "2026_r1-gd7a2c1",
  rootfs: "v2.4-8405-g04dce",
  iio: "0.25 (git tag v0.25)",
};

const NAV = [
  { group: null, items: [["dashboard", "Dashboard", "dashboard"]] },
  { group: "RF", items: [["spectrum", "Spectrum", "spectrum"], ["arch", "Architecture", "chip"]] },
  { group: "Application", items: [["datv", "DATV Controller", "datv", [["analysis", "Analysis", "analysis"]]], ["transverter", "Transverter", "transverter"], ["iqtape", "IQ Tape", "tape"], ["siggen", "Signal generator", "wave"]] },
  { group: "System", items: [["versions", "Versions", "versions"], ["network", "Network", "network"], ["diagnostic", "Diagnostic", "pulse"], ["calibrate", "Calibrate", "target", [["kalibrate", "Kalibrate", "search"]]], ["reboot", "Reboot", "power"]] },
];

const TWEAK_DEFAULTS = /*EDITMODE-BEGIN*/{
  "accent": "#F5B301",
  "accent2": "#5BB1F5",
  "density": "regular",
  "labels": true,
  "monoReadout": true
}/*EDITMODE-END*/;

function Sidebar({ route, setRoute, collapsed, labels, operator }) {
  return (
    <aside className={`sidebar ${collapsed ? "collapsed" : ""}`}>
      <div className="brand">
        <div className="brand-mark"><Icon name="wave" size={20} /></div>
        {!collapsed && <div className="brand-text">Tezuka<span>SDR control · by F5OEO</span></div>}
      </div>
      <nav className="nav">
        {NAV.map((sec, i) => (
          <div key={i} className="nav-sec">
            {sec.group && !collapsed && <div className="nav-group">{sec.group}</div>}
            {sec.items.map((item) => {
              const [key, label, icon, children] = item;
              const childActive = children && children.some(([ck]) => ck === route);
              const open = children && !collapsed && (route === key || childActive);
              return (
                <React.Fragment key={key}>
                  <button className={`nav-item ${route === key ? "active" : ""}`} onClick={() => setRoute(key)} title={label}>
                    <Icon name={icon} size={20} />
                    {!collapsed && labels && <span>{label}</span>}
                    {!collapsed && labels && children && <span className={`nav-caret ${open ? "open" : ""}`}><Icon name="chevron" size={14} /></span>}
                    {route === key && <i className="nav-bar" />}
                  </button>
                  {open && (
                    <div className="nav-children">
                      {children.map(([ck, cl, ci]) => (
                        <button key={ck} className={`nav-item nav-child ${route === ck ? "active" : ""}`} onClick={() => setRoute(ck)} title={cl}>
                          <Icon name={ci} size={18} />
                          {labels && <span>{cl}</span>}
                          {route === ck && <i className="nav-bar" />}
                        </button>
                      ))}
                    </div>
                  )}
                </React.Fragment>
              );
            })}
          </div>
        ))}
      </nav>
      <div className="sb-user">
        <button className={`sb-userbtn ${route === "operator" ? "active" : ""}`} onClick={() => setRoute("operator")} title="Operator profile">
          <div className="sb-avatar"><Icon name="user" size={18} /></div>
          {!collapsed && <div className="sb-uinfo"><b>{operator.name}</b><span className="mono">{operator.callsign}</span></div>}
        </button>
        {!collapsed && <button className="sb-logout"><Icon name="power" size={16} /></button>}
      </div>
    </aside>
  );
}

const TITLES = { dashboard: "Dashboard", spectrum: "Spectrum", datv: "DATV Controller", transverter: "Transverter", iqtape: "IQ Tape", siggen: "Signal generator", calibrate: "Calibrate", analysis: "Analysis", arch: "Architecture", versions: "Versions & system", network: "Network", diagnostic: "Diagnostic", kalibrate: "Kalibrate from RF", reboot: "Reboot", operator: "Operator" };

function Topbar({ onMenu, route, mqtt }) {
  return (
    <header className="topbar">
      <button className="icon-btn" onClick={onMenu}><Icon name="menu" size={20} /></button>
      <div className="crumb"><span className="dim">Tezuka</span><span className="sep">/</span><b>{TITLES[route]}</b></div>
      <div className="search"><Icon name="search" size={16} /><input placeholder="Search settings, topics…" /></div>
      <div className="top-actions">
        <span className={`mqtt-chip ${mqtt ? "up" : ""}`}><i />MQTT</span>
        <button className="icon-btn"><Icon name="bell" size={18} /></button>
        <button className="icon-btn"><Icon name="grid" size={18} /></button>
        <button className="icon-btn"><Icon name="settings" size={18} /></button>
      </div>
    </header>
  );
}

function App() {
  const [t, setTweak] = useTweaks(TWEAK_DEFAULTS);
  const [route, setRoute] = useA(() => location.hash.replace("#", "") || "dashboard");
  const [collapsed, setCollapsed] = useA(false);
  const [operator, setOperator] = useA(() => {
    try { return JSON.parse(localStorage.getItem("tezuka.operator")) || null; } catch (e) { return null; }
  });
  const op = operator || { name: "Operator", callsign: "F5OEO", locator: "JN18cv" };
  const d = useLiveData(true);

  // Merge live MQTT version fields over the build-time defaults
  const ver = {
    ...VER,
    model:  d.hwModel          || VER.model,
    serial: d.serial           || VER.serial,
    tezuka: d.fwVersion        || VER.tezuka,
    linux:  d.linuxVersion     || VER.linux,
    uboot:  d.ubootVersion     || VER.uboot,
    rootfs: d.buildrootVersion || VER.rootfs,
    iio:    d.iioVersion       || VER.iio,
    fpga:   d.fpgaVersion      || VER.fpga,
  };

  useAE(() => { localStorage.setItem("tezuka.operator", JSON.stringify(op)); }, [op.name, op.callsign, op.locator]);

  useAE(() => { location.hash = route; }, [route]);
  useAE(() => {
    const r = document.documentElement;
    r.style.setProperty("--accent", t.accent);
    r.style.setProperty("--accent-2", t.accent2);
    r.dataset.density = t.density;
  }, [t.accent, t.accent2, t.density]);

  const page = () => {
    switch (route) {
      case "dashboard": return <Dashboard d={d} ver={ver} />;
      case "spectrum": return <SpectrumPage />;
      case "datv": return <DATV d={d} callsign={op.callsign} />;
      case "transverter": return <Transverter d={d} />;
      case "iqtape": return <IQTape d={d} />;
      case "siggen": return <SigGen d={d} />;
      case "calibrate": return <Calibrate d={d} />;
      case "analysis": return <Analysis d={d} />;
      case "arch": return <Architecture d={d} />;
      case "versions": return <Versions ver={ver} d={d} />;
      case "network": return <Network d={d} />;
      case "diagnostic": return <Diagnostic d={d} />;
      case "kalibrate": return <Kalibrate d={d} />;
      case "reboot": return <Reboot d={d} ver={ver} />;
      case "operator": return <Operator operator={op} onSave={setOperator} />;
      default: return <Dashboard d={d} ver={ver} />;
    }
  };

  return (
    <div className="app" data-density={t.density}>
      <Sidebar route={route} setRoute={(r) => setRoute(r)} collapsed={collapsed} labels={t.labels} operator={op} />
      <div className="main">
        <Topbar onMenu={() => setCollapsed((c) => !c)} route={route} mqtt={d.mqtt} />
        <div className="scroll" key={route}>{page()}</div>
      </div>

      <TweaksPanel>
        <TweakSection label="Accent" />
        <TweakColor label="Primary" value={t.accent} options={["#F5B301", "#FF7847", "#46D39A", "#5BB1F5", "#C68BFF"]} onChange={(v) => setTweak("accent", v)} />
        <TweakColor label="Secondary" value={t.accent2} options={["#5BB1F5", "#F5B301", "#46D39A", "#FF7847", "#C68BFF"]} onChange={(v) => setTweak("accent2", v)} />
        <TweakSection label="Layout" />
        <TweakRadio label="Density" value={t.density} options={["compact", "regular", "comfy"]} onChange={(v) => setTweak("density", v)} />
        <TweakToggle label="Sidebar labels" value={t.labels} onChange={(v) => setTweak("labels", v)} />
        <TweakToggle label="Mono readouts" value={t.monoReadout} onChange={(v) => setTweak("monoReadout", v)} />
      </TweaksPanel>
    </div>
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(<App />);

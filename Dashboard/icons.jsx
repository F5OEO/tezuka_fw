// icons.jsx — simple line icons for nav + UI
function Icon({ name, size = 20 }) {
  const p = { width: size, height: size, viewBox: "0 0 24 24", fill: "none", stroke: "currentColor", strokeWidth: 1.7, strokeLinecap: "round", strokeLinejoin: "round" };
  const paths = {
    dashboard: <><path d="M3 13a9 9 0 0 1 18 0" /><path d="M12 13l4-3" /><circle cx="12" cy="13" r="1.4" fill="currentColor" stroke="none" /></>,
    rf: <><path d="M5 12h2l2-6 4 12 2-6h4" /></>,
    spectrum: <><path d="M3 20V4" /><path d="M3 20h18" /><path d="M6 20v-4M10 20v-8M14 20v-12M18 20v-6" /></>,
    datv: <><path d="M4 8h13a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H4z" /><path d="M19 11l2-1.5v5L19 13" /><circle cx="8" cy="13" r="1.2" fill="currentColor" stroke="none" /></>,
    analysis: <><path d="M4 19V5" /><path d="M4 19h16" /><path d="M8 16l3-4 3 2 4-7" /></>,
    versions: <><circle cx="12" cy="12" r="8.5" /><path d="M12 8v4l2.5 2" /></>,
    network: <><rect x="3" y="3" width="7" height="7" rx="1" /><rect x="14" y="14" width="7" height="7" rx="1" /><path d="M6.5 10v3.5h11M17.5 14v-3" /></>,
    settings: <><circle cx="12" cy="12" r="3" /><path d="M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1" /></>,
    menu: <><path d="M4 7h16M4 12h16M4 17h16" /></>,
    search: <><circle cx="11" cy="11" r="7" /><path d="M21 21l-4-4" /></>,
    power: <><path d="M12 3v9" /><path d="M6.5 7a8 8 0 1 0 11 0" /></>,
    bell: <><path d="M6 9a6 6 0 0 1 12 0c0 5 2 6 2 6H4s2-1 2-6" /><path d="M10 20a2 2 0 0 0 4 0" /></>,
    grid: <><rect x="3" y="3" width="7" height="7" rx="1" /><rect x="14" y="3" width="7" height="7" rx="1" /><rect x="3" y="14" width="7" height="7" rx="1" /><rect x="14" y="14" width="7" height="7" rx="1" /></>,
    chip: <><rect x="6" y="6" width="12" height="12" rx="2" /><path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2" /></>,
    user: <><circle cx="12" cy="8" r="4" /><path d="M4 21a8 8 0 0 1 16 0" /></>,
    refresh: <><path d="M3 12a9 9 0 0 1 15-6.7L21 8" /><path d="M21 3v5h-5" /><path d="M21 12a9 9 0 0 1-15 6.7L3 16" /><path d="M3 21v-5h5" /></>,
    download: <><path d="M12 3v12" /><path d="M7 11l5 4 5-4" /><path d="M5 21h14" /></>,
    upload: <><path d="M12 21V9" /><path d="M7 13l5-4 5 4" /><path d="M5 3h14" /></>,
    check: <><path d="M5 12l5 5L20 6" /></>,
    wave: <><path d="M2 12c2 0 2-6 4-6s2 12 4 12 2-12 4-12 2 6 4 6" /></>,
    thermo: <><path d="M10 13.5V5a2 2 0 1 1 4 0v8.5a4 4 0 1 1-4 0z" /></>,
    bolt: <><path d="M13 2L4 14h6l-1 8 9-12h-6z" /></>,
    clock: <><circle cx="12" cy="12" r="8.5" /><path d="M12 7v5l3 2" /></>,
    chevron: <><path d="M9 6l6 6-6 6" /></>,
    transverter: <><path d="M7 21V5" /><path d="M4 8l3-3 3 3" /><path d="M17 3v16" /><path d="M14 16l3 3 3-3" /></>,
    tape: <><rect x="3" y="6" width="18" height="12" rx="2" /><circle cx="8.5" cy="12" r="2.2" /><circle cx="15.5" cy="12" r="2.2" /><path d="M7 18l1.6-2.6M17 18l-1.6-2.6" /></>,
    play: <><path d="M7 5l12 7-12 7z" /></>,
    repeat: <><path d="M17 2l4 4-4 4" /><path d="M3 11V9a4 4 0 014-4h14M7 22l-4-4 4-4" /><path d="M21 13v2a4 4 0 01-4 4H3" /></>,
    target: <><circle cx="12" cy="12" r="8.5" /><circle cx="12" cy="12" r="3.5" /><path d="M12 1.5v4M12 18.5v4M1.5 12h4M18.5 12h4" /></>,
    pulse: <><path d="M3 12h4l2.5-6 4 12 2.5-6h5" /></>,
    save: <><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z" /><polyline points="17 21 17 13 7 13 7 21" /><polyline points="7 3 7 8 15 8" /></>,
    circuit: <><rect x="7" y="7" width="10" height="10" rx="1.5" /><path d="M10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4" /></>,
  };
  return <svg {...p}>{paths[name] || null}</svg>;
}
window.Icon = Icon;

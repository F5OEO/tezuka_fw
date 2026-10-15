// qo100wb.js — live signals on the QO-100 wideband transponder, from the BATC spectrum monitor.
//
// https://eshail.batc.org.uk/wb/ publishes the transponder's spectrum on a WebSocket
// (wss://eshail.batc.org.uk/wb/fft, sub-protocol "fft"): about four frames a second, each an array of
// 16-bit power values (little endian), 922 bins across 10490.5 - 10499.5 MHz, about 3277 counts per dB,
// noise floor near 11000. The site finds the signals in the browser; so does this file: it reads the
// frames, finds the regions that stand out of the noise, measures their width and gives each a centre
// frequency and a symbol rate.
//
//   const mon = new Qo100Wb((signals, state) => { ... });   // called after every frame / state change
//   mon.start();  ...  mon.stop();          (a second argument receives every raw frame, to draw the spectrum)
//   Qo100Wb.detect(frame)  -> [{ freq, bw, sr, level, start, end }]      (pure, for tests)
//
// A signal: freq = centre (MHz, downlink), bw = measured width (MHz), sr = symbol rate in kS/s aligned to
// the QO-100 grid (33, 66, 125, 250, 333, 500, 1000, 1500, 2000), level = dB above the noise floor,
// start / end = the edges (MHz). The beacon (below 10492 MHz) is returned too, with beacon: true.
(function (root) {
  'use strict';

  const WS_URL = 'wss://eshail.batc.org.uk/wb/fft';
  const PROTOCOL = 'fft';
  const F0 = 10490.5, SPAN = 9.0;          // MHz: the frame covers F0 .. F0 + SPAN
  const NOISE = 11000;                      // counts: the usual noise floor of the feed
  const THRESHOLD = 16000;                  // counts: smoothed power above which something is a signal
  const COUNTS_PER_DB = 3276.8;             // the feed's scale: 20 dB over the 16-bit range
  const TOP_FRACTION = 0.75;                // the width is taken at 75 % of the way from the noise to the signal's top
  const BEACON_BELOW = 10492.0;             // MHz
  // measured width (kHz) -> symbol rate (kS/s). The width is read near the top of the spectrum, so it comes out
  // a little under the symbol rate: the limits sit between the widths each rate produces.
  const WIDTH_LIMITS = [22, 60, 86, 185, 277, 388, 700, 1200, 1600, 2200];
  const SYMBOL_RATES = [0, 33, 66, 125, 250, 333, 500, 1000, 1500, 2000];
  const RETRY_MS = [2000, 5000, 10000, 30000];

  function alignSymbolRate(widthMHz) {
    const k = widthMHz * 1000;
    for (let i = 0; i < WIDTH_LIMITS.length; i++) if (k < WIDTH_LIMITS[i]) return SYMBOL_RATES[i];
    return Math.round(k / 200) * 200;   // wider than the grid: report it rounded
  }

  // frame: Uint16Array of power values across the band
  function detect(frame) {
    const n = frame.length, binMHz = SPAN / n, out = [];
    if (n < 16) return out;
    const avg = (i) => (frame[i] + frame[i - 1] + frame[i - 2]) / 3;     // 3-bin moving average
    let i = 2;
    while (i < n) {
      if (avg(i) <= THRESHOLD) { i++; continue; }
      // a region above the threshold: [a, b)
      const a = i - 2;
      while (i < n && avg(i) > THRESHOLD) i++;
      const b = i;
      if (b - a < 3) continue;
      // the top of the signal: the mean of its middle 40 %
      const lo = a + Math.floor((b - a) * 0.3), hi = Math.max(lo + 1, a + Math.floor((b - a) * 0.7));
      let sum = 0;
      for (let j = lo; j < hi; j++) sum += frame[j];
      const top = sum / (hi - lo);
      // edges where the power reaches 75 % of the way from the noise to the top
      const edge = NOISE + TOP_FRACTION * (top - NOISE);
      let s = a, e = b - 1;
      while (s < e && frame[s] < edge) s++;
      while (e > s && frame[e] < edge) e--;
      const bw = (e - s + 1) * binMHz;
      const sr = alignSymbolRate(bw);
      if (sr === 0) continue;
      const centre = F0 + ((s + e + 1) / 2) * binMHz;
      out.push({ freq: centre, bw, sr, level: (top - NOISE) / COUNTS_PER_DB, start: F0 + s * binMHz, end: F0 + (e + 1) * binMHz, beacon: centre < BEACON_BELOW });
    }
    return out;
  }

  class Qo100Wb {
    constructor(onUpdate, onFrame) {
      this.onUpdate = onUpdate || function () {};
      this.onFrame = onFrame || null;     // called with each raw frame (Uint16Array) before the signals are found
      this.ws = null; this.timer = null; this.attempt = 0; this.wanted = false;
      this.state = 'idle';            // idle | connecting | live | offline
      this.signals = []; this.frames = 0;
    }
    _set(state) { if (state !== this.state) { this.state = state; this.onUpdate(this.signals, state); } }
    start() { this.wanted = true; this._open(); }
    stop() {
      this.wanted = false; clearTimeout(this.timer);
      const w = this.ws; this.ws = null;
      if (w) { w.onopen = w.onmessage = w.onclose = w.onerror = null; try { w.close(); } catch (_) { /* already closed */ } }
      this.signals = []; this._set('idle');
    }
    _open() {
      if (!this.wanted || this.ws) return;
      this._set('connecting');
      let w;
      try { w = new WebSocket(WS_URL, PROTOCOL); } catch (e) { this._retry(); return; }
      w.binaryType = 'arraybuffer';
      this.ws = w;
      w.onopen = () => { this.attempt = 0; };
      w.onmessage = (e) => {
        if (!(e.data instanceof ArrayBuffer) || e.data.byteLength < 32) return;
        this.frames++;
        const frame = new Uint16Array(e.data);
        if (this.onFrame) this.onFrame(frame);
        this.signals = detect(frame);
        if (this.state !== 'live') this.state = 'live';
        this.onUpdate(this.signals, this.state);
      };
      w.onclose = w.onerror = () => { if (this.ws === w) { this.ws = null; this._retry(); } };
    }
    _retry() {
      if (!this.wanted) return;
      this.signals = []; this._set('offline');
      clearTimeout(this.timer);
      this.timer = setTimeout(() => this._open(), RETRY_MS[Math.min(this.attempt++, RETRY_MS.length - 1)]);
    }
  }
  Qo100Wb.detect = detect;
  Qo100Wb.alignSymbolRate = alignSymbolRate;

  root.Qo100Wb = Qo100Wb;
  if (typeof module !== 'undefined' && module.exports) module.exports = Qo100Wb;
})(typeof window !== 'undefined' ? window : globalThis);

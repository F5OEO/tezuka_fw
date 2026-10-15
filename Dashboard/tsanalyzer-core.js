// tsanalyzer-core.js — MPEG-2 transport stream analyzer, plain JavaScript, no dependencies.
//
// Feed it the bytes of a transport stream as they arrive (any chunking; it finds the
// 188-byte packets itself) and read snapshot() once a second:
//   const an = new TsAnalyzer();  an.push(uint8array, Date.now());  an.tick(Date.now());  an.snapshot();
//
// What it measures (the measurements of a DATV analyzer):
//   - PAT / PMT: programme, PCR PID, the elementary streams and their codec type;
//   - video / audio parameters from the stream itself: H.264 / HEVC / MPEG-2 sequence header
//     (profile, size, frame rate), AAC-ADTS / MPEG audio / AC-3 header;
//   - bitrates over 1 s windows (TS, video, audio, null), PID shares since the start;
//   - PCR: interval distribution and accuracy;
//   - PTS - PCR of the video and the audio (the decoder buffer delay the multiplexer chose);
//   - continuity counter errors, transport error indicators, lost sync.
//
// PCR accuracy is measured on the packet position, not on the arrival time (which a network adds
// jitter to): a straight line is fitted to PCR against packet number over the last 64 PCRs (the
// stream's own average bitrate) and each PCR is compared with it, two seconds late, in the middle of
// the window. A multiplexer that keeps its bitrate constant gets +-1 PCR tick (1/27 MHz = 37 ns),
// which is also the resolution; ISO 13818-1 allows +-500 ns. A stream sent in bursts (frames written
// as they come, no padding to a constant rate) fails this test: its PCRs do not match the packet
// positions, whatever their spacing in time.
(function (root) {
  'use strict';

  const TS_SIZE = 188;
  const PID_NULL = 0x1FFF, PID_FAKE_NULL = 0x1FFE;   // 0x1FFE: the multiplexer's own filler, counted apart
  const MAX_POINTS = 600;                            // samples kept for each chart
  const PCR_ERR_WINDOW = 500;                        // PCR accuracy statistics over the last samples
  const PCR_INT_KEEP = 2000;                         // PCR intervals kept for the distribution
const PCR_FIT = 64;                                // PCRs in the straight-line fit of the accuracy measurement
  const WIN_MS = 1000;                               // bitrate window
  const TWO33 = 8589934592;                          // 2^33, the PTS / PCR base wrap

  const STREAM_TYPES = {
    0x01: ['video', 'MPEG-1 video'], 0x02: ['video', 'MPEG-2 video'], 0x10: ['video', 'MPEG-4 video'],
    0x1B: ['video', 'H.264'], 0x24: ['video', 'HEVC'], 0x42: ['video', 'AVS'],
    0x03: ['audio', 'MPEG-1 audio'], 0x04: ['audio', 'MPEG-2 audio'], 0x0F: ['audio', 'AAC (ADTS)'], 0x11: ['audio', 'AAC (LATM)'],
    0x81: ['audio', 'AC-3'], 0x87: ['audio', 'E-AC-3'], 0x06: ['other', 'PES private'], 0x05: ['other', 'Private sections'],
    0x15: ['other', 'Metadata'], 0x86: ['other', 'SCTE-35'],
  };
  const AAC_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350];
  const AAC_OBJECT = ['', 'Main', 'LC', 'SSR', 'LTP'];
  const MPEG2_FPS = [0, 24000 / 1001, 24, 25, 30000 / 1001, 30, 50, 60000 / 1001, 60];
  const H264_PROFILE = { 66: 'Baseline', 77: 'Main', 88: 'Extended', 100: 'High', 110: 'High 10', 122: 'High 4:2:2', 244: 'High 4:4:4', 44: 'CAVLC 4:4:4' };
  const HEVC_PROFILE = { 1: 'Main', 2: 'Main 10', 3: 'Main Still Picture', 4: 'RExt', 9: 'SCC' };

  // ---- bit reader on RBSP bytes (emulation prevention removed) ------------------------------
  class Bits {
    constructor(bytes) { this.b = bytes; this.p = 0; }
    u(n) { let v = 0; for (let i = 0; i < n; i++) { v = v * 2 + this.bit(); } return v; }
    bit() { const i = this.p >> 3; if (i >= this.b.length) throw new Error('eof'); const v = (this.b[i] >> (7 - (this.p & 7))) & 1; this.p++; return v; }
    ue() { let z = 0; while (this.bit() === 0) { if (++z > 32) throw new Error('ue'); } return z === 0 ? 0 : (Math.pow(2, z) - 1 + this.u(z)); }
    se() { const k = this.ue(); return (k & 1) ? (k + 1) / 2 : -(k / 2); }
    skip(n) { this.p += n; }
  }
  const unescape = (a) => {
    const o = []; let z = 0;
    for (let i = 0; i < a.length; i++) {
      if (z >= 2 && a[i] === 3) { z = 0; continue; }
      o.push(a[i]); z = a[i] === 0 ? z + 1 : 0;
    }
    return Uint8Array.from(o);
  };

  // HEVC SPS (nal bytes without start code, 2-byte NAL header included): profile, level, size
  function parseHevcSps(nal) {
    const r = new Bits(unescape(nal)); r.skip(16);
    r.skip(4); const maxSub = r.u(3); r.skip(1);
    r.skip(2); r.skip(1); const profile = r.u(5);
    r.skip(32); r.skip(48); const level = r.u(8);
    const subProf = [], subLev = [];
    for (let i = 0; i < maxSub; i++) { subProf.push(r.u(1)); subLev.push(r.u(1)); }
    if (maxSub > 0) for (let i = maxSub; i < 8; i++) r.skip(2);
    for (let i = 0; i < maxSub; i++) { if (subProf[i]) r.skip(88); if (subLev[i]) r.skip(8); }
    r.ue(); const chroma = r.ue(); if (chroma === 3) r.skip(1);
    let w = r.ue(), h = r.ue();
    if (r.u(1)) {   // conformance window
      const l = r.ue(), rt = r.ue(), t = r.ue(), b = r.ue();
      const sx = (chroma === 1 || chroma === 2) ? 2 : 1, sy = chroma === 1 ? 2 : 1;
      w -= sx * (l + rt); h -= sy * (t + b);
    }
    return { codec: 'HEVC', profile: HEVC_PROFILE[profile] || 'profile ' + profile, level: (level / 30).toFixed(1).replace(/\.0$/, ''), width: w, height: h };
  }

  // H.264 SPS (nal bytes without start code, 1-byte NAL header included)
  function parseH264Sps(nal) {
    const r = new Bits(unescape(nal)); r.skip(8);
    const profile = r.u(8); r.skip(8); const level = r.u(8);
    r.ue();
    let chroma = 1;
    if ([100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135].includes(profile)) {
      chroma = r.ue(); if (chroma === 3) r.skip(1);
      r.ue(); r.ue(); r.skip(1);
      if (r.u(1)) { const n = chroma !== 3 ? 8 : 12; for (let i = 0; i < n; i++) { if (r.u(1)) { let last = 8, next = 8; const sz = i < 6 ? 16 : 64; for (let j = 0; j < sz; j++) { if (next !== 0) { next = (last + r.se() + 256) % 256; } last = next === 0 ? last : next; } } } }
    }
    r.ue();
    const poc = r.ue();
    if (poc === 0) r.ue();
    else if (poc === 1) { r.skip(1); r.se(); r.se(); const n = r.ue(); for (let i = 0; i < n; i++) r.se(); }
    r.ue(); r.skip(1);
    const mbW = r.ue() + 1, mbH = r.ue() + 1;
    const frameMbsOnly = r.u(1);
    if (!frameMbsOnly) r.skip(1);
    r.skip(1);
    let w = mbW * 16, h = mbH * 16 * (2 - frameMbsOnly);
    if (r.u(1)) {
      const l = r.ue(), rt = r.ue(), t = r.ue(), b = r.ue();
      const cx = chroma === 0 ? 1 : (chroma === 3 ? 1 : 2), cy = (chroma === 0 ? 1 : (chroma === 1 ? 2 : 1)) * (2 - frameMbsOnly);
      w -= cx * (l + rt); h -= cy * (t + b);
    }
    return { codec: 'H.264', profile: H264_PROFILE[profile] || 'profile ' + profile, level: (level / 10).toFixed(1).replace(/\.0$/, ''), width: w, height: h };
  }

  class TsAnalyzer {
    constructor() { this.reset(); }

    reset() {
      this.rest = new Uint8Array(0);
      this.n = 0;                       // packets processed
      this.bytes = 0;
      this.syncLost = 0; this.tei = 0; this.ccErrors = 0;
      this.pidCount = new Uint32Array(8192);
      this.cc = new Int16Array(8192).fill(-1);
      this.pat = null; this.pmtPids = new Set(); this.pmt = null;
      this.pcrPid = -1; this.streams = {};            // pid -> {type, kind, name}
      this.videoPid = -1; this.audioPid = -1;
      this.sections = {};                              // pid -> partial PSI section
      this.vinfo = null; this.ainfo = null;
      this.scan = { z: 0, hdrNext: false, collect: null };
      this.pcrPrev = null; this.pcrPrev2 = null;       // {n, v}: packet index and PCR (27 MHz)
      this.pcrFit = []; this.pcrOff = 0; this.pcrRaw = -1; this.pcrSlope = 0;   // window for the line fit, unwrapped PCRs
      this.pcrIntervals = []; this.pcrAcc = []; this.pcrCount = 0;
      this.ptsVideo = []; this.ptsAudio = [];
      this.rates = [];
      this.win = null; this.cur = { t0: 0, ts: 0, video: 0, audio: 0, nul: 0, pkts: 0 };
      this.vPts = [];                                  // PTS of the last video PES (for the frame rate)
      this.startMs = 0;
    }

    // ---- input ---------------------------------------------------------------------------
    push(chunk, nowMs) {
      nowMs = nowMs || Date.now();
      if (!this.startMs) this.startMs = nowMs;
      let d = chunk;
      if (this.rest.length) { d = new Uint8Array(this.rest.length + chunk.length); d.set(this.rest); d.set(chunk, this.rest.length); }
      let i = 0;
      const len = d.length;
      while (len - i >= TS_SIZE) {
        if (d[i] !== 0x47) {   // lost sync: look for a 0x47 that repeats one packet later
          let j = i + 1;
          while (j < len && !(d[j] === 0x47 && (j + TS_SIZE >= len || d[j + TS_SIZE] === 0x47))) j++;
          this.syncLost++; i = j; continue;
        }
        this._packet(d, i, nowMs);
        i += TS_SIZE;
      }
      this.rest = i < len ? d.slice(i) : new Uint8Array(0);
      this._window(nowMs);
    }

    // closes the bitrate windows even when no data arrives
    tick(nowMs) { this._window(nowMs || Date.now()); }

    _window(now) {
      const c = this.cur;
      if (!c.t0) { c.t0 = now; return; }
      const dt = now - c.t0;
      if (dt < WIN_MS) return;
      const s = dt / 1000;
      this.rates.push({ t: now, ts: c.ts * 8 / 1000 / s, video: c.video * 8 / 1000 / s, audio: c.audio * 8 / 1000 / s, nul: c.pkts ? c.nul / c.pkts * 100 : 0 });
      if (this.rates.length > MAX_POINTS) this.rates.shift();
      this.cur = { t0: now, ts: 0, video: 0, audio: 0, nul: 0, pkts: 0 };
    }

    // ---- one 188-byte packet --------------------------------------------------------------
    _packet(d, o, now) {
      const n = this.n++;
      this.bytes += TS_SIZE;
      const b1 = d[o + 1], b2 = d[o + 2], b3 = d[o + 3];
      const pid = ((b1 & 0x1F) << 8) | b2;
      const pusi = (b1 & 0x40) !== 0;
      const afc = (b3 >> 4) & 3, ccv = b3 & 15;
      this.pidCount[pid]++;
      const c = this.cur;
      c.ts += TS_SIZE; c.pkts++;
      if (pid === PID_NULL || pid === PID_FAKE_NULL) { c.nul++; return; }
      if (b1 & 0x80) this.tei++;
      let pl = o + 4;
      let discont = false;
      if (afc & 2) {
        const afLen = d[o + 4];
        if (afLen > 0 && o + 5 < o + TS_SIZE) {
          const fl = d[o + 5];
          discont = (fl & 0x80) !== 0;
          if ((fl & 0x10) && afLen >= 7 && pid === this._pcrCandidate(pid)) {
            const base = d[o + 6] * 33554432 + d[o + 7] * 131072 + d[o + 8] * 512 + d[o + 9] * 2 + (d[o + 10] >> 7);
            const ext = ((d[o + 10] & 1) << 8) | d[o + 11];
            this._pcr(n, base * 300 + ext, now, discont);
          }
        }
        pl = o + 5 + afLen;
      }
      // continuity (only packets with a payload advance the counter)
      const has = (afc & 1) !== 0;
      const last = this.cc[pid];
      if (last >= 0 && !discont) {
        if (has) { if (ccv !== ((last + 1) & 15) && ccv !== last) this.ccErrors++; else if (ccv === last && this._dup === pid) this.ccErrors++; }
        else if (ccv !== last) this.ccErrors++;
      }
      this._dup = (has && ccv === last) ? pid : -1;
      this.cc[pid] = ccv;
      if (!has || pl >= o + TS_SIZE) { if (pid === this.videoPid) c.video += TS_SIZE; else if (pid === this.audioPid) c.audio += TS_SIZE; return; }
      if (pid === this.videoPid) c.video += TS_SIZE; else if (pid === this.audioPid) c.audio += TS_SIZE;

      const end = o + TS_SIZE;
      if (pid === 0 || this.pmtPids.has(pid)) { this._psi(pid, d, pl, end, pusi); return; }
      if (pid === this.videoPid || pid === this.audioPid) this._pes(pid, d, pl, end, pusi, n, now);
    }

    _pcrCandidate(pid) {
      if (this.pcrPid >= 0) return this.pcrPid;
      return pid;                              // before the PMT is known: any PID with a PCR
    }

    // ---- PSI -------------------------------------------------------------------------------
    _psi(pid, d, p, end, pusi) {
      let s = this.sections[pid];
      if (pusi) {
        const ptr = d[p]; p += 1 + ptr;
        s = this.sections[pid] = { buf: [], need: -1 };
      }
      if (!s) return;
      for (let i = p; i < end; i++) s.buf.push(d[i]);
      if (s.need < 0 && s.buf.length >= 3) s.need = 3 + (((s.buf[1] & 0x0F) << 8) | s.buf[2]);
      if (s.need >= 0 && s.buf.length >= s.need) {
        const t = s.buf.slice(0, s.need); this.sections[pid] = null;
        if (pid === 0 && t[0] === 0x00) this._pat(t); else if (t[0] === 0x02) this._pmt(t);
      }
    }
    _pat(t) {
      const set = new Set(); let first = -1;
      for (let i = 8; i + 4 <= t.length - 4; i += 4) {
        const prog = (t[i] << 8) | t[i + 1], pid = ((t[i + 2] & 0x1F) << 8) | t[i + 3];
        if (prog !== 0) { set.add(pid); if (first < 0) first = pid; }
      }
      this.pmtPids = set; this.pat = { pmtPid: first };
    }
    _pmt(t) {
      const pcrPid = ((t[8] & 0x1F) << 8) | t[9];
      const piLen = ((t[10] & 0x0F) << 8) | t[11];
      let i = 12 + piLen;
      const streams = {};
      while (i + 5 <= t.length - 4) {
        const type = t[i], pid = ((t[i + 1] & 0x1F) << 8) | t[i + 2], esLen = ((t[i + 3] & 0x0F) << 8) | t[i + 4];
        let kind = (STREAM_TYPES[type] || ['other', '0x' + type.toString(16)])[0], name = (STREAM_TYPES[type] || [0, '0x' + type.toString(16)])[1];
        if (type === 0x06) {   // private PES: the descriptors say what it is
          for (let j = i + 5; j + 2 <= i + 5 + esLen;) {
            const tag = t[j], l = t[j + 1];
            if (tag === 0x6A) { kind = 'audio'; name = 'AC-3'; } else if (tag === 0x7A) { kind = 'audio'; name = 'E-AC-3'; } else if (tag === 0x56) { name = 'Teletext'; } else if (tag === 0x59) { name = 'DVB subtitles'; }
            j += 2 + l;
          }
        }
        streams[pid] = { type, kind, name };
        i += 5 + esLen;
      }
      this.pmt = { pcrPid, streams };
      this.pcrPid = pcrPid; this.streams = streams;
      for (const pid in streams) {
        if (streams[pid].kind === 'video' && this.videoPid < 0) this.videoPid = +pid;
        if (streams[pid].kind === 'audio' && this.audioPid < 0) this.audioPid = +pid;
      }
    }

    // ---- PCR -------------------------------------------------------------------------------
    _pcr(n, v, now, discont) {
      this.pcrCount++;
      const p1 = this.pcrPrev;
      if (discont) { this.pcrPrev = { n, v }; this.pcrPrev2 = null; this.pcrFit = []; this.pcrRaw = -1; return; }
      if (p1) {
        let dv = v - p1.v; if (dv < 0) dv += TWO33 * 300;       // PCR wrap
        this.pcrIntervals.push(dv / 27000);
        if (this.pcrIntervals.length > PCR_INT_KEEP) this.pcrIntervals.shift();
      }
      // unwrapped PCR for the line fit
      if (this.pcrRaw >= 0 && v < this.pcrRaw - TWO33 * 150) this.pcrOff += TWO33 * 300;
      this.pcrRaw = v;
      const f = this.pcrFit;
      f.push({ n, v: v + this.pcrOff, t: now });
      if (f.length > PCR_FIT) f.shift();
      if (f.length === PCR_FIT) {
        const n0 = f[0].n, v0 = f[0].v;
        let sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const q of f) { const x = q.n - n0, y = q.v - v0; sx += x; sy += y; sxx += x * x; sxy += x * y; }
        const k = f.length, den = k * sxx - sx * sx;
        if (den > 0) {
          const b = (k * sxy - sx * sy) / den, a = (sy - b * sx) / k;
          this.pcrSlope = b;
          const mid = f[k >> 1];
          const res = (mid.v - v0) - (a + b * (mid.n - n0));      // PCR ticks
          this.pcrAcc.push({ t: mid.t, v: res * 1000 / 27 });     // ns
          if (this.pcrAcc.length > MAX_POINTS) this.pcrAcc.shift();
        }
      }
      this.pcrPrev2 = p1; this.pcrPrev = { n, v };
    }

    // PCR (27 MHz) at packet n, extrapolated from the last two PCRs; null before there are two
    _pcrAt(n) {
      const p1 = this.pcrPrev, p2 = this.pcrPrev2;
      if (!p1) return null;
      if (this.pcrSlope > 0) return p1.v + (n - p1.n) * this.pcrSlope;
      if (!p2 || p1.n === p2.n) return null;
      let dprev = p1.v - p2.v; if (dprev < 0) dprev += TWO33 * 300;
      return p1.v + (n - p1.n) * dprev / (p1.n - p2.n);
    }

    // ---- PES -------------------------------------------------------------------------------
    _pes(pid, d, p, end, pusi, n, now) {
      const isVideo = pid === this.videoPid;
      if (pusi && d[p] === 0 && d[p + 1] === 0 && d[p + 2] === 1) {
        if (isVideo) { this.scan.z = 0; this.scan.collect = null; this.scan.hdrNext = false; }
        const flags = d[p + 7], hl = d[p + 8];
        if (flags & 0x80) {
          const pts = (((d[p + 9] >> 1) & 7) * 1073741824) + d[p + 10] * 4194304 + (d[p + 11] >> 1) * 32768 + d[p + 12] * 128 + (d[p + 13] >> 1);
          if (isVideo) { this.vPts.push(pts); if (this.vPts.length > 200) this.vPts.shift(); }
          const pcr = this._pcrAt(n);
          if (pcr !== null) {
            let diff = pts - Math.floor(pcr / 300) % TWO33;
            if (diff > TWO33 / 2) diff -= TWO33; else if (diff < -TWO33 / 2) diff += TWO33;
            const arr = isVideo ? this.ptsVideo : this.ptsAudio;
            arr.push({ t: now, v: diff / 90 });
            if (arr.length > MAX_POINTS) arr.shift();
          }
        }
        p += 9 + hl;
        if (!isVideo && !this.ainfo) this._audioHeader(d, p, end);
      }
      if (isVideo && !this.vinfo && p < end) this._scanVideo(d, p, end);
    }

    // ---- video parameters from the elementary stream ----------------------------------------
    _scanVideo(d, p, end) {
      const st = this.scan, type = this.streams[this.videoPid] ? this.streams[this.videoPid].type : 0;
      for (let i = p; i < end; i++) {
        const b = d[i];
        if (st.hdrNext) {
          st.hdrNext = false;
          let want = false;
          if (type === 0x24) want = ((b >> 1) & 0x3F) === 33;
          else if (type === 0x1B) want = (b & 0x1F) === 7;
          else if (type === 0x02 || type === 0x01) want = b === 0xB3;
          if (want) st.collect = [b];
          st.z = 0; continue;
        }
        if (b === 0) { st.z++; if (st.collect) st.collect.push(b); continue; }
        if (b === 1 && st.z >= 2) {
          if (st.collect) { st.collect.length -= Math.min(st.z, st.collect.length); this._spsDone(type, st.collect); st.collect = null; if (this.vinfo) return; }
          st.hdrNext = true; st.z = 0; continue;
        }
        if (st.collect) { st.collect.push(b); if (st.collect.length > 400) { this._spsDone(type, st.collect); st.collect = null; if (this.vinfo) return; } }
        st.z = 0;
      }
    }
    _spsDone(type, bytes) {
      try {
        const a = Uint8Array.from(bytes);
        if (type === 0x24) this.vinfo = parseHevcSps(a);
        else if (type === 0x1B) this.vinfo = parseH264Sps(a);
        else if (a.length >= 8) {
          const w = (a[1] << 4) | (a[2] >> 4), h = ((a[2] & 15) << 8) | a[3], fr = a[4] & 15;
          this.vinfo = { codec: 'MPEG-2', profile: '', level: '', width: w, height: h, fps: MPEG2_FPS[fr] || 0 };
        }
      } catch (e) { /* a truncated SPS: the next one will do */ }
    }

    _audioHeader(d, p, end) {
      if (p + 7 > end) return;
      const t = this.streams[this.audioPid] ? this.streams[this.audioPid].type : 0;
      if ((t === 0x0F || t === 0x11) && d[p] === 0xFF && (d[p + 1] & 0xF6) === 0xF0) {
        const obj = ((d[p + 2] >> 6) & 3) + 1, sfi = (d[p + 2] >> 2) & 15, ch = ((d[p + 2] & 1) << 2) | (d[p + 3] >> 6);
        this.ainfo = { codec: 'AAC ' + (AAC_OBJECT[obj] || obj), rate: AAC_RATES[sfi] || 0, channels: ch === 7 ? 8 : ch };
      } else if ((t === 0x03 || t === 0x04) && d[p] === 0xFF && (d[p + 1] & 0xE0) === 0xE0) {
        const layer = 4 - ((d[p + 1] >> 1) & 3), sfi = (d[p + 2] >> 2) & 3, mode = d[p + 3] >> 6;
        const rates = [44100, 48000, 32000];
        this.ainfo = { codec: 'MPEG audio layer ' + layer, rate: rates[sfi] || 0, channels: mode === 3 ? 1 : 2 };
      } else if ((t === 0x81 || t === 0x06) && d[p] === 0x0B && d[p + 1] === 0x77) {
        const fs = d[p + 4] >> 6, acmod = (d[p + 6] >> 5) & 7;
        this.ainfo = { codec: 'AC-3', rate: [48000, 44100, 32000][fs] || 0, channels: [2, 1, 2, 3, 3, 4, 4, 5][acmod] || 0 };
      }
    }

    // ---- results ---------------------------------------------------------------------------
    snapshot() {
      const total = Math.max(1, this.n);
      const pc = (pid) => pid >= 0 ? this.pidCount[pid] : 0;
      const nul = this.pidCount[PID_NULL], fake = this.pidCount[PID_FAKE_NULL];
      const v = pc(this.videoPid), a = pc(this.audioPid);
      const pct = (x) => x / total * 100;
      const last5 = this.rates.slice(-5);
      const avg = (k) => last5.length ? last5.reduce((s, r) => s + r[k], 0) / last5.length : 0;
      // frame rate: the SPS VUI is not parsed, so take the median spacing of the video PTS (sorted: B-frames reorder them)
      let fps = this.vinfo && this.vinfo.fps ? this.vinfo.fps : 0;
      if (!fps && this.vPts.length > 8) {
        const t = this.vPts.slice().sort((x, y) => x - y), dl = [];
        for (let i = 1; i < t.length; i++) if (t[i] > t[i - 1]) dl.push(t[i] - t[i - 1]);
        dl.sort((x, y) => x - y);
        if (dl.length) fps = 90000 / dl[dl.length >> 1];
      }
      const acc = this.pcrAcc.slice(-PCR_ERR_WINDOW).map((x) => Math.abs(x.v));
      const lastAcc = this.pcrAcc.length ? Math.abs(this.pcrAcc[this.pcrAcc.length - 1].v) : null;
      const pids = [];
      for (let pid = 0; pid < 8192; pid++) {
        if (!this.pidCount[pid]) continue;
        const s = this.streams[pid];
        let name = pid === 0 ? 'PAT' : this.pmtPids.has(pid) ? 'PMT' : pid === PID_NULL ? 'Null' : pid === PID_FAKE_NULL ? 'Fake null' : pid === 17 ? 'SDT' : s ? s.name : '—';
        let kind = s ? s.kind : (pid === 0 || this.pmtPids.has(pid) || pid === 17) ? 'psi' : (pid === PID_NULL || pid === PID_FAKE_NULL) ? 'null' : 'other';
        pids.push({ pid, kind, name, packets: this.pidCount[pid], pct: pct(this.pidCount[pid]), isPcr: pid === this.pcrPid });
      }
      return {
        packets: this.n, bytes: this.bytes, seconds: this.startMs ? (Date.now() - this.startMs) / 1000 : 0,
        syncLost: this.syncLost, tei: this.tei, ccErrors: this.ccErrors,
        programme: this.pmt ? { pmtPid: this.pat && this.pat.pmtPid, pcrPid: this.pmt.pcrPid } : null,
        video: this.videoPid >= 0 ? { pid: this.videoPid, type: this.streams[this.videoPid].name, info: this.vinfo, fps, kbps: avg('video') } : null,
        audio: this.audioPid >= 0 ? { pid: this.audioPid, type: this.streams[this.audioPid].name, info: this.ainfo, kbps: avg('audio') } : null,
        tsKbps: avg('ts'),
        pcr: { pid: this.pcrPid, count: this.pcrCount, nowNs: lastAcc, avgNs: acc.length ? acc.reduce((s, x) => s + x, 0) / acc.length : null, maxNs: acc.length ? Math.max(...acc) : null, limitNs: 500 },
        dist: { video: pct(v), audio: pct(a), nul: pct(nul), fake: pct(fake), other: Math.max(0, 100 - pct(v) - pct(a) - pct(nul) - pct(fake)) },
        pids,
        series: { ptsVideo: this.ptsVideo, ptsAudio: this.ptsAudio, rates: this.rates, pcrAcc: this.pcrAcc, pcrHist: this._hist() },
      };
    }

    // PCR interval distribution: 20 bins between the smallest and the largest interval
    _hist() {
      const a = this.pcrIntervals;
      if (a.length < 2) return { start: 0, width: 1, counts: [] };
      let lo = Math.min(...a), hi = Math.max(...a);
      if (hi - lo < 1) { const m = (hi + lo) / 2; lo = m - 0.5; hi = m + 0.5; }
      const bins = 20, width = (hi - lo) / bins, counts = new Array(bins).fill(0);
      for (const x of a) counts[Math.min(bins - 1, Math.floor((x - lo) / width))]++;
      return { start: lo, width, counts };
    }
  }

  root.TsAnalyzer = TsAnalyzer;
  if (typeof module !== 'undefined' && module.exports) module.exports = TsAnalyzer;
})(typeof window !== 'undefined' ? window : globalThis);

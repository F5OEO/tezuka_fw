// pages4.jsx — Documentation page (bilingual EN/FR)
const { useState: useS4, useEffect: useE4, useRef: useR4 } = React;

// ---------------------------------------------------------------------------
// Content database
// ---------------------------------------------------------------------------
const DOCS = {
  en: {
    langLabel: 'FR',
    toc: 'Contents',
    intro: {
      title: 'Tezuka Dashboard — User Guide',
      desc: 'Web control interface for Tezuka SDR boards (Zynq-7020 / AD9363). Communication uses MQTT WebSocket (ws://[host]:9001/mqtt) and a binary WebSocket (ws://[host]/waterfall) for real-time spectrum data.',
    },
    sections: [
      {
        id: 'shell',
        icon: 'dashboard',
        title: 'Application Shell',
        desc: 'The persistent frame shared by all pages: sidebar, topbar, and the Tweaks customisation panel.',
        groups: [
          {
            title: 'Sidebar',
            rows: [
              ['Logo / link', 'Clickable brand mark — opens the Tezuka firmware GitHub repository.'],
              ['Hamburger button (topbar)', 'Collapses / expands the sidebar (icon-only mode).'],
              ['Nav groups', 'Sections: RF · Application · System · Documentation — each containing page links.'],
              ['Operator avatar', 'Bottom button — navigates to the Operator profile page.'],
            ],
          },
          {
            title: 'Topbar',
            rows: [
              ['Breadcrumb', '"Tezuka / <Page name>" — current location indicator.'],
              ['MQTT chip', 'Green = connected to MQTT broker · grey = disconnected.'],
              ['Notification button', 'Bell icon — reserved for future alerts.'],
            ],
          },
          {
            title: 'Tweaks Panel',
            rows: [
              ['Primary accent', 'Choose from 5 preset colours for the main UI accent.'],
              ['Secondary accent', 'Choose from 5 preset colours for secondary highlights.'],
              ['Density', 'compact / regular / comfy — adjusts spacing throughout the UI.'],
              ['Sidebar labels', 'Toggle text labels next to nav icons.'],
              ['Mono readouts', 'Toggle monospace font for numeric values.'],
            ],
          },
        ],
      },

      {
        id: 'dashboard',
        icon: 'dashboard',
        title: 'Dashboard',
        desc: 'Real-time RF overview with direct control of both receive and transmit paths. All changes take effect immediately via MQTT.',
        groups: [
          {
            title: 'Baseband (shared RX + TX)',
            rows: [
              ['Sample rate FreqTuner', '520 833 S/s – 61.44 MS/s. Applies to both RX and TX simultaneously.'],
              ['Decimation (MiniSelect)', 'RX FIR filter: None / ×2 / ×4 — reduces host data rate.'],
              ['Interpolation (MiniSelect)', 'TX FIR filter: None / ×2 / ×4 — increases DAC rate.'],
              ['Bandwidth FreqTuner', '200 kHz – 56 MHz. Auto checkbox: tracks sample rate × 1.5 (max 56 MHz).'],
            ],
          },
          {
            title: 'RX Panel',
            rows: [
              ['Frequency FreqTuner', 'RX LO: 47 MHz – 6 GHz. Scroll digits or click digit halves.'],
              ['Gain control segment', 'Manual · Slow AGC · Fast AGC — publishes cmd/rx/gain_mode.'],
              ['RSSI readout', 'Received signal level in dBm (live, read-only).'],
              ['RX gain Slider', '0 – 73 dB. Greyed out when AGC is active.'],
              ['LED Clipping', 'Red when state/rx/overload = 1.'],
              ['LED Underflow', 'Red when state/rx/underflow = 1.'],
              ['DMA / buffer size', 'Buffer size in bytes + DMA transfer bar gauge.'],
              ['Input BigStat', 'Active input: RX1 / RX2. Click to toggle — publishes cmd/rx/rfinput.'],
            ],
          },
          {
            title: 'TX Panel',
            rows: [
              ['Frequency FreqTuner', 'TX LO: 47 MHz – 6 GHz.'],
              ['TX gain Slider', 'Attenuation −89 to 0 dB, step 0.25 dB.'],
              ['LED Clipping', 'Red when state/tx/overload = 1.'],
              ['LED Underflow', 'Red when state/tx/underflow = 1.'],
              ['Output BigStat', 'Active output: TX1 / TX2. Click to toggle — publishes cmd/tx/rfinput.'],
            ],
          },
          {
            title: 'Temperature',
            rows: [
              ['FPGA DialGauge', 'On-die FPGA temperature in °C (max 65 °C).'],
              ['AD9361 DialGauge', 'RF transceiver die temperature in °C (max 65 °C).'],
            ],
          },
          {
            title: 'Device strip',
            rows: [
              ['Model', 'Hardware model string from state/main/hw_model.'],
              ['Uptime', 'System uptime since last boot.'],
              ['Tezuka core', 'Firmware version from state/main/fw_version.'],
              ['MQTT broker', 'Connection status and broker hostname.'],
            ],
          },
        ],
      },

      {
        id: 'spectrum',
        icon: 'spectrum',
        title: 'Spectrum',
        desc: 'Real-time FFT spectrum analyser with phosphor-style canvas rendering and waterfall. Connects to ws://[host]/waterfall for binary FFT frames.',
        groups: [
          {
            title: 'Top row controls',
            rows: [
              ['REF (DbTuner)', 'Reference level in dBm. Click the "−" sign to flip polarity. Scroll digits to adjust.'],
              ['MKR (read-only)', 'Frequency (MHz) and level (dB) under the mouse cursor.'],
              ['FULL', 'Click to toggle fullscreen (requestFullscreen API).'],
              ['ANT', 'Active antenna: RX1 / RX2. Click to swap — briefly applies fast AGC then restores manual gain.'],
            ],
          },
          {
            title: 'Second row controls',
            rows: [
              ['RANGE (FreqTuner)', 'Total dB span visible across 8 divisions. Also: Shift+scroll on canvas.'],
              ['dB/DIV (read-only)', 'Computed as RANGE / 8.'],
              ['FOSFOR', 'Phosphor persistence mode. Click to cycle: OFF → light → medium → high.'],
              ['GAIN (DbTuner)', 'RX hardware gain in dB — publishes cmd/rx/gain.'],
            ],
          },
          {
            title: 'Canvas interactions',
            rows: [
              ['Plain scroll', 'Graphical zoom ×1–×8 (no MQTT). At zoom limits, steps the hardware span.'],
              ['Ctrl + scroll', 'Hardware span change — publishes cmd/rx/span and cmd/rx/frequency.'],
              ['Shift + scroll', 'Adjusts RANGE (dB per division).'],
              ['Left-drag horizontal', 'Pans center frequency — publishes cmd/rx/frequency (throttled 200 ms).'],
              ['Left-drag vertical', 'Adjusts REF level.'],
              ['Hover', 'Shows MKR frequency/level crosshair on the canvas.'],
              ['1-finger touch', 'Pan frequency (horizontal) + REF (vertical).'],
              ['2-finger pinch', 'Horizontal = graphical zoom; vertical = RANGE. At limits, changes hardware span.'],
            ],
          },
          {
            title: 'Bottom row controls',
            rows: [
              ['CENTER (FreqTuner)', 'Center frequency in kHz — publishes cmd/rx/frequency (or sweep equivalent).'],
              ['VFW (FreqTuner)', 'Waterfall frame interval in ms — publishes cmd/spectro/fps.'],
              ['SPAN (FreqTuner)', 'Hardware span in kHz — publishes cmd/rx/span (throttled 500 ms + debounce).'],
              ['ST (read-only)', 'Frame period in ms (1000 / FPS).'],
              ['ZOOM (read-only)', 'Current graphical zoom factor — shown only when > ×1.'],
            ],
          },
          {
            title: 'Keyboard shortcuts',
            rows: [
              ['+ / =', 'REF +5 dB.'],
              ['- / _', 'REF −5 dB.'],
              ['[', 'RANGE −1 division.'],
              [']', 'RANGE +1 division.'],
            ],
          },
        ],
      },

      {
        id: 'datv',
        icon: 'datv',
        title: 'DATV Controller',
        desc: 'DVB-S / DVB-S2 modulator control for the PlutoSDR. Commands are published to cmd/pluto/<callsign>/…',
        groups: [
          {
            title: 'Header',
            rows: [
              ['ON AIR / STANDBY lamp', 'Animated indicator — red when transmitting.'],
              ['TX ON/OFF Toggle', 'Mutes or unmutes the transmitter — publishes cmd/pluto/<call>/tx/mute (0 = on air).'],
            ],
          },
          {
            title: 'Modulator card',
            rows: [
              ['Frequency FreqTuner', 'TX carrier frequency: 47 MHz – 6 GHz.'],
              ['TX gain Slider', '−80 to 0 dB, step 0.25 dB.'],
              ['Stream mode Select', 'Test tone / Passthrough / DVB-S2/TS / DVB-S2/GSE / DVB-S.'],
              ['Symbol rate TextInput', '25 000 – 4 000 000 Bd. Shown for DVB-S and DVB-S2 modes.'],
              ['Constellation Select', 'QPSK / 8PSK / 16APSK / 32APSK (DVB-S2 only).'],
              ['FEC Select', 'Auto (variable) / 1/4 / 1/3 / 2/5 / 3/5 / 4/5 / 5/6 / 8/9 / 9/10.'],
              ['Pilots Select', 'Off / On (DVB-S2).'],
              ['Frame Select', 'Long frame / Short frame (DVB-S2).'],
              ['FIR rolloff Select', '0.20 standard / 0.15 narrow (DVB-S2).'],
            ],
          },
          {
            title: 'TS Source card (DVB-S2 only)',
            rows: [
              ['Input mode Select', 'UDP / File / Internal pattern.'],
              ['UDP address:port TextInput', 'Multicast or unicast address (e.g. 239.0.0.1:5004). Shown in UDP mode.'],
            ],
          },
          {
            title: 'Stream status card',
            rows: [
              ['TS bitrate', 'Live MQTT value or estimate: SR × 2 × FEC × 0.88.'],
              ['Buffer queue', 'BBframe queue depth. Shown red if > 100.'],
              ['Current FEC', 'Active FEC in variable (auto) mode.'],
              ['CC error PID', 'PID with continuity counter errors (red).'],
              ['Firmware', 'PlutoSDR firmware version string.'],
            ],
          },
        ],
      },

      {
        id: 'analysis',
        icon: 'analysis',
        title: 'Analysis',
        desc: 'DVB transport stream analysis — accessible as a sub-item under DATV.',
        groups: [
          {
            title: 'Charts',
            rows: [
              ['Transport stream rate StreamChart', 'Live DVB output bitrate in Mb/s (60-point history, 1 Hz).'],
              ['Video buffer fill StreamChart', 'Encoder output buffer fill level in %.'],
            ],
          },
          {
            title: 'PID table',
            rows: [
              ['PID column', 'Hexadecimal PID value.'],
              ['Type column', 'PAT / PMT / Video / Audio / Null.'],
              ['Codec column', 'Codec identifier (H.265, AAC…).'],
              ['Bitrate column', 'Per-PID bitrate.'],
              ['Continuity column', 'Pill indicator — green = no errors.'],
            ],
          },
        ],
      },

      {
        id: 'transverter',
        icon: 'transverter',
        title: 'Transverter',
        desc: 'IIO loopback mode that routes the ADC output directly to the DAC, enabling internal frequency conversion.',
        groups: [
          {
            title: 'Loopback',
            rows: [
              ['Loopback Toggle', 'Off / Active — publishes cmd/rx/loopback (0 or 2). RX and TX panels are greyed out when inactive.'],
            ],
          },
          {
            title: 'RX path (active when loopback enabled)',
            rows: [
              ['RX frequency FreqTuner', '47 – 6 000 MHz.'],
              ['RX bandwidth FreqTuner', '200 – 56 000 kHz.'],
              ['RX input power Slider', '0 – 73 dB.'],
            ],
          },
          {
            title: 'TX path (active when loopback enabled)',
            rows: [
              ['TX frequency FreqTuner', '47 – 6 000 MHz.'],
              ['TX bandwidth FreqTuner', '200 – 56 000 kHz.'],
              ['TX output power Slider', '−89.75 to 0 dB, step 0.25 dB.'],
            ],
          },
        ],
      },

      {
        id: 'iqtape',
        icon: 'tape',
        title: 'IQ Tape',
        desc: 'Record and replay raw I/Q baseband captures via WebSocket (ws://[host]:8765). Recording survives page navigation.',
        groups: [
          {
            title: 'Local folder card',
            rows: [
              ['IQ Tape Toggle', 'Enables / disables the iio_ws_proxy service on the device — publishes cmd/system/iqtape.'],
              ['Folder picker', 'File System Access API (secure context) or multi-file input. Handle persisted in IndexedDB across sessions.'],
            ],
          },
          {
            title: 'Capture card',
            rows: [
              ['Sample rate FreqTuner', 'Sets both RX and TX sampling rate.'],
              ['RX / TX frequency FreqTuner', 'Center frequencies for metadata embedded in the filename.'],
              ['Format Select', 'cs8 (8-bit) or cs16 (16-bit complex). Locked during active recording.'],
              ['Filename (read-only)', 'Auto-generated: YYYYMMDD_HHMMSS_<freq>_<sr>.<format>.'],
              ['Record / Stop button', 'Starts or stops the WebSocket capture (protocol: iio-rx).'],
              ['Bitrate / total indicator', 'Live MB/s and cumulative bytes while recording.'],
            ],
          },
          {
            title: 'Playback card',
            rows: [
              ['File selection Select', 'Lists .iq/.bin/.cf32/.cs16/.cs8/.raw files from the chosen folder.'],
              ['Play / Stop button', 'Streams the file to the device over WebSocket (protocol: iio-tx) in 64 KB frames.'],
              ['Loop button', 'Loops playback continuously when active.'],
              ['Progress bar', 'Playback position as percentage + MB/s throughput.'],
            ],
          },
        ],
      },

      {
        id: 'siggen',
        icon: 'wave',
        title: 'Signal Generator',
        desc: 'Synthetic I/Q waveform generator. The buffer is computed in the browser and streamed to the TX over WebSocket. Streaming survives page navigation.',
        groups: [
          {
            title: 'Header controls',
            rows: [
              ['Service on/off button', 'Enables / disables the iio_ws_proxy TX service — publishes cmd/system/siggen.'],
              ['TX ON/OFF Toggle', 'Starts or stops WebSocket streaming. UI is greyed out when service is off.'],
            ],
          },
          {
            title: 'RF output card',
            rows: [
              ['TX frequency FreqTuner', '47 MHz – 6 GHz.'],
              ['TX gain Slider', '−89.75 to 0 dB, step 0.25 dB.'],
              ['Sample rate FreqTuner', 'Must match the device TX sampling rate.'],
            ],
          },
          {
            title: 'Waveform card — signal types',
            rows: [
              ['CW', 'Carrier wave with optional baseband frequency offset.'],
              ['Two tone', 'Two equal-amplitude tones symmetric around a center offset.'],
              ['AM', 'Amplitude modulation: carrier offset, modulating frequency, depth (%).'],
              ['FM', 'Frequency modulation: carrier offset, modulating frequency, deviation.'],
              ['SSB', 'Single sideband: audio tone frequency + USB or LSB selection.'],
              ['Sweep', 'Linear frequency chirp: start/stop frequencies, period (ms), shape (Triangle or Sawtooth).'],
              ['AWGN', 'Additive white Gaussian noise (Box-Muller). Amplitude sets 3σ.'],
            ],
          },
          {
            title: 'Preview canvases',
            rows: [
              ['IQ constellation', '2 048-sample scatter plot of I (x) vs Q (y) in a 160 px canvas.'],
              ['Time domain', 'First 512 samples: I in blue, Q in orange, on an 80 px canvas.'],
            ],
          },
        ],
      },

      {
        id: 'arch',
        icon: 'chip',
        title: 'Architecture',
        desc: 'Interactive SVG block diagram of the AD9361 RF transceiver. Click any block to view and edit its parameters in the detail panel.',
        groups: [
          {
            title: 'SVG diagram blocks',
            rows: [
              ['RX1/RX2 (port)', 'RF input ports — select active input, 50 Ω, RSSI readout.'],
              ['LNA + Att (rx)', 'Low-noise amp with stepped attenuator — gain mode + RX gain slider.'],
              ['RX Mixer (rx)', 'Zero-IF quadrature down-converter — RX LO frequency tuner.'],
              ['BB Filter RX (rx)', 'Baseband LPF — bandwidth slider (0.2 – 18 MHz).'],
              ['ADC (rx)', 'Σ-Δ 12-bit ADC — sample rate tuner, resolution, Σ-Δ clock (read-only).'],
              ['Dec / HB (rx)', 'Half-band decimation filter — factor select (÷1 / ÷2 / ÷4 / ÷8).'],
              ['TX1/TX2 (port)', 'RF output ports — select active output, 50 Ω.'],
              ['Driver + Att (tx)', 'PA driver with digital attenuator — TX attenuation slider.'],
              ['TX Mixer (tx)', 'Direct-conversion up-converter — TX LO frequency tuner.'],
              ['BB Filter TX (tx)', 'Reconstruction LPF — bandwidth slider.'],
              ['DAC (tx)', '12-bit DAC — sample rate tuner (read-only resolution).'],
              ['Int / HB (tx)', 'Interpolation filter — factor select (×1 / ×2 / ×4 / ×8).'],
              ['RX PLL (lo)', 'Fractional-N RX synthesiser — frequency tuner + lock status.'],
              ['TX PLL (lo)', 'Fractional-N TX synthesiser — frequency tuner + lock status.'],
              ['AuxADC (sys)', 'Auxiliary ADC + temperature sensor — SoC and FPGA temps (live).'],
              ['BB PLL (sys)', 'Baseband PLL — reference select (40 MHz TCXO / 30.72 MHz / 10 MHz ext), clock, lock.'],
              ['SPI / ENSM (sys)', 'SPI control bus + Enable State Machine — ENSM state select (FDD/TDD/Alert/Sleep).'],
              ['Data Port (sys)', 'Digital I/Q interface — LVDS / CMOS select, RX data rate (live).'],
              ['Zynq PL (fpga)', 'Programmable logic — XC7Z020, Tezuka HDL core, 100 MHz PL clock (read-only).'],
            ],
          },
          {
            title: 'Detail panel',
            rows: [
              ['Group tag', 'Colour-coded label matching the block group.'],
              ['Description', 'Functional description of the selected block.'],
              ['Editable fields', 'FreqTuner, Slider, or Select controls — changes are staged locally.'],
              ['Read-only fields', 'Live values streamed from MQTT (temperatures, clock frequencies, lock status).'],
              ['Action bar', 'Appears when there are staged changes. Reset discards them; Apply publishes all to MQTT.'],
            ],
          },
        ],
      },

      {
        id: 'versions',
        icon: 'versions',
        title: 'Versions',
        desc: 'Hardware platform info and firmware component version table.',
        groups: [
          {
            title: 'Platform card',
            rows: [
              ['Model / serial', 'Hardware model string and device serial number (live via MQTT).'],
              ['SoC', 'Zynq-7020 (fixed).'],
              ['RF transceiver', 'AD9363 (fixed).'],
              ['GCC target', 'arm-linux-gnueabihf cross-compilation target.'],
            ],
          },
          {
            title: 'Firmware components table',
            rows: [
              ['Tezuka', 'Application firmware version.'],
              ['Linux kernel', 'Kernel version string.'],
              ['U-Boot', 'Bootloader version.'],
              ['FPGA bitstream', 'PL fabric bitstream version.'],
              ['Root FS', 'Read-only squashfs version.'],
              ['libiio', 'IIO library version.'],
            ],
          },
          {
            title: 'Update card',
            rows: [
              ['Status pill', 'Shows "Up to date" or update availability.'],
              ['Check for updates button', 'Queries the update channel.'],
              ['Install from file button', 'Allows manual firmware upload.'],
            ],
          },
        ],
      },

      {
        id: 'network',
        icon: 'network',
        title: 'Network',
        desc: 'Network interface configuration and system throughput charts.',
        groups: [
          {
            title: 'Interface tabs',
            rows: [
              ['LAN tab', 'Shows eth0 IP, mask, gateway, DNS, MAC, hostname from MQTT state/net/…'],
              ['Wi-Fi tab', 'Placeholder for future wireless interface.'],
              ['USB tab', 'Shows usb0 IP, mask, MAC — typical Pluto/Tezuka USB gadget address.'],
            ],
          },
          {
            title: 'MQTT broker card',
            rows: [
              ['Host / Port', 'Broker IP and port (1883).'],
              ['Base topic', 'state/# subscription root.'],
              ['Status pill', 'Connected (green) / Offline (orange).'],
            ],
          },
          {
            title: 'Service ports table',
            rows: [
              ['HTTP 80', 'Web dashboard server.'],
              ['RTSP 554', 'Video stream output.'],
              ['RTMP 1935', 'Video ingest.'],
              ['SSH 22', 'Secure shell access.'],
              ['MQTT 1883', 'MQTT broker.'],
            ],
          },
          {
            title: 'System & throughput StreamChart',
            rows: [
              ['CPU %', 'ARM Cortex-A9 usage (accent colour).'],
              ['Memory %', 'RAM usage (blue).'],
              ['SoC °C', 'Zynq PS temperature (green).'],
              ['FPGA °C', 'PL temperature (coral).'],
              ['RX B/s', 'Receive throughput (right axis, purple).'],
              ['TX B/s', 'Transmit throughput (right axis, pink).'],
            ],
          },
        ],
      },

      {
        id: 'calibrate',
        icon: 'target',
        title: 'Calibrate',
        desc: 'Frequency reference oscillator trim and gain-vs-frequency / DAC-gain calibration curves.',
        groups: [
          {
            title: 'Frequency calibration card',
            rows: [
              ['Oscillator PPM Slider', '−200 to +200 ppm, step 0.1 — publishes cmd/main/freq_correction in Hz (formula: 40 000 000 + ppm × 40).'],
              ['PPM TextInput', 'Direct numeric entry. Press Enter or click away to commit.'],
              ['Save button', 'Writes xo_correction to U-Boot environment via cmd/system/setenv/xo_correction.'],
              ['ON / OFF Toggle', 'Auto-discipline indicator (greyed out — reserved for GPS/GPSDO discipline).'],
            ],
          },
          {
            title: 'Gain vs frequency chart',
            rows: [
              ['XYChart (editable)', '56 control points from 47 MHz to 6 GHz. Drag any point vertically.'],
              ['Apply button', 'Appears when the curve is modified — publishes cmd/main/gain_table_config.'],
            ],
          },
          {
            title: 'DAC gain vs amplitude chart',
            rows: [
              ['XYChart (editable)', '40 control points from 0 % to 100 % FS. Drag vertically.'],
            ],
          },
          {
            title: 'Navigation',
            rows: [
              ['Kalibrate from RF button', 'Navigates to the Kalibrate sub-page for GSM-based XO calibration.'],
            ],
          },
        ],
      },

      {
        id: 'kalibrate',
        icon: 'search',
        title: 'Kalibrate (from RF)',
        desc: 'Calibrates the crystal oscillator by scanning live GSM base stations and computing the frequency error. Sub-page of Calibrate.',
        groups: [
          {
            title: 'Scan header',
            rows: [
              ['Band Select', 'GSM850 / GSM-R / GSM900 / EGSM / DCS.'],
              ['Launch scan button', 'Publishes cmd/system/kalibrate/scan <band>. Spinner shown during scan.'],
            ],
          },
          {
            title: 'XO correction card',
            rows: [
              ['Current correction', 'Live ppm value from state/main/freq_correction.'],
              ['Kalibrate result', 'ppm and ppb after a completed calibration.'],
              ['Apply to XO button', 'Writes the result to cmd/main/freq_correction. Visible after status = done.'],
            ],
          },
          {
            title: 'GSM channels table',
            rows: [
              ['Chan', 'GSM channel number.'],
              ['Freq (MHz)', 'Channel centre frequency.'],
              ['Power (dBFS)', 'Measured signal power — table sorted descending.'],
              ['Kalibrate button', 'Runs kal -c on this channel — publishes cmd/system/kalibrate/run <chan>. Shows result ppm after completion.'],
            ],
          },
          {
            title: 'kal output card',
            rows: [
              ['Log window', 'Raw stdout from the kalibrate tool, streamed line by line from state/system/kalibrate/log.'],
              ['Clear button', 'Empties the display without discarding MQTT history.'],
            ],
          },
        ],
      },

      {
        id: 'diagnostic',
        icon: 'pulse',
        title: 'Diagnostic',
        desc: 'On-demand kernel log (dmesg) and IIO debug register dump.',
        groups: [
          {
            title: 'Actions',
            rows: [
              ['Provide log button', 'Publishes cmd/system/logrequest — device responds with dmesg lines on state/system/log.'],
              ['Show iio debug button', 'Publishes cmd/system/getdebugiio — device responds with /sys/kernel/debug/iio key-value pairs.'],
            ],
          },
          {
            title: 'IIO debug card (conditional)',
            rows: [
              ['Log window', 'Sorted key-value pairs of IIO debug attributes.'],
              ['Clear button', 'Removes the displayed entries.'],
            ],
          },
          {
            title: 'System log card',
            rows: [
              ['Log window', 'Timestamped dmesg lines (HH:MM:SS prefix). Auto-scrolls to bottom.'],
              ['Clear button', 'Clears the display (does not reset MQTT retained messages).'],
            ],
          },
        ],
      },

      {
        id: 'reboot',
        icon: 'power',
        title: 'Reboot',
        desc: 'Controlled device restart with live reconnection tracking.',
        groups: [
          {
            title: 'Restart card (idle state)',
            rows: [
              ['Reboot mode Select', 'Normal / Safe mode / Bootloader.'],
              ['Warning note', 'Warns that TX, recording, and streams will be interrupted (~20 s downtime).'],
              ['Reboot now button', 'Asks for confirmation, then publishes cmd/system/reboot reboot.'],
            ],
          },
          {
            title: 'Progress states',
            rows: [
              ['busy', 'Spinner + countdown (30 s for reboot, 6 s for shutdown).'],
              ['waiting', 'Spinner — polling for MQTT reconnection after boot.'],
              ['done', 'Check icon + "Back online" + Done button. MQTT reconnected.'],
            ],
          },
          {
            title: 'System card (always visible)',
            rows: [
              ['Compact table', 'Model · Firmware version · Linux version · Uptime · Serial.'],
            ],
          },
        ],
      },

      {
        id: 'operator',
        icon: 'user',
        title: 'Operator',
        desc: 'Station identity stored in localStorage. Used as the DATV callsign and displayed in the sidebar.',
        groups: [
          {
            title: 'Profile card',
            rows: [
              ['Operator name TextInput', 'Full name of the operator.'],
              ['Callsign TextInput', 'Ham radio callsign — auto-uppercased on save.'],
              ['Grid locator TextInput', 'Maidenhead locator, e.g. JN18cv.'],
              ['Save / Reset buttons', 'Active only when unsaved changes exist.'],
            ],
          },
          {
            title: 'Identity preview card',
            rows: [
              ['Avatar + details', 'Real-time preview of name, callsign, and locator as they will appear.'],
            ],
          },
        ],
      },

      {
        id: 'persistent',
        icon: 'save',
        title: 'Persistent Storage',
        desc: 'Read and write U-Boot environment variables (fw_printenv / fw_setenv) via MQTT.',
        groups: [
          {
            title: 'New variable card',
            rows: [
              ['Name TextInput', 'Variable name — alphanumeric and underscore only. Save is disabled otherwise.'],
              ['Value TextInput', 'Value to set.'],
              ['Save button', 'Publishes cmd/system/setenv/<name>. Pill shows "saved" for 2 s.'],
            ],
          },
          {
            title: 'Environment table',
            rows: [
              ['Filter TextInput', 'Searches in both variable names and values (case-insensitive).'],
              ['Variable column', 'U-Boot environment variable name (monospace, sorted alphabetically).'],
              ['Value column', 'Editable — single-line TextInput or multi-line textarea if value contains newlines.'],
              ['Save / Reset per row', 'Appears when a value is modified. Save publishes cmd/system/setenv/<name>.'],
            ],
          },
        ],
      },

      {
        id: 'gpio',
        icon: 'circuit',
        title: 'GPIO',
        desc: 'Toggle individual GPIO pins (gpiochip0). State received via state/gpio/<n>.',
        groups: [
          {
            title: 'GPIO grid',
            rows: [
              ['Pin tile', 'Shows GPIO number, a coloured dot (green = ON, dim = OFF), and state label.'],
              ['Click', 'Toggles the pin — publishes cmd/gpio/<pin> (0 or 1).'],
            ],
          },
        ],
      },

      {
        id: 'performance',
        icon: 'chip',
        title: 'Performance',
        desc: 'CPU overclock profile selection.',
        groups: [
          {
            title: 'CPU card',
            rows: [
              ['Overclock profile Select', 'Available profiles loaded from state/system/overclock_cap at MQTT connect. Changing the profile publishes cmd/system/overclock <profile>. Takes effect on next boot.'],
            ],
          },
        ],
      },

      {
        id: 'mqtt',
        icon: 'network',
        title: 'MQTT Reference',
        desc: 'Topic convention used by the dashboard. All payloads are plain UTF-8 strings. Boolean flags use "0" / "1".',
        groups: [
          {
            title: 'Topic structure',
            rows: [
              ['state/<path>', 'Published by the device (usually retained). Dashboard subscribes to state/#.'],
              ['cmd/<path>', 'Published by the dashboard to control the device.'],
              ['cmd/pluto/<callsign>/<path>', 'PlutoSDR-specific commands (DATV controller).'],
              ['dt/pluto/<callsign>/<key>', 'PlutoSDR telemetry (DATV status).'],
            ],
          },
          {
            title: 'Key state topics',
            rows: [
              ['state/rx/frequency', 'RX LO frequency in Hz.'],
              ['state/tx/frequency', 'TX LO frequency in Hz.'],
              ['state/rx/sampling', 'RX sampling rate in Hz.'],
              ['state/rx/bandwidth', 'RX RF bandwidth in Hz.'],
              ['state/rx/gain', 'RX hardware gain in dB.'],
              ['state/rx/gain_mode', 'AGC mode: manual / slow_attack / fast_attack.'],
              ['state/rx/overload', '"1" when the ADC is clipping.'],
              ['state/tx/overload', '"1" when the TX filter stages overflow.'],
              ['state/rx/sweep/activate', '"1" in sweep mode.'],
              ['state/rx/span', 'Sweep or display span in Hz.'],
              ['state/main/freq_correction', 'TCXO reference frequency in Hz (base = 40 000 000).'],
              ['state/net/<iface>/ip', 'Interface IP address (iface = usb0 or eth0).'],
              ['state/system/kalibrate/status', 'scanning / calibrating / done / error / writing.'],
              ['state/system/kalibrate/channels', 'JSON array of scanned GSM channels [{chan, freq, power}].'],
              ['state/system/log', 'One dmesg line per message (not retained).'],
            ],
          },
        ],
      },
    ],
  },

  // ---------------------------------------------------------------------------
  // French
  // ---------------------------------------------------------------------------
  fr: {
    langLabel: 'EN',
    toc: 'Sommaire',
    intro: {
      title: 'Tezuka Dashboard — Guide utilisateur',
      desc: "Interface web de contrôle pour les cartes SDR Tezuka (Zynq-7020 / AD9363). La communication utilise MQTT WebSocket (ws://[host]:9001/mqtt) et un WebSocket binaire (ws://[host]/waterfall) pour les données spectrales temps réel.",
    },
    sections: [
      {
        id: 'shell',
        icon: 'dashboard',
        title: 'Structure de l\'application',
        desc: 'Le cadre commun à toutes les pages : barre latérale, barre supérieure et panneau Tweaks de personnalisation.',
        groups: [
          {
            title: 'Barre latérale',
            rows: [
              ['Logo / lien', 'Logo cliquable — ouvre le dépôt GitHub du firmware Tezuka.'],
              ['Bouton hamburger (topbar)', 'Réduit / agrandit la barre latérale (mode icônes seules).'],
              ['Groupes de navigation', 'Sections : RF · Application · Système · Documentation.'],
              ['Avatar opérateur', 'Bouton en bas — accède au profil opérateur.'],
            ],
          },
          {
            title: 'Barre supérieure (Topbar)',
            rows: [
              ['Fil d\'Ariane', '"Tezuka / <Nom de page>" — indicateur de position courante.'],
              ['Puce MQTT', 'Verte = connecté au broker MQTT · grise = déconnecté.'],
              ['Bouton notification', 'Icône cloche — réservé pour les futures alertes.'],
            ],
          },
          {
            title: 'Panneau Tweaks',
            rows: [
              ['Accent primaire', '5 couleurs prédéfinies pour l\'accent principal de l\'interface.'],
              ['Accent secondaire', '5 couleurs prédéfinies pour les éléments secondaires.'],
              ['Densité', 'compact / regular / comfy — ajuste l\'espacement global.'],
              ['Labels sidebar', 'Afficher ou masquer les étiquettes texte des icônes de navigation.'],
              ['Mono readouts', 'Police monospace pour les valeurs numériques.'],
            ],
          },
        ],
      },

      {
        id: 'dashboard',
        icon: 'dashboard',
        title: 'Tableau de bord',
        desc: 'Vue d\'ensemble RF en temps réel avec contrôle direct des voies d\'émission et de réception. Toutes les modifications sont appliquées immédiatement via MQTT.',
        groups: [
          {
            title: 'Bande de base (commun RX + TX)',
            rows: [
              ['Taux d\'échantillonnage FreqTuner', '520 833 S/s – 61,44 MS/s. Appliqué simultanément à RX et TX.'],
              ['Décimation (MiniSelect)', 'Filtre FIR RX : None / ×2 / ×4 — réduit le débit vers l\'hôte.'],
              ['Interpolation (MiniSelect)', 'Filtre FIR TX : None / ×2 / ×4 — augmente le débit vers le DAC.'],
              ['Bande passante FreqTuner', '200 kHz – 56 MHz. Case Auto : suit le taux d\'échantillonnage × 1,5 (max 56 MHz).'],
            ],
          },
          {
            title: 'Panneau RX',
            rows: [
              ['Fréquence FreqTuner', 'OL RX : 47 MHz – 6 GHz. Molette ou clic sur les demi-chiffres.'],
              ['Contrôle de gain', 'Manual · Slow AGC · Fast AGC — publie cmd/rx/gain_mode.'],
              ['Affichage RSSI', 'Niveau du signal reçu en dBm (lecture seule).'],
              ['Gain RX Slider', '0 – 73 dB. Grisé si l\'AGC est actif.'],
              ['LED Clipping', 'Rouge si state/rx/overload = 1.'],
              ['LED Underflow', 'Rouge si state/rx/underflow = 1.'],
              ['DMA / taille buffer', 'Taille en octets + jauge de transfert DMA.'],
              ['BigStat Entrée', 'Entrée active : RX1 / RX2. Clic pour basculer — publie cmd/rx/rfinput.'],
            ],
          },
          {
            title: 'Panneau TX',
            rows: [
              ['Fréquence FreqTuner', 'OL TX : 47 MHz – 6 GHz.'],
              ['Gain TX Slider', 'Atténuation −89 à 0 dB, pas 0,25 dB.'],
              ['LED Clipping', 'Rouge si state/tx/overload = 1.'],
              ['LED Underflow', 'Rouge si state/tx/underflow = 1.'],
              ['BigStat Sortie', 'Sortie active : TX1 / TX2. Clic pour basculer — publie cmd/tx/rfinput.'],
            ],
          },
          {
            title: 'Température',
            rows: [
              ['Jauge FPGA', 'Température du FPGA en °C (max 65 °C).'],
              ['Jauge AD9361', 'Température du transceiver RF en °C (max 65 °C).'],
            ],
          },
          {
            title: 'Bandeau appareil',
            rows: [
              ['Modèle', 'Chaîne de modèle matériel (state/main/hw_model).'],
              ['Uptime', 'Durée de fonctionnement depuis le dernier démarrage.'],
              ['Version Tezuka', 'Version du firmware (state/main/fw_version).'],
              ['Broker MQTT', 'État de connexion et nom d\'hôte du broker.'],
            ],
          },
        ],
      },

      {
        id: 'spectrum',
        icon: 'spectrum',
        title: 'Spectre',
        desc: 'Analyseur de spectre FFT temps réel avec rendu canvas style phosphore et waterfall. Connexion ws://[host]/waterfall pour les trames FFT binaires.',
        groups: [
          {
            title: 'Ligne supérieure',
            rows: [
              ['REF (DbTuner)', 'Niveau de référence en dBm. Clic sur "−" pour inverser le signe.'],
              ['MKR (lecture seule)', 'Fréquence (MHz) et niveau (dB) sous le curseur.'],
              ['FULL', 'Bascule le plein écran (API requestFullscreen).'],
              ['ANT', 'Antenne active : RX1 / RX2. Clic pour permuter — applique brièvement le fast AGC puis restaure le gain manuel.'],
            ],
          },
          {
            title: 'Deuxième ligne',
            rows: [
              ['RANGE (FreqTuner)', 'Dynamique visible en dB sur 8 divisions. Également : Shift+molette sur le canvas.'],
              ['dB/DIV (lecture seule)', 'Calculé : RANGE / 8.'],
              ['FOSFOR', 'Persistance phosphore. Clic pour cycler : OFF → light → medium → high.'],
              ['GAIN (DbTuner)', 'Gain matériel RX en dB — publie cmd/rx/gain.'],
            ],
          },
          {
            title: 'Interactions canvas',
            rows: [
              ['Molette simple', 'Zoom graphique ×1–×8 (sans MQTT). Aux limites, change le span matériel.'],
              ['Ctrl + molette', 'Changement de span matériel — publie cmd/rx/span et cmd/rx/frequency.'],
              ['Shift + molette', 'Ajuste la RANGE (dB par division).'],
              ['Clic-glisser horizontal', 'Décale la fréquence centrale — publie cmd/rx/frequency (throttle 200 ms).'],
              ['Clic-glisser vertical', 'Ajuste le niveau REF.'],
              ['Survol', 'Affiche le marqueur de fréquence/niveau sur le canvas.'],
              ['1 doigt (tactile)', 'Pan fréquence (horizontal) + REF (vertical).'],
              ['2 doigts (pinch)', 'Horizontal = zoom graphique ; vertical = RANGE. Aux limites, change le span matériel.'],
            ],
          },
          {
            title: 'Ligne inférieure principale',
            rows: [
              ['CENTER (FreqTuner)', 'Fréquence centrale en kHz — publie cmd/rx/frequency.'],
              ['VFW (FreqTuner)', 'Intervalle waterfall en ms — publie cmd/spectro/fps.'],
              ['SPAN (FreqTuner)', 'Span matériel en kHz — publie cmd/rx/span (throttle 500 ms + debounce).'],
              ['ST (lecture seule)', 'Période de trame en ms (1000 / FPS).'],
              ['ZOOM (lecture seule)', 'Facteur de zoom graphique — affiché seulement si > ×1.'],
            ],
          },
          {
            title: 'Raccourcis clavier',
            rows: [
              ['+ / =', 'REF +5 dB.'],
              ['- / _', 'REF −5 dB.'],
              ['[', 'RANGE −1 division.'],
              [']', 'RANGE +1 division.'],
            ],
          },
        ],
      },

      {
        id: 'datv',
        icon: 'datv',
        title: 'Contrôleur DATV',
        desc: 'Contrôle du modulateur DVB-S / DVB-S2 PlutoSDR. Les commandes sont publiées sur cmd/pluto/<callsign>/…',
        groups: [
          {
            title: 'En-tête',
            rows: [
              ['Témoin ON AIR / STANDBY', 'Indicateur animé — rouge lors de la transmission.'],
              ['Toggle TX ON/OFF', 'Active ou coupe l\'émetteur — publie cmd/pluto/<call>/tx/mute (0 = en émission).'],
            ],
          },
          {
            title: 'Carte Modulator',
            rows: [
              ['Fréquence FreqTuner', 'Porteuse TX : 47 MHz – 6 GHz.'],
              ['Gain TX Slider', '−80 à 0 dB, pas 0,25 dB.'],
              ['Mode flux Select', 'Tonalité test / Passthrough / DVB-S2/TS / DVB-S2/GSE / DVB-S.'],
              ['Débit symboles TextInput', '25 000 – 4 000 000 Bd. Visible en modes DVB-S et DVB-S2.'],
              ['Constellation Select', 'QPSK / 8PSK / 16APSK / 32APSK (DVB-S2 uniquement).'],
              ['FEC Select', 'Auto (variable) / 1/4 / 1/3 / 2/5 / 3/5 / 4/5 / 5/6 / 8/9 / 9/10.'],
              ['Pilotes Select', 'Off / On (DVB-S2).'],
              ['Trame Select', 'Trame longue / Trame courte (DVB-S2).'],
              ['Roll-off FIR Select', '0,20 standard / 0,15 étroit (DVB-S2).'],
            ],
          },
          {
            title: 'Carte Source TS (DVB-S2 uniquement)',
            rows: [
              ['Mode d\'entrée Select', 'UDP / Fichier / Motif interne.'],
              ['Adresse UDP:port TextInput', 'Adresse multicast ou unicast (ex. 239.0.0.1:5004). Visible en mode UDP.'],
            ],
          },
          {
            title: 'Carte État du flux',
            rows: [
              ['Débit TS', 'Valeur MQTT live ou estimation : SR × 2 × FEC × 0,88.'],
              ['File d\'attente buffer', 'Profondeur de la file BBframes. Affiché en rouge si > 100.'],
              ['FEC courant', 'FEC actif en mode variable (auto).'],
              ['Erreur CC PID', 'PID avec erreur de continuité (rouge).'],
              ['Firmware', 'Version du firmware PlutoSDR.'],
            ],
          },
        ],
      },

      {
        id: 'analysis',
        icon: 'analysis',
        title: 'Analyse',
        desc: 'Analyse du flux de transport DVB — accessible en sous-menu de DATV.',
        groups: [
          {
            title: 'Graphiques',
            rows: [
              ['Débit TS StreamChart', 'Débit DVB de sortie en Mb/s (60 points, 1 Hz).'],
              ['Remplissage buffer vidéo StreamChart', 'Niveau de remplissage du buffer encodeur en %.'],
            ],
          },
          {
            title: 'Tableau PID',
            rows: [
              ['PID', 'Valeur hexadécimale du PID.'],
              ['Type', 'PAT / PMT / Video / Audio / Null.'],
              ['Codec', 'Identifiant codec (H.265, AAC…).'],
              ['Débit', 'Débit par PID.'],
              ['Continuité', 'Pill indicateur — vert = aucune erreur.'],
            ],
          },
        ],
      },

      {
        id: 'transverter',
        icon: 'transverter',
        title: 'Transverter',
        desc: 'Mode loopback IIO — route la sortie ADC directement vers le DAC pour la conversion de fréquence interne.',
        groups: [
          {
            title: 'Loopback',
            rows: [
              ['Toggle Loopback', 'Off / Actif — publie cmd/rx/loopback (0 ou 2). Panneaux RX et TX grisés si inactif.'],
            ],
          },
          {
            title: 'Voie RX (active si loopback activé)',
            rows: [
              ['Fréquence RX FreqTuner', '47 – 6 000 MHz.'],
              ['Bande passante RX FreqTuner', '200 – 56 000 kHz.'],
              ['Puissance entrée RX Slider', '0 – 73 dB.'],
            ],
          },
          {
            title: 'Voie TX (active si loopback activé)',
            rows: [
              ['Fréquence TX FreqTuner', '47 – 6 000 MHz.'],
              ['Bande passante TX FreqTuner', '200 – 56 000 kHz.'],
              ['Puissance sortie TX Slider', '−89,75 à 0 dB, pas 0,25 dB.'],
            ],
          },
        ],
      },

      {
        id: 'iqtape',
        icon: 'tape',
        title: 'IQ Tape',
        desc: 'Enregistrement et relecture de captures I/Q brutes via WebSocket (ws://[host]:8765). L\'enregistrement survit à la navigation entre pages.',
        groups: [
          {
            title: 'Carte Dossier local',
            rows: [
              ['Toggle IQ Tape', 'Active / désactive le service iio_ws_proxy sur l\'appareil — publie cmd/system/iqtape.'],
              ['Sélecteur de dossier', 'API File System Access (contexte sécurisé) ou saisie multi-fichiers. Handle persisté en IndexedDB entre les sessions.'],
            ],
          },
          {
            title: 'Carte Capture',
            rows: [
              ['Taux d\'échantillonnage FreqTuner', 'Définit le SR de RX et TX simultanément.'],
              ['Fréquence RX / TX FreqTuner', 'Fréquences centrales — intégrées dans le nom de fichier.'],
              ['Format Select', 'cs8 (8 bits) ou cs16 (16 bits complexe). Verrouillé pendant l\'enregistrement.'],
              ['Nom de fichier (lecture seule)', 'Généré automatiquement : YYYYMMDD_HHMMSS_<fréq>_<sr>.<format>.'],
              ['Bouton Enregistrer / Arrêter', 'Démarre ou arrête la capture WebSocket (protocole : iio-rx).'],
              ['Indicateur débit / total', 'MB/s et octets cumulés en temps réel pendant l\'enregistrement.'],
            ],
          },
          {
            title: 'Carte Lecture',
            rows: [
              ['Sélection de fichier Select', 'Liste les fichiers .iq/.bin/.cf32/.cs16/.cs8/.raw du dossier choisi.'],
              ['Bouton Lire / Arrêter', 'Envoie le fichier vers l\'appareil via WebSocket (protocole : iio-tx) par trames de 64 Ko.'],
              ['Bouton Boucle', 'Lit le fichier en continu.'],
              ['Barre de progression', 'Position en pourcentage + débit en MB/s.'],
            ],
          },
        ],
      },

      {
        id: 'siggen',
        icon: 'wave',
        title: 'Générateur de signaux',
        desc: 'Génère des signaux I/Q synthétiques dans le navigateur et les stream vers le TX via WebSocket. Le streaming survit à la navigation entre pages.',
        groups: [
          {
            title: 'En-tête',
            rows: [
              ['Bouton Service on/off', 'Active / désactive le service iio_ws_proxy TX — publie cmd/system/siggen.'],
              ['Toggle TX ON/OFF', 'Démarre ou arrête le streaming WebSocket. Interface grisée si le service est désactivé.'],
            ],
          },
          {
            title: 'Carte Sortie RF',
            rows: [
              ['Fréquence TX FreqTuner', '47 MHz – 6 GHz.'],
              ['Gain TX Slider', '−89,75 à 0 dB, pas 0,25 dB.'],
              ['Taux d\'échantillonnage FreqTuner', 'Doit correspondre au SR TX de l\'appareil.'],
            ],
          },
          {
            title: 'Carte Forme d\'onde — types de signaux',
            rows: [
              ['CW', 'Onde continue avec décalage fréquence en bande de base optionnel.'],
              ['Two tone', 'Deux tonalités d\'amplitude égale symétriques autour d\'un décalage central.'],
              ['AM', 'Modulation d\'amplitude : décalage porteuse, fréquence modulante, profondeur (%).'],
              ['FM', 'Modulation de fréquence : décalage porteuse, fréquence modulante, déviation.'],
              ['SSB', 'Bande latérale unique : fréquence audio + sélection USB ou LSB.'],
              ['Sweep', 'Chirp linéaire : fréquences de début et de fin, période (ms), forme (Triangle ou Dent de scie).'],
              ['AWGN', 'Bruit blanc gaussien additif (Box-Muller). L\'amplitude règle 3σ.'],
            ],
          },
          {
            title: 'Previews canvas',
            rows: [
              ['Constellation IQ', 'Nuage de 2 048 points I (x) vs Q (y) dans un canvas 160 px.'],
              ['Domaine temporel', '512 premiers échantillons : I en bleu, Q en orange, canvas 80 px.'],
            ],
          },
        ],
      },

      {
        id: 'arch',
        icon: 'chip',
        title: 'Architecture',
        desc: 'Schéma fonctionnel SVG interactif du transceiver AD9361. Cliquez sur un bloc pour afficher et modifier ses paramètres dans le panneau de détail.',
        groups: [
          {
            title: 'Blocs SVG',
            rows: [
              ['RX1/RX2 (port)', 'Ports RF d\'entrée — sélection entrée active, 50 Ω, RSSI.'],
              ['LNA + Att (rx)', 'Ampli faible bruit + atténuateur par paliers — mode gain + slider gain RX.'],
              ['Mélangeur RX (rx)', 'Mélangeur quadrature zero-IF — syntoniseur fréquence OL RX.'],
              ['Filtre BB RX (rx)', 'Filtre passe-bas baseband — slider bande passante (0,2 – 18 MHz).'],
              ['ADC (rx)', 'Convertisseur Σ-Δ 12 bits — syntoniseur taux d\'échantillonnage, résolution, horloge.'],
              ['Déc / HB (rx)', 'Filtre décimation half-band — sélection facteur (÷1 / ÷2 / ÷4 / ÷8).'],
              ['TX1/TX2 (port)', 'Ports RF de sortie — sélection sortie active, 50 Ω.'],
              ['Driver + Att (tx)', 'Driver PA + atténuateur numérique — slider atténuation TX.'],
              ['Mélangeur TX (tx)', 'Mélangeur à conversion directe — syntoniseur fréquence OL TX.'],
              ['Filtre BB TX (tx)', 'Filtre LPF de reconstruction — slider bande passante.'],
              ['DAC (tx)', 'Convertisseur 12 bits — syntoniseur taux d\'échantillonnage.'],
              ['Int / HB (tx)', 'Filtre interpolation — sélection facteur (×1 / ×2 / ×4 / ×8).'],
              ['PLL RX (lo)', 'Synthétiseur fractional-N RX — syntoniseur fréquence + état de verrouillage.'],
              ['PLL TX (lo)', 'Synthétiseur fractional-N TX — syntoniseur fréquence + état de verrouillage.'],
              ['AuxADC (sys)', 'ADC auxiliaire + capteur température — températures SoC et FPGA en temps réel.'],
              ['BB PLL (sys)', 'PLL baseband — sélection référence (40 MHz TCXO / 30,72 MHz / 10 MHz ext), horloge, verrou.'],
              ['SPI / ENSM (sys)', 'Bus SPI + machine d\'état ENSM — sélection état (FDD/TDD/Alert/Sleep).'],
              ['Port données (sys)', 'Interface I/Q numérique — sélection LVDS / CMOS, débit RX en temps réel.'],
              ['Zynq PL (fpga)', 'Logique programmable — XC7Z020, cœur Tezuka HDL, horloge PL 100 MHz.'],
            ],
          },
          {
            title: 'Panneau de détail',
            rows: [
              ['Tag de groupe', 'Étiquette colorée correspondant au groupe du bloc.'],
              ['Description', 'Description fonctionnelle du bloc sélectionné.'],
              ['Champs éditables', 'FreqTuner, Slider ou Select — les modifications sont stockées localement.'],
              ['Champs lecture seule', 'Valeurs temps réel reçues par MQTT (températures, horloges, verrouillages).'],
              ['Barre d\'action', 'Apparaît si des changements sont en attente. Reset annule ; Apply publie tout via MQTT.'],
            ],
          },
        ],
      },

      {
        id: 'versions',
        icon: 'versions',
        title: 'Versions',
        desc: 'Informations sur la plateforme matérielle et tableau des versions des composants firmware.',
        groups: [
          {
            title: 'Carte Plateforme',
            rows: [
              ['Modèle / numéro de série', 'Chaîne de modèle et numéro de série (temps réel via MQTT).'],
              ['SoC', 'Zynq-7020 (fixe).'],
              ['Transceiver RF', 'AD9363 (fixe).'],
              ['Cible GCC', 'arm-linux-gnueabihf.'],
            ],
          },
          {
            title: 'Tableau des composants firmware',
            rows: [
              ['Tezuka', 'Version du firmware applicatif.'],
              ['Noyau Linux', 'Chaîne de version du noyau.'],
              ['U-Boot', 'Version du chargeur de démarrage.'],
              ['Bitstream FPGA', 'Version du tissu PL.'],
              ['Root FS', 'Version du squashfs en lecture seule.'],
              ['libiio', 'Version de la bibliothèque IIO.'],
            ],
          },
          {
            title: 'Carte Mise à jour',
            rows: [
              ['Pill de statut', 'Affiche "À jour" ou la disponibilité d\'une mise à jour.'],
              ['Vérifier les mises à jour', 'Interroge le canal de mise à jour.'],
              ['Installer depuis un fichier', 'Permet le chargement manuel du firmware.'],
            ],
          },
        ],
      },

      {
        id: 'network',
        icon: 'network',
        title: 'Réseau',
        desc: 'Configuration des interfaces réseau et graphiques de débit système.',
        groups: [
          {
            title: 'Onglets interface',
            rows: [
              ['LAN', 'Affiche IP eth0, masque, passerelle, DNS, MAC, nom d\'hôte depuis state/net/…'],
              ['Wi-Fi', 'Réservé pour une future interface sans fil.'],
              ['USB', 'Affiche IP usb0, masque, MAC — adresse USB gadget typique Tezuka.'],
            ],
          },
          {
            title: 'Carte Broker MQTT',
            rows: [
              ['Hôte / Port', 'IP du broker et port (1883).'],
              ['Topic de base', 'Racine d\'abonnement state/#.'],
              ['Pill de statut', 'Connecté (vert) / Hors ligne (orange).'],
            ],
          },
          {
            title: 'Tableau des ports de service',
            rows: [
              ['HTTP 80', 'Serveur du dashboard web.'],
              ['RTSP 554', 'Flux vidéo sortant.'],
              ['RTMP 1935', 'Ingestion vidéo.'],
              ['SSH 22', 'Accès shell sécurisé.'],
              ['MQTT 1883', 'Broker MQTT.'],
            ],
          },
          {
            title: 'Graphique Système & débit',
            rows: [
              ['CPU %', 'Utilisation ARM Cortex-A9 (couleur accent).'],
              ['Mémoire %', 'Utilisation RAM (bleu).'],
              ['SoC °C', 'Température Zynq PS (vert).'],
              ['FPGA °C', 'Température PL (corail).'],
              ['RX B/s', 'Débit réception (axe droit, violet).'],
              ['TX B/s', 'Débit émission (axe droit, rose).'],
            ],
          },
        ],
      },

      {
        id: 'calibrate',
        icon: 'target',
        title: 'Calibrage',
        desc: 'Ajustement de l\'oscillateur de référence et courbes de gain en fréquence / gain DAC.',
        groups: [
          {
            title: 'Carte Calibrage fréquence',
            rows: [
              ['Slider PPM oscillateur', '−200 à +200 ppm, pas 0,1 — publie cmd/main/freq_correction en Hz (formule : 40 000 000 + ppm × 40).'],
              ['TextInput PPM', 'Saisie numérique directe. Valider avec Entrée ou en quittant le champ.'],
              ['Bouton Enregistrer', 'Écrit xo_correction dans l\'environnement U-Boot via cmd/system/setenv/xo_correction.'],
              ['Toggle ON / OFF', 'Indicateur de discipline automatique (réservé GPS/GPSDO).'],
            ],
          },
          {
            title: 'Graphique gain vs fréquence',
            rows: [
              ['XYChart (éditable)', '56 points de contrôle de 47 MHz à 6 GHz. Faire glisser verticalement.'],
              ['Bouton Appliquer', 'Apparaît si la courbe est modifiée — publie cmd/main/gain_table_config.'],
            ],
          },
          {
            title: 'Graphique gain DAC vs amplitude',
            rows: [
              ['XYChart (éditable)', '40 points de contrôle de 0 % à 100 % FS. Faire glisser verticalement.'],
            ],
          },
          {
            title: 'Navigation',
            rows: [
              ['Bouton Kalibrate RF', 'Navigue vers la sous-page Kalibrate pour le calibrage par GSM.'],
            ],
          },
        ],
      },

      {
        id: 'kalibrate',
        icon: 'search',
        title: 'Kalibrate (depuis RF)',
        desc: 'Calibre l\'oscillateur à cristal en scannant des stations GSM en direct et en calculant l\'erreur de fréquence. Sous-page de Calibrage.',
        groups: [
          {
            title: 'En-tête du scan',
            rows: [
              ['Select bande', 'GSM850 / GSM-R / GSM900 / EGSM / DCS.'],
              ['Bouton Lancer scan', 'Publie cmd/system/kalibrate/scan <bande>. Spinner pendant le scan.'],
            ],
          },
          {
            title: 'Carte Correction XO',
            rows: [
              ['Correction courante', 'Valeur live en ppm depuis state/main/freq_correction.'],
              ['Résultat Kalibrate', 'ppm et ppb après calibrage terminé.'],
              ['Bouton Appliquer au XO', 'Écrit le résultat dans cmd/main/freq_correction. Visible si statut = done.'],
            ],
          },
          {
            title: 'Tableau canaux GSM',
            rows: [
              ['Chan', 'Numéro de canal GSM.'],
              ['Fréq (MHz)', 'Fréquence centrale du canal.'],
              ['Puissance (dBFS)', 'Puissance du signal mesuré — tableau trié par ordre décroissant.'],
              ['Bouton Kalibrate', 'Lance kal -c sur ce canal — publie cmd/system/kalibrate/run <chan>. Affiche le résultat en ppm.'],
            ],
          },
          {
            title: 'Carte sortie kal',
            rows: [
              ['Fenêtre de log', 'Stdout brut de l\'outil kalibrate, diffusé ligne par ligne via state/system/kalibrate/log.'],
              ['Bouton Effacer', 'Vide l\'affichage sans supprimer l\'historique MQTT.'],
            ],
          },
        ],
      },

      {
        id: 'diagnostic',
        icon: 'pulse',
        title: 'Diagnostic',
        desc: 'Journal noyau (dmesg) à la demande et dump des registres de debug IIO.',
        groups: [
          {
            title: 'Actions',
            rows: [
              ['Bouton Fournir le log', 'Publie cmd/system/logrequest — l\'appareil répond avec les lignes dmesg sur state/system/log.'],
              ['Bouton Debug IIO', 'Publie cmd/system/getdebugiio — l\'appareil répond avec les paires clé-valeur /sys/kernel/debug/iio.'],
            ],
          },
          {
            title: 'Carte Debug IIO (conditionnelle)',
            rows: [
              ['Fenêtre de log', 'Attributs IIO debug en paires clé-valeur, triés alphabétiquement.'],
              ['Bouton Effacer', 'Supprime les entrées affichées.'],
            ],
          },
          {
            title: 'Carte Journal système',
            rows: [
              ['Fenêtre de log', 'Lignes dmesg horodatées (préfixe HH:MM:SS). Défilement automatique vers le bas.'],
              ['Bouton Effacer', 'Vide l\'affichage (ne réinitialise pas les messages MQTT retenus).'],
            ],
          },
        ],
      },

      {
        id: 'reboot',
        icon: 'power',
        title: 'Redémarrage',
        desc: 'Redémarrage contrôlé de l\'appareil avec suivi de reconnexion en temps réel.',
        groups: [
          {
            title: 'Carte Redémarrer (état initial)',
            rows: [
              ['Select mode de redémarrage', 'Normal / Mode sans échec / Chargeur de démarrage.'],
              ['Note d\'avertissement', 'L\'émission TX, l\'enregistrement et les flux sont interrompus (~20 s d\'indisponibilité).'],
              ['Bouton Redémarrer', 'Demande confirmation, puis publie cmd/system/reboot reboot.'],
            ],
          },
          {
            title: 'États de progression',
            rows: [
              ['busy', 'Spinner + compte à rebours (30 s pour le redémarrage, 6 s pour l\'arrêt).'],
              ['waiting', 'Spinner — attente de reconnexion MQTT après le démarrage.'],
              ['done', 'Icône ✓ + "Retour en ligne" + bouton Terminer. MQTT reconnecté.'],
            ],
          },
          {
            title: 'Carte Système (toujours visible)',
            rows: [
              ['Tableau compact', 'Modèle · Version firmware · Version Linux · Uptime · Numéro de série.'],
            ],
          },
        ],
      },

      {
        id: 'operator',
        icon: 'user',
        title: 'Opérateur',
        desc: 'Identité de la station stockée dans localStorage. Utilisée comme callsign DATV et affichée dans la barre latérale.',
        groups: [
          {
            title: 'Carte Profil',
            rows: [
              ['Nom TextInput', 'Nom complet de l\'opérateur.'],
              ['Callsign TextInput', 'Indicatif radioamateur — converti en majuscules lors de la sauvegarde.'],
              ['Locator de grille TextInput', 'Locator Maidenhead, ex. JN18cv.'],
              ['Boutons Enregistrer / Réinitialiser', 'Actifs uniquement si des modifications non sauvegardées existent.'],
            ],
          },
          {
            title: 'Carte Aperçu identité',
            rows: [
              ['Avatar + détails', 'Prévisualisation en temps réel du nom, callsign et locator.'],
            ],
          },
        ],
      },

      {
        id: 'persistent',
        icon: 'save',
        title: 'Stockage persistant',
        desc: 'Lecture et écriture des variables d\'environnement U-Boot (fw_printenv / fw_setenv) via MQTT.',
        groups: [
          {
            title: 'Carte Nouvelle variable',
            rows: [
              ['Nom TextInput', 'Alphanumériques et underscore uniquement. Le bouton Enregistrer est désactivé sinon.'],
              ['Valeur TextInput', 'Valeur à définir.'],
              ['Bouton Enregistrer', 'Publie cmd/system/setenv/<nom>. Pill "saved" affiché pendant 2 s.'],
            ],
          },
          {
            title: 'Tableau des variables',
            rows: [
              ['Filtre TextInput', 'Recherche dans les noms et les valeurs (insensible à la casse).'],
              ['Colonne Variable', 'Nom de la variable U-Boot (monospace, trié alphabétiquement).'],
              ['Colonne Valeur', 'Éditable — TextInput ou textarea multi-ligne si la valeur contient des retours à la ligne.'],
              ['Enregistrer / Réinitialiser par ligne', 'Apparaît si la valeur est modifiée. Enregistrer publie cmd/system/setenv/<nom>.'],
            ],
          },
        ],
      },

      {
        id: 'gpio',
        icon: 'circuit',
        title: 'GPIO',
        desc: 'Basculement des broches GPIO individuelles (gpiochip0). État reçu via state/gpio/<n>.',
        groups: [
          {
            title: 'Grille GPIO',
            rows: [
              ['Tuile de broche', 'Affiche le numéro GPIO, un point coloré (vert = ON, terne = OFF) et l\'état.'],
              ['Clic', 'Bascule la broche — publie cmd/gpio/<broche> (0 ou 1).'],
            ],
          },
        ],
      },

      {
        id: 'performance',
        icon: 'chip',
        title: 'Performance',
        desc: 'Sélection du profil de surcadençage CPU.',
        groups: [
          {
            title: 'Carte CPU',
            rows: [
              ['Select profil d\'overclocking', 'Profils disponibles chargés depuis state/system/overclock_cap à la connexion MQTT. Publie cmd/system/overclock <profil>. Effet au prochain démarrage.'],
            ],
          },
        ],
      },

      {
        id: 'mqtt',
        icon: 'network',
        title: 'Référence MQTT',
        desc: 'Convention de topics utilisée par le dashboard. Toutes les valeurs sont des chaînes UTF-8. Les drapeaux booléens utilisent "0" / "1".',
        groups: [
          {
            title: 'Structure des topics',
            rows: [
              ['state/<chemin>', 'Publié par l\'appareil (généralement retenu). Le dashboard s\'abonne à state/#.'],
              ['cmd/<chemin>', 'Publié par le dashboard pour contrôler l\'appareil.'],
              ['cmd/pluto/<callsign>/<chemin>', 'Commandes spécifiques PlutoSDR (contrôleur DATV).'],
              ['dt/pluto/<callsign>/<clé>', 'Télémétrie PlutoSDR (état DATV).'],
            ],
          },
          {
            title: 'Topics state principaux',
            rows: [
              ['state/rx/frequency', 'Fréquence OL RX en Hz.'],
              ['state/tx/frequency', 'Fréquence OL TX en Hz.'],
              ['state/rx/sampling', 'Taux d\'échantillonnage RX en Hz.'],
              ['state/rx/bandwidth', 'Bande passante RF RX en Hz.'],
              ['state/rx/gain', 'Gain matériel RX en dB.'],
              ['state/rx/gain_mode', 'Mode AGC : manual / slow_attack / fast_attack.'],
              ['state/rx/overload', '"1" si l\'ADC sature.'],
              ['state/tx/overload', '"1" si les étages de filtre TX débordent.'],
              ['state/rx/sweep/activate', '"1" en mode sweep.'],
              ['state/rx/span', 'Span sweep ou d\'affichage en Hz.'],
              ['state/main/freq_correction', 'Fréquence TCXO de référence en Hz (base = 40 000 000).'],
              ['state/net/<iface>/ip', 'Adresse IP de l\'interface (iface = usb0 ou eth0).'],
              ['state/system/kalibrate/status', 'scanning / calibrating / done / error / writing.'],
              ['state/system/kalibrate/channels', 'Tableau JSON des canaux GSM scannés [{chan, freq, power}].'],
              ['state/system/log', 'Une ligne dmesg par message (non retenu).'],
            ],
          },
        ],
      },
    ],
  },
};

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------
function Documentation() {
  const [lang, setLang] = useS4('en');
  const [active, setActive] = useS4(null);
  const sectionRefs = useR4({});
  const tocRef = useR4(null);

  const content = DOCS[lang];

  // Scroll to section
  const scrollTo = (id) => {
    setActive(id);
    const el = sectionRefs.current[id];
    if (el) el.scrollIntoView({ behavior: 'smooth', block: 'start' });
  };

  // Highlight active section on scroll via IntersectionObserver
  useE4(() => {
    const observers = [];
    content.sections.forEach(({ id }) => {
      const el = sectionRefs.current[id];
      if (!el) return;
      const obs = new IntersectionObserver(
        ([entry]) => { if (entry.isIntersecting) setActive(id); },
        { threshold: 0.25 }
      );
      obs.observe(el);
      observers.push(obs);
    });
    return () => observers.forEach(o => o.disconnect());
  }, [lang]);

  const accentStyle = { color: 'var(--accent)' };

  return (
    <div className="page docs-page">
      {/* ---- Page header ---- */}
      <div className="docs-header">
        <div>
          <h1 className="docs-title">{content.intro.title}</h1>
          <p className="docs-subtitle">{content.intro.desc}</p>
        </div>
        <button
          className="btn primary docs-lang-btn"
          onClick={() => setLang(l => l === 'en' ? 'fr' : 'en')}
          title="Switch language / Changer de langue"
        >
          <Icon name="globe" size={15} />
          {content.langLabel}
        </button>
      </div>

      <div className="docs-layout">
        {/* ---- Table of contents (sticky) ---- */}
        <nav className="docs-toc" ref={tocRef}>
          <div className="docs-toc-title">{content.toc}</div>
          {content.sections.map(({ id, icon, title }) => (
            <button
              key={id}
              className={`docs-toc-item ${active === id ? 'active' : ''}`}
              onClick={() => scrollTo(id)}
            >
              <Icon name={icon} size={14} />
              <span>{title}</span>
            </button>
          ))}
        </nav>

        {/* ---- Sections ---- */}
        <div className="docs-content">
          {content.sections.map(({ id, icon, title, desc, groups }) => (
            <section
              key={id}
              className="docs-section"
              ref={el => { sectionRefs.current[id] = el; }}
            >
              <div className="docs-sec-head">
                <span className="docs-sec-icon" style={accentStyle}>
                  <Icon name={icon} size={20} />
                </span>
                <h2 className="docs-sec-title">{title}</h2>
              </div>
              {desc && <p className="docs-sec-desc">{desc}</p>}

              {groups.map((group, gi) => (
                <div key={gi} className="docs-group">
                  <div className="docs-group-title">{group.title}</div>
                  <table className="docs-table">
                    <tbody>
                      {group.rows.map(([name, description], ri) => (
                        <tr key={ri}>
                          <td className="docs-td-name mono">{name}</td>
                          <td className="docs-td-desc">{description}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              ))}
            </section>
          ))}
        </div>
      </div>
    </div>
  );
}

window.Documentation = Documentation;

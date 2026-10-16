# zynqrx options

DVB-S, DVB-S2 and DVB-S2X test signals and reception. Source: `zynqrx.cpp` (option parser and header comment).

Subcommands:

- `zynqrx rx` receives from a recording or from the radio.
- `zynqrx gen` writes a synthetic signal.
- `zynqrx bench` measures CPU cost.
- `zynqrx ranges` lists accepted symbol rates.

## `zynqrx rx [recording] [options]`

Input is either a recording file or the radio (`--iio`).

| Option | Argument | Meaning |
|---|---|---|
| `--iio` | `[uri]` | Live input from the AD936x through libiio. The URI is optional; the default is the local context. It can also be `ip:...` or `usb:...`. Needs a build with `IIO=1`. |
| `--freq` | MHz | RF centre frequency (live input). |
| `--rate` | Msps | Sample rate. Default 10 for recordings. With `--prefiltered` it defaults to 2 x `--rs`. |
| `--bw` | MHz | Analog bandwidth (default: same as `--rate`). |
| `--gain` | dB | Fixed RX gain (default: the radio's slow-attack AGC). |
| `--format` | `cs8`, `cu8` or `cf32` | Recording sample format. |
| `--rs` | Msym/s | Symbol rate. 0, the default, means it is measured from the spectrum. |
| `--std` | `0`, `1` or `2` | Limits the search: 0 for both, 1 for DVB-S, 2 for DVB-S2. Use 2 on the board, or failed hunts fall into the slow DVB-S Viterbi decoder. |
| `--roll` | value | Roll-off. Default 0. With `--prefiltered` it defaults to 0.35. |
| `--prefiltered` | | The radio's FPGA or FIR already did the RRC matched filtering, at 2 samples per symbol. Needs `--rs`. The receiver then skips its own resampler and filter. |
| `--center` | Hz | The carrier offset from the centre is known, so the spectrum search is skipped. Needs `--rs`. |
| `--spec-secs` | seconds | Spectrum analysis interval (default 0.25). |
| `--isi` | n | Input stream identifier (default -1). |
| `--pls` | n | Physical layer scrambling code (default -1). |
| `--ts` | file | Write the transport stream to a file. |
| `--secs` | seconds | Run time (0 = until Ctrl-C). |
| `--max-lag` | seconds | Default 4, 0 = off. When the receiver falls this far behind, the oldest samples are dropped. |
| `--nice` | N | Scheduling priority. Default -10 for live input and `--mqtt`, otherwise 0. Needs root. |
| `--fast-timing` | `on`, `off` or `auto` | Cheaper symbol timing. `auto` means on above 0.5 Msym/s when the symbol rate is given. |
| `--ldpc8` | | The 8-bit CPU LDPC decoder at every code rate. Cheaper, but about 2 dB less sensitive at QPSK 2/5. DVB-S2 only. |
| `--ccm` | | Constant coding and modulation. After the first lock it searches only for that MODCOD, which is far cheaper. |
| `--mods` | list | Modulations to search for: `qpsk`, `8psk`, `16apsk`, `32apsk` (default all). Fewer means a cheaper search. |
| `--presence` / `--no-presence` | | Presence gate. A cheap test looks for the signal, plus a 0.6 s look every 4 s. Default on with `--mqtt`. |

### Outputs

| Option | Argument | Meaning |
|---|---|---|
| `--udp` | `addr:port` | Send the transport stream as UDP, 7 packets (1316 bytes) per datagram. Unicast or multicast. |
| `--ttl` | n | Multicast hop limit (default 16). |
| `--udp-if` | local addr | Local interface address the multicast leaves from. |
| `--ws` | port | Serve the transport stream over WebSocket at `ws://board:port/stream.ts`. |
| `--ws-keyframe` | | A new client waits (at most 5 s) for a keyframe before it gets the stream. |

### MQTT control

| Option | Argument | Meaning |
|---|---|---|
| `--mqtt` | `broker[:port]` | Control and status over MQTT in the longmynd scheme, topics `cmd/zynqrx/...` and `dt/zynqrx/...`. |
| `--start` | | Start receiving at once. Without it, the receiver idles until `cmd/zynqrx/process start`. |

Commands (`cmd/zynqrx/...`):

| Command | Meaning |
|---|---|
| `frequency` | In kHz, 70000 to 6000000. |
| `sr` | In kS, 33 to 7680. |
| `tsip` | IPv4 of the transport stream output; the port stays that of `--udp`, else 1234. Takes effect at once. |
| `swport`, `polar` | Accepted and reported back, but there is no such hardware. |
| `process` | `start` or `stop`. Frequency and sr sent while stopped are kept for the next start. |
| `gain` | RX gain in dB (0 - 73), or `auto` for the radio's AGC. Same as `--gain`. |
| `bw` | Analog bandwidth in kHz. 0 (the default) = 3 x the symbol rate, within the AD9361's 0.2 - 56 MHz. Same as `--bw`, which without MQTT defaults to the sample rate. |
| `std` | `0` (both), `1` (DVB-S) or `2` (DVB-S2). Same as `--std`. |
| `roll` | Roll-off 0.05 - 0.35, 0 = default (0.35 on the radio filter). Same as `--roll`. |
| `mods` | Modulations to search for, e.g. `qpsk,8psk`. Same as `--mods`. |
| `ccm`, `presence`, `ldpc8` | `0` or `1`. Same as `--ccm`, `--presence`/`--no-presence`, `--ldpc8`. |
| `fasttiming` | `on`, `off` or `auto`. Same as `--fast-timing`. |
| `maxlag` | Seconds, 0 = off. Same as `--max-lag`. |
| `isi`, `pls` | Input stream identifier (-1 - 255) and scrambling code (-1 - 262142), -1 = any. Same as `--isi`, `--pls`. |
| `wskeyframe` | `0` or `1`, takes effect at once. Same as `--ws-keyframe`. |

Every command except `wskeyframe` restarts the receiver when it is running, which takes a few seconds (while stopped the value is kept for the next start). Each setting is echoed on `dt/zynqrx/set/<name>` (same names: `gain`, `bw`, `std`, `roll`, `mods`, `ccm`, `presence`, `fasttiming`, `ldpc8`, `maxlag`, `isi`, `pls`, `wskeyframe`). A refused value is logged and ignored. The initial values come from the command line options.

Not available over MQTT: `--prefiltered` (chosen from the symbol rate), `--rate`, `--center`, `--ts`, `--secs`, `--nice`, `--ttl`, `--udp-if`, `--udp` port.

A new frequency or symbol rate restarts the receiver too. Without `--freq` and `--rs` the receiver waits for frequency and sr.

Status (`dt/zynqrx/...`) is published every second, not retained, QoS 2:

- `process`, `rx_state` (Init, Hunting, found header, demod_s, demod_s2)
- `set/sr|frequency|swport|tsip`
- `lna_gain`, `agc2`, `puncrate`, `carrier_frequency` (kHz), `lnb_supply`, `polarisation`
- `symbolrate` (kS), `viterbi_error`, `ber`, `mer` (dB), `margin_db`
- `bch_uncorect`, `bch_errors`, `ldpc_errors`
- `service_name`, `provider_name`, `ts_null` (%), `es_pid`/`es_type`
- `modulation`, `fec`, `short_frame`, `pilots`, `rolloff`, `matype1`, `matype2`

Not sent: `poweri`, `powerq`, `constel_i`, `constel_q`, `agc1`, `videobuffer`, `audiobuffer`, `transdelay`.

Notes:

- `ber` and `ldpc_errors` are estimates from the pre-FEC bit error rate. `bch_errors` is 0.
- `mer` is sent from "found header" on.
- `ber`, `ldpc_errors` and `margin_db` are sent only once the transport stream is locked.
- From 261 kS the radio's RRC filter is used (2 samples per symbol). Below that, the radio runs at 2.1 Msps and the receiver filters in software.

## `zynqrx gen out.cs8 [options]`

Writes the L-band IF of a satellite transponder as complex baseband. Nothing is transmitted. The payload is the built-in test programme, or the packets of `--ts` (repeated).

| Option | Meaning |
|---|---|
| `--secs` | Duration (default 10). |
| `--rate` | Sample rate in Msps (default 10). |
| `--rs` | Symbol rate in Msym/s (default `min(5e6, max for the rate)`). |
| `--std` | `s` or `1` for DVB-S, otherwise DVB-S2. |
| `--mod` | `qpsk`, `8psk`, `16apsk` or `32apsk`. |
| `--fec` | Code rate, default 2/3. |
| `--roll` | Roll-off. |
| `--short` | Short frames. |
| `--pilots` | Pilots. |
| `--vcm` | VCM. |
| `--inv` | Spectrum inversion. |
| `--snr` | Es/N0 in dB. |
| `--cfo` | Carrier offset. |
| `--ppm` | Clock error. |
| `--pn` | LNB phase noise mask 0 to 3 (EN 302 307-1 H.8 typical and critical, M.2 non DTH). |
| `--dc` | Receiver DC offset fault. |
| `--iq-db`, `--iq-deg` | Receiver IQ gain and phase imbalance faults. |
| `--seed` | Random seed. |
| `--ts` | Transport stream file to use as payload. |
| `--format` | `cs8` or `cf32`. |
| `--prefiltered` | Also accepted. |

## `zynqrx bench [gen options]`

Makes the test signal in memory, runs the receiver on it the way the engine does (frames are dropped when the decoder threads are behind) and reports the CPU time of the receiver thread and of all threads per second of signal. It takes the `gen` options plus:

- `--prefiltered`
- `--center`
- `--blocking` (wait for the decoder threads instead of dropping frames)
- `--hint` (search only for the standard that was generated)
- `--realtime`
- `--spec-secs`
- `--ldpc8`
- `--fast-timing`

## `zynqrx ranges`

Prints the symbol rates the receiver accepts at each input sample rate.

## Environment variables

- `DECT2_COMPUTE=cpu|gpu|auto` chooses where the error correction runs.
- `DECT2_DVBS_REF=1` uses the DVB-S chain as it was before the speed-up.

## On the board

`package/zynqrx/start_zynqrx.sh` runs:

```
zynqrx rx --iio --mqtt 127.0.0.1:1883 --freq 437 --rs 0.333 --std 2 --mods qpsk,8psk --ccm --udp 230.0.0.3:1234 --ws 8080 [--start]
```

`--start` is added when `AUTOSTART=1` (the default). Extra arguments you pass are appended, and later options win. `S97datv` uses `AUTOSTART=0`, so the receiver idles until the Dashboard starts it with `cmd/zynqrx/process start`.

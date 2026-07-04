# Validate maia-kmod crash fix on nano hardware

Build the 0003 TOCTOU-race patch into tezuka firmware for the nano board,
deploy via MSD, and prove the fix is stable: 5 clean reboots, 90-minute soak,
recording and rxbuffer device paths still work.

- **Facts** — `goals/validate-maia-kmod-crash-fix/facts.md`
- **Plan** — `goals/validate-maia-kmod-crash-fix/plan.md`

**Done when:**
- CI builds nano firmware with 0003 applied
- Firmware flashed via MSD, device boots and is reachable
- `harness.py check` passes (recording + rxbuffer dev nodes clean)
- 5/5 reboot cycles without panic (dmesg clean)
- 90-minute soak without kernel splats or loss of connectivity

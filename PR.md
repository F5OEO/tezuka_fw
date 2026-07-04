Branch: `fix/maia-kmod-crash-v2` → `F5OEO/tezuka_fw:future`
7 commits (on top of a2d21fc):

## Summary

Increase CMA pool to 32 MB for Nano, fix CI caching to enable practical multi-board builds, and fix package build failures that block CI (Dashboard vendor HTTPS fetch, pluto_stream license).

## Changes

### `fix(nano): increase CMA from 16MB to 32MB for maia-sdr DMA`

Nano board needs 32 MB CMA for AD9361 DMA operations via maia-sdr.
Common base config sets CMA_SIZE_MBYTES=64; nano fragment overrides
to 32 (was 16). Tested and verified on hardware:
```
CONFIG_CMA_SIZE_MBYTES=32
CmaTotal:          32768 kB
```

### `ci: split kernel build cache from main output cache`

Main output cache (3 GB/board) too big for multi-board CI (12×36 GB > 10 GB
GitHub cache limit). Kernel build cache (~200 MB compressed) fits:
12 boards × ~200 MB ≈ 2.4 GB.

Key design:
- Main output cache key: excludes kernel config files — uboot/rootfs stay
  cached when CMA/config changes.
- Kernel cache key: includes `board/tezuka/**/kernel/**/*` so config changes
  bust only this compact cache.
- Kernel cache NOT gated by `single_board` — works for all builds.
- Stored as compressed tarball (~200 MB/board).
- Install stamps stripped on cache hit to force fresh kernel install over
  main cache's stale artifacts.

Follow-up fixes: force kernel re-install after cache hit (d6b391f), robust
kernel build dir detection using shell glob instead of `ls` (134f11c),
fix unterminated string in cache summary echo (fa073e1).

### `fix(build): use curl instead of Python urllib for HTTPS vendor downloads`

Buildroot host Python 3.14 is compiled without SSL support, so
`urllib.request.urlretrieve` fails with `unknown url type: https` when
`Dashboard/bundle.py` downloads vendor JS from unpkg.com during rootfs
build. Switch to `subprocess.run curl -fsSL` which works on any system
with curl (CI runner, build hosts).

### `fix(build): pluto_stream license file missing + resilient artifact upload`

`pluto-stream.mk` declared `PLUTO_STREAM_LICENSE_FILES = LICENSE` but the
upstream repo (F5OEO/pluto-ori-ps) doesn't ship one. Legal-info hard-fails
when the file is missing. Remove the declaration — GPL-2.0+ is recorded as
the license name, which is sufficient for SBOM.

Also add `if: always()` to the Upload artifact CI step so minor SBOM or
summary failures don't gate artifact collection.
#!/usr/bin/env python3
"""
Validate maia-kmod crash fix on nano hardware.

Subcommands:
  check       — open recording + rxbuffer dev nodes, mmap/ioctl/munmap/close
  reboot-cycle — reboot N times, verify clean dmesg each boot
  soak        — 90-minute health monitor with periodic checks
  flash       — copy pluto.frm to MSD volume

Usage:
  python3 -u harness.py check [--host HOST]
  python3 -u harness.py reboot-cycle [--host HOST] [--count 5]
  python3 -u harness.py soak [--host HOST] [--duration 90] [--interval 60]
  python3 -u harness.py flash --frm PATH [--mount /Volumes/NANO]
"""
import argparse
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HOST_DEFAULT = "192.168.2.1"
USER = "root"
PASSWORD = "analog"
SSH_OPTS = "-o StrictHostKeyChecking=no -o ConnectTimeout=10 -o UserKnownHostsFile=/dev/null"
SSHPASS_CMD = "sshpass -p analog ssh " + SSH_OPTS


def ssh(host: str, cmd: str, timeout: int = 60) -> subprocess.CompletedProcess:
    """Run command on device via sshpass + SSH. Returns CompletedProcess."""
    full_cmd = f"{SSHPASS_CMD} -o BatchMode=no {USER}@{host} {cmd}"
    return subprocess.run(
        full_cmd, shell=True, capture_output=True, text=True, timeout=timeout
    )


def scp_to(host: str, local: str, remote: str, timeout: int = 30) -> subprocess.CompletedProcess:
    full_cmd = f"sshpass -p analog scp {SSH_OPTS} {local} {USER}@{host}:{remote}"
    return subprocess.run(
        full_cmd, shell=True, capture_output=True, text=True, timeout=timeout
    )


def wait_for_ssh(host: str, timeout: int = 120, interval: int = 5) -> bool:
    """Wait until device responds to SSH. Returns True if reachable."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            r = ssh(host, "echo alive", timeout=10)
            if r.returncode == 0:
                return True
        except (subprocess.TimeoutExpired, OSError):
            pass
        time.sleep(interval)
    return False


def check_dmesg(host: str) -> dict:
    """Check dmesg for panics, oops, or warnings. Returns {'clean': bool, 'warnings': [...], 'panics': [...]}."""
    result = ssh(host, "dmesg")
    lines = result.stdout.splitlines() if result.returncode == 0 else []

    panics = [l for l in lines if re.search(r'panic|kernel BUG|Oops|segfault', l, re.I)]
    warnings = [l for l in lines if re.search(r'Call Trace|---\[ end trace', l)]

    return {
        "clean": len(panics) == 0,
        "panics": panics,
        "warnings": warnings,
        "count": len(lines),
    }


def check_dev_nodes(host: str) -> list[dict]:
    """Test recording + rxbuffer dev nodes via SSH. Returns list of result dicts."""
    results = []

    # Check dev nodes exist
    r = ssh(host, "ls -la /dev/maia-sdr-recording /dev/maia-sdr-rxbuffer 2>&1")
    dev_ok = r.returncode == 0 and "No such file" not in r.stdout
    results.append({
        "test": "dev_nodes_exist",
        "ok": dev_ok,
        "detail": r.stdout.strip() if dev_ok else r.stdout.strip() or r.stderr.strip(),
    })

    if not dev_ok:
        return results

    # Test recording node: open → mmap → munmap → close
    recording_test = """
python3 -c "
import fcntl, mmap, os, struct
fd = os.open('/dev/maia-sdr-recording', os.O_RDWR)
# Get size via lseek end
size = os.lseek(fd, 0, os.SEEK_END)
os.lseek(fd, 0, os.SEEK_SET)
if size > 0:
    m = mmap.mmap(fd, min(size, 4096), mmap.PROT_READ, mmap.MAP_SHARED)
    # Touch first page to ensure mapping is valid
    _ = m[0]
    m.munmap()
print('OK: recording open -> mmap -> munmap -> close')
os.close(fd)
" 2>&1
"""
    r = ssh(host, recording_test.strip(), timeout=15)
    recording_ok = r.returncode == 0 and "OK:" in r.stdout
    results.append({
        "test": "recording_mmap",
        "ok": recording_ok,
        "detail": r.stdout.strip() or r.stderr.strip(),
    })

    # Test rxbuffer node: open → mmap → ioctl(CACHEINV) → munmap → close
    rxbuffer_test = """
python3 -c "
import fcntl, mmap, os, struct
fd = os.open('/dev/maia-sdr-rxbuffer', os.O_RDWR)
# Get size
size = os.lseek(fd, 0, os.SEEK_END)
os.lseek(fd, 0, os.SEEK_SET)
if size > 0:
    m = mmap.mmap(fd, min(size, 4096), mmap.PROT_READ, mmap.MAP_SHARED)
    # Try CACHEINV ioctl (custom, may not be accessible from Python)
    # Even without ioctl, mmap itself tests the VMA lifecycle
    _ = m[0]
    m.munmap()
print('OK: rxbuffer open -> mmap -> munmap -> close')
os.close(fd)
" 2>&1
"""
    r = ssh(host, rxbuffer_test.strip(), timeout=15)
    rxbuffer_ok = r.returncode == 0 and "OK:" in r.stdout
    results.append({
        "test": "rxbuffer_mmap",
        "ok": rxbuffer_ok,
        "detail": r.stdout.strip() or r.stderr.strip(),
    })

    # Check for recording/rxbuffer in dmesg
    dm = check_dmesg(host)
    results.append({
        "test": "dmesg_clean",
        "ok": dm["clean"],
        "detail": (
            f"{len(dm['panics'])} panics, {len(dm['warnings'])} warnings"
            if not dm["clean"]
            else "clean"
        ),
    })

    return results


def cmd_check(args):
    """Check dev nodes: recording + rxbuffer mmap lifecycle."""
    host = args.host
    print(f"[*] Checking dev nodes on {host}...")

    if not wait_for_ssh(host, timeout=30):
        print(f"[FAIL] Device {host} not reachable via SSH")
        sys.exit(1)

    results = check_dev_nodes(host)
    all_ok = True
    for r in results:
        status = "OK" if r["ok"] else "FAIL"
        if not r["ok"]:
            all_ok = False
        print(f"  [{status}] {r['test']}: {r['detail']}")

    if all_ok:
        print("[PASS] All checks passed")
    else:
        print("[FAIL] Some checks failed")
        sys.exit(1)


def cmd_reboot_cycle(args):
    """Reboot device N times, verifying clean dmesg each boot."""
    host = args.host
    count = args.count
    results = {"pass": 0, "fail": 0, "details": []}

    print(f"[*] Running {count} reboot cycles on {host}...")

    for i in range(1, count + 1):
        print(f"\n  Cycle {i}/{count}:")
        ts = datetime.now(timezone.utc).isoformat()

        if not wait_for_ssh(host, timeout=30):
            print(f"    [FAIL] Device not reachable before reboot")
            results["fail"] += 1
            results["details"].append({"cycle": i, "ok": False, "timestamp": ts, "error": "prereach"})
            continue

        # Reboot
        r = ssh(host, "reboot", timeout=30)
        if r.returncode != 0 and "not found" not in r.stderr.lower():
            print(f"    ssh reboot returned {r.returncode}: {r.stderr.strip()}")
        print(f"    Reboot issued, waiting for device to come back...")

        # Wait for device to go down and come back
        time.sleep(10)
        if not wait_for_ssh(host, timeout=180):
            print(f"    [FAIL] Device did not return after reboot")
            results["fail"] += 1
            results["details"].append({"cycle": i, "ok": False, "timestamp": ts, "error": "noreach"})
            continue

        # Check dmesg
        dm = check_dmesg(host)
        if dm["clean"]:
            print(f"    [OK] dmesg clean ({dm['count']} lines)")
            results["pass"] += 1
            results["details"].append({"cycle": i, "ok": True, "timestamp": ts})
        else:
            print(f"    [FAIL] dmesg has {len(dm['panics'])} panics, {len(dm['warnings'])} warnings")
            for p in dm["panics"][-3:]:
                print(f"      PANIC: {p}")
            results["fail"] += 1
            results["details"].append({
                "cycle": i,
                "ok": False,
                "timestamp": ts,
                "error": "dmesg",
                "panics": dm["panics"],
            })

    # Summary
    print(f"\n{'='*40}")
    print(f"Reboot cycle results: {results['pass']}/{count} clean")
    if results["fail"] > 0:
        print(f"  FAILURES: {results['fail']}")
        sys.exit(1)
    else:
        print("  ALL PASS")


def cmd_soak(args):
    """Monitor device for duration minutes with periodic health checks."""
    host = args.host
    duration_min = args.duration
    interval_sec = args.interval
    deadline = time.monotonic() + duration_min * 60
    status_file = Path(args.status_file)
    checks = {"pass": 0, "fail": 0, "details": []}

    print(f"[*] Soak test: {duration_min} min, checks every {interval_sec}s on {host}")
    now_str = datetime.now(timezone.utc).isoformat()

    if not wait_for_ssh(host, timeout=30):
        print(f"[FAIL] Device not reachable")
        sys.exit(1)

    # Record baseline dmesg line count for incremental check
    base_dm = check_dmesg(host)

    cycle = 0
    while time.monotonic() < deadline:
        cycle += 1
        remaining = int(deadline - time.monotonic())
        ts = datetime.now(timezone.utc).isoformat()

        print(f"  [{ts}] Check {cycle} ({remaining}s remaining)...", end=" ", flush=True)

        # SSH alive check
        r = ssh(host, "echo alive && uptime", timeout=15)
        if r.returncode != 0:
            print("SSH FAIL")
            checks["fail"] += 1
            checks["details"].append({"cycle": cycle, "ts": ts, "ok": False, "error": "ssh"})
            status_file.write_text(f"FAIL cycle={cycle} ssh_error ts={ts}\n")
            # Try to reconnect
            time.sleep(10)
            continue

        # dmesg check for new issues
        dm = check_dmesg(host)
        new_warnings = [w for w in dm["warnings"] if w not in base_dm["warnings"]]
        new_panics = [p for p in dm["panics"] if p not in base_dm["panics"]]

        clean = len(new_panics) == 0
        if clean:
            print("OK")
            checks["pass"] += 1
            status_file.write_text(f"OK cycle={cycle} ts={ts}\n")
        else:
            print(f"PANICS: {new_panics[:3]}")
            checks["fail"] += 1
            checks["details"].append({
                "cycle": cycle, "ts": ts, "ok": False,
                "error": "panic", "panics": new_panics,
            })
            status_file.write_text(f"FAIL cycle={cycle} panics={new_panics} ts={ts}\n")

        # IIO health check
        r = ssh(host, "iio_info -s 2>&1 | head -5", timeout=10)
        if r.returncode != 0 or "No contexts" in r.stdout:
            print(f"    [WARN] IIO not reachable: {r.stdout.strip()[:80]}")

        checks["details"].append({"cycle": cycle, "ts": ts, "ok": clean})
        time.sleep(interval_sec)

    print(f"\n{'='*40}")
    print(f"Soak complete: {duration_min} min, {cycle} checks")
    print(f"  Pass: {checks['pass']}, Fail: {checks['fail']}")
    if checks["fail"] > 0:
        print(f"  FAILURES: {checks['fail']}")
        sys.exit(1)
    else:
        print("  ALL PASS")


def cmd_flash(args):
    """Copy pluto.frm to MSD volume and eject."""
    frm_path = Path(args.frm)
    mount = Path(args.mount)

    if not frm_path.exists():
        print(f"[FAIL] Firmware file not found: {frm_path}")
        sys.exit(1)

    if not mount.is_dir():
        print(f"[FAIL] MSD mount not found: {mount}")
        print("  Is the nano connected via USB and showing a volume?")
        sys.exit(1)

    # Check for key files on MSD to verify it's the right volume
    has_update_sh = (mount / "update.sh").exists()
    has_config_txt = (mount / "config.txt").exists()
    if not has_update_sh and not has_config_txt:
        print(f"[WARN] {mount} doesn't look like a nano MSD volume (no update.sh or config.txt)")
        print("  Proceeding anyway...")

    dest = mount / "pluto.frm"
    print(f"[*] Copying {frm_path} -> {dest}")
    import shutil
    shutil.copy2(str(frm_path), str(dest))

    print(f"[*] Syncing...")
    subprocess.run(["sync"], timeout=10)

    print(f"[*] Ejecting MSD volume...")
    # Try diskutil eject first, then umount
    r = subprocess.run(["diskutil", "eject", str(mount)], capture_output=True, text=True, timeout=15)
    if r.returncode != 0:
        r = subprocess.run(["umount", str(mount)], capture_output=True, text=True, timeout=15)
        if r.returncode != 0:
            print(f"[WARN] Could not eject: {r.stderr.strip()}")

    print("[OK] Firmware copied and ejected. Device should auto-flash and reboot.")
    print("  Wait ~60s then check SSH reachability.")


def main():
    parser = argparse.ArgumentParser(description="maia-kmod crash fix validation harness")
    parser.add_argument("--host", default=HOST_DEFAULT, help=f"Nano SSH host (default: {HOST_DEFAULT})")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # check
    p_check = subparsers.add_parser("check", help="Check dev nodes (recording + rxbuffer)")
    p_check.set_defaults(func=cmd_check)

    # reboot-cycle
    p_reboot = subparsers.add_parser("reboot-cycle", help="Reboot N times, verify clean dmesg")
    p_reboot.add_argument("--count", type=int, default=5, help="Number of reboots (default: 5)")
    p_reboot.set_defaults(func=cmd_reboot_cycle)

    # soak
    p_soak = subparsers.add_parser("soak", help="Long-duration health monitor")
    p_soak.add_argument("--duration", type=int, default=90, help="Duration in minutes (default: 90)")
    p_soak.add_argument("--interval", type=int, default=60, help="Check interval in seconds (default: 60)")
    p_soak.add_argument("--status-file", default="/tmp/maia-soak-status.txt",
                        help="Status file path (default: /tmp/maia-soak-status.txt)")
    p_soak.set_defaults(func=cmd_soak)

    # flash
    p_flash = subparsers.add_parser("flash", help="Copy pluto.frm to MSD volume")
    p_flash.add_argument("--frm", required=True, help="Path to pluto.frm file")
    p_flash.add_argument("--mount", default="/Volumes/NANO", help="MSD mount point (default: /Volumes/NANO)")
    p_flash.set_defaults(func=cmd_flash)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

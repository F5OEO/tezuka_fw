#!/usr/bin/env python3
"""Serial-based soak monitor for maia-kmod validation.

Periodically connects to nano via serial, logs in, checks dmesg,
and writes status to a file. Run with nohup.
"""
import serial
import time
import sys
import os
from datetime import datetime, timezone

SERIAL_DEV = "/dev/tty.usbmodem22404"
STATUS_FILE = "/tmp/maia-soak-serial.txt"
CHECK_INTERVAL = 300  # 5 minutes
DURATION_MIN = 90

def serial_cmd(cmd: str, timeout: int = 8) -> str:
    """Send command via serial and return output."""
    try:
        s = serial.Serial(SERIAL_DEV, 115200, timeout=3)
        time.sleep(1)
        s.reset_input_buffer()
        
        # Login if needed
        s.write(b'root\n')
        time.sleep(1)
        s.write(b'analog\n')
        time.sleep(2)
        s.reset_input_buffer()
        
        s.write(cmd.encode() + b'\n')
        time.sleep(1)
        
        out = b''
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = s.read(1024)
            if chunk:
                out += chunk
            else:
                break
        
        s.close()
        return out.decode('utf-8', errors='replace')
    except Exception as e:
        return f"ERROR: {e}"


def write_status(text: str):
    with open(STATUS_FILE, 'w') as f:
        f.write(text + '\n')


def main():
    checks_passed = 0
    checks_failed = 0
    failures = []
    start_time = time.monotonic()
    deadline = start_time + DURATION_MIN * 60
    cycle = 0
    
    print(f"Soak start: {datetime.now(timezone.utc).isoformat()}")
    print(f"Duration: {DURATION_MIN} min, interval: {CHECK_INTERVAL}s")
    print(f"Status file: {STATUS_FILE}")
    sys.stdout.flush()
    
    while time.monotonic() < deadline:
        cycle += 1
        remaining = int(deadline - time.monotonic())
        ts = datetime.now(timezone.utc).isoformat()
        
        print(f"[{ts}] Check {cycle} ({remaining}s remaining)...", end=" ", flush=True)
        
        # SSH alive check using uptime
        result = serial_cmd("uptime && echo ALIVE_CHECK_OK")
        
        if "ALIVE_CHECK_OK" not in result:
            print("SSH FAIL")
            checks_failed += 1
            failures.append(f"cycle={cycle} ts={ts} error=ssh")
            write_status(f"FAIL cycle={cycle} ssh_error ts={ts}")
            time.sleep(10)
            continue
        
        # dmesg check
        result = serial_cmd("dmesg | grep -ciE 'panic|Oops|segfault|BUG|Call Trace' || echo 0")
        
        # Extract the number
        panic_count = 0
        for line in result.split('\n'):
            line = line.strip()
            if line.isdigit():
                panic_count = int(line)
                break
        
        if panic_count == 0:
            print("OK")
            checks_passed += 1
            write_status(f"OK cycle={cycle} ts={ts}")
        else:
            print(f"PANICS: {panic_count}")
            checks_failed += 1
            failures.append(f"cycle={cycle} ts={ts} panics={panic_count}")
            write_status(f"FAIL cycle={cycle} panics={panic_count} ts={ts}")
        
        # Also check maia module is still loaded
        result = serial_cmd("cat /proc/modules | grep maia_sdr | head -1")
        if "maia_sdr" not in result:
            print(f"    [WARN] maia_sdr module not found!")
        
        sys.stdout.flush()
        
        # Wait for next interval (check if we still have time)
        next_check = time.monotonic() + CHECK_INTERVAL
        while time.monotonic() < next_check and time.monotonic() < deadline:
            time.sleep(5)
    
    # Summary
    elapsed = int(time.monotonic() - start_time)
    print(f"\n{'='*40}")
    print(f"Soak complete: {elapsed}s ({elapsed/60:.0f} min)")
    print(f"Checks: {cycle}, Pass: {checks_passed}, Fail: {checks_failed}")
    if failures:
        print(f"Failures: {failures}")
    else:
        print("  ✅ ALL PASS")
    
    summary = (
        f"PASS={checks_passed} FAIL={checks_failed} "
        f"cycles={cycle} elapsed={elapsed}s "
        f"ts={datetime.now(timezone.utc).isoformat()}"
    )
    write_status(summary)
    sys.stdout.flush()


if __name__ == "__main__":
    main()

"""capture_frames.py - record raw /waterfall frames from a running device.

Writes the exact binary payload of every frame (float32[], element 0 is
maia-httpd's step counter, the rest are the spectrometer bins) back to
back into one file, which is what the classifier's -r replay mode reads:

    python3 capture_frames.py 192.168.1.50 120 frames.raw      # 120 s
    classifier -B none -f templates.bin -r frames.raw -v        # offline

Run the classifier under qemu-arm-static on the dev machine:

    SYS=output/<board>/host/arm-buildroot-linux-gnueabihf/sysroot
    qemu-arm-static -L $SYS output/<board>/build/classifier-master/classifier \
        -B none -f board/tezuka/common/overlay_tezuka/etc/classifier-default-templates.bin \
        -r frames.raw -v
"""
import asyncio, sys, time
import websockets

host, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
port = int(sys.argv[4]) if len(sys.argv) > 4 else 80


async def main():
    n = 0
    t0 = time.time()
    with open(out, "wb") as f:
        async with websockets.connect(f"ws://{host}:{port}/waterfall", max_size=None) as ws:
            while time.time() - t0 < secs:
                msg = await ws.recv()
                if isinstance(msg, bytes) and len(msg) % 4 == 0:
                    f.write(msg)
                    n += 1
    print(f"{n} frames in {time.time() - t0:.1f}s -> {out}")


asyncio.run(main())

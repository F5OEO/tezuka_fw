# Mock /iq-waterfall with exactly known content, to check the page's own maths
#   rc    : first-order low-pass H(f) = A / (1 + j f/fc), fc = 500 kHz at fs = 10 MS/s (known magnitude and phase)
#   tones : two single-bin tones with exactly known amplitude and phase
import sys, asyncio, numpy as np
from websockets.asyncio.server import serve
mode, port = sys.argv[1], int(sys.argv[2])
N, FS = 4096, 10e6
f = (np.arange(N) - N/2) * FS / N
rng = np.random.default_rng(3)
def frame():
    if mode == 'rc':
        X = 20000 / (1 + 1j * f / 500e3)
    else:
        X = np.zeros(N, complex)
        X[2300] = 20000 * np.exp(1j * 0.5)       # +0.5 rad, 20000 LSB = 86.02 dB
        X[2600] = 2000 * np.exp(-2j)             # -2.0 rad,  2000 LSB = 66.02 dB
        X[1500] = 5000 * np.exp(2.5j)            # below the LO: 2.5 rad, 73.98 dB
    X = X + rng.normal(0, 2, N) + 1j * rng.normal(0, 2, N)
    I = np.clip(np.round(X.real), -32768, 32767).astype('<i2').astype('<u2').astype('<u4')
    Q = np.clip(np.round(X.imag), -32768, 32767).astype('<i2').astype('<u2').astype('<u4')
    return ((I << 16) | Q).astype('<u4').tobytes()
async def h(ws):
    try:
        while True:
            await ws.send(frame()); await asyncio.sleep(0.2)
    except Exception: pass
async def main():
    async with serve(h, '127.0.0.1', port):
        await asyncio.Future()
asyncio.run(main())

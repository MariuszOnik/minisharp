#!/usr/bin/env python3
"""Draws the small assets used by examples/sprites.cs: hero.png (4-frame walk, 16x16 each),
coin.png, grass.png and two 8-bit WAV sounds. Needs Pillow."""
import math, os, struct, wave
from PIL import Image

out = os.path.join(os.path.dirname(__file__), "..", "examples")

# ---- hero: 4 walking frames side by side ----
SKIN, HAIR, SHIRT, PANTS, SHOE, EYE = (255, 205, 160), (90, 55, 30), (40, 120, 220), (50, 50, 90), (30, 30, 30), (0, 0, 0)
hero = Image.new("RGBA", (64, 16), (0, 0, 0, 0))
p = hero.load()
legs = [((5, 9), (6, 10)), ((4, 10), (7, 9)), ((5, 9), (6, 10)), ((6, 10), (5, 9))]   # (x1, x2) per leg per frame
for f in range(4):
    ox = f * 16
    bob = 1 if f in (1, 3) else 0
    def put(x, y, c):
        p[ox + x, y + bob if y < 12 else y] = c + (255,)
    for x in range(5, 11):
        for y in range(1, 3): put(x, y, HAIR)
    for x in range(5, 11):
        for y in range(3, 6): put(x, y, SKIN)
    put(9, 4, EYE)
    for x in range(4, 12):
        for y in range(6, 10): put(x, y, SHIRT)
    for x in (4, 11): put(x, 9, SKIN)
    for leg, (x1, x2) in enumerate(legs[f]):
        x = 6 if leg == 0 else 9
        dx = (x1 - 5) if leg == 0 else (x2 - 10)
        for y in range(10, 13): p[ox + x + dx, y] = PANTS + (255,)
        p[ox + x + dx, 13] = SHOE + (255,)
hero.save(os.path.join(out, "hero.png"))

# ---- coin ----
coin = Image.new("RGBA", (12, 12), (0, 0, 0, 0))
c = coin.load()
for y in range(12):
    for x in range(12):
        d = math.hypot(x - 5.5, y - 5.5)
        if d <= 5.5:
            c[x, y] = (255, 210, 40, 255) if d < 4.2 else (200, 140, 20, 255)
for y in range(3, 9): c[5, y] = (255, 245, 180, 255)
coin.save(os.path.join(out, "coin.png"))

# ---- grass tile ----
grass = Image.new("RGBA", (16, 16), (60, 150, 60, 255))
g = grass.load()
for i in range(16):
    x, y = (i * 7) % 16, (i * 11) % 16
    g[x, y] = (90, 190, 80, 255)
    g[(x + 3) % 16, (y + 5) % 16] = (45, 120, 50, 255)
grass.save(os.path.join(out, "grass.png"))

# ---- sounds: 8-bit mono 22050 Hz ----
RATE = 22050
def save_wav(name, samples):
    with wave.open(os.path.join(out, name), "wb") as w:
        w.setnchannels(1); w.setsampwidth(1); w.setframerate(RATE)
        w.writeframes(bytes(max(0, min(255, int(128 + s * 100))) for s in samples))

def square(freq, t): return 1.0 if (t * freq) % 1.0 < 0.5 else -1.0

coin_snd = []
for i in range(int(RATE * 0.16)):
    t = i / RATE
    f = 988 if t < 0.05 else 1319
    coin_snd.append(square(f, t) * (1 - t / 0.16) * 0.6)
save_wav("coin.wav", coin_snd)

jump = []
for i in range(int(RATE * 0.18)):
    t = i / RATE
    f = 300 + 900 * (t / 0.18)
    jump.append(square(f, t) * (1 - t / 0.18) * 0.5)
save_wav("jump.wav", jump)
print("assets written to", os.path.abspath(out))

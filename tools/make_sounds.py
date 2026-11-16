#!/usr/bin/env python3
"""Synthesizes the game's sound effects and its music.

    python3 tools/make_sounds.py           # only the missing ones
    python3 tools/make_sounds.py --force   # all of them, overwriting
    python3 tools/make_sounds.py --only ui_click combat_heavy_laser   # just these, overwriting

Needs NumPy (pip install numpy). Files go under assets/sounds/ (see the
readme's "Sounds" section), as 16-bit mono WAV:

    sfx/<name>.wav       one short effect per game event (see SOUNDS),
                         44.1 kHz
    music/<name>.wav     the tracks (see MUSIC: four calm, four for
                         combat), 32 kHz, each written to loop seamlessly:
                         its echo tail is folded back onto its start

Without --force (or --only, which names what to overwrite), files that
already exist are left alone, so real audio is never overwritten. The game plays every file it lists in src/audio/sounds.h
(tests/test_sounds.cpp checks the two agree), so a new sound goes there too. Everything is made from scratch here, with nothing
sampled: pulse, saw and triangle oscillators, noise, pitch sweeps, filters,
bit crushing and echoes. The style follows the sprites': cold, hard and
retro, so effects are short, dry and mechanical (clicks of relays, thumps of
heavy plating, servo whines, clipped alarms) and lasers are chip-style
pitch drops rather than anything lush. The noise is seeded per sound, so a
run always writes the same files.
"""

import argparse
import math
import os
import sys
import wave
import zlib

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SR = 44100          # sample rate of the effects
MUSIC_SR = 32000    # and of the music (a little duller, and smaller)
LOUDNESS = -13.0    # effects are leveled to this loudness (dBFS, the RMS of
                    # their loudest 30 ms), each offset by LEVELS below,
PEAK = 0.89         # and never peak above this (about -1 dBFS)

_rate = SR          # the rate the helpers below build at (main() switches it)
_rng = np.random.default_rng(0)


# ---------------------------------------------------------------------------
# Building blocks. Signals are float64 arrays in [-1, 1] at _rate.

def n_of(dur):
    return max(1, int(round(dur * _rate)))


def ramp(a, b, dur, curve="exp"):
    """A per-sample value going from a to b: an exponential glide (for pitch
    and cutoff) or a straight line."""
    n = n_of(dur)
    if curve == "exp" and a > 0 and b > 0:
        return a * (b / a) ** np.linspace(0.0, 1.0, n)
    return np.linspace(a, b, n)


def _freq(freq, dur):
    if np.isscalar(freq):
        return np.full(n_of(dur), float(freq))
    return np.asarray(freq, dtype=float)


def phase(freq, dur):
    """Phase in cycles (0..1 wraps) for a constant or per-sample frequency."""
    f = _freq(freq, dur)
    return np.cumsum(f) / _rate


def sine(freq, dur):
    return np.sin(2 * np.pi * phase(freq, dur))


def square(freq, dur, duty=0.5):
    """Pulse wave; duty may be per-sample (pulse-width sweeps)."""
    p = phase(freq, dur) % 1.0
    return np.where(p < duty, 1.0, -1.0)


def saw(freq, dur):
    p = phase(freq, dur) % 1.0
    return 2.0 * p - 1.0


def tri(freq, dur):
    p = phase(freq, dur) % 1.0
    return 4.0 * np.abs(p - 0.5) - 1.0


def noise(dur):
    return _rng.uniform(-1.0, 1.0, n_of(dur))


def stepped_noise(dur, hz):
    """Noise held for 1/hz seconds per value: the gritty 'chip' noise of old
    sound hardware (low hz rumbles, high hz hisses)."""
    n = n_of(dur)
    hold = max(1, int(_rate / hz))
    vals = _rng.uniform(-1.0, 1.0, n // hold + 2)
    return np.repeat(vals, hold)[:n]


def env(dur, attack=0.002, decay=None, curve=4.0):
    """Short attack, then an exponential decay to silence over the rest
    (or over `decay` seconds, silence after)."""
    n = n_of(dur)
    t = np.arange(n) / _rate
    a = np.clip(t / max(attack, 1e-6), 0, 1)
    d = (dur - attack) if decay is None else decay
    k = np.clip((t - attack) / max(d, 1e-6), 0, 1)
    return a * np.exp(-curve * k) * (1 - k)


def adsr(dur, a, d, s, r):
    """Attack/decay/sustain/release, with the release at the end of dur."""
    n = n_of(dur)
    t = np.arange(n) / _rate
    out = np.full(n, s)
    out = np.where(t < a + d, 1.0 - (1.0 - s) * np.clip((t - a) / max(d, 1e-6), 0, 1), out)
    out = np.where(t < a, t / max(a, 1e-6), out)
    rel_start = dur - r
    out = np.where(t > rel_start, out * np.clip((dur - t) / max(r, 1e-6), 0, 1), out)
    return out


def lowpass(x, cutoff, res=0.0):
    """State-variable low-pass, cutoff (Hz) constant or per-sample; res 0..0.95
    adds a resonant peak (the 'zap' in sweeps)."""
    n = len(x)
    c = np.broadcast_to(np.asarray(cutoff, dtype=float), (n,)) if np.ndim(cutoff) == 0 else cutoff[:n]
    f = 2.0 * np.sin(np.pi * np.clip(c, 10, _rate / 6) / _rate)
    q = 1.0 - min(max(res, 0.0), 0.95)
    lo = band = 0.0
    out = np.empty(n)
    xs = x.tolist()
    fs = f.tolist()
    for i in range(n):
        fi = fs[i]
        lo += fi * band
        hi = xs[i] - lo - q * band
        band += fi * hi
        out[i] = lo
    return out


def highpass(x, cutoff):
    """One-pole high-pass: strips rumble, thins noise into hiss."""
    a = math.exp(-2 * math.pi * cutoff / _rate)
    out = np.empty(len(x))
    prev_x = prev_y = 0.0
    for i, v in enumerate(x.tolist()):
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        out[i] = prev_y
    return out


def crush(x, bits=6, hold=1):
    """Bit crushing (fewer levels) and sample holding (a lower sample rate):
    the hard digital edge of the whole set."""
    if hold > 1:
        x = np.repeat(x[::hold], hold)[: len(x)]
    levels = 2 ** (bits - 1)
    return np.round(x * levels) / levels


def drive(x, amount=2.0):
    """Soft saturation, for weight."""
    return np.tanh(x * amount) / np.tanh(amount)


def echo(x, time, feedback=0.35, mix=0.3, repeats=6, damp=0.6):
    """A dry-ish slap or ping-pong-less delay, each repeat duller. Returns a
    longer signal so the repeats ring out."""
    d = n_of(time)
    out = np.concatenate([x, np.zeros(d * repeats)])
    tap = x
    for k in range(1, repeats + 1):
        tap = tap * feedback
        tap = 0.5 * tap + 0.5 * np.concatenate([[0.0], tap[:-1]]) if damp else tap
        out[k * d : k * d + len(tap)] += tap * (mix / feedback)
    return out


def silence(dur):
    return np.zeros(n_of(dur))


def seq(*parts):
    """Plays signals one after another."""
    return np.concatenate(parts)


def mix(*parts, at=None):
    """Sums signals, each starting at the matching time in `at` (seconds)."""
    at = at or [0.0] * len(parts)
    starts = [n_of(t) if t > 0 else 0 for t in at]
    n = max(s + len(p) for s, p in zip(starts, parts))
    out = np.zeros(n)
    for s, p in zip(starts, parts):
        out[s : s + len(p)] += p
    return out


def fit(x, n):
    return x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x))])


def note(name):
    """'A4' / 'C#3' / 'Eb2' -> Hz."""
    names = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    semis = names[name[0]]
    rest = name[1:]
    if rest[0] == "#":
        semis += 1
        rest = rest[1:]
    elif rest[0] == "b":
        semis -= 1
        rest = rest[1:]
    midi = 12 * (int(rest) + 1) + semis
    return 440.0 * 2 ** ((midi - 69) / 12)


def loudness(x):
    """RMS of the loudest 30 ms, in dBFS: how loud a short effect sounds,
    near enough, whether it is a click or a long rumble."""
    w = max(1, n_of(0.03))
    sq = np.concatenate([[0.0], np.cumsum(x * x)])
    if len(x) <= w:
        return 10 * np.log10(sq[-1] / w + 1e-12)
    return 10 * np.log10(np.max(sq[w:] - sq[:-w]) / w + 1e-12)


def finish(x, level=0.0):
    """Clears DC, fades the very ends (no clicks), trims trailing silence and
    levels it to LOUDNESS + level dB, short of PEAK."""
    x = x - np.mean(x)
    nz = np.nonzero(np.abs(x) > 1e-4)[0]
    if len(nz):
        x = x[: nz[-1] + 1]
    fade = min(len(x) // 2, n_of(0.004))
    if fade > 0:
        x[-fade:] *= np.linspace(1, 0, fade)
        x[: min(fade, n_of(0.0008))] *= np.linspace(0, 1, min(fade, n_of(0.0008)))
    m = np.max(np.abs(x))
    if m == 0:
        return x
    gain = min(10 ** ((LOUDNESS + level - loudness(x)) / 20), PEAK / m)
    return x * gain


# ---------------------------------------------------------------------------
# Recurring pieces.

def relay_click(pitch=1.0, dur=0.025):
    """The tick of a hard relay: a click of filtered noise over a tiny ping."""
    body = square(1800 * pitch, dur, 0.25) * env(dur, 0.0005, curve=9)
    snap = highpass(noise(dur), 2500) * env(dur, 0.0002, decay=0.006, curve=6)
    return 0.45 * body + 0.8 * snap


def thump(freq=90, dur=0.18, drop=0.45):
    """A heavy plate settling: a pitch-dropping sine with a noisy knock."""
    body = sine(ramp(freq, freq * drop, dur), dur) * env(dur, 0.001, curve=5)
    knock = lowpass(noise(dur), 900) * env(dur, 0.0005, decay=0.04, curve=5)
    return drive(1.1 * body + 1.4 * knock, 2.0)


def beep(freq, dur, duty=0.25, bits=6):
    return crush(square(freq, dur, duty) * adsr(dur, 0.002, 0.02, 0.7, 0.02), bits)


def zap(f0, f1, dur, duty=0.5, res=0.6, cutoff=6000, curve=5):
    """A chip-style laser: a pulse diving in pitch, through a resonant filter
    that dives with it."""
    tone = square(ramp(f0, f1, dur), dur, duty)
    cut = ramp(cutoff, cutoff * 0.15, dur)
    return lowpass(tone, cut, res) * env(dur, 0.001, curve=curve)


def burst(dur, cutoff=3000, hz=8000, curve=5, attack=0.001):
    """A short blast of chip noise, low-passed."""
    return lowpass(stepped_noise(dur, hz), cutoff) * env(dur, attack, curve=curve)


def boom(dur=1.0, size=1.0):
    """An explosion in vacuum, as games hear it: a rumbling noise swell with a
    sub-bass drop, crushed hard."""
    rumble = lowpass(stepped_noise(dur, 2500 / size), ramp(2400, 180, dur)) * env(dur, 0.003, curve=4)
    sub = sine(ramp(110 / size, 28, dur), dur) * env(dur, 0.002, curve=3.5)
    crack = highpass(noise(0.08), 1500) * env(0.08, 0.0005, curve=6)
    return crush(drive(mix(1.3 * rumble, 1.2 * sub, 0.7 * crack), 2.5), 7, 2)


def alarm(f_lo, f_hi, period, cycles, duty=0.5):
    """A two-tone klaxon."""
    half = period / 2
    tone = seq(*[seq(square(f_hi, half, duty), square(f_lo, half, duty)) for _ in range(cycles)])
    return crush(lowpass(tone, 3500) * adsr(len(tone) / _rate, 0.004, 0.05, 0.8, 0.05), 6)


# ---------------------------------------------------------------------------
# The effects. Each is (description, function); the name is the file name.

# -- UI ---------------------------------------------------------------------

def ui_click():
    return relay_click()


def ui_toggle_on():
    return seq(relay_click(0.9), silence(0.03), beep(1320, 0.05))


def ui_toggle_off():
    return seq(relay_click(0.9), silence(0.03), beep(660, 0.05))


def ui_tab_switch():
    swish = lowpass(highpass(noise(0.07), 1200), ramp(1500, 5500, 0.07)) * env(0.07, 0.02, curve=4)
    return mix(0.6 * swish, relay_click(1.2), at=[0, 0.045])


def ui_panel_open():
    """A servo sliding a shutter open: a rising whine, then a latch."""
    d = 0.16
    whine = lowpass(saw(ramp(180, 420, d), d), 1600) * adsr(d, 0.01, 0.05, 0.7, 0.04)
    return mix(0.5 * whine, relay_click(1.1), at=[0, d - 0.01])


def ui_panel_close():
    d = 0.14
    whine = lowpass(saw(ramp(420, 170, d), d), 1500) * adsr(d, 0.01, 0.05, 0.7, 0.04)
    return mix(0.5 * whine, relay_click(0.8), at=[0, d - 0.01])


def ui_confirm():
    return crush(seq(beep(880, 0.06), beep(1320, 0.11)), 5)


def ui_error():
    """Denied: a low, buzzing double blip."""
    blip = lambda: lowpass(square(110, 0.09, 0.3) + 0.5 * saw(113, 0.09), 1800) * adsr(0.09, 0.002, 0.02, 0.8, 0.02)
    return crush(drive(seq(blip(), silence(0.04), blip()), 2.5), 5)


def ui_type():
    """One keystroke in a text field (renaming a ship)."""
    return 0.5 * relay_click(2.1, 0.015)


# -- Building -----------------------------------------------------------------

def build_place():
    """A plate bolted onto the hull: a thump, then the clank of a bolt."""
    clank = lowpass(square(ramp(620, 540, 0.06), 0.06, 0.35) + 0.6 * square(931, 0.06, 0.2), 4000) * env(0.06, 0.0005, curve=6)
    return mix(thump(95, 0.2), 0.45 * clank, at=[0, 0.035])


def build_place_belt():
    """Belts go down by the dozen, so lighter: a tap and a short ratchet."""
    tap = thump(160, 0.08, 0.6)
    ratchet = seq(*[relay_click(0.6 + 0.1 * i, 0.012) for i in range(3)])
    return mix(0.8 * tap, 0.35 * ratchet, at=[0, 0.02])


def build_remove():
    """Whisked back into the bridge: a quick, soft swish of noise sweeping
    up and away, with a faint tick at its start. Right-dragging removes a
    row of buildings, so it stays small."""
    d = 0.09
    swish = lowpass(highpass(noise(d), 900), ramp(1200, 7000, d), 0.3) * adsr(d, 0.025, 0.02, 0.6, 0.05)
    return mix(swish, 0.25 * relay_click(1.4, 0.01))


def build_rotate():
    """A detent clicking round."""
    return seq(relay_click(0.7, 0.02), silence(0.025), relay_click(1.05, 0.02))


def build_invalid():
    """Can't build there: a flat buzz, shorter than ui_error."""
    d = 0.12
    buzz = lowpass(square(98, d, 0.5) * (0.6 + 0.4 * square(30, d)), 1300) * adsr(d, 0.002, 0.02, 0.9, 0.03)
    return crush(drive(buzz, 3), 4)


def build_blueprint_place():
    """A whole blueprint stamped down: a quick ripple of clanks over a thump."""
    clanks = mix(*[0.5 * relay_click(0.5 + 0.12 * i, 0.03) for i in range(6)], at=[0.025 * i for i in range(6)])
    return mix(thump(80, 0.32, 0.4), clanks, beep(1760, 0.07, 0.125), at=[0, 0.02, 0.18])


# -- Game events --------------------------------------------------------------

def game_craft_queued():
    """A hand-craft added to the bridge's queue: a soft, short blip, quieter
    than a confirm (it's clicked often, five at a time with Shift)."""
    d = 0.045
    blip = tri(ramp(1400, 1900, d), d) * env(d, 0.002, curve=5)
    return mix(crush(blip, 6), 0.3 * relay_click(1.2, 0.01))


def game_milestone_complete():
    """A fanfare, kept cold: a rising arpeggio of a minor ninth chord through
    a slap echo."""
    notes = ["A4", "C5", "E5", "B5", "E6"]
    d = 0.09
    arp = seq(*[crush(square(note(n), d, 0.25) * adsr(d, 0.002, 0.02, 0.6, 0.03), 5) for n in notes])
    tail = crush(lowpass(square(note("A5"), 0.5, 0.125) + 0.5 * square(note("E6") * 1.003, 0.5, 0.25), 3000) * env(0.5, 0.004, curve=3), 5)
    return echo(seq(arp, 0.8 * tail), 0.11, feedback=0.4, mix=0.35, repeats=4)


def game_cargo_launch():
    """A pod fired from the bridge: a pneumatic thunk and a hiss dropping away."""
    d = 0.45
    hiss = lowpass(highpass(noise(d), 600), ramp(6000, 600, d)) * env(d, 0.005, curve=4)
    return mix(thump(130, 0.12, 0.5), 0.6 * hiss, at=[0, 0.02])


def game_cargo_land():
    """A pod docking: reverse hiss, then a clamp."""
    d = 0.3
    hiss = lowpass(highpass(noise(d), 600), ramp(700, 5000, d)) * adsr(d, 0.25, 0.01, 1.0, 0.03)
    return mix(0.45 * hiss, thump(110, 0.15), relay_click(0.8), at=[0, d - 0.01, d + 0.04])


def game_ship_built():
    """A new hull rolls off: deep clangs, then a power-up rising to a chord."""
    d = 0.7
    up = lowpass(saw(ramp(55, 220, d), d) + saw(ramp(55.4, 221, d), d), ramp(300, 3000, d), 0.5) * adsr(d, 0.05, 0.1, 0.8, 0.1)
    chord = sum(square(note(n), 0.6, 0.25) for n in ("A3", "E4", "B4")) / 3
    chord = crush(lowpass(chord, 2500) * env(0.6, 0.004, curve=3), 6)
    return mix(thump(70, 0.3, 0.4), thump(60, 0.35, 0.4), 0.5 * up, 0.6 * chord, at=[0, 0.18, 0.3, 0.95])


def game_ship_switch():
    """Switching the ship on screen: a short descending scan blip."""
    return seq(beep(1568, 0.025, 0.125), beep(1175, 0.025, 0.125), beep(784, 0.04, 0.125))


def game_thrusters_charged():
    """Every ion thruster full: a rising hum that settles on a steady tone."""
    d = 0.5
    hum = lowpass(saw(ramp(80, 160, d), d) * 0.6 + square(ramp(160, 320, d), d, 0.3) * 0.4, ramp(400, 2200, d), 0.4)
    hum *= adsr(d, 0.05, 0.1, 0.9, 0.08)
    return mix(0.7 * hum, beep(note("E6"), 0.12, 0.125), at=[0, d - 0.06])


def game_jump():
    """The jump: a charge whining up, a hard cut, a crack and a long falling
    swoosh."""
    c = 1.2
    charge = lowpass(saw(ramp(60, 1400, c), c) + square(ramp(60.5, 1410, c), c, 0.3), ramp(300, 6000, c), 0.6)
    charge *= np.linspace(0.2, 1.0, n_of(c)) ** 2
    d = 1.4
    swoosh = lowpass(noise(d), ramp(8000, 120, d)) * env(d, 0.002, curve=3)
    drop = sine(ramp(220, 30, d), d) * env(d, 0.002, curve=3)
    crack = highpass(noise(0.05), 2000) * env(0.05, 0.0003, curve=5)
    out = mix(0.5 * charge, 0.9 * swoosh, 1.0 * drop, 0.8 * crack, at=[0, c, c, c])
    return crush(drive(out, 1.8), 8)


def game_sector_surveyed():
    """A sensor ping coming back: a sonar-ish blip with long echoes."""
    d = 0.2
    ping = sine(ramp(2600, 2400, d), d) * env(d, 0.001, curve=6) + 0.3 * tri(1300, d) * env(d, 0.001, curve=8)
    return echo(crush(ping, 7), 0.16, feedback=0.45, mix=0.4, repeats=5)


def game_map_open():
    """The star map coming up: a bit-crushed scan sweep."""
    d = 0.35
    sweep = square(ramp(300, 2400, d), d, ramp(0.1, 0.5, d, "lin"))
    return crush(lowpass(sweep, 4000) * adsr(d, 0.005, 0.1, 0.5, 0.1), 4, 2)


def game_enemy_detected():
    """An enemy ship in the sector: a klaxon."""
    return alarm(370, 554, 0.36, 3, 0.4)


def game_over():
    """The last bridge falls: a long power-down, everything sagging to
    nothing."""
    d = 2.2
    f = ramp(440, 35, d)
    chord = (square(f, d, 0.4) + square(f * 1.19, d, 0.3) + saw(f * 1.5, d)) / 3
    down = lowpass(chord, ramp(3000, 150, d), 0.5) * adsr(d, 0.01, 0.2, 0.8, 0.8)
    return crush(mix(boom(1.4, 1.6), 0.7 * down, at=[0, 0.1]), 6, 2)


# -- Combat -------------------------------------------------------------------

def combat_burst_laser():
    """Three fast chip zaps."""
    shot = lambda k: zap(2400 * k, 300 * k, 0.07, 0.5, 0.7, 7000, 6)
    return mix(shot(1.0), 0.9 * shot(1.04), 0.8 * shot(0.97), at=[0, 0.075, 0.15])


def combat_heavy_laser():
    """One slow, fat zap with a charge click before it."""
    d = 0.32
    body = zap(1500, 90, d, 0.3, 0.75, 5000, 4) + 0.6 * zap(1508, 92, d, 0.5, 0.5, 3000, 4)
    return crush(drive(mix(0.4 * relay_click(0.5), body, at=[0, 0.025]), 2), 7)


def combat_missile_launch():
    """A clamp letting go, then a rocket hissing away."""
    d = 0.6
    rocket = lowpass(stepped_noise(d, 6000), ramp(1500, 4500, d), 0.3) * adsr(d, 0.02, 0.1, 0.7, 0.3)
    whistle = 0.15 * sine(ramp(600, 1800, d), d) * adsr(d, 0.05, 0.1, 0.6, 0.3)
    return mix(thump(110, 0.1, 0.6), 0.7 * rocket + whistle, at=[0, 0.03])


def combat_ion_blaster():
    """A crackling electric bolt: a wobbling, ring-modulated pulse."""
    d = 0.35
    wobble = 1.0 + 0.3 * np.sin(2 * np.pi * phase(28, d))
    tone = square(ramp(900, 220, d) * wobble, d, 0.5) * square(ramp(130, 70, d), d)
    crackle = highpass(stepped_noise(d, 12000), 3000) * (np.abs(stepped_noise(d, 60)) > 0.5)
    out = lowpass(tone, 5000, 0.5) * env(d, 0.002, curve=3) + 0.35 * crackle * env(d, 0.002, curve=3)
    return crush(out, 6)


def combat_flak_cannon():
    """A dull boom and a spray of pellets."""
    pellets = mix(*[0.35 * burst(0.03, 5000, 14000, 6) for _ in range(5)], at=[0.04 + 0.018 * i for i in range(5)])
    return crush(mix(thump(70, 0.22, 0.4), 1.2 * burst(0.15, 1800, 3000, 5), pellets), 7)


def combat_beam_emitter():
    """A beam held across the enemy hull: a rough, sustained hum that swells
    and is cut off."""
    d = 0.9
    f = 180 + 12 * np.sin(2 * np.pi * phase(6, d))
    tone = saw(f, d) + square(f * 2.005, d, 0.3) + 0.5 * saw(f * 3.01, d)
    out = lowpass(tone, 1800 + 1200 * np.sin(np.linspace(0, np.pi, n_of(d))), 0.6) * adsr(d, 0.06, 0.1, 0.85, 0.06)
    out += 0.15 * highpass(stepped_noise(d, 9000), 4000) * adsr(d, 0.06, 0.1, 0.85, 0.06)
    return crush(drive(out, 1.5), 6)


def combat_hit_hull():
    """A shot striking plating: a hard metallic clang."""
    d = 0.3
    partials = [(310, 1.0), (747, 0.6), (1190, 0.45), (1803, 0.3)]
    ring = sum(a * square(f, d, 0.3) * env(d, 0.0005, curve=6 + i) for i, (f, a) in enumerate(partials))
    return crush(drive(mix(lowpass(ring, 3500), 1.2 * burst(0.06, 4000, 9000, 5)), 2), 7)


def combat_hit_shield():
    """A shot stopped by a shield layer: a bright, shimmering ping."""
    d = 0.4
    shimmer = 1 + 0.02 * np.sin(2 * np.pi * phase(35, d))
    ping = (sine(1760 * shimmer, d) + 0.6 * tri(2637 * shimmer, d) + 0.4 * sine(3520, d)) * env(d, 0.001, curve=5)
    return crush(mix(ping, 0.5 * highpass(noise(0.03), 4000) * env(0.03, 0.0003, curve=5)), 7)


def combat_shield_down():
    """The last shield layer broken: a crackle and a falling buzz."""
    d = 0.55
    fall = lowpass(square(ramp(880, 110, d), d, 0.5) * square(ramp(47, 23, d), d), ramp(4000, 400, d), 0.5) * env(d, 0.002, curve=3)
    return crush(mix(0.8 * highpass(stepped_noise(0.15, 10000), 2000) * env(0.15, 0.001, curve=4), fall), 5)


def combat_shield_recharge():
    """One layer back up: a rising hum ending in a soft chime."""
    d = 0.35
    rise = lowpass(tri(ramp(330, 990, d), d) + 0.4 * square(ramp(331, 992, d), d, 0.25), 3000) * adsr(d, 0.04, 0.05, 0.8, 0.05)
    return mix(0.6 * rise, 0.5 * (sine(1980, 0.3) + 0.4 * sine(2970, 0.3)) * env(0.3, 0.002, curve=5), at=[0, d - 0.03])


def combat_miss():
    """A shot dodged: a zap flying past and away."""
    d = 0.3
    return lowpass(square(ramp(1800, 700, d), d, 0.4), ramp(5000, 800, d), 0.5) * adsr(d, 0.08, 0.05, 0.7, 0.15)


def combat_building_destroyed():
    """One of your buildings shot to pieces: a crunchy, short explosion."""
    return mix(boom(0.6, 0.8), 0.5 * thump(140, 0.15, 0.5))


def combat_engine_offline():
    """An engine knocked out: a spark and a turbine winding down."""
    d = 0.9
    wind = lowpass(saw(ramp(320, 40, d), d) + square(ramp(640, 80, d), d, 0.3), ramp(2500, 200, d), 0.5) * env(d, 0.003, curve=2.5)
    spark = highpass(stepped_noise(0.12, 11000), 2500) * env(0.12, 0.0005, curve=4)
    return crush(mix(0.8 * spark, 0.8 * wind), 6)


def combat_enemy_destroyed():
    """The enemy ship blown apart: a long rumbling blast with a few late
    secondary bursts."""
    secondary = [0.5 * boom(0.5, 0.7), 0.35 * boom(0.4, 0.6), 0.3 * boom(0.35, 0.5)]
    return mix(boom(1.8, 1.4), *secondary, at=[0, 0.35, 0.6, 0.9])


def combat_ship_lost():
    """One of your ships lost (not the last): a heavy blast and a sagging
    alarm."""
    d = 0.8
    sag = crush(lowpass(square(ramp(554, 200, d), d, 0.4), 2500) * adsr(d, 0.005, 0.1, 0.8, 0.3), 5)
    return mix(boom(1.5, 1.5), 0.5 * sag, at=[0, 0.25])


def combat_target_set():
    """Targeting an enemy building: a quick lock-on chirp."""
    return crush(seq(square(1760, 0.025, 0.25), silence(0.015), square(1760, 0.025, 0.25), silence(0.015), square(2349, 0.06, 0.25) * env(0.06, 0.001, curve=3)), 5)


def combat_target_clear():
    return crush(seq(square(1760, 0.025, 0.25), silence(0.015), square(1175, 0.05, 0.25) * env(0.05, 0.001, curve=3)), 5)


def combat_salvage():
    """Salvage aboard: a clatter of parts and a pickup arpeggio."""
    clatter = mix(*[0.4 * relay_click(0.4 + 0.25 * (i % 3), 0.035) for i in range(7)], at=[0.03 * i + 0.01 * (i % 2) for i in range(7)])
    arp = seq(*[beep(note(n), 0.05, 0.125) for n in ("E5", "G5", "B5", "E6")])
    return mix(clatter, arp, at=[0, 0.22])


def combat_weapon_found():
    """A new weapon found in a hold: the rarest reward, so the longest
    jingle."""
    phrase = [("A4", 0.08), ("E5", 0.08), ("A5", 0.08), ("C6", 0.08), ("B5", 0.08), ("E6", 0.5)]
    lead = seq(*[crush(square(note(n), d, 0.25) * adsr(d, 0.002, 0.03, 0.7, 0.03), 5) for n, d in phrase])
    bass = crush(tri(note("A2"), len(lead) / _rate), 6) * adsr(len(lead) / _rate, 0.003, 0.1, 0.7, 0.3)
    return echo(mix(lead, 0.6 * bass), 0.13, 0.35, 0.3, 4)


SOUNDS = {
    # UI
    "ui_click": ui_click,
    "ui_toggle_on": ui_toggle_on,
    "ui_toggle_off": ui_toggle_off,
    "ui_tab_switch": ui_tab_switch,
    "ui_panel_open": ui_panel_open,
    "ui_panel_close": ui_panel_close,
    "ui_confirm": ui_confirm,
    "ui_error": ui_error,
    "ui_type": ui_type,
    # Building
    "build_place": build_place,
    "build_place_belt": build_place_belt,
    "build_remove": build_remove,
    "build_rotate": build_rotate,
    "build_invalid": build_invalid,
    "build_blueprint_place": build_blueprint_place,
    # Game events
    "game_craft_queued": game_craft_queued,
    "game_milestone_complete": game_milestone_complete,
    "game_cargo_launch": game_cargo_launch,
    "game_cargo_land": game_cargo_land,
    "game_ship_built": game_ship_built,
    "game_ship_switch": game_ship_switch,
    "game_thrusters_charged": game_thrusters_charged,
    "game_jump": game_jump,
    "game_sector_surveyed": game_sector_surveyed,
    "game_map_open": game_map_open,
    "game_enemy_detected": game_enemy_detected,
    "game_over": game_over,
    # Combat
    "combat_burst_laser": combat_burst_laser,
    "combat_heavy_laser": combat_heavy_laser,
    "combat_missile_launch": combat_missile_launch,
    "combat_ion_blaster": combat_ion_blaster,
    "combat_flak_cannon": combat_flak_cannon,
    "combat_beam_emitter": combat_beam_emitter,
    "combat_hit_hull": combat_hit_hull,
    "combat_hit_shield": combat_hit_shield,
    "combat_shield_down": combat_shield_down,
    "combat_shield_recharge": combat_shield_recharge,
    "combat_miss": combat_miss,
    "combat_building_destroyed": combat_building_destroyed,
    "combat_engine_offline": combat_engine_offline,
    "combat_enemy_destroyed": combat_enemy_destroyed,
    "combat_ship_lost": combat_ship_lost,
    "combat_target_set": combat_target_set,
    "combat_target_clear": combat_target_clear,
    "combat_salvage": combat_salvage,
    "combat_weapon_found": combat_weapon_found,
}

# Loudness offsets in dB: quieter for what's heard constantly (UI, belts,
# guns firing every few seconds), louder for the big moments.
LEVELS = {
    "ui_type": -10, "ui_click": -5, "ui_tab_switch": -4,
    "ui_toggle_on": -4, "ui_toggle_off": -4, "ui_panel_open": -4, "ui_panel_close": -4,
    "build_place_belt": -4, "build_rotate": -5, "build_remove": -9, "build_invalid": -3,
    "game_ship_switch": -4, "game_craft_queued": -11,
    "combat_burst_laser": -2, "combat_heavy_laser": -2, "combat_missile_launch": -2,
    "combat_ion_blaster": -2, "combat_flak_cannon": -2, "combat_beam_emitter": -3,
    "combat_hit_shield": -3, "combat_miss": -5,
    "combat_target_set": -4, "combat_target_clear": -4,
    "game_jump": 2, "combat_enemy_destroyed": 2, "game_over": 2, "combat_ship_lost": 2,
    "game_milestone_complete": 1, "combat_weapon_found": 1,
}


# ---------------------------------------------------------------------------
# The music: loops, each played a few times through as one song (the game
# shuffles them, see src/audio/sounds.h), in two sets.
#
#   cold_orbit   calm: A minor, 96 bpm, 16 bars, calm and cold
#   ice_drift    calm: E minor, 84 bpm, 16 bars, chiming and sparse
#   deep_core    calm: D Dorian, 104 bpm, 16 bars, plucked and busy
#   far_beacon   calm: C minor, 88 bpm, 16 bars, lonely, a pinging beacon
#   red_alert    combat: D minor, 132 bpm, 16 bars, tense and driving
#   broadside    combat: E Phrygian, 140 bpm, 16 bars, chord stabs
#   hard_burn    combat: G minor, 126 bpm, 16 bars, a galloping bass
#   hull_breach  combat: B minor, 150 bpm, 16 bars, the most frantic
#
# Each is built over its length plus a tail, and the echoes ringing past the
# end are folded back onto its start, so it loops without a seam.


def place(track, sig, t):
    """Adds `sig` into `track` starting at `t` seconds (cut off at its end)."""
    s = n_of(t)
    e = min(len(track), s + len(sig))
    track[s:e] += sig[: e - s]


def place_melody(track, pattern, t0, step, voice):
    """Plays `pattern` (note names, "-" holding the last note, "." resting,
    "|" between bars, one step each) into `track` from `t0`, `step` seconds
    a step, each note made by voice(freq, dur)."""
    steps = pattern.replace("|", " ").split()
    k = 0
    while k < len(steps):
        if steps[k] in ("-", "."):
            k += 1
            continue
        length = 1
        while k + length < len(steps) and steps[k + length] == "-":
            length += 1
        place(track, voice(note(steps[k]), length * step * 0.95), t0 + k * step)
        k += length


def fold_loop(out, total, peak=0.6):
    """`out` (longer than `total` seconds) with everything past the loop
    point added back onto its start, normalized to `peak` (under the
    effects)."""
    loop = n_of(total)
    looped = out[:loop].copy()
    looped[: len(out) - loop] += out[loop:]
    looped -= np.mean(looped)
    return looped * (peak / np.max(np.abs(looped)))


def pad_voice(freq, dur):
    """Two detuned saws, dark and slow, with a slow cutoff wobble."""
    v = saw(freq * 0.997, dur) + saw(freq * 1.003, dur) + 0.5 * square(freq / 2, dur, 0.5)
    cut = 900 + 500 * np.sin(np.linspace(0, np.pi, n_of(dur)))
    return lowpass(v, cut, 0.2) * adsr(dur, 0.6, 0.4, 0.7, 0.8)


def bass_note(freq, dur, bite=1400):
    v = square(freq, dur, 0.35) + 0.6 * saw(freq * 2, dur)
    cut = 180 + bite * env(dur, 0.002, curve=5)
    return drive(lowpass(v, cut, 0.6) * adsr(dur, 0.003, 0.08, 0.7, 0.03), 1.5)


def arp_note(freq, dur):
    return crush(square(freq, dur, 0.125) * env(dur, 0.001, curve=6), 5)


def lead_note(freq, dur):
    vib = 1 + 0.004 * np.sin(2 * np.pi * phase(5.5, dur)) * np.clip(np.linspace(-0.5, 1.5, n_of(dur)), 0, 1)
    v = square(freq * vib, dur, 0.25) + 0.5 * tri(freq * vib, dur)
    return crush(lowpass(v, 3200) * adsr(dur, 0.005, 0.1, 0.6, 0.06), 6)


def kick():
    d = 0.3
    f = fit(ramp(140, 42, 0.12), n_of(d))
    f[n_of(0.12):] = 42
    body = sine(f, d) * env(d, 0.001, curve=5)
    click = lowpass(noise(d), 600) * env(d, 0.0005, decay=0.02, curve=4)
    return drive(body + 0.3 * click, 2.5)


def hat(open_=False):
    d = 0.12 if open_ else 0.035
    return crush(highpass(stepped_noise(d, 16000), 6000) * env(d, 0.0005, curve=5), 4)


def snare():
    d = 0.18
    tone = tri(ramp(220, 160, d), d) * env(d, 0.001, curve=7)
    hiss = lowpass(highpass(stepped_noise(d, 11000), 1500), 6000) * env(d, 0.001, curve=5)
    return crush(0.5 * tone + hiss, 5)


def tom(freq):
    d = 0.25
    return drive(sine(ramp(freq, freq * 0.55, d), d) * env(d, 0.001, curve=5), 1.8)


# -- Cold Orbit -----------------------------------------------------------------
#
# Two eight-bar halves over the same chords (i VI III VII / i VI iv V): the
# first is bass, pad and a ticking arpeggio; the second adds a sparse pulse
# lead and a little more drum. The last bar's E major turns back into the
# first bar's A minor.

COLD_BEAT = 60.0 / 96
COLD_BAR = 4 * COLD_BEAT
COLD_CHORDS = [  # one per bar: (bass root, pad voicing)
    ("A1", ["A3", "C4", "E4", "B4"]),
    ("F1", ["F3", "A3", "C4", "E4"]),
    ("C2", ["G3", "C4", "E4", "B4"]),
    ("G1", ["G3", "B3", "D4", "A4"]),
    ("A1", ["A3", "C4", "E4", "B4"]),
    ("F1", ["F3", "A3", "C4", "E4"]),
    ("D2", ["F3", "A3", "D4", "E4"]),
    ("E1", ["G#3", "B3", "E4", "F#4"]),
]
# Lead for bars 9-16, in eighth notes.
COLD_LEAD = (
    "E5 - - - . . B4 C5 | D5 - C5 - B4 - . . | C5 - - - . . A4 C5 | E5 - - - - - . . | "
    "E5 - - - . . B4 C5 | D5 - E5 - A5 - . . | F5 - - - E5 - D5 - | B4 - - - G#4 - . ."
)


def music_cold_orbit():
    beat, bar = COLD_BEAT, COLD_BAR
    total = 2 * len(COLD_CHORDS) * bar
    n = n_of(total + 4.0)
    bass, pad, arp, lead, drums = (np.zeros(n) for _ in range(5))
    eighth = beat / 2
    sixteenth = beat / 4

    for i, (root, voicing) in enumerate(COLD_CHORDS * 2):
        t0 = i * bar
        second_half = i >= len(COLD_CHORDS)
        # Pad: the whole bar, re-struck each chord, overlapping the next.
        for nm in voicing:
            place(pad, pad_voice(note(nm), bar + 0.5), t0)
        # Bass: driving eighths, root and its octave, a fifth near the end.
        r = note(root)
        for k, f in enumerate([r, r, r * 2, r, r, r * 2, r * 1.5, r * 2]):
            place(bass, bass_note(f, eighth * 0.85), t0 + k * eighth)
        # Arpeggio: sixteenths up and down the voicing, an octave up, from bar 3.
        if i >= 2:
            tones = [note(nm) * 2 for nm in voicing]
            order = tones + tones[-2:0:-1]
            for k in range(16):
                place(arp, arp_note(order[k % len(order)], sixteenth * 0.9), t0 + k * sixteenth)
        # Drums: kick on 1 and 3 throughout; hats from bar 5; snare in the
        # second half, with an open hat lifting every second bar.
        place(drums, kick(), t0)
        place(drums, 0.8 * kick(), t0 + 2 * beat)
        if i >= 4:
            for k in range(8):
                place(drums, (0.5 if k % 2 else 0.8) * hat(), t0 + k * eighth)
        if second_half:
            place(drums, 0.7 * snare(), t0 + beat)
            place(drums, 0.7 * snare(), t0 + 3 * beat)
            if i % 2 == 1:
                place(drums, 0.5 * hat(True), t0 + 3.5 * beat)
                place(drums, 0.5 * kick(), t0 + 3.5 * beat)
    place_melody(lead, COLD_LEAD, len(COLD_CHORDS) * bar, eighth, lead_note)

    arp = echo(arp, 3 * sixteenth, feedback=0.4, mix=0.35, repeats=5)[:n]
    lead = echo(lead, 3 * eighth, feedback=0.35, mix=0.35, repeats=5)[:n]
    pad = echo(pad, 2 * beat, feedback=0.3, mix=0.25, repeats=3)[:n]
    out = 0.38 * bass + 0.16 * pad + 0.10 * arp + 0.17 * lead + 0.42 * drums
    return fold_loop(np.tanh(out * 1.4) / np.tanh(1.4), total)


# -- Red Alert ------------------------------------------------------------------
#
# The same kit, wound tight: D minor at 132 bpm, the chords leaning on the
# half step above the root (D minor to E flat, the Phrygian sound of a
# threat), a bass pulsing in sixteenths, a two-note ostinato ticking like a
# warning, and four-on-the-floor kicks. The first eight bars build; the
# second add a siren-like lead and a tom fill into the loop. The last bar's
# A major pulls back to D minor.

RED_BEAT = 60.0 / 132
RED_BAR = 4 * RED_BEAT
RED_CHORDS = [  # one per bar: (bass root, pad voicing)
    ("D1", ["D3", "F3", "A3", "E4"]),
    ("Eb1", ["Eb3", "G3", "Bb3", "D4"]),
    ("D1", ["D3", "F3", "A3", "C4"]),
    ("C1", ["C3", "Eb3", "G3", "D4"]),
    ("Bb0", ["Bb2", "D3", "F3", "A3"]),
    ("G0", ["G2", "Bb2", "D3", "F3"]),
    ("A0", ["A2", "C#3", "E3", "Bb3"]),
    ("A0", ["A2", "C#3", "E3", "G3"]),
]
# Over the chords, an ostinato in sixteenths: the root, the half step above
# it, the root an octave up (the "-" here never hold: each step plays).
RED_OSTINATO = [0, 1, 12, 1, 0, 1, 7, 1]
# Lead for bars 9-16, in eighth notes: a falling siren, then a climbing line.
RED_LEAD = (
    "A5 - - - Bb5 - - - | A5 - - - . . F5 E5 | D5 - - - Eb5 - - - | D5 - - - . . . . | "
    "F5 - E5 - D5 - C5 - | D5 - - - Bb4 - - - | A4 - C#5 - E5 - G5 - | A5 - - - - - . ."
)


def siren_note(freq, dur):
    """The lead: a hard pulse with a fast, narrow vibrato, like an alarm."""
    vib = 1 + 0.008 * np.sin(2 * np.pi * phase(9.0, dur))
    v = square(freq * vib, dur, 0.3) + 0.4 * saw(freq * vib * 1.004, dur)
    return crush(lowpass(v, 3600, 0.3) * adsr(dur, 0.004, 0.06, 0.75, 0.04), 5)


def music_red_alert():
    beat, bar = RED_BEAT, RED_BAR
    total = 2 * len(RED_CHORDS) * bar
    n = n_of(total + 4.0)
    bass, pad, ostinato, lead, drums = (np.zeros(n) for _ in range(5))
    eighth = beat / 2
    sixteenth = beat / 4

    for i, (root, voicing) in enumerate(RED_CHORDS * 2):
        t0 = i * bar
        second_half = i >= len(RED_CHORDS)
        for nm in voicing:
            place(pad, pad_voice(note(nm), bar + 0.3), t0)
        # Bass: sixteenths on the root, an octave jump on every off-beat,
        # biting harder in the second half.
        r = note(root)
        bite = 2600 if second_half else 1800
        for k in range(16):
            f = r * 2 if k % 4 == 2 else r
            place(bass, bass_note(f, sixteenth * 0.8, bite), t0 + k * sixteenth)
        # Ostinato: from bar 3, two octaves over the bass's root.
        if i >= 2:
            base = r * 4
            for k in range(16):
                semis = RED_OSTINATO[k % len(RED_OSTINATO)]
                place(ostinato, arp_note(base * 2 ** (semis / 12), sixteenth * 0.8), t0 + k * sixteenth)
        # Drums: four on the floor; sixteenth hats from bar 2; snare on 2
        # and 4 from bar 5; a tom fill in each half's last bar.
        last_bar = i % len(RED_CHORDS) == len(RED_CHORDS) - 1
        for b in range(4):
            place(drums, kick(), t0 + b * beat)
        if i >= 1:
            for k in range(16):
                place(drums, (0.6 if k % 4 == 2 else 0.35) * hat(), t0 + k * sixteenth)
        if i >= 4:
            place(drums, 0.75 * snare(), t0 + beat)
            if not last_bar:
                place(drums, 0.75 * snare(), t0 + 3 * beat)
        if last_bar:
            for k, f in enumerate([180, 180, 140, 140, 110, 110, 90, 90]):
                place(drums, 0.7 * tom(f), t0 + 2 * beat + k * sixteenth)
    place_melody(lead, RED_LEAD, len(RED_CHORDS) * bar, eighth, siren_note)

    ostinato = echo(ostinato, 3 * sixteenth, feedback=0.3, mix=0.25, repeats=3)[:n]
    lead = echo(lead, 2 * eighth, feedback=0.3, mix=0.3, repeats=4)[:n]
    pad = echo(pad, beat, feedback=0.25, mix=0.2, repeats=3)[:n]
    out = 0.36 * bass + 0.13 * pad + 0.09 * ostinato + 0.16 * lead + 0.45 * drums
    return fold_loop(np.tanh(out * 1.6) / np.tanh(1.6), total)


# -- More voices, for the tracks below ------------------------------------------

def bell_note(freq, dur):
    """A cold metallic chime: a sine with two inharmonic partials, ringing
    down (at least 0.6 s, whatever the note's length)."""
    d = max(dur, 0.6)
    v = (sine(freq, d) + 0.35 * sine(freq * 2.76, d) * env(d, 0.001, curve=9)
         + 0.18 * sine(freq * 5.4, d) * env(d, 0.001, curve=14))
    return crush(v * env(d, 0.002, curve=5), 7)


def pluck_note(freq, dur):
    """A plucked saw: the filter snaps open and closes again."""
    v = saw(freq, dur) + 0.5 * square(freq * 1.005, dur, 0.3)
    return crush(lowpass(v, 300 + 3500 * env(dur, 0.001, curve=6), 0.4) * env(dur, 0.001, curve=4), 6)


def soft_lead(freq, dur):
    """A hollow triangle-and-pulse voice with a slow vibrato, for the calm
    tracks."""
    vib = 1 + 0.005 * np.sin(2 * np.pi * phase(4.5, dur)) * np.clip(np.linspace(-1, 1.5, n_of(dur)), 0, 1)
    v = tri(freq * vib, dur) + 0.3 * square(freq * vib, dur, 0.5)
    return crush(lowpass(v, 2400) * adsr(dur, 0.03, 0.15, 0.7, 0.12), 6)


def drone_bass(freq, dur):
    """A held bass: round, with a little pulse edge."""
    v = tri(freq, dur) + 0.4 * square(freq, dur, 0.5)
    return lowpass(v, 420, 0.3) * adsr(dur, 0.05, 0.2, 0.8, 0.2)


def beacon_ping(freq):
    """A navigation beacon: a thin pulse blip."""
    d = 0.16
    return crush(square(freq, d, 0.125) * env(d, 0.001, curve=7), 5)


def stab(freqs, dur):
    """A chord hit: bright saws, gone quickly."""
    v = sum(saw(f * 0.998, dur) + saw(f * 1.002, dur) for f in freqs) / len(freqs)
    return crush(lowpass(v, 2600, 0.3) * env(dur, 0.002, curve=5), 6)


def power_lead(freq, dur):
    """A brassy lead: a pulse doubled an octave down, swelling into its
    vibrato and driven."""
    vib = 1 + 0.006 * np.sin(2 * np.pi * phase(6.0, dur)) * np.clip(np.linspace(-0.3, 1.5, n_of(dur)), 0, 1)
    v = square(freq * vib, dur, 0.4) + 0.6 * square(freq * vib / 2 * 1.003, dur, 0.25)
    return crush(drive(lowpass(v, 2800, 0.2), 1.6) * adsr(dur, 0.02, 0.1, 0.75, 0.05), 5)


def song(bpm, chords, bar_fn, lead, lead_voice, tail=4.0):
    """The shared frame of the tracks below: `chords` (one per bar) played
    twice, `bar_fn(layers, i, t0, root, voicing, second_half)` adding each
    bar's parts into the layers (bass, pad, arp, lead, drums), `lead` (eighth
    notes) over the second half. Returns the layers, the beat's length, the
    samples to build over and the loop's length (see finish_song)."""
    beat = 60.0 / bpm
    bar = 4 * beat
    total = 2 * len(chords) * bar
    n = n_of(total + tail)
    layers = {k: np.zeros(n) for k in ("bass", "pad", "arp", "lead", "drums")}
    for i, (root, voicing) in enumerate(chords * 2):
        bar_fn(layers, i, i * bar, note(root), voicing, i >= len(chords))
    if lead:
        place_melody(layers["lead"], lead, len(chords) * bar, beat / 2, lead_voice)
    return layers, beat, n, total


def finish_song(layers, beat, n, total, echoes, mix_levels, heat, peak=0.6):
    """Each layer's echo (by `echoes`: layer -> (time in beats, feedback,
    mix, repeats)), the mix by `mix_levels`, a tanh saturation of `heat`, and
    the loop folded."""
    for k, (time, fb, wet, reps) in echoes.items():
        layers[k] = echo(layers[k], time * beat, feedback=fb, mix=wet, repeats=reps)[:n]
    out = sum(mix_levels[k] * layers[k] for k in mix_levels)
    return fold_loop(np.tanh(out * heat) / np.tanh(heat), total, peak)


# -- Ice Drift (calm) -------------------------------------------------------------
#
# The quietest: E minor at 84 bpm, a chiming bell arpeggio over a held bass
# and the pad, no drums at first, then a soft kick and off-beat hats under a
# slow triangle lead. The last bar's B7 turns back to E minor.

ICE_CHORDS = [
    ("E2", ["E3", "G3", "B3", "F#4"]),
    ("C2", ["C3", "E3", "G3", "B3"]),
    ("G1", ["G3", "B3", "D4", "F#4"]),
    ("D2", ["D3", "F#3", "A3", "E4"]),
    ("A1", ["A3", "C4", "E4", "G4"]),
    ("E2", ["G3", "B3", "E4", "F#4"]),
    ("C2", ["C3", "G3", "B3", "E4"]),
    ("B1", ["B2", "D#3", "F#3", "A3"]),
]
ICE_LEAD = (
    "B4 - - - - - E5 - | G5 - - - F#5 - E5 - | D5 - - - - - B4 - | A4 - - - - - . . | "
    "C5 - - - B4 - A4 - | B4 - - - G4 - E4 - | G4 - - - A4 - B4 - | D#5 - - - - - . ."
)
ICE_ARP = [0, 2, 1, 3, 2, 1, 3, 0]


def music_ice_drift():
    def bar_fn(L, i, t0, r, voicing, second_half):
        beat = 60.0 / 84
        bar, eighth = 4 * beat, beat / 2
        for nm in voicing:
            place(L["pad"], pad_voice(note(nm), bar + 0.6), t0)
        place(L["bass"], drone_bass(r, 2 * beat), t0)
        place(L["bass"], drone_bass(r, 2 * beat), t0 + 2 * beat)
        tones = [note(nm) * 2 for nm in voicing]
        for k, idx in enumerate(ICE_ARP):
            place(L["arp"], (0.8 if k % 2 else 1.0) * bell_note(tones[idx], eighth), t0 + k * eighth)
        if i >= 4:
            place(L["drums"], 0.6 * kick(), t0)
        if second_half:
            place(L["drums"], 0.45 * kick(), t0 + 2.5 * beat)
            for k in range(4):
                place(L["drums"], 0.45 * hat(), t0 + (k + 0.5) * beat)
            if i % 4 == 3:
                place(L["drums"], 0.35 * hat(True), t0 + 3.5 * beat)

    layers, beat, n, total = song(84, ICE_CHORDS, bar_fn, ICE_LEAD, soft_lead)
    return finish_song(layers, beat, n, total,
                       {"arp": (1.5, 0.45, 0.4, 5), "lead": (1.5, 0.35, 0.35, 5), "pad": (2, 0.3, 0.3, 3)},
                       {"bass": 0.34, "pad": 0.18, "arp": 0.14, "lead": 0.2, "drums": 0.4}, 1.3)


# -- Deep Core (calm) -------------------------------------------------------------
#
# The busiest of the calm ones, for long stretches of building: D Dorian at
# 104 bpm, a syncopated bass, a plucked sixteenth ostinato and a steady
# beat, the lead in the second half. The last bar's A7 leads back to D minor.

CORE_CHORDS = [
    ("D2", ["D3", "F3", "A3", "C4"]),
    ("G1", ["D3", "G3", "B3", "E4"]),
    ("D2", ["F3", "A3", "C4", "E4"]),
    ("A1", ["E3", "A3", "C4", "E4"]),
    ("Bb1", ["F3", "Bb3", "D4", "A4"]),
    ("C2", ["E3", "G3", "C4", "D4"]),
    ("D2", ["D3", "F3", "A3", "E4"]),
    ("A1", ["C#3", "E3", "G3", "A3"]),
]
CORE_LEAD = (
    "A4 - - - C5 - D5 - | E5 - - - D5 - B4 - | A4 - - - F4 - A4 - | E4 - - - - - . . | "
    "F4 - A4 - D5 - - - | E5 - - - C5 - G4 - | A4 - - - F5 - E5 - | C#5 - - - - - . ."
)
CORE_ARP = [0, 2, 1, 3, 2, 0, 3, 1]
# The bass, in eighths: (step, octave up?)
CORE_BASS = [(0, False), (3, True), (4, False), (6, False), (7, True)]


def music_deep_core():
    def bar_fn(L, i, t0, r, voicing, second_half):
        beat = 60.0 / 104
        bar, eighth, sixteenth = 4 * beat, beat / 2, beat / 4
        for nm in voicing:
            place(L["pad"], pad_voice(note(nm), bar + 0.4), t0)
        for step, up in CORE_BASS:
            place(L["bass"], bass_note(r * 2 if up else r, eighth * 0.85, 1100), t0 + step * eighth)
        if i >= 1:
            tones = [note(nm) * 2 for nm in voicing]
            for k in range(16):
                place(L["arp"], (1.0 if k % 4 == 0 else 0.7) * pluck_note(tones[CORE_ARP[k % 8]], sixteenth * 0.9),
                      t0 + k * sixteenth)
        if i >= 2:
            for t in (0, 1.5, 2):
                place(L["drums"], 0.8 * kick(), t0 + t * beat)
            for k in range(8):
                place(L["drums"], (0.5 if k % 2 else 0.7) * hat(), t0 + k * eighth)
        if i >= 4:
            place(L["drums"], 0.6 * snare(), t0 + beat)
            place(L["drums"], 0.6 * snare(), t0 + 3 * beat)
        if second_half and i % 2 == 1:
            place(L["drums"], 0.4 * hat(True), t0 + 3.5 * beat)

    layers, beat, n, total = song(104, CORE_CHORDS, bar_fn, CORE_LEAD, lead_note)
    return finish_song(layers, beat, n, total,
                       {"arp": (0.75, 0.35, 0.3, 4), "lead": (1.5, 0.35, 0.35, 5), "pad": (2, 0.3, 0.25, 3)},
                       {"bass": 0.36, "pad": 0.14, "arp": 0.1, "lead": 0.17, "drums": 0.4}, 1.4)


# -- Far Beacon (calm) ------------------------------------------------------------
#
# Lonely and wide: C minor at 88 bpm, a beacon pinging every bar into a long
# echo, a pulsing bass and the pad; hats creep in, then a half-time beat and
# a low triangle lead. The last bar's G7 turns back to C minor.

BEACON_CHORDS = [
    ("C2", ["C3", "Eb3", "G3", "D4"]),
    ("Ab1", ["Eb3", "Ab3", "C4", "G4"]),
    ("Eb2", ["Eb3", "G3", "Bb3", "D4"]),
    ("Bb1", ["D3", "F3", "Bb3", "C4"]),
    ("F1", ["F3", "Ab3", "C4", "Eb4"]),
    ("C2", ["Eb3", "G3", "C4", "D4"]),
    ("Ab1", ["Eb3", "Ab3", "C4", "Bb3"]),
    ("G1", ["D3", "G3", "B3", "F4"]),
]
BEACON_LEAD = (
    "G4 - - - Eb5 - - - | D5 - - - C5 - Bb4 - | G4 - - - - - - - | F4 - - - G4 - Ab4 - | "
    "Ab4 - - - G4 - F4 - | Eb4 - - - G4 - C5 - | C5 - - - Bb4 - Ab4 - | B4 - - - - - D5 - "
)


def music_far_beacon():
    def bar_fn(L, i, t0, r, voicing, second_half):
        beat = 60.0 / 88
        bar, sixteenth = 4 * beat, beat / 4
        for nm in voicing:
            place(L["pad"], pad_voice(note(nm), bar + 0.6), t0)
        for b in range(4):
            place(L["bass"], bass_note(r * 2 if b == 3 else r, beat * 0.7, 600), t0 + b * beat)
        place(L["arp"], beacon_ping(note(voicing[-1]) * 4), t0)
        if i >= 4:
            place(L["arp"], 0.6 * beacon_ping(note(voicing[1]) * 4), t0 + 2.5 * beat)
            for k in range(16):
                place(L["drums"], (0.45 if k % 4 == 2 else 0.2) * hat(), t0 + k * sixteenth)
        if second_half:
            place(L["drums"], 0.8 * kick(), t0)
            place(L["drums"], 0.5 * kick(), t0 + 1.5 * beat)
            place(L["drums"], 0.65 * snare(), t0 + 2 * beat)

    layers, beat, n, total = song(88, BEACON_CHORDS, bar_fn, BEACON_LEAD, soft_lead, tail=6.0)
    return finish_song(layers, beat, n, total,
                       {"arp": (1.5, 0.55, 0.5, 8), "lead": (1.5, 0.35, 0.35, 5), "pad": (2, 0.35, 0.3, 3)},
                       {"bass": 0.34, "pad": 0.2, "arp": 0.16, "lead": 0.2, "drums": 0.4}, 1.3)


# -- Broadside (combat) -----------------------------------------------------------
#
# Trading fire: E Phrygian at 140 bpm, the F major leaning on E minor,
# driving eighth bass, syncopated chord stabs (sparse, then busier), a
# punchy beat, and the lead in the second half. B major leads back to E
# minor.

BROADSIDE_CHORDS = [
    ("E1", ["E3", "G3", "B3", "D4"]),
    ("F1", ["F3", "A3", "C4", "E4"]),
    ("E1", ["E3", "G3", "B3", "E4"]),
    ("D1", ["D3", "F#3", "A3", "C4"]),
    ("C1", ["C3", "E3", "G3", "B3"]),
    ("D1", ["D3", "F#3", "A3", "D4"]),
    ("E1", ["E3", "G3", "B3", "F4"]),
    ("B0", ["B2", "D#3", "F#3", "A3"]),
]
BROADSIDE_LEAD = (
    "E5 - - - F5 - E5 - | D5 - - - C5 - - - | B4 - - - - - E5 - | F#5 - - - - - . . | "
    "G5 - F#5 - E5 - D5 - | C5 - - - D5 - E5 - | F5 - - - E5 - D5 - | D#5 - - - F#5 - - - "
)


def music_broadside():
    def bar_fn(L, i, t0, r, voicing, second_half):
        beat = 60.0 / 140
        bar, eighth, sixteenth = 4 * beat, beat / 2, beat / 4
        for nm in voicing:
            place(L["pad"], pad_voice(note(nm), bar + 0.3), t0)
        for k in range(8):
            place(L["bass"], bass_note(r * 2 if k % 4 == 3 else r, eighth * 0.8, 2400 if second_half else 1700),
                  t0 + k * eighth)
        freqs = [note(nm) * 2 for nm in voicing]
        for step in ((0, 3, 6, 10, 12) if second_half else (0, 6)):
            place(L["arp"], stab(freqs, sixteenth * 1.6), t0 + step * sixteenth)
        last_bar = i % len(BROADSIDE_CHORDS) == len(BROADSIDE_CHORDS) - 1
        for step in (0, 6, 8, 11):
            place(L["drums"], kick(), t0 + step * sixteenth)
        place(L["drums"], 0.75 * snare(), t0 + beat)
        if not last_bar:
            place(L["drums"], 0.75 * snare(), t0 + 3 * beat)
        if i >= 2:
            for k in range(16):
                place(L["drums"], (0.55 if k % 2 == 0 else 0.3) * hat(), t0 + k * sixteenth)
        if last_bar:
            for k, f in enumerate([200, 170, 150, 130, 115, 100, 90, 80]):
                place(L["drums"], 0.7 * tom(f), t0 + 2 * beat + k * sixteenth)

    layers, beat, n, total = song(140, BROADSIDE_CHORDS, bar_fn, BROADSIDE_LEAD, lead_note)
    return finish_song(layers, beat, n, total,
                       {"arp": (0.75, 0.3, 0.25, 3), "lead": (1, 0.3, 0.3, 4), "pad": (1, 0.25, 0.2, 3)},
                       {"bass": 0.36, "pad": 0.12, "arp": 0.12, "lead": 0.16, "drums": 0.45}, 1.6)


# -- Hard Burn (combat) -----------------------------------------------------------
#
# Running at full thrust: G minor at 126 bpm, a galloping bass (an eighth
# and two sixteenths a beat), a ticking ostinato from bar 5, and a brassy
# lead in the second half; the A flat chord (the Neapolitan) and D major
# lead back to G minor.

BURN_CHORDS = [
    ("G1", ["G3", "Bb3", "D4", "A4"]),
    ("Eb1", ["G3", "Bb3", "Eb4", "F4"]),
    ("F1", ["F3", "A3", "C4", "G4"]),
    ("G1", ["G3", "Bb3", "D4", "F4"]),
    ("C1", ["G3", "C4", "Eb4", "G4"]),
    ("Eb1", ["G3", "Bb3", "Eb4", "Bb4"]),
    ("Ab0", ["Ab3", "C4", "Eb4", "G4"]),
    ("D1", ["F#3", "A3", "C4", "D4"]),
]
BURN_LEAD = (
    "G5 - - - - - D5 - | Eb5 - - - D5 - C5 - | D5 - - - - - A4 - | Bb4 - - - - - . . | "
    "C5 - - - Eb5 - G5 - | Bb5 - - - G5 - Eb5 - | Ab5 - - - G5 - Eb5 - | F#5 - - - - - D5 - "
)
BURN_OSTINATO = [0, 7, 12, 7, 15, 12, 7, 3]


def music_hard_burn():
    def bar_fn(L, i, t0, r, voicing, second_half):
        beat = 60.0 / 126
        bar, eighth, sixteenth = 4 * beat, beat / 2, beat / 4
        for nm in voicing:
            place(L["pad"], pad_voice(note(nm), bar + 0.3), t0)
        for b in range(4):
            for step, length in ((0, 2), (2, 1), (3, 1)):
                f = r * 2 if (b == 3 and step == 3) else r
                place(L["bass"], bass_note(f, length * sixteenth * 0.85, 2200), t0 + b * beat + step * sixteenth)
        if i >= 4:
            base = r * 4
            for k in range(16):
                semis = BURN_OSTINATO[k % len(BURN_OSTINATO)]
                place(L["arp"], arp_note(base * 2 ** (semis / 12), sixteenth * 0.8), t0 + k * sixteenth)
        for step in (0, 8, 15):
            place(L["drums"], kick(), t0 + step * sixteenth)
        if i >= 1:
            place(L["drums"], 0.75 * snare(), t0 + beat)
            place(L["drums"], 0.75 * snare(), t0 + 3 * beat)
        for k in range(8):
            place(L["drums"], (0.6 if k % 2 else 0.4) * hat(), t0 + k * eighth)
        if i % 2 == 1:
            place(L["drums"], 0.45 * hat(True), t0 + 3.5 * beat)

    layers, beat, n, total = song(126, BURN_CHORDS, bar_fn, BURN_LEAD, power_lead)
    return finish_song(layers, beat, n, total,
                       {"arp": (0.75, 0.3, 0.25, 3), "lead": (1, 0.3, 0.3, 4), "pad": (1, 0.25, 0.2, 3)},
                       {"bass": 0.34, "pad": 0.13, "arp": 0.09, "lead": 0.15, "drums": 0.45}, 1.6)


# -- Hull Breach (combat) ---------------------------------------------------------
#
# The most frantic: B minor at 150 bpm, a sixteenth bass, a klaxon-like
# ostinato, four-on-the-floor kicks and a snare roll into the loop. F sharp
# major leads back to B minor.

BREACH_CHORDS = [
    ("B1", ["B2", "D3", "F#3", "C#4"]),
    ("G1", ["B2", "D3", "G3", "A3"]),
    ("A1", ["C#3", "E3", "A3", "B3"]),
    ("F#1", ["C#3", "F#3", "A#3", "E4"]),
    ("B1", ["D3", "F#3", "B3", "C#4"]),
    ("G1", ["D3", "G3", "B3", "F#4"]),
    ("E1", ["E3", "G3", "B3", "D4"]),
    ("F#1", ["C#3", "F#3", "A#3", "C#4"]),
]
BREACH_LEAD = (
    "F#5 - - - - - E5 D5 | D5 - - - B4 - - - | C#5 - - - E5 - A5 - | A#5 - - - - - . . | "
    "B5 - - - A5 - F#5 - | G5 - - - F#5 - D5 - | E5 - - - D5 - B4 - | A#4 - - - C#5 - F#5 - "
)
BREACH_OSTINATO = [0, 12, 7, 12, 0, 12, 8, 12]


def music_hull_breach():
    def bar_fn(L, i, t0, r, voicing, second_half):
        beat = 60.0 / 150
        bar, sixteenth = 4 * beat, beat / 4
        for nm in voicing:
            place(L["pad"], pad_voice(note(nm), bar + 0.3), t0)
        for k in range(16):
            place(L["bass"], bass_note(r * 2 if k % 8 == 6 else r, sixteenth * 0.8, 2600 if second_half else 2000),
                  t0 + k * sixteenth)
        if i >= 2:
            base = r * 4
            for k in range(16):
                semis = BREACH_OSTINATO[k % len(BREACH_OSTINATO)]
                place(L["arp"], arp_note(base * 2 ** (semis / 12), sixteenth * 0.7), t0 + k * sixteenth)
        last_bar = i % len(BREACH_CHORDS) == len(BREACH_CHORDS) - 1
        for b in range(4):
            place(L["drums"], kick(), t0 + b * beat)
        for k in range(16):
            place(L["drums"], (0.6 if k % 4 == 2 else 0.3) * hat(), t0 + k * sixteenth)
        if i >= 4 or second_half:
            place(L["drums"], 0.75 * snare(), t0 + beat)
            if not last_bar:
                place(L["drums"], 0.75 * snare(), t0 + 3 * beat)
        if last_bar:
            for k in range(8):
                place(L["drums"], (0.3 + 0.07 * k) * snare(), t0 + 2 * beat + k * sixteenth)

    layers, beat, n, total = song(150, BREACH_CHORDS, bar_fn, BREACH_LEAD, siren_note)
    return finish_song(layers, beat, n, total,
                       {"arp": (0.75, 0.3, 0.25, 3), "lead": (1, 0.3, 0.3, 4), "pad": (1, 0.25, 0.2, 3)},
                       {"bass": 0.35, "pad": 0.12, "arp": 0.09, "lead": 0.16, "drums": 0.45}, 1.6)


MUSIC = {
    # Calm: shuffled while nothing is fighting.
    "cold_orbit": music_cold_orbit,
    "ice_drift": music_ice_drift,
    "deep_core": music_deep_core,
    "far_beacon": music_far_beacon,
    # Combat: shuffled while the ship on screen is in a fight.
    "red_alert": music_red_alert,
    "broadside": music_broadside,
    "hard_burn": music_hard_burn,
    "hull_breach": music_hull_breach,
}


# ---------------------------------------------------------------------------

def write_wav(path, x, rate):
    data = (np.clip(x, -1, 1) * 32767).astype("<i2").tobytes()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data)


def main():
    global _rate, _rng
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--force", action="store_true", help="overwrite sounds that already exist")
    parser.add_argument("--out", default=os.path.join(ROOT, "assets", "sounds"), help="sounds folder")
    parser.add_argument("--only", nargs="+", metavar="NAME", help="make just these sounds (and/or music), overwriting them")
    args = parser.parse_args()

    unknown = set(args.only or []) - set(SOUNDS) - set(MUSIC)
    if unknown:
        print("unknown sound(s): " + ", ".join(sorted(unknown)), file=sys.stderr)
        return 1

    written = skipped = 0

    def write(rel, name, make, rate, level):
        nonlocal written, skipped
        global _rate, _rng
        if args.only and name not in args.only:
            return
        path = os.path.join(args.out, rel)
        if os.path.exists(path) and not (args.force or args.only):
            skipped += 1
            return
        _rate = rate
        _rng = np.random.default_rng(zlib.crc32(name.encode()))
        x = make()
        if level is not None:
            x = finish(x, level)
        write_wav(path, x, rate)
        written += 1

    for name, make in SOUNDS.items():
        write(f"sfx/{name}.wav", name, make, SR, LEVELS.get(name, 0))
    for name, make in MUSIC.items():
        write(f"music/{name}.wav", name, make, MUSIC_SR, None)

    print(f"{written} written, {skipped} already there (kept; --force to overwrite)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

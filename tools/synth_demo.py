#!/usr/bin/env python3
"""
Japi-base synth simulator — generates a WAV file that demonstrates
exactly what the built-in 6-channel synth would sound like.

4 melody channels (wavetable + ADSR) + 2 rhythm channels (noise + envelope).
Sample rate: 48360 Hz (806 scanlines × 60 Hz), 16-bit stereo.
"""

import wave, struct, math, random

SAMPLE_RATE = 48360
DURATION = 8.0
NUM_SAMPLES = int(SAMPLE_RATE * DURATION)
PWM_WRAP = 6500

# --- Wavetables (256 entries, values 0.0 to 1.0) ---

def make_sine():
    return [0.5 + 0.5 * math.sin(2 * math.pi * i / 256) for i in range(256)]

def make_square():
    return [1.0 if i < 128 else 0.0 for i in range(256)]

def make_sawtooth():
    return [i / 255.0 for i in range(256)]

def make_triangle():
    return [i / 127.5 if i < 128 else (255 - i) / 127.5 for i in range(256)]

WAVES = {
    'sine': make_sine(),
    'square': make_square(),
    'saw': make_sawtooth(),
    'triangle': make_triangle(),
}

# --- ADSR Envelope ---

class ADSR:
    def __init__(self, attack, decay, sustain, release):
        self.attack = int(attack * SAMPLE_RATE)
        self.decay = int(decay * SAMPLE_RATE)
        self.sustain = sustain
        self.release = int(release * SAMPLE_RATE)
        self.level = 0.0
        self.phase = 'idle'
        self.counter = 0

    def note_on(self):
        self.phase = 'attack'
        self.counter = 0

    def note_off(self):
        self.phase = 'release'
        self.counter = 0

    def tick(self):
        if self.phase == 'attack':
            self.counter += 1
            self.level = self.counter / max(self.attack, 1)
            if self.counter >= self.attack:
                self.phase = 'decay'
                self.counter = 0
                self.level = 1.0
        elif self.phase == 'decay':
            self.counter += 1
            t = self.counter / max(self.decay, 1)
            self.level = 1.0 - t * (1.0 - self.sustain)
            if self.counter >= self.decay:
                self.phase = 'sustain'
                self.level = self.sustain
        elif self.phase == 'sustain':
            self.level = self.sustain
        elif self.phase == 'release':
            self.counter += 1
            start = self.sustain if self.level <= self.sustain else self.level
            t = self.counter / max(self.release, 1)
            self.level = start * (1.0 - t)
            if self.counter >= self.release:
                self.phase = 'idle'
                self.level = 0.0
        else:
            self.level = 0.0
        return self.level

# --- Melody Channel ---

class MelodyChannel:
    def __init__(self, wave='sine', pan=0.5, volume=0.8):
        self.wavetable = WAVES[wave]
        self.phase = 0.0
        self.step = 0.0
        self.volume = volume
        self.pan = pan
        self.env = ADSR(0.01, 0.1, 0.7, 0.3)

    def set_freq(self, hz):
        self.step = hz * 256.0 / SAMPLE_RATE

    def set_envelope(self, a, d, s, r):
        self.env = ADSR(a, d, s, r)

    def note_on(self, hz=None):
        if hz: self.set_freq(hz)
        self.env.note_on()

    def note_off(self):
        self.env.note_off()

    def tick(self):
        self.phase = (self.phase + self.step) % 256.0
        sample = self.wavetable[int(self.phase)]
        env = self.env.tick()
        val = (sample - 0.5) * env * self.volume
        left = val * (1.0 - self.pan) * 2
        right = val * self.pan * 2
        return left, right

# --- Rhythm Channel (noise-based) ---

class DrumChannel:
    def __init__(self, pan=0.5, volume=0.6):
        self.volume = volume
        self.pan = pan
        self.env = ADSR(0.001, 0.05, 0.0, 0.05)
        self.use_tone = False
        self.tone_phase = 0.0
        self.tone_step = 0.0
        self.tone_env = ADSR(0.001, 0.08, 0.0, 0.02)

    def hit_snare(self):
        self.use_tone = False
        self.env = ADSR(0.001, 0.06, 0.0, 0.04)
        self.env.note_on()

    def hit_kick(self):
        self.use_tone = True
        self.tone_phase = 0.0
        self.tone_step = 60.0 * 256.0 / SAMPLE_RATE
        self.env = ADSR(0.001, 0.03, 0.0, 0.03)
        self.tone_env = ADSR(0.001, 0.12, 0.0, 0.02)
        self.env.note_on()
        self.tone_env.note_on()

    def hit_hihat(self):
        self.use_tone = False
        self.env = ADSR(0.001, 0.02, 0.0, 0.02)
        self.env.note_on()

    def tick(self):
        noise = random.random() - 0.5
        env = self.env.tick()
        val = noise * env * self.volume

        if self.use_tone:
            self.tone_phase = (self.tone_phase + self.tone_step) % 256.0
            tone = (WAVES['sine'][int(self.tone_phase)] - 0.5)
            tone_env = self.tone_env.tick()
            val += tone * tone_env * self.volume * 1.5

        left = val * (1.0 - self.pan) * 2
        right = val * self.pan * 2
        return left, right

# --- Composition ---

# Note frequencies
def note_hz(name, octave=4):
    notes = {'C':0,'C#':1,'D':2,'D#':3,'E':4,'F':5,'F#':6,'G':7,'G#':8,'A':9,'A#':10,'B':11}
    return 440.0 * (2.0 ** ((notes[name] + (octave - 4) * 12 - 9) / 12.0))

# Create channels
mel = [
    MelodyChannel('sine',     pan=0.4, volume=0.5),   # ch0: melody
    MelodyChannel('triangle', pan=0.3, volume=0.35),   # ch1: harmony 1
    MelodyChannel('triangle', pan=0.7, volume=0.35),   # ch2: harmony 2
    MelodyChannel('saw',      pan=0.5, volume=0.2),    # ch3: bass
]
mel[0].set_envelope(0.02, 0.1, 0.6, 0.4)
mel[1].set_envelope(0.05, 0.15, 0.5, 0.3)
mel[2].set_envelope(0.05, 0.15, 0.5, 0.3)
mel[3].set_envelope(0.01, 0.05, 0.8, 0.2)

drums = [
    DrumChannel(pan=0.5, volume=0.4),   # kick
    DrumChannel(pan=0.45, volume=0.25),  # snare/hihat
]

# Song: chord progression C-Am-F-G with melody, 120 BPM
BPM = 120
beat = int(SAMPLE_RATE * 60 / BPM)
half = beat // 2
quarter = beat // 4

# Schedule events: (sample_time, action)
events = []

chords = [
    # bar 1: C major
    [('C',4), ('E',4), ('G',4), ('C',3)],
    # bar 2: A minor
    [('A',4), ('C',5), ('E',4), ('A',2)],
    # bar 3: F major
    [('F',4), ('A',4), ('C',5), ('F',2)],
    # bar 4: G major
    [('G',4), ('B',4), ('D',5), ('G',2)],
]

melody_notes = [
    # bar 1
    ('E',5,0.5), ('G',5,0.5), ('C',6,0.75), ('B',5,0.25),
    # bar 2
    ('A',5,0.5), ('C',6,0.5), ('E',5,0.75), ('D',5,0.25),
    # bar 3
    ('F',5,0.5), ('A',5,0.5), ('C',6,0.5), ('A',5,0.5),
    # bar 4
    ('G',5,0.5), ('B',5,0.5), ('D',6,0.75), ('C',6,0.25),
]

# Repeat twice
for repeat in range(2):
    offset = repeat * 4 * 4 * beat

    # Chords (channels 1,2,3)
    for bar, chord in enumerate(chords):
        t = offset + bar * 4 * beat
        events.append((t, 'chord_on', chord))
        events.append((t + 4 * beat - quarter, 'chord_off', None))

    # Melody (channel 0)
    mel_time = offset
    for note_name, octave, dur_beats in melody_notes:
        dur = int(dur_beats * beat)
        events.append((mel_time, 'mel_on', (note_name, octave)))
        events.append((mel_time + dur - quarter, 'mel_off', None))
        mel_time += dur

    # Drums: kick on 1,3 — hihat on every beat — snare on 2,4
    for bar in range(4):
        for b in range(4):
            t = offset + (bar * 4 + b) * beat
            if b % 2 == 0:
                events.append((t, 'kick', None))
            else:
                events.append((t, 'snare', None))
            events.append((t, 'hihat', None))
            events.append((t + half, 'hihat', None))

events.sort(key=lambda e: e[0])

# --- Render ---

print("Rendering audio...")
samples_out = []
event_idx = 0

for i in range(NUM_SAMPLES):
    while event_idx < len(events) and events[event_idx][0] <= i:
        _, action, data = events[event_idx]
        if action == 'mel_on':
            mel[0].note_on(note_hz(data[0], data[1]))
        elif action == 'mel_off':
            mel[0].note_off()
        elif action == 'chord_on':
            mel[1].note_on(note_hz(data[0][0], data[0][1]))
            mel[2].note_on(note_hz(data[1][0], data[1][1]))
            mel[3].note_on(note_hz(data[2][0], data[2][1]))
        elif action == 'chord_off':
            mel[1].note_off()
            mel[2].note_off()
            mel[3].note_off()
        elif action == 'kick':
            drums[0].hit_kick()
        elif action == 'snare':
            drums[1].hit_snare()
        elif action == 'hihat':
            drums[1].hit_hihat()
        event_idx += 1

    left_mix = 0.0
    right_mix = 0.0
    for ch in mel:
        l, r = ch.tick()
        left_mix += l
        right_mix += r
    for ch in drums:
        l, r = ch.tick()
        left_mix += l
        right_mix += r

    left_mix = max(-1.0, min(1.0, left_mix))
    right_mix = max(-1.0, min(1.0, right_mix))

    left_16 = int(left_mix * 32000)
    right_16 = int(right_mix * 32000)
    samples_out.append(struct.pack('<hh', left_16, right_16))

# --- Write WAV ---

filename = '/home/jan/Japi/synth_demo.wav'
with wave.open(filename, 'w') as wf:
    wf.setnchannels(2)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(b''.join(samples_out))

print(f"Klaar! {filename}")
print(f"{NUM_SAMPLES} samples, {DURATION:.1f} seconden, {SAMPLE_RATE} Hz stereo")
print(f"Afspelen: aplay {filename}")

import numpy as np
import pyaudio
import math
from copy import copy, deepcopy


sample_rate_hz = 44100
beats_per_min = 200

beats_per_sec = beats_per_min / 60

samples_per_beat = sample_rate_hz / beats_per_sec
beat_len_sec = 1 / beats_per_sec

beats_per_meas = 4
window_size_meass = 4
window_size_beats = beats_per_meas * window_size_meass

assert int(samples_per_beat) == samples_per_beat
samples_per_beat = int(samples_per_beat)


def tone(freq=440, amp=0.5, sample_rate=44100, duration_sec=beat_len_sec):
    n = int(sample_rate * duration_sec)
    t = np.arange(n, dtype=np.float32) / sample_rate
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def kick_drum(
    freq_start=80,
    freq_end=30,
    amp=10,
    sample_rate=44100,
    duration_sec=beat_len_sec,
    amp_shift_rate=5,
    amp_decay_rate=8,
):
    # TODO parameterize amp decay
    time = np.linspace(0, duration_sec, int(sample_rate * duration_sec), endpoint=False)

    # Exponential frequency decay
    decay_factor = np.exp(
        -1 * amp_shift_rate * time / duration_sec
    )  # Adjust '5' for amplitude shift speed
    frequencies = freq_start * decay_factor + freq_end * (1 - decay_factor)

    # Generate the sine wave with changing frequency
    kick_drum = amp * np.sin(2 * np.pi * frequencies * time)
    # Exponential amplitude decay
    amplitude_envelope = np.exp(
        -1 * amp_decay_rate * time / duration_sec
    )  # Adjust '8' for decay speed
    kick_drum *= amplitude_envelope
    return kick_drum


p = pyaudio.PyAudio()
stream = p.open(
    rate=sample_rate_hz,
    channels=1,
    format=pyaudio.paFloat32,  # <— float32 output in [-1, 1]
    output=True,
    frames_per_buffer=1024,
)

waveform = np.zeros(window_size_beats * samples_per_beat, dtype=np.float32)
sounds = [[] for _ in range(window_size_beats)]


# each instrument has this
enable_matrix = tuple(
    [tuple([False for _ in range(beats_per_meas)]) for _ in range(window_size_meass)]
)


instruments = {
    "tone": (
        lambda: tone(freq=440, amp=0.5, sample_rate=sample_rate_hz),
        deepcopy(enable_matrix),
    ),
    "metronome": (
        lambda freq=800: tone(
            freq=freq,
            amp=0.5,
            sample_rate=sample_rate_hz,
            duration_sec=beat_len_sec / 10,
        ),
        deepcopy(enable_matrix),
    ),
    "kick": (lambda: kick_drum(), deepcopy(enable_matrix)),
}


instruments["metronome"] = (
    instruments["metronome"][0],
    (
        (1, 0, 1, 0),
        (1, 1, 1, 0),
        (1, 0, 1, 0),
        (1, 1, 1, 0),
    ),
    (  # override frequency on first tone
        (1000, None, None, None),
        (1000, None, None, None),
        (1000, None, None, None),
        (1000, None, None, None),
    ),
)

instruments["tone"] = (
    instruments["tone"][0],
    ((1, 0, 1, 0), (0, 0, 0, 0), (1, 0, 1, 0), (0, 0, 0, 0)),
)

instruments["kick"] = (
    instruments["kick"][0],
    ((0, 1, 0, 0), (1, 1, 0, 0), (0, 1, 0, 0), (1, 1, 0, 0)),
)


# compile all instruments into a beat-aligned sound tensor
# meass by beats by a dynamic (but perhaps bounded) quantity of waveforms that start on that beat
# we can encode swing into the individual instrument waveform samples and align it as usual

play = 0

for instrument_name, instrument in instruments.items():
    sound = instrument[0]
    en_matrix = instrument[1]
    param_matrix = tuple(
        tuple(None for _ in range(len(en_matrix))) for _ in range(len(en_matrix[0]))
    )
    if len(instrument) > 2:
        param_matrix = instrument[2]

    for meas_idx, meas in enumerate(en_matrix):
        for beat_idx, beat_en in enumerate(meas):
            if beat_en:
                print(meas_idx, beat_idx)
                sounds[meas_idx * beats_per_meas + beat_idx].append(
                    (sound, param_matrix[meas_idx][beat_idx])
                )


for beat_idx, sound in enumerate(sounds):
    for sub_sound in sound:
        print(beat_idx)
        offset = beat_idx * samples_per_beat

        if sub_sound[1]:
            evaled_sub_sound = sub_sound[0](sub_sound[1])
        else:
            evaled_sub_sound = sub_sound[0]()
        waveform[
            offset : offset + len(evaled_sub_sound)
        ] += evaled_sub_sound  # beware potential clipping


# waveform = metallic_noise(duration_sec = 1)
# waveform = fir_bandpass(waveform, f_lo=400, f_hi=10e3)[0]
# waveform = waveform/max(waveform)
# waveform *= .001


stream.write(waveform.tobytes())
stream.stop_stream()
stream.close()
p.terminate()

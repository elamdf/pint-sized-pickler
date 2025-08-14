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
        freq_start=80, freq_end=30, amp=10, sample_rate=44100, duration_sec=beat_len_sec, amp_shift_rate = 5, amp_decay_rate = 8
):
    # TODO parameterize amp decay
    time = np.linspace(0, duration_sec, int(sample_rate * duration_sec), endpoint=False)

    # Exponential frequency decay
    decay_factor = np.exp(-1 * amp_shift_rate * time / duration_sec)  # Adjust '5' for amplitude shift speed
    frequencies = freq_start * decay_factor + freq_end * (1 - decay_factor)

    # Generate the sine wave with changing frequency
    kick_drum = amp * np.sin(2 * np.pi * frequencies * time)
    # Exponential amplitude decay
    amplitude_envelope = np.exp(-1 * amp_decay_rate * time / duration_sec)  # Adjust '8' for decay speed
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
        lambda: tone(
            freq=800,
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
)

instruments["tone"] = (
    instruments["tone"][0], (
        (1, 0, 1, 0),
        (0, 0, 0, 0),
        (1, 0, 1, 0),
        (0, 0, 0, 0)
    ),
)

instruments["kick"] = (
    instruments["kick"][0], (
        (0, 1, 0, 0),
        (1, 1, 0, 0),
        (0, 1, 0, 0),
        (1, 1, 0, 0)
    ),
)



# compile all instruments into a beat-aligned sound tensor
# meass by beats by a dynamic (but perhaps bounded) quantity of waveforms that start on that beat
# we can encode swing into the individual instrument waveform samples and align it as usual

play = 0

for instrument_name, instrument in instruments.items():
    sound = instrument[0]
    en_matrix = instrument[1]
    for meas_idx, meas in enumerate(en_matrix):
        for beat_idx, beat_en in enumerate(meas):
            if beat_en:
                print(meas_idx, beat_idx)
                sounds[meas_idx * beats_per_meas + beat_idx].append(sound)


for beat_idx, sound in enumerate(sounds):
    for sub_sound in sound:
        print(beat_idx)
        offset = beat_idx * samples_per_beat

        evaled_sub_sound = sub_sound()
        waveform[
            offset : offset + len(evaled_sub_sound)
        ] += evaled_sub_sound  # beware potential clipping



# ---------- Option 1: FIR band-pass (windowed-sinc, linear phase) ----------
def fir_bandpass_kernel(fs, f_lo, f_hi, numtaps=257, window="hann"):
    """
    Create a real FIR band-pass kernel with passband [f_lo, f_hi] Hz.
    numtaps should be odd for exact linear-phase symmetry.
    """
    assert 0 < f_lo < f_hi < fs/2, "Frequencies must be within (0, fs/2)"
    assert numtaps % 2 == 1, "Use an odd number of taps for linear phase"
    n = np.arange(numtaps) - (numtaps - 1)/2
    # Ideal lowpass impulse responses via normalized sinc
    def sinc_lp(fc):
        # fc in Hz; normalized by fs
        x = 2 * fc / fs
        # np.sinc is sin(pi x)/(pi x)
        return 2 * x * np.sinc(2 * x * n)
    h = sinc_lp(f_hi) - sinc_lp(f_lo)                  # ideal band-pass
    # Window to control sidelobes
    if window == "hann":
        w = 0.5 - 0.5*np.cos(2*np.pi*(np.arange(numtaps))/(numtaps-1))
    elif window == "hamming":
        w = 0.54 - 0.46*np.cos(2*np.pi*(np.arange(numtaps))/(numtaps-1))
    else:
        raise ValueError("window must be 'hann' or 'hamming'")
    h *= w
    # Normalize for ~unity gain in passband (L1 norm)
    h /= np.sum(h)
    return h.astype(np.float64)

def fir_bandpass(x, f_lo, f_hi, fs=sample_rate_hz, numtaps=257, window="hann", zi=None):
    """
    Convolve x with an FIR band-pass kernel. Supports streaming with zi.
    Returns y, zf where zf is tail state to feed into the next block.
    """
    h = fir_bandpass_kernel(fs, f_lo, f_hi, numtaps=numtaps, window=window)
    if zi is None:
        zi = np.zeros(len(h)-1, dtype=np.float64)
    # Direct-form FIR with overlap-save style state
    y = np.convolve(x, h, mode="full")
    y[:len(zi)] += zi
    zf = y[-(len(h)-1):].copy()
    y = y[:len(x)]  # time-aligned output (group delay = (numtaps-1)/2 samples)
    y = y/max(y)
    return y, zf



def metallic_noise(sample_rate=sample_rate_hz, duration_sec=beat_len_sec):
    """
    Create metallic sounding noise by mixing a bunch of sin waves together.
    """
    n = int(sample_rate * duration_sec)
    t = np.arange(n, dtype=np.float32) / sample_rate
    out = np.ones(n, dtype=np.float32)

    freqs = [123, 219, 150, 180, 240, 261] # hz

    for freq in freqs:
        out *= (np.sin(2 * np.pi * freq * t)).astype(np.float32)
    return out / max(out)


# waveform = metallic_noise(duration_sec = 1)
# waveform = fir_bandpass(waveform, f_lo=400, f_hi=10e3)[0]
# waveform = waveform/max(waveform)
# waveform *= .001


stream.write(waveform.tobytes())
stream.stop_stream()
stream.close()
p.terminate()

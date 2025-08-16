#include <Arduino.h>
#include <Audio.h>
#include <cmath>
#include <cstring>

#include "pickle_consts.h"

// for more sensible indexing
enum instruments {
  KICK,
  SNARE,
  HAT,
  TONE,
  SILENCE
};

enum buttons {
  KICK_BUTTON,
  SNARE_BUTTON,
  HAT_BUTTON,
  SILENCE_BUTTON
};


// ===== Synth renderers =====
using RenderFn = void(*)(float*, size_t, size_t, int, int, int);


static void render_noise(float* acc, size_t start, size_t n, int A, int, int V) {
  float tau = ((A > 0 ? (float)A : 60.0f) / 1000.0f);
  float gain = (float)V * 0.01f;

  uint32_t rng = 0x1234567u;
  float a = 1.0f;
  float k = expf(-1.0f / (tau * SAMPLE_RATE_HZ));
  for (size_t i = 0; i < n; ++i) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    float white = ((int)(rng & 0xFFFF) - 32768) / 32768.0f;
    acc[start + i] += white * a * gain;
    a *= k;
  }
}

static void render_tone(float* acc, size_t start, size_t n_samples, int freq, int perc_active, int V) {

  float gain = (float)V;
  float phase = start *  2.0f * (float)M_PI * freq / SAMPLE_RATE_HZ;

   while (phase > 2.0f * (float) M_PI) {
     phase -= 2.0f * (float)M_PI;
   }

  for (size_t i = 0; i < n_samples; ++i) {
    acc[start + i] += sinf(phase) * gain;
    phase += 2.0f * (float)M_PI * freq / SAMPLE_RATE_HZ;
    if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
  }

}

static void render_silence(float*, size_t, size_t, int, int, int) {}

#include <cmath>
#include <cstddef>

// Fixed attack pitch; make configurable if you want.
static constexpr float F0_HZ = 80.0f;

// Add a synthesized kick into acc[start..start+n)
inline void synth_kick_add(float* acc,
                           size_t start, size_t n,
                           float sample_rate,
                           float amp,
                           float pitch_drop_semitones,  // e.g., 24.f
                           float decay_ms)              // e.g., 120.f
{
  if (!acc || n == 0) return;

  const float tau = decay_ms * 1e-3f;                 // seconds
  const float g_per_sample = std::exp(-1.0f / (tau * sample_rate));

  // End frequency = F0 * 2^(-drop/12)
  const float f_end = F0_HZ * std::pow(2.0f, -pitch_drop_semitones / 12.0f);

  // We’ll use a smooth exponential glide from F0 → f_end driven by the same time constant:
  // f(t) = F0 * exp( ln(f_end/F0) * (1 - exp(-t/tau)) )
  // Implemented incrementally in the loop with a “time since start” in samples.
  const float log_ratio = std::log(f_end / F0_HZ);

  float phase = 0.0f;        // radians
  float gain  = amp;         // amplitude envelope (decays by g_per_sample)
  float t_s   = 0.0f;        // seconds since hit start

  for (size_t i = 0; i < n; ++i) {
    // exp(-t/tau) using per-sample multiply for speed:
    // keep a running e^{-t/tau} via the gain’s inverse (both share tau)
    const float e_minus_t_over_tau = gain / (amp + 1e-20f);  // stable when amp>0

    // instantaneous frequency from the closed form above:
    const float f = F0_HZ * std::exp(log_ratio * (1.0f - e_minus_t_over_tau));

    // integrate phase and write sample
    phase += 2.0f * float(M_PI) * f / sample_rate;
    if (phase > 2.0f * float(M_PI)) phase -= 2.0f * float(M_PI);

    acc[start + i] += gain * sinf(phase);

    // advance envelopes
    gain *= g_per_sample;
    t_s  += 1.0f / sample_rate;
  }
}

// Matches: using RenderFn = void(*)(float*, size_t, size_t, int, int, int);
inline void render_kick(float* acc, size_t start, size_t n,
                        int A_pitchDropSemi, int B_decayMs, int V_volPct)
{
  const float amp = (float)V_volPct * 0.01f;         // 0..1
  const float pitch_drop_semi = (float)A_pitchDropSemi;
  const float decay_ms = (float)B_decayMs;

  synth_kick_add(acc, start, n,
                 SAMPLE_RATE_HZ,         // provided externally
                 amp,
                 pitch_drop_semi,
                 decay_ms);
}

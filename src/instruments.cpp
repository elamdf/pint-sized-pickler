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
    acc[i] += white * a * gain;
    a *= k;
  }
}

static void render_tone(float* acc, size_t start, size_t n_samples, int freq, int perc_active, int V) {

  float gain = (float)V;
  float phase =  2.0f * (float)M_PI * freq / SAMPLE_RATE_HZ;

   while (phase > 2.0f * (float) M_PI) {
     phase -= 2.0f * (float)M_PI;
   }

  for (size_t i = 0; i < n_samples; ++i) {
    acc[i] += sinf(phase) * gain;
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
static inline void render_kick(float* acc,
                               size_t position_within_beat,     // NEW
                               size_t n,
                               int A, int B, int V)
{
  if (n == 0) return;

  const float sr   = SAMPLE_RATE_HZ;
  const float amp  = (float)V * 0.01f;        // 0..1
  const float drop = (float)A;                // semitones
  const float tau  = ((float)B > 1.f ? (float)B : 1.f) * 1e-3f;  // ms → s, clamp
  const float f0   = 80.0f;                   // attack freq (can be external)
  const float fend = f0 * powf(2.0f, -drop / 12.0f);

  // Shared exponential time constant for amp & pitch
  const float g_per_sample = expf(-1.0f / (tau * sr));
  const float log_ratio    = logf(fend / f0);

  // Pre-advance to t0
  Serial.println(position_within_beat);
  float e = powf(g_per_sample, (float)position_within_beat); // e^{-t0/tau} using same g
  float gain  = amp * e;
  float phase = 0.0f;

  // If you want exact phase continuity, integrate f from 0..t0 here.

  for (size_t i = 0; i < n; ++i) {
    // Frequency is driven by e^{-t/tau} which we update multiplicatively
    const float f = f0 * expf(log_ratio * (1.0f - e));

    phase += 2.0f * (float)M_PI * f / sr;
    if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;




    acc[i] += gain * sinf(phase);

    // advance both envelopes one sample
    e    *= g_per_sample;
    gain *= g_per_sample;
  }
}

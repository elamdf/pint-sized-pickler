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

static void render_kick(float* acc, size_t start, size_t n, int A, int B, int V) {
  float f0 = (A > 0 ? (float)A : 120.0f);
  float f1 = (B > 0 ? (float)B : 40.0f);
  float gain = (float)V * 0.01f;
  float tau = 0.08f;

  float phase = 0.0f;
  float a = 1.0f;
  float k = expf(-1.0f / (tau * SAMPLE_RATE_HZ));
  float df = (n > 1) ? (f1 - f0) / (float)n : 0.0f;
  float f = f0;

  for (size_t i = 0; i < n; ++i) {
    acc[start + i] += sinf(phase) * a * gain;
    phase += 2.0f * (float)M_PI * f / SAMPLE_RATE_HZ;
    if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
    a *= k;
    f += df;
  }
}


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

static void render_silence(float*, size_t, size_t, int, int, int) {}

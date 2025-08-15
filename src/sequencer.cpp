#include <Arduino.h>
#include <Audio.h>
#include <cmath>
#include <cstring>

// ===== Float-safe timing constants =====
static constexpr float SAMPLE_RATE_HZ = AUDIO_SAMPLE_RATE_EXACT; // ~44117.647
static constexpr float BEATS_PER_MIN   = 200.0f;
static constexpr float BEATS_PER_MEAS  = 4.0f;
static constexpr float WINDOW_SIZE_MEAS= 4.0f;
static constexpr int   N_INSTRUMENTS   = 16;

// Derived (float) timing values
static constexpr float BEATS_PER_SEC    = BEATS_PER_MIN / 60.0f;
static constexpr float SAMPLES_PER_BEAT = SAMPLE_RATE_HZ / BEATS_PER_SEC;
static constexpr float BEAT_LEN_SEC     = 1.0f / BEATS_PER_SEC;
static constexpr float WINDOW_SIZE_BEATS= BEATS_PER_MEAS * WINDOW_SIZE_MEAS;

// Mutable pattern data
static bool instrument_en[(int)WINDOW_SIZE_MEAS][(int)BEATS_PER_MEAS][N_INSTRUMENTS];
static int  instrument_params[(int)WINDOW_SIZE_MEAS][(int)BEATS_PER_MEAS][N_INSTRUMENTS][3];

// Helper: convert beats → samples
static inline size_t beats_to_samples(float beats) {
  return (size_t)lroundf(beats * SAMPLE_RATE_HZ / BEATS_PER_SEC);
}
static inline size_t step_to_abs_sample(int meas, int beat) {
  float beats_from_start = (float)meas * BEATS_PER_MEAS + (float)beat;
  return beats_to_samples(beats_from_start);
}
static inline size_t step_len_samples() {
  return beats_to_samples(1.0f); // 1 beat per step here
}

// ===== Audio graph =====
AudioPlayQueue   queue1;
AudioOutputI2S   i2s1;
AudioConnection  patchCord1(queue1, 0, i2s1, 0);

// ===== Synth renderers =====
using RenderFn = void(*)(float*, size_t, size_t, int, int, int);

static void render_kick_add(float* acc, size_t start, size_t n, int A, int B, int V) {
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

static void render_noise_add(float* acc, size_t start, size_t n, int A, int, int V) {
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

// Map instrument index → renderer
static RenderFn renderers[N_INSTRUMENTS] = {
  render_kick_add,  // 0
  render_noise_add, // 1 (snare)
  render_noise_add, // 2 (hat)
  render_silence,   // 3
  render_silence,   // 4
  render_silence,   // 5
  render_silence,   // 6
  render_silence,   // 7
  render_silence,   // 8
  render_silence,   // 9
  render_silence,   // 10
  render_silence,   // 11
  render_silence,   // 12
  render_silence,   // 13
  render_silence,   // 14
  render_silence    // 15
};

// ===== Soft clip + quantize =====
static inline void limit_and_to_i16(const float* in, int16_t* out, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    float x = in[i];
    float y = x / (1.0f + fabsf(x));
    int v = (int)lroundf(y * 32767.0f);
    if (v > 32767) v = 32767;
    else if (v < -32768) v = -32768;
    out[i] = (int16_t)v;
  }
}

// ===== Block synthesis =====
static size_t playhead_samples = 0;
static constexpr int BLOCK_SAMPLES = AUDIO_BLOCK_SAMPLES;

static void fill_block_and_queue_one_window() {
  float acc[BLOCK_SAMPLES] = {0};

  size_t block_start = playhead_samples;
  size_t block_end   = playhead_samples + BLOCK_SAMPLES;
  size_t song_len_samples = step_to_abs_sample((int)WINDOW_SIZE_MEAS, 0);
  size_t step_nsamp = step_len_samples();

  for (int meas = 0; meas < (int)WINDOW_SIZE_MEAS; ++meas) {
    for (int beat = 0; beat < (int)BEATS_PER_MEAS; ++beat) {
      size_t hit_start_abs = step_to_abs_sample(meas, beat);
      size_t hit_end_abs   = hit_start_abs + step_nsamp;

      if (hit_end_abs <= block_start || hit_start_abs >= block_end) continue;

      size_t abs_start = (hit_start_abs > block_start) ? hit_start_abs : block_start;
      size_t rel_start = abs_start - block_start;
      size_t render_len = BLOCK_SAMPLES - rel_start;
      if (abs_start + render_len > block_end) render_len = block_end - abs_start;

      for (int inst = 0; inst < N_INSTRUMENTS; ++inst) {
        if (!instrument_en[meas][beat][inst]) continue;
        int A = instrument_params[meas][beat][inst][0];
        int B = instrument_params[meas][beat][inst][1];
        int V = instrument_params[meas][beat][inst][2];
        renderers[inst](acc, rel_start, render_len, A, B, V);
      }
    }
  }

  if (queue1.available() > 0) {
    int16_t* buf = queue1.getBuffer();
    limit_and_to_i16(acc, buf, BLOCK_SAMPLES);
    queue1.playBuffer();
  }

  playhead_samples += BLOCK_SAMPLES;
  if (playhead_samples >= song_len_samples) {
    playhead_samples = 0;
  }
}

// ===== Arduino entry =====
void setup() {
  AudioMemory(16);

  // Example: kick on beat 0, snare on beat 2, hat on all beats
  memset(instrument_en, 0, sizeof(instrument_en));
  memset(instrument_params, 0, sizeof(instrument_params));

  instrument_en[0][0][0] = true; instrument_params[0][0][0][0] = 120; instrument_params[0][0][0][1] = 40; instrument_params[0][0][0][2] = 100;
  instrument_en[0][2][1] = true; instrument_params[0][2][1][0] = 60; instrument_params[0][2][1][2] = 80;
  for (int b = 0; b < (int)BEATS_PER_MEAS; ++b) {
    instrument_en[0][b][2] = true; instrument_params[0][b][2][0] = 20; instrument_params[0][b][2][2] = 50;
  }
}

void loop() {
  fill_block_and_queue_one_window();
}

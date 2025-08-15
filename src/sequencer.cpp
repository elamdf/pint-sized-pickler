#include <Arduino.h>
#include <Audio.h>
#include <cmath>
#include <cstring>

#include "instruments.cpp"
#include "pickle_consts.h"



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

// utility variables
static size_t song_len_samples = step_to_abs_sample((int)WINDOW_SIZE_MEAS, 0);
static size_t step_nsamp = step_len_samples();


// ===== Audio graph =====
AudioPlayQueue   queue1;
AudioOutputI2S   i2s1;
AudioConnection  patchCord1(queue1, 0, i2s1, 0);



// Map instrument index → renderer
static RenderFn renderers[N_INSTRUMENTS] = {
  render_kick,  // 0
  render_noise, // 1 (snare)
  render_noise, // 2 (hat)
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

static void render_block_into(float acc[BLOCK_SAMPLES], size_t playhead_samples) {

  size_t block_start = playhead_samples;
  size_t block_end   = playhead_samples + BLOCK_SAMPLES;



  // This cast will probably cause jitter/drift between windows.
  // I do not care.
  for (int meas = 0; meas < (int)WINDOW_SIZE_MEAS; ++meas) {
    for (int beat = 0; beat < (int)BEATS_PER_MEAS; ++beat) {
      // TODO(elamdf): dimly drive an LED corresponding to the beat,
      // perhaps with a shift register or something.

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
}

static void pump_audio() {

  // Feed as many blocks as the queue can accept right now.

  while (queue1.available() > 0) {
    // 1) Render the next block at *current* playhead
    float acc[AUDIO_BLOCK_SAMPLES] = {0};
    render_block_into(acc, /*start_sample=*/playhead_samples);

    // 2) Convert + queue
    int16_t *buf = queue1.getBuffer();
    limit_and_to_i16(acc, buf, AUDIO_BLOCK_SAMPLES);
    queue1.playBuffer();

    // 3) Advance ONLY after successful queue
    playhead_samples += AUDIO_BLOCK_SAMPLES;
    if (playhead_samples >= song_len_samples) {
      // also can cause clipping of end of pattern if misaligned
      playhead_samples = 0;
    }

  }
}

// ===== Arduino entry =====
void setup() {
  AudioMemory(16);


  // TODO(elamdf): is this necessary
  memset(instrument_en, 0, sizeof(instrument_en));
  memset(instrument_params, 0, sizeof(instrument_params));

  // kick on beat 0, snare on beat 2, hat on all beats
  instrument_en[0][0][KICK] = true;
  instrument_params[0][0][KICK][0] = 120;
  instrument_params[0][0][KICK][1] = 40;
  instrument_params[0][0][KICK][2] = 100;

  instrument_en[0][2][SNARE] = true;
  instrument_params[0][2][SNARE][0] = 60;
  instrument_params[0][2][SNARE][2] = 80;

  for (int b = 0; b < (int)BEATS_PER_MEAS; ++b) {
    instrument_en[0][b][HAT] = true;
    instrument_params[0][b][HAT][0] = 20;
    instrument_params[0][b][HAT][2] = 50;
  }
}

void loop() {
  pump_audio();
}

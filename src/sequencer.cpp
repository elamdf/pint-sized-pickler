#include <Arduino.h>
#include <Audio.h>
#include <cmath>
#include <cstring>

#include "instruments.cpp"
#include "pickle_consts.h"

// ===== Beat sync additions =====
constexpr int BEAT_PIN = LED_BUILTIN;         // GPIO to pulse
constexpr int BEAT_PULSE_MS = 20;   // Pulse width in ms
elapsedMillis beatPulseAge;
bool beatPulseActive = false;
IntervalTimer beatTimer;
volatile bool pendingBeat = false;



// Timer ISR: turn on pin
void beat_isr() {

  // TODO do digital read here?
  Serial.println("beat isr");
  digitalWriteFast(BEAT_PIN, HIGH);
  beatPulseActive = true;
  beatPulseAge = 0;
  pendingBeat = false;
}

// ===== Mutable pattern data =====
static bool instrument_en[(int)WINDOW_SIZE_MEAS][(int)BEATS_PER_MEAS][N_INSTRUMENTS];
static int  instrument_params[(int)WINDOW_SIZE_MEAS][(int)BEATS_PER_MEAS][N_INSTRUMENTS][3];
static size_t instrument_hit_len[(int)WINDOW_SIZE_MEAS][(int)BEATS_PER_MEAS][N_INSTRUMENTS];


// ===== Beat/sample helpers =====
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


// ===== Utility variables =====
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
  render_tone, //  3 (tone)
  render_silence, render_silence, render_silence,
  render_silence, render_silence, render_silence, render_silence,
  render_silence, render_silence, render_silence, render_silence, render_silence
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
static size_t nextStepSample   = 0; // NEW: absolute sample index of next step edge
static constexpr int BLOCK_SAMPLES = AUDIO_BLOCK_SAMPLES;


// TODO(elamdf): structural issue: a render can't know how far it is
static void render_block_into(float acc[BLOCK_SAMPLES], size_t playhead_samples) {
  size_t block_start = playhead_samples;
  size_t block_end   = playhead_samples + BLOCK_SAMPLES;

  for (int meas = 0; meas < (int)WINDOW_SIZE_MEAS; ++meas) {
    for (int beat = 0; beat < (int)BEATS_PER_MEAS; ++beat) {
      const size_t hit_start_abs = step_to_abs_sample(meas, beat);

      // --- moved *inside* the instrument loop so each instrument can have its own duration
      for (int inst = 0; inst < N_INSTRUMENTS; ++inst) {
        if (!instrument_en[meas][beat][inst]) continue;

        const size_t hit_len_samp = instrument_hit_len[meas][beat][inst];
        const size_t hit_end_abs  = hit_start_abs + hit_len_samp;

        // overlap test for this instrument's hit
        if (hit_end_abs <= block_start || hit_start_abs >= block_end) continue;

        // compute overlap region (relative to current block)
        const size_t abs_start  = (hit_start_abs > block_start) ? hit_start_abs : block_start;
        const size_t rel_start  = abs_start - block_start;
        size_t render_len       = block_end - abs_start;         // cap to block
        const size_t remaining  = hit_end_abs - abs_start;       // cap to remaining hit length
        size_t position_within_beat = abs_start - hit_start_abs;
        if (render_len > remaining) render_len = remaining;
        if (render_len > BLOCK_SAMPLES - rel_start) render_len = BLOCK_SAMPLES - rel_start;

        // render with your existing signature
        const int A = instrument_params[meas][beat][inst][0];
        const int B = instrument_params[meas][beat][inst][1];
        const int V = instrument_params[meas][beat][inst][2];
        renderers[inst](acc + rel_start, position_within_beat, render_len, A, B, V);
      }
    }
  }
}


// ===== Pump audio + schedule beats =====
static void pump_audio() {



  while (queue1.available() > 0) {
    // Serial.println("queue entered");
    float acc[AUDIO_BLOCK_SAMPLES] = {0};
    render_block_into(acc, playhead_samples);

    int16_t *buf = queue1.getBuffer();
    limit_and_to_i16(acc, buf, AUDIO_BLOCK_SAMPLES);
    queue1.playBuffer();

    // Check if next step edge falls in this block
    size_t block_start = playhead_samples;
    size_t block_end   = playhead_samples + BLOCK_SAMPLES;
    while (nextStepSample >= block_start && nextStepSample < block_end) {
      size_t offset_samples = nextStepSample - block_start;
      float offset_us = (float)offset_samples * 1e6f / SAMPLE_RATE_HZ;
      if (!pendingBeat) {
        pendingBeat = true;
        beatTimer.begin(beat_isr, offset_us); // schedule GPIO high
        beatTimer.priority(32);
      }
      nextStepSample += step_nsamp; // schedule next step
    }

    playhead_samples = block_end;
    if (playhead_samples >= song_len_samples) {
      playhead_samples = 0;
      nextStepSample = 0;
    }
  }

  // Turn off beat pulse after width
  if (beatPulseActive && beatPulseAge >= BEAT_PULSE_MS) {
    digitalWriteFast(BEAT_PIN, LOW);
    beatPulseActive = false;
  }
}

// ===== Arduino entry =====
void setup() {
  Serial.begin(9600);
  AudioMemory(16);
  pinMode(BEAT_PIN, OUTPUT);
  digitalWriteFast(BEAT_PIN, LOW);

  memset(instrument_en, 0, sizeof(instrument_en));
  memset(instrument_params, 0, sizeof(instrument_params));

// Default every hit length to one full beat
for (int m = 0; m < (int)WINDOW_SIZE_MEAS; ++m)
  for (int b = 0; b < (int)BEATS_PER_MEAS; ++b)
    for (int i = 0; i < N_INSTRUMENTS; ++i)
      instrument_hit_len[m][b][i] = beats_to_samples(1.0f);

  // kick on beat 0, snare on beat 2, hat on all beats
  for (int m = 0; m < (int)WINDOW_SIZE_MEAS; ++m) {
      instrument_en[m][0][TONE] = true;
      instrument_params[m][0][TONE][0] = 1000;
      instrument_params[m][0][TONE][1] = 30;
      instrument_params[m][0][TONE][2] = -1;
      instrument_hit_len[m][0][TONE] = beats_to_samples(0.25f);
    for (int b = 1; b < (int)BEATS_PER_MEAS; ++b) {


      // instrument_en[m][b][TONE] = true;
      // instrument_params[m][b][TONE][0] = 400;
      // instrument_params[m][b][TONE][1] = 30;
      // instrument_params[m][b][TONE][2] = ;
      // instrument_hit_len[m][b][TONE] = beats_to_samples(0.5f);

      instrument_en[m][b][KICK] = true;
      instrument_params[m][b][KICK][0] = 24;
      instrument_params[m][b][KICK][1] = 100;
      instrument_params[m][b][KICK][2] = 40;
      instrument_hit_len[m][b][KICK] = beats_to_samples(1.0f);
      // instrument_en[2][0][KICK] = true;
      // instrument_params[2][0][KICK][0] = 120;
      // instrument_params[2][0][KICK][1] = 40;
      // instrument_params[2][0][KICK][2] = 100;
    }
  }

  // instrument_en[0][2][SNARE] = true;
  // instrument_params[0][2][SNARE][0] = 60;
  // instrument_params[0][2][SNARE][2] = 80;

  // for (int b = 0; b < (int)BEATS_PER_MEAS; ++b) {
  //   instrument_en[0][b][HAT] = true;
  //   instrument_params[0][b][HAT][0] = 20;
  //   instrument_params[0][b][HAT][2] = 50;
  // }

  playhead_samples = 0;
  nextStepSample   = 0;
  beatPulseActive  = false;
}

void loop() {
  pump_audio();
}

#pragma once
#include <Audio.h>  // for AUDIO_SAMPLE_RATE_EXACT

// -------- Sizes / counts (integral) --------
inline constexpr int   N_INSTRUMENTS    = 16;
inline constexpr int   BEATS_PER_MEAS   = 4;   // 4/4 time
inline constexpr int   WINDOW_SIZE_MEAS = 4;   // 4 measures in the window
inline constexpr int   STEPS_PER_BEAT   = 4;   // optional: 16-step grid

// Convenience if you need combined sizes
inline constexpr int   WINDOW_SIZE_BEATS = BEATS_PER_MEAS * WINDOW_SIZE_MEAS;
inline constexpr int   WINDOW_SIZE_STEPS = WINDOW_SIZE_BEATS * STEPS_PER_BEAT;

// -------- Timing (float math) --------
inline constexpr float SAMPLE_RATE_HZ   = AUDIO_SAMPLE_RATE_EXACT; // ~44117.647
inline constexpr float BEATS_PER_MIN    = 120.0f;
inline constexpr float BEATS_PER_SEC    = BEATS_PER_MIN / 60.0f;
inline constexpr float SAMPLES_PER_BEAT = SAMPLE_RATE_HZ / BEATS_PER_SEC;
inline constexpr float BEAT_LEN_SEC     = 1.0f / BEATS_PER_SEC;


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

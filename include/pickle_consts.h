// some_header_file.h
#ifndef PICKLE_CONSTS
#define PICKLE_CONSTS
// ===== Float-safe timing constants =====
static constexpr float SAMPLE_RATE_HZ = AUDIO_SAMPLE_RATE_EXACT; // ~44117.647
static constexpr float BEATS_PER_MIN   = 120.0f;
static constexpr float BEATS_PER_MEAS  = 4.0f;
static constexpr float WINDOW_SIZE_MEAS= 4.0f;
static constexpr int   N_INSTRUMENTS   = 16;

// Derived (float) timing values
static constexpr float BEATS_PER_SEC    = BEATS_PER_MIN / 60.0f;
static constexpr float SAMPLES_PER_BEAT = SAMPLE_RATE_HZ / BEATS_PER_SEC;
static constexpr float BEAT_LEN_SEC     = 1.0f / BEATS_PER_SEC;
static constexpr float WINDOW_SIZE_BEATS= BEATS_PER_MEAS * WINDOW_SIZE_MEAS;

#endif

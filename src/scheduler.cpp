#include "scheduler.h"
#include "synths.h"

// Provide a default if header doesn't define it
#ifndef STEPS_PER_BEAT
#define STEPS_PER_BEAT 4   // 16-step classic
#endif

// Pattern tables (row-major measure × beat × instrument)
bool instrument_en[WINDOW_SIZE_MEAS][BEATS_PER_MEAS][STEPS_PER_BEAT][N_INSTRUMENTS];
int  instrument_params[WINDOW_SIZE_MEAS][BEATS_PER_MEAS][STEPS_PER_BEAT][N_INSTRUMENTS][3];

// Step timer
static IntervalTimer stepTimer;
static IntervalTimer beatTimer;
static IntervalTimer ledTimer;
static volatile uint32_t stepIndex = 0;
static volatile uint32_t global_vol_pct = 40;

static int selected_instrument = KICK;

static float led_beat_pulse_duty_cycle = 0.5f;

// Period (μs) for one STEP (not beat)
static inline float step_period_us() {
  const float beats_per_sec = BEATS_PER_MIN / 60.0f;
  const float steps_per_sec = beats_per_sec * STEPS_PER_BEAT;
  return 1e6f / steps_per_sec;
}


static void  step_isr() {
  const uint32_t steps_per_meas = (uint32_t)(BEATS_PER_MEAS * STEPS_PER_BEAT);
  const uint32_t total_steps    = (uint32_t)(WINDOW_SIZE_MEAS * steps_per_meas);
  const uint32_t step              = stepIndex % total_steps;

  const uint32_t meas = step / steps_per_meas;
  // TODO(elamdf): these modulos operations are probably expensive
  const uint32_t stepInMeas = step % steps_per_meas;
  const uint32_t stepInBeat  = step % STEPS_PER_BEAT;
  const uint32_t beat        = stepInMeas / STEPS_PER_BEAT;

  // only trigger on beat for now

  if (instrument_en[meas][beat][stepInBeat][selected_instrument]) {
    digitalWrite(LED_BUILTIN, HIGH);  // turn the LED on (HIGH is the voltage level)

  } else { // if (stepInMeas % STEPS_PER_BEAT == (long)((float)STEPS_PER_BEAT * led_beat_pulse_duty_cycle)) {
    digitalWrite(LED_BUILTIN, LOW);
  }

  // Fire all instruments scheduled for this (meas, beat, step)
  for (int inst = 0; inst < N_INSTRUMENTS; ++inst) {
    if (!instrument_en[meas][beat][stepInBeat][inst]) continue;

    const int A = instrument_params[meas][beat][stepInBeat][inst][0];
    const int B = instrument_params[meas][beat][stepInBeat][inst][1];
    const int V = (int) ((float)instrument_params[meas][beat][stepInBeat][inst][2] * 1/global_vol_pct);

    switch (inst) {
    case KICK:  trigger_kick(A, B, V); break;
    case SNARE: trigger_snare(A, 0, V);           break;
    case HAT:
      trigger_hat(A, 0, V);
      break;
    case TONE:   trigger_tone(A,   B, V);           break;
    default:                   break;
    }


  }



  stepIndex++;


}

void scheduler_init() {
  // Clear pattern tables
  memset(instrument_en,     0, sizeof(instrument_en));
  memset(instrument_params, 0, sizeof(instrument_params));
  stepIndex = 0;
}

void scheduler_start() {
  const float step_us = step_period_us();
  stepIndex = 0;
  stepTimer.begin(step_isr, step_us);

  stepTimer.priority(30);

}

void scheduler_stop() {
  stepTimer.end();
  beatTimer.end();
}

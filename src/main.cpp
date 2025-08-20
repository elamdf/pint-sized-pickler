#include <Arduino.h>
#include <Audio.h>
#include "pickle_consts.h"
#include "synths.h"
#include "scheduler.h"

void set_instrument(int meas, int beat, int step, int instrument, bool en,
                    int A, int B, int V) {
  instrument_en[meas][beat][step][instrument] = en;
  instrument_params[meas][beat][step][instrument][0] = A;   // A: f0 Hz
  instrument_params[meas][beat][step][instrument][1] = B;  // B: drop ms
  instrument_params[meas][beat][step][instrument][2] = V;  // V: volume %

}
void setup() {
  AudioMemory(32);          // give the graph some headroom
  synths_setup_audio();     // build the synth graph

  scheduler_init(); // zero the pattern
  // control_init();        // zero the pattern

  selected_instrument = KICK;

  // LED setup
  pinMode(LED_BUILTIN, OUTPUT);

  for (int m = 0; m < WINDOW_SIZE_MEAS; m++) {
    // Kick: beat 0
    for (int b=0; b < BEATS_PER_MEAS; ++b) {
      set_instrument(m, b, 0, KICK, true, 80, 200, 100);
    }

    if (m == 1 || m == 3) {
      set_instrument(m, 2, 0, KICK, true, 80, 200, 80);
    } else {
      // Snare: beat 2
      set_instrument(m, 2, 0, SNARE, true, 140, 0, 80);
      // Snare: beat 3, step 2
      set_instrument(m, 3, 2, SNARE, true, 140, 0, 80);
    }

    if (m % 2 == 0) {

      set_instrument(m, 0, 0, TONE, true, 130, 120, 80);
      set_instrument(m, 0, 3, TONE, true, 130, 120, 80);
      set_instrument(m, 1, 2, TONE, true, 130, 120, 80);
      if (m == 2) {
      set_instrument(m, 2, 0, TONE, true, 170, 200, 80);
      set_instrument(m, 2, 2, TONE, true, 170, 200, 80);
      set_instrument(m, 3, 0, TONE, true, 190, 200, 80);
      set_instrument(m, 3, 2, TONE, true, 190, 200, 80);
      }

    }

    // Hats: every beat
    for (int b = 0; b < (int)BEATS_PER_MEAS; ++b) {
      for (int s = 0; s < (int)STEPS_PER_BEAT; ++s) {
        set_instrument(m, b, s, HAT, true, 60, 0, 40);
      }
    }
  }

  // -------------------------------------------

  scheduler_start();        // start the step ISR clock
}


void loop() {
  // per-loop updates (kick pitch glides, etc.)
  synths_update();
  // If BPM changes at runtime, call scheduler_stop(); scheduler_start();
}

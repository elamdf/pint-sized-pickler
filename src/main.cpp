#include <Arduino.h>
#include <Audio.h>
#include "pickle_consts.h"
#include "synths.h"
#include "scheduler.h"

void setup() {
  AudioMemory(32);          // give the graph some headroom
  synths_setup_audio();     // build the synth graph

  scheduler_begin();        // zero the pattern

  selected_instrument = KICK;


  // LED setup
  pinMode(LED_BUILTIN, OUTPUT);

  for (int m =0; m < WINDOW_SIZE_MEAS; m++) {
    // Kick: beat 0 and 2
    instrument_en[m][0][0][KICK] = true;
    instrument_params[m][0][0][KICK][0] = 80;   // A: f0 Hz
    instrument_params[m][0][0][KICK][1] = 120;  // B: drop ms
    instrument_params[m][0][0][KICK][2] = 100;  // V: volume %



    if (m == 1 || m == 3) {
      instrument_en[m][2][0][KICK] = true;
      instrument_params[m][2][0][KICK][0] = 80;
      instrument_params[m][2][0][KICK][1] = 200;
      instrument_params[m][2][0][KICK][2] = 80;

      // instrument_en[m][0][2][SNARE] = true;
      // instrument_params[m][0][2][SNARE][0] = 80;  // decay ms
      // instrument_params[m][0][2][SNARE][2] = 80;  // volume %

      // instrument_en[m][3][1][SNARE] = true;
      // instrument_params[m][3][1][SNARE][0] = 80;  // decay ms
      // instrument_params[m][3][1][SNARE][2] = 80;  // volume %
    } else {

      instrument_en[m][2][0][SNARE] = true;
      instrument_params[m][2][0][SNARE][0] = 80;  // decay ms
      instrument_params[m][2][0][SNARE][2] = 80;  // volume %

      // instrument_en[m][2][0][TONE] = true;
      // instrument_params[m][2][0][TONE][0] = 60;
      // instrument_params[m][2][0][TONE][1] = 120;
      // instrument_params[m][2][0][TONE][2] = 80;

      instrument_en[m][3][2][SNARE] = true;
      instrument_params[m][3][2][SNARE][0] = 80;  // decay ms
      instrument_params[m][3][2][SNARE][2] = 80;  // volume %
    }

    if (m == 0) {
      for (int i=0; i< 3; ++i) {
        instrument_en[m][0][i][TONE] = true;
        instrument_params[m][0][i][TONE][0] = 60 * (i+2);
        instrument_params[m][0][i][TONE][1] = 120;
        instrument_params[m][0][i][TONE][2] = 80;
      }
    }





    // Hats: every beat
    for (int b = 0; b < (int)BEATS_PER_MEAS; ++b) {
      for (int s = 0; s < (int)STEPS_PER_BEAT; ++s) {
        if (s % 2 == 0) {
          instrument_en[m][b][s][HAT] = true;
          instrument_params[m][b][s][HAT][0] = 20;  // decay ms
          instrument_params[m][b][s][HAT][2] = 40;  // volume %
        }
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

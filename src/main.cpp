#include <Arduino.h>
#include <Audio.h>
#include "pickle_consts.h"
#include "synths.h"
#include "scheduler.h"

void setup() {
  AudioMemory(32);          // give the graph some headroom
  synths_setup_audio();     // build the synth graph

  scheduler_begin();        // zero the pattern



  // LED setup
  pinMode(LED_BUILTIN, OUTPUT);

  for (int m =0; m < WINDOW_SIZE_MEAS; m++) {
    // Kick: beat 0 and 2
    instrument_en[m][0][KICK] = true;
    instrument_params[m][0][KICK][0] = 80;   // A: f0 Hz
    instrument_params[m][0][KICK][1] = 120;  // B: drop ms
    instrument_params[m][0][KICK][2] = 100;  // V: volume %

    if (m == 3) {
          instrument_en[m][2][KICK] = true;
    instrument_params[m][2][KICK][0] = 80;
    instrument_params[m][2][KICK][1] = 120;
    instrument_params[m][2][KICK][2] = 80;
    }



    // // Snare: beat 2
    // instrument_en[m][2][SNARE] = true;
    // instrument_params[m][2][SNARE][0] = 80;  // decay ms
    // instrument_params[m][2][SNARE][2] = 80;  // volume %

    // Hats: every beat
    for (int b = 0; b < (int)BEATS_PER_MEAS; ++b) {
      instrument_en[m][b][HAT] = true;
      instrument_params[m][b][HAT][0] = 20;  // decay ms
      instrument_params[m][b][HAT][2] = 40;  // volume %
    }
  }

  // -------------------------------------------

  scheduler_start();        // start the step ISR clock
}

void loop() {
  // per-loop updates (kick pitch glides, etc.)
  synths_update();

  // ...your UI / buttons / tempo changes...
  // If BPM changes at runtime, call scheduler_stop(); scheduler_start();
}

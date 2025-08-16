#pragma once
#include <Arduino.h>
#include "pickle_consts.h"

// Expose your pattern tables so main.cpp can edit them
extern bool instrument_en[WINDOW_SIZE_MEAS][BEATS_PER_MEAS][N_INSTRUMENTS];
extern int  instrument_params[WINDOW_SIZE_MEAS][BEATS_PER_MEAS][N_INSTRUMENTS][3];

// Start/stop the step scheduler (timer-driven)
void scheduler_begin();  // init pattern + timer (not started)
void scheduler_start();  // start ISR at the current BPM/STEPS_PER_BEAT
void scheduler_stop();

#pragma once
#include <Arduino.h>
#include <Audio.h>
#include "pickle_consts.h"

// --- Public API for synth subsystem ---
void synths_setup_audio();   // create graph, set mixer gains, etc.
void synths_update();        // call often (glides envelopes/pitch etc.)

// Trigger one-shot drums (called by the scheduler ISR)
void trigger_kick(int A_initFreqHz, int B_dropMs, int V_volPct);
void trigger_snare(int A_decayMs, int /*unused*/, int V_volPct);
void trigger_hat(int A_decayMs, int /*unused*/, int V_volPct);

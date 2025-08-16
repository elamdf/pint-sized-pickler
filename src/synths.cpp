#include "synths.h"

// ========= Voice pool sizes =========
static constexpr int KICK_VOICES  = 3;
static constexpr int SNARE_VOICES = 3;
static constexpr int HAT_VOICES   = 4;

// ========= Audio Objects =========
// Kick: sine -> env
AudioSynthWaveform  kickOsc [KICK_VOICES];
AudioEffectEnvelope kickEnv [KICK_VOICES];
AudioConnection     kickConn0(kickOsc[0], kickEnv[0]);
AudioConnection     kickConn1(kickOsc[1], kickEnv[1]);
AudioConnection     kickConn2(kickOsc[2], kickEnv[2]);

// Snare: white noise -> env
AudioSynthNoiseWhite snareNoise[SNARE_VOICES];
AudioEffectEnvelope  snareEnv  [SNARE_VOICES];
AudioConnection      snareConn0(snareNoise[0], snareEnv[0]);
AudioConnection      snareConn1(snareNoise[1], snareEnv[1]);
AudioConnection      snareConn2(snareNoise[2], snareEnv[2]);

// Hat: white noise -> env (closed hat)
AudioSynthNoiseWhite hatNoise[HAT_VOICES];
AudioEffectEnvelope  hatEnv  [HAT_VOICES];
AudioConnection      hatConn0(hatNoise[0], hatEnv[0]);
AudioConnection      hatConn1(hatNoise[1], hatEnv[1]);
AudioConnection      hatConn2(hatNoise[2], hatEnv[2]);
AudioConnection      hatConn3(hatNoise[3], hatEnv[3]);

// Mixers -> I2S (mono summed to L/R)
AudioMixer4 mixA;   // kick(0..2), snare(0)
AudioMixer4 mixB;   // snare(1..2), hat(0..1)
AudioMixer4 mixC;   // hat(2..3)
AudioMixer4 mixSUM;

AudioConnection mixA0(kickEnv[0], 0, mixA, 0);
AudioConnection mixA1(kickEnv[1], 0, mixA, 1);
AudioConnection mixA2(kickEnv[2], 0, mixA, 2);
AudioConnection mixA3(snareEnv[0],0, mixA, 3);

AudioConnection mixB0(snareEnv[1],0, mixB, 0);
AudioConnection mixB1(snareEnv[2],0, mixB, 1);
AudioConnection mixB2(hatEnv[0],  0, mixB, 2);
AudioConnection mixB3(hatEnv[1],  0, mixB, 3);

AudioConnection mixC0(hatEnv[2],  0, mixC, 0);
AudioConnection mixC1(hatEnv[3],  0, mixC, 1);

AudioConnection sumA(mixA,  0, mixSUM, 0);
AudioConnection sumB(mixB,  0, mixSUM, 1);
AudioConnection sumC(mixC,  0, mixSUM, 2);

AudioOutputI2S i2s1;
AudioConnection outL(mixSUM, 0, i2s1, 0);
AudioConnection outR(mixSUM, 0, i2s1, 1);

// Uncomment if you use the Audio Shield (SGTL5000)
AudioControlSGTL5000 sgtl;

// ========= Kick glide state =========
struct KickState {
  bool          active = false;
  elapsedMicros t_us;        // time since trigger
  float         f0_hz  = 80.f;
  float         tau_ms = 120.f;   // pitch & amp decay time-constant
  float         max_ms = 300.f;   // auto-off guard
};
static KickState kickState[KICK_VOICES];

static volatile uint8_t kickIdx  = 0;
static volatile uint8_t snareIdx = 0;
static volatile uint8_t hatIdx   = 0;

static inline float clamp01(float x){ return x<0.f?0.f:(x>1.f?1.f:x); }

void synths_setup_audio() {
  // If using codec:
  sgtl.enable(); sgtl.volume(0.6f);

  // Init sources
  for (int i=0;i<KICK_VOICES;i++) { kickOsc[i].begin(WAVEFORM_SINE); kickOsc[i].amplitude(0.0f); }
  for (int i=0;i<SNARE_VOICES;i++){ snareNoise[i].amplitude(0.0f); }
  for (int i=0;i<HAT_VOICES;i++)  { hatNoise[i].amplitude(0.0f); }

  // Mixer gains (headroom)
  mixA.gain(0,0.9f); mixA.gain(1,0.9f); mixA.gain(2,0.9f); mixA.gain(3,0.8f);
  mixB.gain(0,0.8f); mixB.gain(1,0.8f); mixB.gain(2,0.6f); mixB.gain(3,0.6f);
  mixC.gain(0,0.6f); mixC.gain(1,0.6f);
  mixSUM.gain(0,0.9f); mixSUM.gain(1,0.9f); mixSUM.gain(2,0.9f);
}

// ---------------- Triggers ----------------
// Kick: A = init freq (Hz), B = drop ms, V = volume %
void trigger_kick(int A_initFreqHz, int B_dropMs, int V_volPct) {
  const uint8_t v = kickIdx; kickIdx = (kickIdx + 1) % KICK_VOICES;

  const float f0  = (float)max(1, A_initFreqHz);
  const float dms = (float)max(1, B_dropMs);
  const float amp = clamp01((float)V_volPct * 0.01f);

  // amp envelope (decay only)
  kickEnv[v].attack(0.0f); kickEnv[v].hold(0.0f);
  kickEnv[v].decay(dms);   kickEnv[v].sustain(0.0f);
  kickEnv[v].release(0.0f);
  kickEnv[v].noteOn();

  kickOsc[v].amplitude(amp);
  kickOsc[v].frequency(f0);

  kickState[v].active = true;
  kickState[v].t_us   = 0;
  kickState[v].f0_hz  = f0;
  kickState[v].tau_ms = dms;
  kickState[v].max_ms = max(5.f, dms * 3.f);
}

// Snare: A = decay ms, V = volume %
void trigger_snare(int A_decayMs, int /*unused*/, int V_volPct) {
  const uint8_t v = snareIdx; snareIdx = (snareIdx + 1) % SNARE_VOICES;
  const float dms = (float)max(1, A_decayMs);
  const float amp = clamp01((float)V_volPct * 0.01f);

  snareEnv[v].attack(0.0f); snareEnv[v].hold(0.0f);
  snareEnv[v].decay(dms);   snareEnv[v].sustain(0.0f);
  snareEnv[v].release(0.0f); snareEnv[v].noteOn();
  snareNoise[v].amplitude(amp);
}

// Hat (closed): A = decay ms, V = volume %
void trigger_hat(int A_decayMs, int /*unused*/, int V_volPct) {
  const uint8_t v = hatIdx; hatIdx = (hatIdx + 1) % HAT_VOICES;
  const float dms = (float)max(1, A_decayMs);
  const float amp = clamp01((float)V_volPct * 0.01f);

  hatEnv[v].attack(0.0f); hatEnv[v].hold(0.0f);
  hatEnv[v].decay(dms);   hatEnv[v].sustain(0.0f);
  hatEnv[v].release(0.0f); hatEnv[v].noteOn();
  hatNoise[v].amplitude(amp);
}

// ---------------- Per-loop updater ----------------
// Kick pitch glide: f(t) = f0 * exp(-t/tau)
void synths_update() {
  for (int v=0; v<KICK_VOICES; ++v) {
    if (!kickState[v].active) continue;
    const float t_ms = 0.001f * (float)kickState[v].t_us; // μs → ms
    if (t_ms > kickState[v].max_ms) {
      kickEnv[v].noteOff();
      kickOsc[v].amplitude(0.0f);
      kickState[v].active = false;
      continue;
    }
    const float f = kickState[v].f0_hz * expf(-t_ms / kickState[v].tau_ms);
    kickOsc[v].frequency(max(1.0f, f));
  }
}

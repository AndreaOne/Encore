#include <Arduino.h>
#include <math.h>

#define MIC_PIN 34

const int SAMPLE_RATE = 16000;
const int BUFFER_SIZE = 1024;

const float MIN_FREQ = 380.0;
const float MAX_FREQ = 900.0;
float RMS_THRESHOLD = 30.0;

const int ADC_CENTER = 2048;

#define NOTE_C4 261.63
#define NOTE_CS4 277.18
#define NOTE_D4 293.66
#define NOTE_DS4 311.13
#define NOTE_E4 329.63
#define NOTE_F4 349.23
#define NOTE_FS4 369.99
#define NOTE_G4 392.00
#define NOTE_GS4 415.30
#define NOTE_A4 440.00
#define NOTE_AS4 466.16
#define NOTE_B4 493.88
#define NOTE_C5 523.25
#define NOTE_CS5 554.37
#define NOTE_D5 587.33
#define NOTE_DS5 622.25
#define NOTE_E5 659.25
#define NOTE_F5 698.46
#define NOTE_FS5 739.99

const int SCORE_LEN = 42;

const float referenceScore[SCORE_LEN] = {
  NOTE_A4, NOTE_A4, NOTE_E5, NOTE_E5, NOTE_FS5, NOTE_FS5, NOTE_E5,
  NOTE_D5, NOTE_D5, NOTE_CS5, NOTE_CS5, NOTE_B4, NOTE_B4, NOTE_A4,

  NOTE_E5, NOTE_E5, NOTE_D5, NOTE_D5, NOTE_CS5, NOTE_CS5, NOTE_B4,
  NOTE_E5, NOTE_E5, NOTE_D5, NOTE_D5, NOTE_CS5, NOTE_CS5, NOTE_B4,

  NOTE_A4, NOTE_A4, NOTE_E5, NOTE_E5, NOTE_FS5, NOTE_FS5, NOTE_E5,
  NOTE_D5, NOTE_D5, NOTE_CS5, NOTE_CS5, NOTE_B4, NOTE_B4, NOTE_A4
};

const int phraseFlipPoints[6] = {5, 12, 19, 26, 33, 40};
const int PHRASE_COUNT = 6;

const int N_PARTICLES = 120;
const float STEP_MEAN = 0.18;
const float STEP_STD = 0.35;
const float PITCH_SIGMA = 16.0;

struct NoteRef {
  const char* name;
  float frequency;
};

const NoteRef noteTable[] = {
  {"C4", NOTE_C4},
  {"C#4", NOTE_CS4},
  {"D4", NOTE_D4},
  {"D#4", NOTE_DS4},
  {"E4", NOTE_E4},
  {"F4", NOTE_F4},
  {"F#4", NOTE_FS4},
  {"G4", NOTE_G4},
  {"G#4", NOTE_GS4},
  {"A4", NOTE_A4},
  {"A#4", NOTE_AS4},
  {"B4", NOTE_B4},
  {"C5", NOTE_C5},
  {"C#5", NOTE_CS5},
  {"D5", NOTE_D5},
  {"D#5", NOTE_DS5},
  {"E5", NOTE_E5},
  {"F5", NOTE_F5},
  {"F#5", NOTE_FS5}
};

const int NOTE_COUNT = sizeof(noteTable) / sizeof(noteTable[0]);

float particles[N_PARTICLES];
float weights[N_PARTICLES];
float estimatedIndex = 0;
int16_t audioBuffer[BUFFER_SIZE];
int lastPhrase = -1;

float randUniform() {
  return random(0, 10000) / 10000.0;
}

float randNormal() {
  float u1 = randUniform();
  float u2 = randUniform();

  if (u1 < 1e-6) u1 = 1e-6;

  return sqrt(-2 * log(u1)) * cos(2 * PI * u2);
}

float clamp(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

float absFloat(float x) {
  return x < 0 ? -x : x;
}

float gaussian(float x, float mu, float sigma) {
  float d = x - mu;
  return exp(-(d * d) / (2 * sigma * sigma));
}

float centsOff(float pitch, float reference) {
  return 1200.0 * log(pitch / reference) / log(2.0);
}

const char* noteNameFromFrequency(float frequency) {
  if (frequency <= 0) return "--";

  for (int i = 0; i < NOTE_COUNT; i++) {
    if (absFloat(frequency - noteTable[i].frequency) < 0.5) {
      return noteTable[i].name;
    }
  }

  return "UNK";
}

float snapPitchToReference(float pitch) {
  float bestNote = -1;
  float bestCents = 1e9;

  for (int i = 0; i < NOTE_COUNT; i++) {
    float cents = absFloat(centsOff(pitch, noteTable[i].frequency));

    if (cents < bestCents) {
      bestCents = cents;
      bestNote = noteTable[i].frequency;
    }
  }

  if (bestCents > 35.0) return -1;

  return bestNote;
}

void captureAudio(int16_t* buf, int n, int sampleRate) {
  unsigned long period = 1000000UL / sampleRate;
  unsigned long start = micros();

  for (int i = 0; i < n; i++) {
    while (micros() - start < i * period) {}

    int raw = analogRead(MIC_PIN);
    buf[i] = raw - ADC_CENTER;
  }
}

float computeRMS(const int16_t* buf, int n) {
  double sum = 0;

  for (int i = 0; i < n; i++) {
    sum += (double)buf[i] * buf[i];
  }

  return sqrt(sum / n);
}

float estimatePitch(const int16_t* buf, int n, int sampleRate, float minFreq, float maxFreq) {
  static float x[BUFFER_SIZE];

  float mean = 0;

  for (int i = 0; i < n; i++) {
    mean += buf[i];
  }

  mean /= n;

  for (int i = 0; i < n; i++) {
    x[i] = buf[i] - mean;
  }

  int minLag = sampleRate / maxFreq;
  int maxLag = sampleRate / minFreq;

  float bestCorr = -1;
  int bestLag = -1;

  for (int lag = minLag; lag <= maxLag; lag++) {
    double corr = 0;
    double e1 = 0;
    double e2 = 0;

    for (int i = 0; i < n - lag; i++) {
      float a = x[i];
      float b = x[i + lag];

      corr += a * b;
      e1 += a * a;
      e2 += b * b;
    }

    if (e1 < 1e-9 || e2 < 1e-9) continue;

    float norm = corr / sqrt(e1 * e2);

    if (norm > bestCorr) {
      bestCorr = norm;
      bestLag = lag;
    }
  }

  if (bestLag <= 0) return -1;
  if (bestCorr < 0.35) return -1;

  return (float)sampleRate / bestLag;
}

void initParticles() {
  for (int i = 0; i < N_PARTICLES; i++) {
    particles[i] = 0 + 0.4 * randNormal();
    particles[i] = clamp(particles[i], 0, SCORE_LEN - 1);
    weights[i] = 1.0 / N_PARTICLES;
  }
}

void predictParticles() {
  for (int i = 0; i < N_PARTICLES; i++) {
    float step = STEP_MEAN + STEP_STD * randNormal();

    if (step < -0.2) step = -0.2;

    particles[i] += step;
    particles[i] = clamp(particles[i], 0, SCORE_LEN - 1);
  }
}

void updateWeights(float pitch) {
  float sum = 0;

  for (int i = 0; i < N_PARTICLES; i++) {
    int idx = round(particles[i]);
    idx = clamp(idx, 0, SCORE_LEN - 1);

    float expected = referenceScore[idx];
    float w = gaussian(pitch, expected, PITCH_SIGMA);

    weights[i] = w + 1e-12;
    sum += weights[i];
  }

  for (int i = 0; i < N_PARTICLES; i++) {
    weights[i] /= sum;
  }
}

float estimateState() {
  float est = 0;

  for (int i = 0; i < N_PARTICLES; i++) {
    est += particles[i] * weights[i];
  }

  return est;
}

bool checkFlipTrigger(int refIdx) {
  for (int i = lastPhrase + 1; i < PHRASE_COUNT; i++) {
    if (refIdx >= phraseFlipPoints[i]) {
      lastPhrase = i;
      return true;
    }
  }

  return false;
}

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);

  initParticles();

  Serial.println("Mic + particle filter ready");
}

void loop() {
  captureAudio(audioBuffer, BUFFER_SIZE, SAMPLE_RATE);

  float rms = computeRMS(audioBuffer, BUFFER_SIZE);
  float rawPitch = -1;
  float pitch = -1;

  if (rms > RMS_THRESHOLD) {
    rawPitch = estimatePitch(audioBuffer, BUFFER_SIZE, SAMPLE_RATE, MIN_FREQ, MAX_FREQ);

    if (rawPitch > 0) {
      pitch = snapPitchToReference(rawPitch);
    }
  }

  if (pitch > 0) {
    predictParticles();
    updateWeights(pitch);

    estimatedIndex = estimateState();
    estimatedIndex = clamp(estimatedIndex, 0, SCORE_LEN - 1);
  }

  int refIdx = round(estimatedIndex);
  float expectedNote = referenceScore[refIdx];
  bool flipTriggered = checkFlipTrigger(refIdx);

  Serial.print("RMS:");
  Serial.print(rms);

  Serial.print(" RawPitch:");
  Serial.print(rawPitch);

  Serial.print(" Pitch:");
  Serial.print(pitch);

  Serial.print(" Note:");
  Serial.print(noteNameFromFrequency(pitch));

  Serial.print(" Index:");
  Serial.print(estimatedIndex);

  Serial.print(" Expected:");
  Serial.print(noteNameFromFrequency(expectedNote));

  Serial.print(" Flip:");
  Serial.print(flipTriggered ? "YES" : "NO");

  Serial.print(" Phrase:");
  Serial.print(lastPhrase + 1);

  Serial.println();

  delay(35);
}

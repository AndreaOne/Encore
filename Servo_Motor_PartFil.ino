#include <Arduino.h>
#include <math.h>
#include <ESP32Servo.h>

//pins
#define MIC_PIN 34
#define SERVO_PIN 32

#define IN1 25
#define IN2 26
#define ENA 27

//servo 
Servo myservo;
int pos = 0;

// motor PWM 
const int PWM_FREQ = 5000;
const int PWM_RESOLUTION = 8;
int motorSpeed = 200;

//audio 
const int SAMPLE_RATE = 4000;
const int BUFFER_SIZE = 512;

const float MIN_FREQ = 380.0;
const float MAX_FREQ = 820.0;

float RMS_THRESHOLD = 30.0;

const int ADC_CENTER = 2048;

// ref notes
#define NOTE_A4 440.00
#define NOTE_B4 493.88
#define NOTE_CS5 554.37
#define NOTE_D5 587.33
#define NOTE_E5 659.25
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

// phrase end
const int phraseEnds[6] = {6,13,20,27,34,41};
int lastPhrase = -1;

//PARTICLES
const int N_PARTICLES = 120;

float particles[N_PARTICLES];
float weights[N_PARTICLES];
float estimatedIndex = 0;

const float STEP_MEAN = 0.18;
const float STEP_STD = 0.35;
const float PITCH_SIGMA = 16.0;

int16_t audioBuffer[BUFFER_SIZE];

bool flipRunning = false;

// Random
float randUniform(){
  return random(0,10000)/10000.0;
}

float randNormal(){
  float u1=randUniform();
  float u2=randUniform();
  if(u1<1e-6) u1=1e-6;
  return sqrt(-2*log(u1))*cos(2*PI*u2);
}

float clamp(float x,float lo,float hi){
  if(x<lo) return lo;
  if(x>hi) return hi;
  return x;
}

float gaussian(float x,float mu,float sigma){
  float d=x-mu;
  return exp(-(d*d)/(2*sigma*sigma));
}

// audio capture
void captureAudio(int16_t* buf,int n,int sampleRate){

  unsigned long period=1000000UL/sampleRate;
  unsigned long start=micros();

  for(int i=0;i<n;i++){

    while(micros()-start < i*period){}

    int raw=analogRead(MIC_PIN);
    buf[i]=raw-ADC_CENTER;
  }
}

float computeRMS(const int16_t* buf,int n){

  double sum=0;

  for(int i=0;i<n;i++)
  sum += (double)buf[i]*buf[i];

  return sqrt(sum/n);
}

//pitch
float estimatePitch(const int16_t* buf,int n,int sampleRate,float minFreq,float maxFreq){

  static float x[BUFFER_SIZE];

  float mean=0;

  for(int i=0;i<n;i++)
  mean+=buf[i];

  mean/=n;

  for(int i=0;i<n;i++)
  x[i]=buf[i]-mean;

  int minLag=sampleRate/maxFreq;
  int maxLag=sampleRate/minFreq;

  float bestCorr=-1;
  int bestLag=-1;

  for(int lag=minLag;lag<=maxLag;lag++){

    double corr=0;
    double e1=0;
    double e2=0;

    for(int i=0;i<n-lag;i++){

      float a=x[i];
      float b=x[i+lag];

      corr+=a*b;
      e1+=a*a;
      e2+=b*b;
    }

    if(e1<1e-9||e2<1e-9) continue;

    float norm=corr/sqrt(e1*e2);

    if(norm>bestCorr){
      bestCorr=norm;
      bestLag=lag;
    }
  }

  if(bestLag<=0) return -1;
  if(bestCorr<0.35) return -1;

  return (float)sampleRate/bestLag;
}

//PARTICLE FILTER
void initParticles(){

  for(int i=0;i<N_PARTICLES;i++){

    particles[i]=0+0.4*randNormal();
    particles[i]=clamp(particles[i],0,SCORE_LEN-1);

    weights[i]=1.0/N_PARTICLES;
  }
}

void predictParticles(){

  for(int i=0;i<N_PARTICLES;i++){

    float step=STEP_MEAN+STEP_STD*randNormal();

    if(step<-0.2) step=-0.2;

    particles[i]+=step;
    particles[i]=clamp(particles[i],0,SCORE_LEN-1);
  }
}

void updateWeights(float pitch){

  float sum=0;

  for(int i=0;i<N_PARTICLES;i++){

    int idx=round(particles[i]);
    idx=clamp(idx,0,SCORE_LEN-1);

    float expected=referenceScore[idx];

    float w=gaussian(pitch,expected,PITCH_SIGMA);

    weights[i]=w+1e-12;

    sum+=weights[i];
  }

  for(int i=0;i<N_PARTICLES;i++)
  weights[i]/=sum;
}

float estimateState(){

  float est=0;

  for(int i=0;i<N_PARTICLES;i++)
  est+=particles[i]*weights[i];

  return est;
}

//motor functions
void motorStart(){

  digitalWrite(IN1,LOW);
  digitalWrite(IN2,HIGH);

  ledcWrite(ENA,motorSpeed);
}

void motorStop(){

  digitalWrite(IN1,LOW);
  digitalWrite(IN2,LOW);

  ledcWrite(ENA,0);
}

//servo functions
void servo180(){

  for(pos=0;pos<=180;pos++){
    myservo.write(pos);
    delay(6);
  }
}

void servo0(){

  for(pos=180;pos>=0;pos--){
    myservo.write(pos);
    delay(6);
  }
}

// page flip function
void flipPage(){

  flipRunning=true;

  Serial.println("FLIP START");

  motorStart();
  delay(2000);
  motorStop();

  servo180();
  delay(500);
  servo0();

  Serial.println("FLIP END");

  flipRunning=false;
}

// phrase trigger (flip after each phrase)
void checkFlip(int refIdx){

  if(flipRunning) return;

  for(int i=lastPhrase+1;i<6;i++){

    if(refIdx>=phraseEnds[i]){

      lastPhrase=i;

      flipPage();
      break;
    }
  }
}

//SETUP
void setup(){

  Serial.begin(115200);

  pinMode(IN1,OUTPUT);
  pinMode(IN2,OUTPUT);
  pinMode(ENA,OUTPUT);

  ledcAttach(ENA,PWM_FREQ,PWM_RESOLUTION);

  myservo.setPeriodHertz(50);
  myservo.attach(SERVO_PIN,500,2400);
  myservo.write(0);

  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN,ADC_11db);

  initParticles();

  Serial.println("System Ready");
}

//LOOP
void loop(){

  captureAudio(audioBuffer,BUFFER_SIZE,SAMPLE_RATE);

  float rms=computeRMS(audioBuffer,BUFFER_SIZE);

  float pitch=-1;

  if(rms>RMS_THRESHOLD){

    pitch=estimatePitch(audioBuffer,BUFFER_SIZE,SAMPLE_RATE,MIN_FREQ,MAX_FREQ);
  }

  if(pitch>0){

    predictParticles();
    updateWeights(pitch);

    estimatedIndex=estimateState();
    estimatedIndex=clamp(estimatedIndex,0,SCORE_LEN-1);
  }

  int refIdx=round(estimatedIndex);

  checkFlip(refIdx);

  Serial.print("RMS:");
  Serial.print(rms);

  Serial.print(" Pitch:");
  Serial.print(pitch);

  Serial.print(" Index:");
  Serial.print(estimatedIndex);

  Serial.print(" Phrase:");
  Serial.println(lastPhrase);

  delay(35);
}
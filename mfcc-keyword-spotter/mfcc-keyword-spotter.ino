

#include <Arduino.h>
#include <arduinoFFT.h>


#include "constants.h"   // Matriks MFCC
#include "model_data.h"  // Model Random Forest


#define MIC_PIN 34
#define LED_PIN 2
#define NOISE_THRESHOLD 300


Eloquent::ML::Port::YesNoClassifier classifier;

int16_t audioBuffer[SAMPLE_RATE]; 
double vReal[N_FFT];
double vImag[N_FFT];
float mfcc_features[N_MFCC]; 

// FFT Object
arduinoFFT FFT = arduinoFFT(vReal, vImag, N_FFT, SAMPLE_RATE);

void recordAudio() {
    Serial.println("\n Mendengarkan...");
    digitalWrite(LED_PIN, HIGH);
    
    unsigned long interval = 1000000 / SAMPLE_RATE;
    unsigned long prevTime = micros();
    
    for (int i = 0; i < SAMPLE_RATE; i++) {
        while ((micros() - prevTime) < interval) { /* Wait */ }
        prevTime = micros();
        audioBuffer[i] = analogRead(MIC_PIN);
    }
    
    digitalWrite(LED_PIN, LOW);
}


bool preprocessAudio() {
    long signalSum = 0;
    int maxVal = 0;
    int minVal = 4095;

    
    for (int i = 0; i < SAMPLE_RATE; i++) {
        signalSum += audioBuffer[i];
        if (audioBuffer[i] > maxVal) maxVal = audioBuffer[i];
        if (audioBuffer[i] < minVal) minVal = audioBuffer[i];
    }
    
    int peakToPeak = maxVal - minVal;
    int dcOffset = signalSum / SAMPLE_RATE;

   
    if (peakToPeak < NOISE_THRESHOLD) {
        return false; // Hening
    }

   
    for (int i = 0; i < SAMPLE_RATE; i++) {
        audioBuffer[i] = audioBuffer[i] - dcOffset;
    }
    return true;
}

void computeMFCC() {
    // Reset fitur
    for(int i=0; i<N_MFCC; i++) mfcc_features[i] = 0.0;
    
    int frames_processed = 0;
    double scalingFactor = 1000.0; 

    // Sliding Window
    for (int i = 0; i <= SAMPLE_RATE - N_FFT; i += HOP_LENGTH) {
        
        // A. Frame Preparation & Pre-emphasis
        for (int j = 0; j < N_FFT; j++) {
            double val = (double)audioBuffer[i + j] / scalingFactor;
            if (j > 0) {
                double prev = (double)audioBuffer[i + j - 1] / scalingFactor;
                val = val - 0.97 * prev;
            }
            vReal[j] = val;
            vImag[j] = 0;
        }

        // B. FFT
        FFT.Windowing(FFT_WIN_TYP_HANN, FFT_FORWARD);
        FFT.Compute(FFT_FORWARD);
        FFT.ComplexToMagnitude();

        // C. Power Spectrum
        for(int j=0; j < SPECTROGRAM_BINS; j++) vReal[j] = vReal[j] * vReal[j];

        // D. Mel Filterbank
        float mel_energies[N_MELS];
        for (int m = 0; m < N_MELS; m++) {
            float sum = 0.0;
            for (int k = 0; k < SPECTROGRAM_BINS; k++) {
                sum += vReal[k] * MEL_FILTERS[m][k]; // Dari constants.h
            }
            mel_energies[m] = log10(sum + 1e-9);
        }

        // E. DCT (MFCC)
        for (int c = 0; c < N_MFCC; c++) {
            float sum = 0.0;
            for (int m = 0; m < N_MELS; m++) {
                sum += mel_energies[m] * DCT_MATRIX[c][m]; // Dari constants.h
            }
            mfcc_features[c] += sum;
        }
        frames_processed++;
    }

    // F. Global Average
    for(int i=0; i<N_MFCC; i++) {
        mfcc_features[i] /= frames_processed;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(MIC_PIN, INPUT);
    pinMode(LED_PIN, OUTPUT);
    analogSetAttenuation(ADC_11db);

    Serial.println("=== SYSTEM READY: YES vs NO ===");
    Serial.println("Silakan bicara...");
}

void loop() {
    
    recordAudio();

   
    if (preprocessAudio()) {
        
        //  Hitung MFCC
        unsigned long tStart = millis();
        computeMFCC();
        
        // random forest function call
        int prediksi = classifier.predict(mfcc_features);
        unsigned long tEnd = millis();

       
        Serial.print(tEnd - tStart); Serial.print("ms | ");
        
        if (prediksi == 1) {
            Serial.println("YES Detected!");
           
        } 
        else if (prediksi == 2) {
            Serial.println("NO Detected!");
          
        } 
        else {
            Serial.println("Suara lain");
        }
        
    } else {
        Serial.println("... (Hening)");
    }
    
    delay(500); // Jeda antar deteksi
}
#  IMPLEMENTASI DAN PEMBAHASAN

Bab ini membahas implementasi sistem *Keyword Spotting* menggunakan ESP32 yang dibagi menjadi tiga tahap utama: Akuisisi Data, Pelatihan Model, dan Penerapan (Deployment).

##  Tahap 1: Akuisisi Data (Data Acquisition)

Pada tahap ini, ESP32 dikonfigurasi sebagai perangkat akuisisi data untuk mengumpulkan sampel suara (dataset) dari pengguna. Tidak seperti sistem konvensional yang merekam via PC, sistem ini menggunakan perangkat *embedded* yang sama dengan yang akan digunakan saat *deployment* untuk menjaga konsistensi karakteristik sinyal.

### Mekanisme Web Server
ESP32 bekerja dalam mode WiFi Station/AP dan menjalankan Web Server sederhana. Antarmuka berbasis HTML memungkinkan pengguna untuk memberi label data ('Yes', 'No', atau 'Suara Lain') secara langsung saat perekaman.

**Kode Implementasi Arduino (`data-sample-record.ino`):**
```cpp
// Konfigurasi Web Server pada Port 80
WebServer server(80);

void setup() {
  // ... Init WiFi ...
  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });
  server.on("/record", handleRecord); // Endpoint untuk memicu rekaman
  server.begin();
}
```

### Perekaman Sinyal Audio
Proses perekaman dilakukan pada *sampling rate* 16.000 Hz selama 1 detik. Data diambil dari pin analog (ADC) yang terhubung ke mikrofon MAX4466.

**Potongan Kode Perekam (`handleRecord`):**
```cpp
#define SAMPLE_RATE 16000
#define DURATION 1
#define NUM_SAMPLES (SAMPLE_RATE * DURATION)

void handleRecord() {
    // ...
    unsigned long interval = 1000000 / SAMPLE_RATE; // Interval per sampel (mikrodetik)
    unsigned long prevMicros = micros();

    for (int i = 0; i < NUM_SAMPLES; i++) {
        // Tunggu hingga waktu sampling tercapai (Timer blocking sederhana)
        while ((micros() - prevMicros) < interval) { /* wait */ }
        prevMicros = micros();

        // Baca nilai ADC (0-4095)
        int raw = analogRead(MIC_PIN);
        
        // Konversi sederhana dan simpan ke buffer
        audioBuffer[i] = (raw - 1900) * 15; 
    }
    // ... Kirim sebagai file .wav ...
}
```
**Analisis Sinyal Mentah:**
Pada tahap ini, **tidak ada filter digital** (seperti *noise reduction* atau *bandpass filter*) yang diterapkan pada sinyal mentah. Hal ini disengaja agar model *Machine Learning* dapat mempelajari karakteristik *noise* latar belakang yang sesungguhnya. File disimpan dalam format `.wav` PCM 16-bit agar kompatibel dengan pustaka pemrosesan audio standar di Python.

---

## Tahap 2: Pelatihan Model (Model Training di Python)

Tahap pelatihan dilakukan menggunakan Python di lingkungan Jupyter Notebook. Proses ini mencakup pra-pemrosesan sinyal, ekstraksi fitur MFCC, pelatihan model klasifikasi, dan konversi model ke bahasa C++.

### Preprocessing Data
Setiap file audio yang dimuat dipastikan memiliki *sampling rate* 16 kHz. Jika durasi audio kurang atau lebih dari 1 detik, dilakukan teknik *padding* (penambahan nol) atau *truncating* (pemotongan).

```python
# Potongan kode dari trainning.ipynb (Cell 7 - extract_mfcc_manual)
target_len = SAMPLE_RATE * DURATION
if len(audio) < target_len:
    audio = np.pad(audio, (0, target_len - len(audio)))
else:
    audio = audio[:target_len]
```

### Ekstraksi Fitur (MFCC)
Algoritma MFCC (*Mel-Frequency Cepstral Coefficients*) diimplementasikan secara manual (langkah demi langkah) menggunakan fungsi dasar `librosa` dan `numpy`. Hal ini bertujuan agar logika matematika yang sama persis dapat diduplikasi di C++ pada ESP32 nantinya.

Tahapan ekstraksi fitur:
1.  **Pre-emphasis**: Filter *high-pass* untuk menonjolkan frekuensi tinggi.
2.  **Framing & Windowing**: Sinyal dibagi menjadi *frame* (Window Hann).
3.  **FFT & Power Spectrum**: Konversi ke domain frekuensi.
4.  **Mel Filterbank**: Pemetaan ke skala Mel (persepsi pendengaran manusia).
5.  **Log-Energy & DCT**: Menghasilkan koefisien Cepstral.

```python
# Potongan kode MFCC Manual
def extract_mfcc_manual(file_path):
    # ... load audio ...
    
    # Pre-emphasis
    audio = np.append(audio[0], audio[1:] - 0.97 * audio[:-1])
    
    # STFT -> Power Spectrum
    stft = librosa.stft(audio, n_fft=N_FFT, hop_length=HOP_LENGTH, window='hann', center=False)
    spectrogram = np.abs(stft)**2
    
    # Mel Spectrogram & DCT
    mel_spec = np.dot(mel_filters, spectrogram)
    log_mel_spec = np.log10(mel_spec + 1e-9)
    mfcc = np.dot(dct_matrix, log_mel_spec)
    
    # Global Average (Mean Pooling) -> Output 13 fitur
    return np.mean(mfcc, axis=1)
```

### Evaluasi Model
Tiga algoritma diuji: Random Forest, Decision Tree, dan KNN. Evaluasi dilakukan berdasarkan akurasi pada data uji (30% dari total dataset) dan analisis *overfitting*.

**Hasil Evaluasi (Contoh Representatif):**
Dari hasil *training*, model **Random Forest** menunjukkan performa paling stabil dengan akurasi uji tertinggi dan selisih (*gap*) terkecil antara akurasi *train* dan *test*.

```python
# Potongan kode Evaluasi (Cell 11)
models = {
    "Random Forest": RandomForestClassifier(...),
    "KNN": KNeighborsClassifier(...)
}
# ... Loop training dan testing ...
# Output: "Akurasi terbaik: Random Forest (Akurasi Test: 95.xx%)"
```

**(Masukkan Gambar Confusion Matrix dan Grafik Perbandingan Akurasi di sini pada Laporan)**

### Konversi Model (Porting)
Agar model dapat berjalan di mikrokontroler tanpa sistem operasi (*Bare Metal*), model Python dikonversi menjadi kode C++ murni (array dan logika `if-else`).
-   Model Random Forest dikonversi menggunakan pustaka `micromlgen`.
-   Matriks pendukung (Filterbank Mel dan DCT Matrix) diekspor ke `constants.h` agar perhitungan MFCC di ESP32 identik dengan di Python.

```python
# Potongan kode Porting (Cell 17)
from micromlgen import port

# Porting Model Klasifikasi
model_cpp_code = port(best_model_obj, classname="YesNoClassifier")

# Pembuatan constants.h (Array C++)
header_content = f"""
// Mel Filterbank Weights
{to_c_array("MEL_FILTERS", mel_filters)}
// DCT Matrix
{to_c_array("DCT_MATRIX", dct_matrix)}
"""
```

---

## Tahap 3: Penerapan (Deployment di ESP32)

Pada tahap akhir, file `model_data.h` dan `constants.h` diintegrasikan ke dalam *firmware* utama (`mfcc-esp32.ino`). Sistem ini bekerja secara *real-time* dalam loop utama.

### Preprocessing Real-time
Sebelum masuk ke ekstraksi fitur, sinyal mikrofon melewati pra-pemrosesan ringan untuk mengatasi bias DC dan memfilter suara hening.

1.  **Adaptive Normalization**: Menghilangkan DC Offset yang muncul dari tegangan bias mikrofon (biasanya sekitar VCC/2).
2.  **Noise Gate**: Sinyal dengan amplitudo *peak-to-peak* di bawah `NOISE_THRESHOLD` (misal: 300) akan diabaikan. Ini menghemat komputasi baterai saat hening.

```cpp
// Potongan kode mfcc-esp32.ino
bool preprocessAudio() {
    
    int dcOffset = signalSum / SAMPLE_RATE;
    
    // Cek Noise Gate
    if ((maxVal - minVal) < NOISE_THRESHOLD) return false; // Hening (300)

    
    for (int i = 0; i < SAMPLE_RATE; i++) {
        audioBuffer[i] = audioBuffer[i] - dcOffset;
    }
    return true;
}
```

### Sinkronisasi Matematika (MFCC On-Device)
Fungsi `computeMFCC()` di Arduino diprogram ulang untuk meniru `extract_mfcc_manual` di Python. Kuncinya adalah penggunaan matriks `MEL_FILTERS` dan `DCT_MATRIX` yang di-*include* dari `constants.h`. Ini menjamin bahwa input yang masuk ke *classifier* memiliki distribusi fitur yang sama dengan data latih.

### Logika Inferensi
Klasifikasi dilakukan lokal pada perangkat (*Edge Computing*). Fungsi `classifier.predict()` menerima array 13 fitur MFCC dan mengembalikan label kelas (1: Yes, 2: No, 0: Noise).

```cpp
// Potongan kode Inferensi
void loop() {
    recordAudio();
    if (preprocessAudio()) {
        computeMFCC(); // Hitung 13 fitur
        
        // Prediksi Local
        int prediksi = classifier.predict(mfcc_features);
        
        if (prediksi == 1) Serial.println("✅ YES Detected!");
        else if (prediksi == 2) Serial.println("❌ NO Detected!");
    }
}
```

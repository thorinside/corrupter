#ifndef CORRUPTER_TESTS_SPECTRAL_HELPERS_H_
#define CORRUPTER_TESTS_SPECTRAL_HELPERS_H_

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace corrupter_spec {

constexpr float kSampleRate = 96000.0f;
constexpr int kFraPoints = 60;
constexpr float kFraFmin = 20.0f;
constexpr float kFraFmax = 22050.0f;

std::vector<float> StandardFraGrid();

void GenSine(float* out, std::size_t len, float freq_hz,
             float sr = kSampleRate, float amp = 1.0f);

void GenLogSweep(float* out, std::size_t len, float f0, float f1,
                 float sr = kSampleRate, float amp = 1.0f);

void GenImpulse(float* out, std::size_t len, float amp = 1.0f);

void GenWhiteNoise(float* out, std::size_t len, std::uint32_t seed,
                   float amp = 0.25f);

void Fft(const float* in, std::size_t n, std::complex<float>* out);

float MagAtHz(const std::complex<float>* spectrum, std::size_t n, float sr,
              float target_hz);

float PowerInBand(const std::complex<float>* spectrum, std::size_t n, float sr,
                  float f_lo_hz, float f_hi_hz);

float PeakInBand(const std::complex<float>* spectrum, std::size_t n, float sr,
                 float f_lo_hz, float f_hi_hz);

float MagToDbfs(float mag, std::size_t n);

float Rms(const float* x, std::size_t len);
float PeakAbs(const float* x, std::size_t len);

float ClickEnergy(const float* x, std::size_t len);

struct FraPoint {
  float hz;
  float db;
};

std::vector<FraPoint> SteppedSineFra(
    std::function<float(float)> process,
    const std::vector<float>& freqs,
    float sr = kSampleRate,
    float settle_seconds = 0.2f,
    float measure_seconds = 0.3f,
    float input_amp = 0.25f);

float ThdPlusNPct(const std::complex<float>* spectrum, std::size_t n, float sr,
                  float f0, int nharm = 8);

std::vector<FraPoint> IrFra(std::function<float(float)> process,
                            const std::vector<float>& freqs,
                            std::size_t n,
                            float sr = kSampleRate,
                            float impulse_amp = 1.0f);

void WriteFraCsv(const std::string& path, const std::vector<FraPoint>& pts);
void WriteSamplesCsv(const std::string& path, const float* x, std::size_t len);
void WriteSpectrumCsv(const std::string& path,
                      const std::complex<float>* spectrum,
                      std::size_t n, float sr);

std::string OutputPath(const std::string& filename);

}  // namespace corrupter_spec

#endif  // CORRUPTER_TESTS_SPECTRAL_HELPERS_H_

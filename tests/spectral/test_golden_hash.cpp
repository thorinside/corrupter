// T-GOLDEN-DspHash: hash-based regression sentinel for the DSP engine.
//
// Drives corrupter::Engine through a fixed deterministic scenario that
// exercises every algorithm and smoother, then SHA-256s the rendered
// stereo float output. Compares against an in-source golden constant so
// that any unintended numerical change anywhere in the DSP pipeline is
// caught at CI time.
//
// To regenerate the golden after an intentional DSP change:
//   CORRUPTER_GOLDEN_REGEN=1 ./build/corrupter_dsp_spectral_tests \
//       --filter=GOLDEN
// Then paste the printed hash into kGoldenHash below.
//
// The hash is host-only (macOS / Linux on x86_64 or arm64 — all
// little-endian, IEEE-754). ARM hardware build is intentionally not
// covered: float determinism diverges across compilers/FPUs and the
// regression target is the host DSP library, not the plug-in.

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "corrupter_dsp/engine.h"

#include "spectral_helpers.h"
#include "test_framework.h"

namespace corrupter_spec {
namespace {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4) — compact reference implementation.
// ---------------------------------------------------------------------------

struct Sha256 {
  std::uint32_t state[8];
  std::uint64_t bits;
  std::uint8_t buf[64];
  std::uint32_t buf_len;

  static constexpr std::uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  };

  void Reset() {
    state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
    state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
    state[4] = 0x510e527f; state[5] = 0x9b05688c;
    state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
    bits = 0;
    buf_len = 0;
  }

  static std::uint32_t Rotr(std::uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
  }

  void Compress(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
             (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = h + S1 + ch + k[i] + w[i];
      const std::uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
      const std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = S0 + mj;
      h = g; g = f; f = e;
      e = d + t1;
      d = c; c = b; b = a;
      a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
  }

  void Update(const void* data, std::size_t len) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    bits += static_cast<std::uint64_t>(len) * 8;
    while (len > 0) {
      const std::uint32_t take =
          std::min<std::uint32_t>(64u - buf_len, static_cast<std::uint32_t>(len));
      std::memcpy(buf + buf_len, p, take);
      buf_len += take;
      p += take;
      len -= take;
      if (buf_len == 64) {
        Compress(buf);
        buf_len = 0;
      }
    }
  }

  void Final(std::uint8_t out[32]) {
    const std::uint64_t total_bits = bits;
    std::uint8_t pad = 0x80;
    Update(&pad, 1);
    std::uint8_t zero = 0;
    while (buf_len != 56) Update(&zero, 1);
    std::uint8_t len_be[8];
    for (int i = 0; i < 8; ++i) {
      len_be[i] = static_cast<std::uint8_t>(total_bits >> ((7 - i) * 8));
    }
    Update(len_be, 8);
    for (int i = 0; i < 8; ++i) {
      out[i * 4]     = static_cast<std::uint8_t>(state[i] >> 24);
      out[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
      out[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
      out[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
    }
  }
};
constexpr std::uint32_t Sha256::k[64];

std::string HashHex(const std::uint8_t digest[32]) {
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (int i = 0; i < 32; ++i) {
    oss << std::setw(2) << static_cast<int>(digest[i]);
  }
  return oss.str();
}

// ---------------------------------------------------------------------------
// Deterministic scenario.
// ---------------------------------------------------------------------------

constexpr float kSr = 96000.0f;
constexpr std::uint32_t kFrames = 192000;        // 2 s
constexpr std::uint32_t kBlockFrames = 192;      // odd-ish block size
constexpr float kInputAmp = 0.5f;
constexpr float kInputHz = 220.0f;

// Schedule entry: at `start_frame`, apply the listed state. Persistent state
// or knob changes between segments stress the smoothers; gate buffers stress
// the freeze/bend latches.
struct Segment {
  std::uint32_t start_frame;
  corrupter::CorruptBank bank;
  corrupter::CorruptAlgorithm algo;
  float corrupt_01;
  float bend_01;
  bool bend_gate;
  bool freeze_gate;
};

const std::array<Segment, 12> kSchedule = {{
  // start  bank                              algo                                corrupt bend  bend_g freeze_g
  {     0,  corrupter::CorruptBank::kLegacy,   corrupter::CorruptAlgorithm::kDecimate,  0.00f, 0.00f, false, false},
  { 16000,  corrupter::CorruptBank::kLegacy,   corrupter::CorruptAlgorithm::kDecimate,  0.50f, 0.00f, false, false},
  { 32000,  corrupter::CorruptBank::kLegacy,   corrupter::CorruptAlgorithm::kDestroy,   0.70f, 0.00f, false, false},
  { 48000,  corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kDjFilter,  0.30f, 0.00f, false, false},
  { 64000,  corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kDjFilter,  0.70f, 0.00f, false, false},
  { 80000,  corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kDjFilter,  0.50f, 0.50f, true,  false},
  { 96000,  corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kDjFilter,  0.50f, 0.50f, true,  true },
  {112000,  corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kDjFilter,  0.50f, 0.50f, true,  false},
  {128000,  corrupter::CorruptBank::kExpanded, corrupter::CorruptAlgorithm::kVinylSim,  0.50f, 0.00f, false, false},
  {144000,  corrupter::CorruptBank::kLegacy,   corrupter::CorruptAlgorithm::kDropout,   0.40f, 0.00f, false, false},
  {160000,  corrupter::CorruptBank::kLegacy,   corrupter::CorruptAlgorithm::kDropout,   0.40f, 0.80f, false, false},
  {176000,  corrupter::CorruptBank::kLegacy,   corrupter::CorruptAlgorithm::kDecimate,  0.00f, 0.00f, false, false},
}};

const Segment& SegmentAt(std::uint32_t frame) {
  const Segment* cur = &kSchedule[0];
  for (const auto& s : kSchedule) {
    if (s.start_frame <= frame) cur = &s;
    else break;
  }
  return *cur;
}

// ---------------------------------------------------------------------------
// Golden hash. Regenerate via CORRUPTER_GOLDEN_REGEN=1.
// ---------------------------------------------------------------------------

constexpr const char* kGoldenHash =
    "8cf19b65d1f316b5542ca83d3842c4147a6e43b59b4e78f419b4216166a29196";

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------

bool TestGoldenDspHash() {
  corrupter::EngineConfig cfg;
  cfg.sample_rate_hz = kSr;
  cfg.max_supported_sample_rate_hz = kSr;
  cfg.max_block_frames = kBlockFrames;
  cfg.max_buffer_seconds = 2.0f;
  cfg.random_seed = 0xC0FFEEu;

  std::vector<std::uint8_t> dram(corrupter::Engine::required_dram_bytes(cfg), 0u);
  corrupter::Engine engine;
  CORRUPTER_SPEC_REQUIRE(engine.initialise(dram.data(), dram.size(), cfg),
                         "engine init");

  corrupter::PersistentState state;
  state.bend_enabled = true;       // let bend gate edges through
  state.gate_latching = false;     // momentary
  state.freeze_latching = false;   // momentary
  state.macro_mode = true;
  state.corrupt_bank = kSchedule[0].bank;
  state.corrupt_algorithm = kSchedule[0].algo;

  corrupter::KnobState knobs;
  knobs.time_01 = 0.4f;
  knobs.repeats_01 = 0.0f;
  knobs.mix_01 = 1.0f;
  knobs.bend_01 = kSchedule[0].bend_01;
  knobs.corrupt_01 = kSchedule[0].corrupt_01;

  engine.set_persistent_state(state);
  engine.set_knobs(knobs);
  engine.set_clock_mode_internal(true);
  engine.set_audio_context(kSr, kBlockFrames);

  std::vector<float> input(kFrames);
  GenSine(input.data(), input.size(), kInputHz, kSr, kInputAmp);

  std::vector<float> in_block(kBlockFrames);
  std::vector<float> out_l_block(kBlockFrames);
  std::vector<float> out_r_block(kBlockFrames);
  std::vector<float> bend_gate_block(kBlockFrames);
  std::vector<float> freeze_gate_block(kBlockFrames);

  Sha256 hasher;
  hasher.Reset();

  Segment last_seg = kSchedule[0];

  std::uint32_t i = 0;
  while (i < kFrames) {
    const std::uint32_t n = std::min<std::uint32_t>(kBlockFrames, kFrames - i);

    // Apply schedule changes at block boundary (sufficient for our test —
    // block size is ≤ 2 ms so per-block updates are tighter than the
    // smoother time constants we want to exercise).
    const Segment& seg = SegmentAt(i);
    if (seg.bank != last_seg.bank || seg.algo != last_seg.algo) {
      state.corrupt_bank = seg.bank;
      state.corrupt_algorithm = seg.algo;
      engine.set_persistent_state(state);
    }
    knobs.corrupt_01 = seg.corrupt_01;
    knobs.bend_01 = seg.bend_01;
    engine.set_knobs(knobs);
    last_seg = seg;

    for (std::uint32_t k = 0; k < n; ++k) {
      in_block[k] = input[i + k];
      bend_gate_block[k] = seg.bend_gate ? 5.0f : 0.0f;
      freeze_gate_block[k] = seg.freeze_gate ? 5.0f : 0.0f;
    }

    corrupter::AudioBlock audio;
    audio.in_l = in_block.data();
    audio.in_r = in_block.data();
    audio.out_l = out_l_block.data();
    audio.out_r = out_r_block.data();
    audio.frames = n;
    corrupter::CvInputs cv;
    corrupter::GateInputs gates;
    gates.bend_gate_v = bend_gate_block.data();
    gates.freeze_gate_v = freeze_gate_block.data();
    engine.process(audio, cv, gates);

    hasher.Update(out_l_block.data(), n * sizeof(float));
    hasher.Update(out_r_block.data(), n * sizeof(float));
    i += n;
  }

  std::uint8_t digest[32];
  hasher.Final(digest);
  const std::string actual = HashHex(digest);

  const char* regen = std::getenv("CORRUPTER_GOLDEN_REGEN");
  if (regen && std::strcmp(regen, "1") == 0) {
    std::cerr << "  REGEN: golden hash = " << actual << "\n";
    std::cerr << "  Paste into kGoldenHash in test_golden_hash.cpp.\n";
    return true;
  }

  std::cerr << "  golden actual:   " << actual << "\n";
  std::cerr << "  golden expected: " << kGoldenHash << "\n";
  if (actual != kGoldenHash) {
    std::cerr << "  Mismatch indicates an unintended numerical change in\n";
    std::cerr << "  the DSP pipeline. If the change was intentional, regen\n";
    std::cerr << "  with CORRUPTER_GOLDEN_REGEN=1.\n";
    return false;
  }
  return true;
}

}  // namespace

void RegisterGoldenHashTests(std::vector<TestCase>& out) {
  out.push_back({"T-GOLDEN-DspHash", TestGoldenDspHash});
}

}  // namespace corrupter_spec

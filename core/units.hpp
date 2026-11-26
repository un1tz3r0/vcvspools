#pragma once
#include <cmath>

namespace spools {

inline float dbToGain(float db) { return std::pow(10.f, db / 20.f); }
inline float semitonesToRatio(float st) { return std::exp2(st / 12.f); }
inline int voltsToSemitones(float v) { return (int)std::lround(v * 12.f); }
inline float semitonesToVolts(int st) { return st / 12.f; }

// Euclidean modulo: wrap(-1, 4) == 3.
inline int wrap(int i, int n) { return n > 0 ? ((i % n) + n) % n : 0; }

}

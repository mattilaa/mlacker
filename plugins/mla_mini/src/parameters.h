#pragma once
#include <cmath>

namespace mla_mini {
// Stable IDs are 100 + Index. Append new parameters to preserve projects.
enum Index {
    Output, Wave1, Range1, Level1, Wave2, Range2, Fine2, Level2,
    Wave3, Range3, Fine3, Level3, Noise, Cutoff, Resonance, EnvDepth, KeyFollow,
    FilterAttack, FilterDecay, FilterSustain, FilterRelease,
    AmpAttack, AmpDecay, AmpSustain, AmpRelease, Drive, Feedback, Glide,
    Legato, Priority, Bend, BendRange, Wheel, ModPitch, ModFilter, Osc3Low,
    Osc3Keys, VelocityAmp, VelocityFilter, Sustain, Panic, OscDrive,
    AnalogOutput, OscDrift, BackgroundHiss, HissLevel, Count
};
struct Spec { const char* name; const char* unit; double lo, hi, initial; int steps; bool log; };
inline constexpr Spec specs[Count] = {
    {"Output", "dB", -60, 6, 0, 0, false},
    {"OSC 1 Wave", "", 0, 5, 2, 5, false},
    {"OSC 1 Range", "oct", -2, 2, 0, 4, false},
    {"OSC 1 Level", "", 0, 1, .85, 0, false},
    {"OSC 2 Wave", "", 0, 5, 2, 5, false},
    {"OSC 2 Range", "oct", -2, 2, 0, 4, false},
    {"OSC 2 Fine", "ct", -25, 25, 3.5, 0, false},
    {"OSC 2 Level", "", 0, 1, .65, 0, false},
    {"OSC 3 Wave", "", 0, 5, 0, 5, false},
    {"OSC 3 Range", "oct", -2, 2, -1, 4, false},
    {"OSC 3 Fine", "ct", -25, 25, -2, 0, false},
    {"OSC 3 Level", "", 0, 1, .3, 0, false},
    {"Noise", "", 0, 1, 0, 0, false},
    {"Cutoff", "Hz", 20, 20000, 600, 0, true},
    {"Resonance", "", 0, 1, .22, 0, false},
    {"Filter Env Depth", "oct", 0, 8, 3, 0, false},
    {"Key Follow", "", 0, 1, .35, 0, false},
    {"Filter Attack", "s", .001, 10, .003, 0, true},
    {"Filter Decay", "s", .01, 10, .65, 0, true},
    {"Filter Sustain", "", 0, 1, .08, 0, false},
    {"Filter Release", "s", .005, 10, .45, 0, true},
    {"Amp Attack", "s", .001, 10, .003, 0, true},
    {"Amp Decay", "s", .01, 10, 1.2, 0, true},
    {"Amp Sustain", "", 0, 1, .85, 0, false},
    {"Amp Release", "s", .005, 10, .48, 0, true},
    {"Drive", "x", 1, 8, 1.6, 0, true},
    {"Feedback", "", 0, 1, .06, 0, false},
    {"Glide", "s", 0, 1, .045, 0, false},
    {"Legato", "", 0, 1, 1, 1, false},
    {"Key Priority", "", 0, 2, 0, 2, false},
    {"Pitch Bend", "", -1, 1, 0, 0, false},
    {"Bend Range", "st", 0, 24, 2, 24, false},
    {"Mod Wheel", "", 0, 1, 0, 0, false},
    {"Mod > Pitch", "st", 0, 12, .15, 0, false},
    {"Mod > Filter", "oct", 0, 6, 1.5, 0, false},
    {"OSC 3 Low", "", 0, 1, 0, 1, false},
    {"OSC 3 Keyboard", "", 0, 1, 1, 1, false},
    {"Velocity > Amp", "", 0, 1, .15, 0, false},
    {"Velocity > Filter", "", 0, 1, .2, 0, false},
    {"Sustain Pedal", "", 0, 1, 0, 1, false},
    {"All Notes Off", "", 0, 1, 0, 1, false},
    {"OSC Drive", "x", 1, 4, 1.4, 0, true},
    {"Analog Output", "", 0, 1, .3, 0, false},
    {"OSC Drift", "ct", 0, 8, 1.2, 0, false},
    {"Background Hiss", "", 0, 10, 6, 10, false},
    {"Hiss Level", "dB", -96, -48, -54, 0, false},
};
inline double normalized(int i, double plain) {
    const auto& s = specs[i];
    return s.log ? std::log(plain / s.lo) / std::log(s.hi / s.lo) : (plain - s.lo) / (s.hi - s.lo);
}
inline double physical(int i, double n) {
    const auto& s = specs[i];
    const double p = s.log ? s.lo * std::pow(s.hi / s.lo, n) : s.lo + n * (s.hi - s.lo);
    return s.steps ? std::round(p) : p;
}
} // namespace mla_mini

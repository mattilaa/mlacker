#pragma once
#include "parameters.h"
namespace mla_jp8000
{
// Original performances, not Roland ROM data or transcriptions of the
// recording.
constexpr const char* kPadNames[]{"Obsession Atmosphere", "Velvet PWM",
                                  "Silver Horizon",       "Glass Tide",
                                  "Warm Fifths",          "Midnight Split"};
constexpr int kPadCount = sizeof(kPadNames) / sizeof(*kPadNames);
using Values = std::array<double, kParamCount>;
inline Values factoryPad(Values values, int program)
{
    using namespace mla_8000;
    const auto set = [&](ParamID id, double value)
    { values[paramIndex(id)] = value; };
    set(kMaster, kUnityLevelNorm - .055);
    set(kMode, double(program == 5 ? kSplitMode : kDual) / 3);
    set(kVoiceAssign, .5);
    set(kSplit, 59. / 127);
    set(kPartDetune, .64);
    set(kUpperTranspose, .5);
    set(kLowerTranspose, program == 5 ? .25 : .5);
    set(kUpperPan, .62);
    set(kLowerPan, .38);
    set(kBend, 8192. / 16383);
    set(kWheel, 0);
    set(kSustain, 0);
    set(kBrightness, .5);
    set(kUpperChannel, 0);
    set(kLowerChannel, 0);
    for(int p = 0; p < 2; ++p)
    {
        const auto patch = [&](ParamID id, double value)
        { set(partId(p, id), value); };
        patch(kOutputParam, kUnityLevelNorm);
        patch(kKeyModeParam, 0);
        patch(kPortamentoParam, 0);
        patch(kPitchBendParam, 8192. / 16383);
        patch(kModWheelParam, 0);
        patch(kWheelCutoffParam, .68);
        patch(kWheelPitchParam, .025);
        patch(kVelocityAmpParam, .18);
        patch(kVelocityFilterParam, .08);
        patch(kOsc1WaveParam, p ? 4. / 6 : 0);
        patch(kOsc1Ctrl1Param, p ? .38 : .57);
        patch(kOsc1Ctrl2Param, p ? .14 : .72);
        patch(kOsc2WaveParam, .5);
        patch(kOsc2RangeParam, .5);
        patch(kOsc2FineParam, p ? .42 : .56);
        patch(kOsc2WidthParam, .5);
        patch(kSyncParam, 0);
        patch(kRingParam, 0);
        patch(kCrossModParam, 0);
        patch(kBalanceParam, p ? .16 : .1);
        patch(kLfo1WaveParam, 0);
        patch(kLfo1RateParam, normFor(p ? .11 : .17, lfoRateFromNorm));
        patch(kLfo1FadeParam, timeNorm(2.2));
        patch(kLfo1PitchParam, .0005);
        patch(kLfo2RateParam, normFor(p ? .071 : .093, lfoRateFromNorm));
        patch(kLfo2PitchParam, .502);
        patch(kLfo2FilterParam, p ? .524 : .515);
        patch(kLfo2AmpParam, .025);
        patch(kLfo1FilterParam, .53);
        patch(kPitchEnvDepthParam, .5);
        patch(kPitchEnvAttackParam, 0);
        patch(kPitchEnvDecayParam, timeNorm(.2));
        patch(kFilterTypeParam, 0);
        patch(kFilterSlopeParam, p ? 0 : 1);
        patch(kCutoffParam, cutoffNorm(p ? 1350 : 1800));
        patch(kResonanceParam, .13);
        patch(kKeyFollowParam, .72);
        patch(kFilterEnvDepthParam, .58);
        patch(kFilterAttackParam, timeNorm(p ? 2.8 : 1.8));
        patch(kFilterDecayParam, timeNorm(3));
        patch(kFilterSustainParam, .68);
        patch(kFilterReleaseParam, timeNorm(3.5));
        patch(kLevelParam, p ? .5 : .58);
        patch(kLfo1AmpParam, .025);
        patch(kAmpAttackParam, timeNorm(p ? 1.7 : 1.2));
        patch(kAmpDecayParam, timeNorm(3.5));
        patch(kAmpSustainParam, .86);
        patch(kAmpReleaseParam, timeNorm(3.8));
        patch(kBassParam, .51);
        patch(kTrebleParam, .49);
        patch(kDriveParam, 0);
        patch(kChorusTypeParam, double(p ? 1 : 2) / 12);
        patch(kChorusRateParam, normFor(.6, chorusRateFromNorm));
        patch(kChorusDepthParam, .6);
        patch(kChorusLevelParam, .48);
        patch(kDelayTypeParam, double(p ? 2 : 1) / 5);
        patch(kDelaySyncParam, 3. / 6);
        patch(kDelayTimeParam, normFor(360, delayMsFromNorm));
        patch(kDelayFeedbackParam, .22);
        patch(kDelayLevelParam, .16);
        if(program == 1)
        {
            patch(kOsc1WaveParam, 4. / 6);
            patch(kOsc1Ctrl1Param, p ? .6 : .25);
            patch(kOsc1Ctrl2Param, .3);
            patch(kBalanceParam, .12);
            patch(kCutoffParam, cutoffNorm(1000));
            patch(kFilterEnvDepthParam, .55);
            patch(kAmpAttackParam, timeNorm(1.6));
            patch(kAmpReleaseParam, timeNorm(4.2));
            patch(kChorusTypeParam, 4. / 12);
        }
        else if(program == 2)
        {
            patch(kOsc1WaveParam, 0);
            patch(kOsc1Ctrl1Param, p ? .5 : .64);
            patch(kOsc1Ctrl2Param, .8);
            patch(kOsc2RangeParam, p ? .75 : .5);
            patch(kBalanceParam, .1);
            patch(kCutoffParam, cutoffNorm(p ? 3400 : 2600));
            patch(kFilterSlopeParam, 0);
            patch(kAmpAttackParam, timeNorm(.8));
            patch(kChorusTypeParam, 3. / 12);
            patch(kTrebleParam, .55);
        }
        else if(program == 3)
        {
            patch(kOsc1WaveParam, p ? 6. / 6 : 1. / 6);
            patch(kOsc1Ctrl1Param, p ? .2 : .14);
            patch(kOsc1Ctrl2Param, .04);
            patch(kOsc2WaveParam, 1);
            patch(kBalanceParam, .32);
            patch(kOsc2RangeParam, p ? .75 : .5);
            patch(kCutoffParam, cutoffNorm(3400));
            patch(kResonanceParam, .25);
            patch(kAmpAttackParam, timeNorm(1.3));
            patch(kLfo1FilterParam, .55);
            patch(kChorusTypeParam, 8. / 12);
            patch(kChorusLevelParam, .3);
        }
        else if(program == 4)
        {
            patch(kOsc1WaveParam, 5. / 6);
            patch(kBalanceParam, .22);
            patch(kOsc2RangeParam, p ? 31. / 48 : .5);
            patch(kCutoffParam, cutoffNorm(1250));
            patch(kChorusTypeParam, 1. / 12);
            patch(kAmpAttackParam, timeNorm(1.9));
            patch(kAmpReleaseParam, timeNorm(4.4));
        }
        else if(program == 5 && p == 1)
        {
            patch(kOsc1WaveParam, 5. / 6);
            patch(kOsc2WaveParam, 1);
            patch(kBalanceParam, .3);
            patch(kCutoffParam, cutoffNorm(480));
            patch(kFilterEnvDepthParam, .57);
            patch(kAmpAttackParam, timeNorm(.15));
            patch(kAmpReleaseParam, timeNorm(1.4));
            patch(kChorusTypeParam, 0);
            patch(kDelayTypeParam, 0);
        }
    }
    return values;
}
} // namespace mla_jp8000

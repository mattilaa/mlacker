#pragma once
#include "../../mla_8000/src/engine.h"
namespace mla_jp8000
{
using namespace Steinberg;
using namespace Steinberg::Vst;
constexpr int kPartParams = mla_8000::kSlots;
constexpr ParamID kLowerOffset = 1000;
enum Common : ParamID
{
    kMaster = 2000,
    kMode,
    kVoiceAssign,
    kSplit,
    kPartDetune,
    kUpperTranspose,
    kLowerTranspose,
    kUpperPan,
    kLowerPan,
    kBend,
    kWheel,
    kSustain,
    kBrightness,
    kUpperChannel,
    kLowerChannel,
    kCommonEnd
};
constexpr int kCommonCount = kCommonEnd - kMaster;
constexpr int kParamCount = kCommonCount + 2 * kPartParams;
enum Mode
{
    kSingleUpper,
    kSingleLower,
    kDual,
    kSplitMode
};
inline ParamID partId(int part, ParamID id)
{
    return id + (part ? kLowerOffset : 0);
}
inline int paramIndex(ParamID id)
{
    if(id >= kMaster && id < kCommonEnd)
        return int(id - kMaster);
    for(int part = 0; part < 2; ++part)
        if(id >= partId(part, mla_8000::kFirstSlot) &&
           id <= partId(part, mla_8000::kLastParam))
            return kCommonCount + part * kPartParams +
                   int(id - partId(part, mla_8000::kFirstSlot));
    return -1;
}
inline ParamID paramId(int index)
{
    if(index < kCommonCount)
        return kMaster + index;
    index -= kCommonCount;
    return partId(index / kPartParams,
                  mla_8000::kFirstSlot + index % kPartParams);
}
inline double timeNorm(double seconds)
{
    return seconds <= 0 ? 0 : std::log10(seconds / .001) / 4;
}
inline double cutoffNorm(double hz)
{
    return std::log(hz / 20) / std::log(1000.);
}
} // namespace mla_jp8000

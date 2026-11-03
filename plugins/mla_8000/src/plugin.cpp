#include "engine.h"

BEGIN_FACTORY_DEF("MLang", "https://github.com/mattilaa/mlang", "mailto:devnull@example.invalid")
DEF_CLASS2(INLINE_UID_FROM_FUID(mla_8000::kProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "Mla 8000",
           0, PlugType::kInstrumentSynth, "0.1.0", kVstVersionString, mla_8000::Processor::createInstance)
END_FACTORY

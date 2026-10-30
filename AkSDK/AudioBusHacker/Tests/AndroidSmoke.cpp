// Standalone Android static-link and callback test. No SoundBank or UE required.
#include <AK/Tools/Common/AkAssert.h>
#include "AudioBusHackerFX.h"
#include <cmath>
#include <cstdio>

DEFINE_PLUGIN_REGISTER_HOOK;
DEFINE_PLUGIN_ASSERT_HOOK;
AK_STATIC_LINK_PLUGIN(AudioBusHackerFX);

namespace
{
int Reports = 0;
bool Valid = true;
AkAudioBusHackerVisualizationData Latest{};
void OnVisualization(const AkAudioBusHackerVisualizationData* Data)
{
    ++Reports;
    Valid = Valid && Data && Data->uVersion == AK_AUDIO_BUS_HACKER_VISUALIZATION_VERSION
        && Data->uStructSize == sizeof(*Data) && Data->uNumChannels == 1
        && Data->uSpectrumBins == AK_AUDIO_BUS_HACKER_SPECTRUM_BINS;
    if (Data) Latest = *Data;
}
}

int main()
{
    if (!g_pAKPluginList || SetAudioBusHackerVisualizationCallback(&OnVisualization) != 8)
        return 1;
    AudioBusHackerFX Effect;
    AudioBusHackerFXParams Params;
    AkAudioFormat Format{};
    Format.uSampleRate = 48000;
    Format.channelConfig.SetStandard(AK_SPEAKER_SETUP_MONO);
    if (Effect.Init(nullptr, nullptr, &Params, Format) != AK_Success)
        return 2;
    constexpr AkUInt16 Frames = 512;
    AkSampleType Samples[Frames]{};
    AkAudioBuffer Buffer;
    Buffer.AttachContiguousDeinterleavedData(Samples, Frames, Frames, Format.channelConfig);
    for (int Block = 0; Block < 8; ++Block) Effect.Execute(&Buffer);
    if (!Valid || Reports == 0 || Latest.fChannelPeakDb[0] > -119.0f)
        return 3;
    for (int Block = 0; Block < 8; ++Block)
    {
        for (int Frame = 0; Frame < Frames; ++Frame)
            Samples[Frame] = 0.5f * std::sin(6.28318530718f * 1000.0f * (Block * Frames + Frame) / 48000.0f);
        Effect.Execute(&Buffer);
    }
    if (!Valid || Latest.fChannelPeakDb[0] < -6.2f || Latest.fChannelPeakDb[0] > -5.9f)
        return 4;
    float SpectrumPeak = -120.0f;
    for (float Db : Latest.fSpectrumDb) if (Db > SpectrumPeak) SpectrumPeak = Db;
    if (SpectrumPeak < -20.0f || SpectrumPeak > 0.0f) return 5;
    const int BeforeUnregister = Reports;
    SetAudioBusHackerVisualizationCallback(nullptr);
    for (int Block = 0; Block < 8; ++Block) Effect.Execute(&Buffer);
    if (Reports != BeforeUnregister) return 6;
    std::puts("PASS: static registration, silence, tone spectrum and callback unregister");
    return 0;
}

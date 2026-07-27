/*******************************************************************************
The content of this file includes portions of the AUDIOKINETIC Wwise Technology
released in source code form as part of the SDK installer package.

Commercial License Usage

Licensees holding valid commercial licenses to the AUDIOKINETIC Wwise Technology
may use this file in accordance with the end user license agreement provided
with the software or, alternatively, in accordance with the terms contained in a
written agreement between you and Audiokinetic Inc.

Apache License Usage

Alternatively, this file may be used under the Apache License, Version 2.0 (the
"Apache License"); you may not use this file except in compliance with the
Apache License. You may obtain a copy of the Apache License at
http://www.apache.org/licenses/LICENSE-2.0.

Unless required by applicable law or agreed to in writing, software distributed
under the Apache License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES
OR CONDITIONS OF ANY KIND, either express or implied. See the Apache License for
the specific language governing permissions and limitations under the License.

  Copyright (c) 2023 Audiokinetic Inc.
*******************************************************************************/
#include "AudioBusHackerFX.h"
#include "../AudioBusHackerConfig.h"
#include <AK/AkWwiseSDKVersion.h>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr AkReal32 kMinimumDb = -120.0f;
}

AK::IAkPlugin* CreateAudioBusHackerFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, AudioBusHackerFX());
}

AK::IAkPluginParam* CreateAudioBusHackerFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, AudioBusHackerFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(AudioBusHackerFX, AkPluginTypeEffect, AudioBusHackerConfig::CompanyID, AudioBusHackerConfig::PluginID)

std::atomic<AkAudioBusHackerPluginExecuteCallbackFunc> AudioBusHackerFX::m_ABHExecCallback{ nullptr };
std::atomic<AkAudioBusHackerVisualizationCallbackFunc> AudioBusHackerFX::m_visualizationCallback{ nullptr };

AudioBusHackerFX::AudioBusHackerFX()
    : m_pParams(nullptr)
    , m_pAllocator(nullptr)
    , m_pContext(nullptr)
    , m_uSampleRate(0)
    , m_uChannelConfig(0)
    , m_uReportIntervalFrames(1)
    , m_uFramesSinceReport(0)
    , m_uSequence(0)
    , m_uSpectrumWritePosition(0)
    , m_uSpectrumSamples(0)
    , m_bAnalysisActive(false)
    , m_fSpectrumMinHz(20.0f)
    , m_fSpectrumMaxHz(20000.0f)
    , m_fSpectrumWindowSum(0.0f)
    , m_fStereoCross(0.0)
    , m_fStereoLeftSquares(0.0)
    , m_fStereoRightSquares(0.0)
{
    std::memset(m_fSpectrumHistory, 0, sizeof(m_fSpectrumHistory));
    std::memset(m_fSpectrumWindow, 0, sizeof(m_fSpectrumWindow));
    std::memset(m_fSpectrumCoefficient, 0, sizeof(m_fSpectrumCoefficient));
    ResetIntervalAnalysis();
}

AudioBusHackerFX::~AudioBusHackerFX()
{
}

AKRESULT AudioBusHackerFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
    m_pParams = (AudioBusHackerFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;
    m_uSampleRate = in_rFormat.uSampleRate;
    if (m_uSampleRate == 0)
        m_uSampleRate = 48000;
    m_uChannelConfig = in_rFormat.channelConfig.Serialize();
    m_uReportIntervalFrames = m_uSampleRate / kVisualizationRateHz;
    if (m_uReportIntervalFrames == 0)
        m_uReportIntervalFrames = 1;

    InitializeSpectrum();
    Reset();

    return AK_Success;
}

AKRESULT AudioBusHackerFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT AudioBusHackerFX::Reset()
{
    std::memset(m_fSpectrumHistory, 0, sizeof(m_fSpectrumHistory));
    m_uSpectrumWritePosition = 0;
    m_uSpectrumSamples = 0;
    m_uFramesSinceReport = 0;
    ResetIntervalAnalysis();
    return AK_Success;
}

AKRESULT AudioBusHackerFX::GetPluginInfo(AkPluginInfo& out_rPluginInfo)
{
    out_rPluginInfo.eType = AkPluginTypeEffect;
    out_rPluginInfo.bIsInPlace = true;
	out_rPluginInfo.bCanProcessObjects = false;
    out_rPluginInfo.uBuildVersion = AK_WWISESDK_VERSION_COMBINED;
    return AK_Success;
}

void AudioBusHackerFX::Execute(AkAudioBuffer* io_pBuffer)
{
    const auto rawCallback = m_ABHExecCallback.load(std::memory_order_acquire);
    if (rawCallback)
        rawCallback(io_pBuffer);

    bool shouldAnalyze = m_visualizationCallback.load(std::memory_order_acquire) != nullptr;
#ifndef AK_OPTIMIZED
    if (!shouldAnalyze && m_pContext)
        shouldAnalyze = m_pContext->CanPostMonitorData();
#endif

    if (shouldAnalyze)
    {
        if (!m_bAnalysisActive)
        {
            Reset();
            m_bAnalysisActive = true;
        }
        AnalyzeBuffer(io_pBuffer);
    }
    else
    {
        m_bAnalysisActive = false;
    }
}

void AudioBusHackerFX::AnalyzeBuffer(AkAudioBuffer* in_pBuffer)
{
    if (!in_pBuffer || !in_pBuffer->HasData() || in_pBuffer->uValidFrames == 0)
        return;

    const AkUInt32 numChannels = in_pBuffer->NumChannels();
    const AkUInt32 analyzedChannels = numChannels < AK_AUDIO_BUS_HACKER_MAX_CHANNELS
        ? numChannels
        : AK_AUDIO_BUS_HACKER_MAX_CHANNELS;
    if (analyzedChannels == 0)
        return;

    AkSampleType* channels[AK_AUDIO_BUS_HACKER_MAX_CHANNELS]{};
    for (AkUInt32 channel = 0; channel < analyzedChannels; ++channel)
        channels[channel] = in_pBuffer->GetChannel(channel);

    for (AkUInt32 frame = 0; frame < in_pBuffer->uValidFrames; ++frame)
    {
        double mono = 0.0;
        for (AkUInt32 channel = 0; channel < analyzedChannels; ++channel)
        {
            AkReal32 sample = channels[channel][frame];
            if (!std::isfinite(sample))
                sample = 0.0f;

            const AkReal32 absoluteSample = std::fabs(sample);
            if (absoluteSample > m_fChannelPeak[channel])
                m_fChannelPeak[channel] = absoluteSample;
            m_fChannelSumSquares[channel] += static_cast<double>(sample) * sample;
            mono += sample;
        }
        mono /= analyzedChannels;

        const AkReal32 monoSample = static_cast<AkReal32>(mono);
        m_fSpectrumHistory[m_uSpectrumWritePosition] = monoSample;
        m_uSpectrumWritePosition = (m_uSpectrumWritePosition + 1) % kSpectrumWindowFrames;
        if (m_uSpectrumSamples < kSpectrumWindowFrames)
            ++m_uSpectrumSamples;

        AkUInt32 waveformBin = static_cast<AkUInt32>(
            (static_cast<AkUInt64>(m_uFramesSinceReport + frame) * AK_AUDIO_BUS_HACKER_WAVEFORM_BINS)
            / m_uReportIntervalFrames);
        if (waveformBin >= AK_AUDIO_BUS_HACKER_WAVEFORM_BINS)
            waveformBin = AK_AUDIO_BUS_HACKER_WAVEFORM_BINS - 1;
        if (monoSample < m_fWaveformMin[waveformBin])
            m_fWaveformMin[waveformBin] = monoSample;
        if (monoSample > m_fWaveformMax[waveformBin])
            m_fWaveformMax[waveformBin] = monoSample;

        if (analyzedChannels >= 2)
        {
            const double left = std::isfinite(channels[0][frame]) ? channels[0][frame] : 0.0;
            const double right = std::isfinite(channels[1][frame]) ? channels[1][frame] : 0.0;
            m_fStereoCross += left * right;
            m_fStereoLeftSquares += left * left;
            m_fStereoRightSquares += right * right;
        }
    }

    m_uFramesSinceReport += in_pBuffer->uValidFrames;
    if (m_uFramesSinceReport >= m_uReportIntervalFrames)
    {
        PublishVisualization(numChannels, analyzedChannels);
        m_uFramesSinceReport = 0;
        ResetIntervalAnalysis();
    }
}

void AudioBusHackerFX::PublishVisualization(AkUInt32 in_uNumChannels, AkUInt32 in_uAnalyzedChannels)
{
    AkAudioBusHackerVisualizationData data{};
    data.uVersion = AK_AUDIO_BUS_HACKER_VISUALIZATION_VERSION;
    data.uStructSize = sizeof(data);
    data.uSequence = ++m_uSequence;
    data.uBusID = m_pContext ? m_pContext->GetAudioNodeID() : AK_INVALID_UNIQUE_ID;
    data.uSampleRate = m_uSampleRate;
    data.uChannelConfig = m_uChannelConfig;
    data.uNumChannels = in_uNumChannels;
    data.uAnalyzedChannels = in_uAnalyzedChannels;
    data.uFrames = m_uFramesSinceReport;
    data.uWaveformBins = AK_AUDIO_BUS_HACKER_WAVEFORM_BINS;
    data.uSpectrumBins = AK_AUDIO_BUS_HACKER_SPECTRUM_BINS;
    data.fSpectrumMinHz = m_fSpectrumMinHz;
    data.fSpectrumMaxHz = m_fSpectrumMaxHz;
    data.fDownstreamGain = m_pContext ? m_pContext->GetDownstreamGain() : 1.0f;

    for (AkUInt32 channel = 0; channel < AK_AUDIO_BUS_HACKER_MAX_CHANNELS; ++channel)
    {
        data.fChannelPeakDb[channel] = kMinimumDb;
        data.fChannelRmsDb[channel] = kMinimumDb;
    }
    for (AkUInt32 channel = 0; channel < in_uAnalyzedChannels; ++channel)
    {
        data.fChannelPeakDb[channel] = LinearToDb(m_fChannelPeak[channel]);
        const AkReal32 rms = static_cast<AkReal32>(
            std::sqrt(m_fChannelSumSquares[channel] / m_uFramesSinceReport));
        data.fChannelRmsDb[channel] = LinearToDb(rms);
    }

    for (AkUInt32 bin = 0; bin < AK_AUDIO_BUS_HACKER_WAVEFORM_BINS; ++bin)
    {
        data.fWaveformMin[bin] = m_fWaveformMin[bin] == FLT_MAX ? 0.0f : m_fWaveformMin[bin];
        data.fWaveformMax[bin] = m_fWaveformMax[bin] == -FLT_MAX ? 0.0f : m_fWaveformMax[bin];
    }

    if (in_uAnalyzedChannels >= 2 && m_fStereoLeftSquares > 0.0 && m_fStereoRightSquares > 0.0)
    {
        data.fStereoCorrelation = static_cast<AkReal32>(
            m_fStereoCross / std::sqrt(m_fStereoLeftSquares * m_fStereoRightSquares));
        if (data.fStereoCorrelation < -1.0f)
            data.fStereoCorrelation = -1.0f;
        else if (data.fStereoCorrelation > 1.0f)
            data.fStereoCorrelation = 1.0f;
    }
    else
    {
        data.fStereoCorrelation = in_uAnalyzedChannels == 1 ? 1.0f : 0.0f;
    }

    for (AkUInt32 bin = 0; bin < AK_AUDIO_BUS_HACKER_SPECTRUM_BINS; ++bin)
    {
        if (m_uSpectrumSamples < kSpectrumWindowFrames)
        {
            data.fSpectrumDb[bin] = kMinimumDb;
            continue;
        }

        // Evaluate each log-spaced Goertzel filter over the latest Hann-windowed history.
        const double coefficient = m_fSpectrumCoefficient[bin];
        double previous = 0.0;
        double previousPrevious = 0.0;
        for (AkUInt32 frame = 0; frame < kSpectrumWindowFrames; ++frame)
        {
            const AkUInt32 historyIndex = (m_uSpectrumWritePosition + frame) % kSpectrumWindowFrames;
            const double sample = m_fSpectrumHistory[historyIndex] * m_fSpectrumWindow[frame];
            const double current = sample + coefficient * previous - previousPrevious;
            previousPrevious = previous;
            previous = current;
        }

        double power = previousPrevious * previousPrevious + previous * previous
            - coefficient * previous * previousPrevious;
        if (power < 0.0)
            power = 0.0;
        const AkReal32 magnitude = m_fSpectrumWindowSum > 0.0f
            ? static_cast<AkReal32>((2.0 * std::sqrt(power)) / m_fSpectrumWindowSum)
            : 0.0f;
        data.fSpectrumDb[bin] = LinearToDb(magnitude);
    }

    const auto visualizationCallback = m_visualizationCallback.load(std::memory_order_acquire);
    if (visualizationCallback)
        visualizationCallback(&data);

#ifndef AK_OPTIMIZED
    if (m_pContext && m_pContext->CanPostMonitorData())
        m_pContext->PostMonitorData(&data, sizeof(data));
#endif
}

void AudioBusHackerFX::ResetIntervalAnalysis()
{
    std::memset(m_fChannelPeak, 0, sizeof(m_fChannelPeak));
    std::memset(m_fChannelSumSquares, 0, sizeof(m_fChannelSumSquares));
    for (AkUInt32 bin = 0; bin < AK_AUDIO_BUS_HACKER_WAVEFORM_BINS; ++bin)
    {
        m_fWaveformMin[bin] = FLT_MAX;
        m_fWaveformMax[bin] = -FLT_MAX;
    }
    m_fStereoCross = 0.0;
    m_fStereoLeftSquares = 0.0;
    m_fStereoRightSquares = 0.0;
}

void AudioBusHackerFX::InitializeSpectrum()
{
    m_fSpectrumWindowSum = 0.0f;
    for (AkUInt32 frame = 0; frame < kSpectrumWindowFrames; ++frame)
    {
        m_fSpectrumWindow[frame] = static_cast<AkReal32>(
            0.5 - 0.5 * std::cos((2.0 * kPi * frame) / (kSpectrumWindowFrames - 1)));
        m_fSpectrumWindowSum += m_fSpectrumWindow[frame];
    }

    m_fSpectrumMaxHz = static_cast<AkReal32>(m_uSampleRate * 0.5);
    if (m_fSpectrumMaxHz > 20000.0f)
        m_fSpectrumMaxHz = 20000.0f;
    m_fSpectrumMinHz = m_fSpectrumMaxHz > 20.0f ? 20.0f : m_fSpectrumMaxHz * 0.5f;

    const double frequencyRatio = m_fSpectrumMinHz > 0.0f
        ? m_fSpectrumMaxHz / m_fSpectrumMinHz
        : 1.0;
    for (AkUInt32 bin = 0; bin < AK_AUDIO_BUS_HACKER_SPECTRUM_BINS; ++bin)
    {
        const double normalizedBin = static_cast<double>(bin)
            / (AK_AUDIO_BUS_HACKER_SPECTRUM_BINS - 1);
        const double frequency = m_fSpectrumMinHz * std::pow(frequencyRatio, normalizedBin);
        m_fSpectrumCoefficient[bin] = static_cast<AkReal32>(
            2.0 * std::cos((2.0 * kPi * frequency) / m_uSampleRate));
    }
}

AkReal32 AudioBusHackerFX::LinearToDb(AkReal32 in_fLinear)
{
    if (in_fLinear <= 0.000001f)
        return kMinimumDb;
    const AkReal32 value = 20.0f * static_cast<AkReal32>(std::log10(in_fLinear));
    return value < kMinimumDb ? kMinimumDb : value;
}

int SetAudioBusHackerCallbacks(
    AkAudioBusHackerPluginExecuteCallbackFunc in_ABHExecCallback
)
{
    AudioBusHackerFX::m_ABHExecCallback.store(in_ABHExecCallback, std::memory_order_release);
    return 8;
}

int SetAudioBusHackerVisualizationCallback(
    AkAudioBusHackerVisualizationCallbackFunc in_visualizationCallback
)
{
    AudioBusHackerFX::m_visualizationCallback.store(in_visualizationCallback, std::memory_order_release);
    return 8;
}


AKRESULT AudioBusHackerFX::TimeSkip(AkUInt32 in_uFrames)
{
    return AK_DataReady;
}

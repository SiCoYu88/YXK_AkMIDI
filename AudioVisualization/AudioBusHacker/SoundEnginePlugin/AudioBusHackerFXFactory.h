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

#ifndef AudioBusHackerFXFactory_H
#define AudioBusHackerFXFactory_H

#ifndef AUDIO_BUS_HACKER_NO_STATIC_LINK
AK_STATIC_LINK_PLUGIN(AudioBusHackerFX);
#endif

#ifndef AudioBusHackerCallbackAPI_H
#define AudioBusHackerCallbackAPI_H

enum AkAudioBusHackerVisualizationConstants : AkUInt32
{
    AK_AUDIO_BUS_HACKER_VISUALIZATION_VERSION = 1,
    AK_AUDIO_BUS_HACKER_MAX_CHANNELS = 16,
    AK_AUDIO_BUS_HACKER_WAVEFORM_BINS = 128,
    AK_AUDIO_BUS_HACKER_SPECTRUM_BINS = 64
};

struct AkAudioBusHackerVisualizationData
{
    AkUInt32 uVersion;
    AkUInt32 uStructSize;
    AkUInt32 uSequence;
    AkUniqueID uBusID;
    AkUInt32 uSampleRate;
    AkUInt32 uChannelConfig;
    AkUInt32 uNumChannels;
    AkUInt32 uAnalyzedChannels;
    AkUInt32 uFrames;
    AkUInt32 uWaveformBins;
    AkUInt32 uSpectrumBins;
    AkReal32 fSpectrumMinHz;
    AkReal32 fSpectrumMaxHz;
    AkReal32 fStereoCorrelation;
    AkReal32 fDownstreamGain;
    AkReal32 fChannelPeakDb[AK_AUDIO_BUS_HACKER_MAX_CHANNELS];
    AkReal32 fChannelRmsDb[AK_AUDIO_BUS_HACKER_MAX_CHANNELS];
    AkReal32 fWaveformMin[AK_AUDIO_BUS_HACKER_WAVEFORM_BINS];
    AkReal32 fWaveformMax[AK_AUDIO_BUS_HACKER_WAVEFORM_BINS];
    AkReal32 fSpectrumDb[AK_AUDIO_BUS_HACKER_SPECTRUM_BINS];
};

AK_CALLBACK(void, AkAudioBusHackerPluginExecuteCallbackFunc)(
    AkAudioBuffer* io_pBufferOut
    );

AK_CALLBACK(void, AkAudioBusHackerVisualizationCallbackFunc)(
    const AkAudioBusHackerVisualizationData* in_pData
    );

extern "C" AK_DLLEXPORT int SetAudioBusHackerCallbacks(
    AkAudioBusHackerPluginExecuteCallbackFunc in_ABHExecCallback
    );

extern "C" AK_DLLEXPORT int SetAudioBusHackerVisualizationCallback(
    AkAudioBusHackerVisualizationCallbackFunc in_visualizationCallback
    );

#endif // AudioBusHackerCallbackAPI_H

#endif // AudioBusHackerFXFactory_H

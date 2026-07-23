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
AK::IAkPlugin* CreateAudioBusHackerFX(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, AudioBusHackerFX());
}

AK::IAkPluginParam* CreateAudioBusHackerFXParams(AK::IAkPluginMemAlloc* in_pAllocator)
{
    return AK_PLUGIN_NEW(in_pAllocator, AudioBusHackerFXParams());
}

AK_IMPLEMENT_PLUGIN_FACTORY(AudioBusHackerFX, AkPluginTypeEffect, AudioBusHackerConfig::CompanyID, AudioBusHackerConfig::PluginID)

//AkAudioBuffer* AudioBusHackerFX::m_pbuffer = nullptr;

AkAudioBusHackerPluginExecuteCallbackFunc AudioBusHackerFX::m_ABHExecCallback = NULL;

AudioBusHackerFX::AudioBusHackerFX()
    : m_pParams(nullptr)
    , m_pAllocator(nullptr)
    , m_pContext(nullptr)
{
}

AudioBusHackerFX::~AudioBusHackerFX()
{
}

AKRESULT AudioBusHackerFX::Init(AK::IAkPluginMemAlloc* in_pAllocator, AK::IAkEffectPluginContext* in_pContext, AK::IAkPluginParam* in_pParams, AkAudioFormat& in_rFormat)
{
   

    m_pParams = (AudioBusHackerFXParams*)in_pParams;
    m_pAllocator = in_pAllocator;
    m_pContext = in_pContext;

    return AK_Success;
}

AKRESULT AudioBusHackerFX::Term(AK::IAkPluginMemAlloc* in_pAllocator)
{
    AK_PLUGIN_DELETE(in_pAllocator, this);
    return AK_Success;
}

AKRESULT AudioBusHackerFX::Reset()
{
    return AK_Success;
}
//static auto test = "Default";
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
    //m_pbuffer = io_pBuffer;
    //AKRESULT mes = m_pContext->PostMonitorMessage(test, AK::Monitor::ErrorLevel_Message);
    if (m_ABHExecCallback) {
        m_ABHExecCallback(io_pBuffer);
    }
    else {
    }
   // io_pBuffer->eState = AK_DataReady;
}

//void AudioBusHackerFX::SetAudioBusHackerCallbacksFX(
//    AkAudioBusHackerPluginExecuteCallbackFunc in_ABHExecCallback
//)
//{
//    if (in_ABHExecCallback)
//    {
//        m_ABHExecCallback = in_ABHExecCallback;
//    }
//}

int SetAudioBusHackerCallbacks(
    AkAudioBusHackerPluginExecuteCallbackFunc in_ABHExecCallback
)
{

    //AudioBusHackerFX::SetAudioBusHackerCallbacksFX(in_ABHExecCallback);
    AudioBusHackerFX::m_ABHExecCallback = in_ABHExecCallback;
    //test = "Finish";
    return 8;
}


AKRESULT AudioBusHackerFX::TimeSkip(AkUInt32 in_uFrames)
{
    return AK_DataReady;
}

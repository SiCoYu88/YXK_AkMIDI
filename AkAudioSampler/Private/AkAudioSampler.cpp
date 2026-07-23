// Fill out your copyright notice in the Description page of Project Settings.


#include "AkAudioSampler.h"
#include "AkAudioDevice.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#define DJ_FFT_IMPLEMENTATION 
#include "dj_fft.h"
//#include "Ak/Plugin/AudioBusHackerFXFactory.h"
#include "Ak/Plugin/AkAudioBusHackerPlugin.h"
#include "Interfaces/IPluginManager.h"

//AK_DLLIMPORT void SetAudioBusHackerCallbacks(AkAudioBusHackerPluginExecuteCallbackFunc m_ABHExecCallback);

UAkAudioSampler::UAkAudioSampler(const class FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    // Property initialization
}

static UAkAudioSampler::Samplerdata* pdata = new UAkAudioSampler::Samplerdata();

void UAkAudioSampler::SaveBufferAndChannels(AkAudioBuffer* buffer, unsigned Numchannels, unsigned ucount) {
    int N = buffer->uValidFrames;
    if (N > 0) {
        pdata->bufferlist.Empty();
        pdata->count = 0;
        AkUInt16 uFramesProcessed = 0;
        //Ĭ���õ�Buffer��ǰ����ͨ��
        AkReal32* AK_RESTRICT pBuf1 = buffer->GetChannel(0);
        AkReal32* AK_RESTRICT pBuf2 = buffer->GetChannel(1);
        // input size
        auto myData1 = std::vector<std::complex<float>>(N);
        auto myData2 = std::vector<std::complex<float>>(N);
        for (int i = 0; i < N / 2; ++i) {
            myData1[i] = (float)pBuf1[2 * i];
        }
        for (int i = 0; i < N / 2; ++i) {
            myData1[i + N / 2] = (float)pBuf2[2 * i];
        }
        auto fftData1 = dj::fft1d(myData1, dj::fft_dir::DIR_FWD);

        for (int i = 0; i < N / 2; ++i) {
            myData2[i] = (float)pBuf1[2 * i + 1];
        }
        for (int i = 0; i < N / 2; ++i) {
            myData2[i + N / 2] = (float)pBuf2[2 * i + 1];
        }
        auto fftData2 = dj::fft1d(myData2, dj::fft_dir::DIR_FWD);
        //��ͨ����ƽ��
        for (int i = 0; i < N; ++i) {
            pdata->bufferlist.Add((abs(fftData2[i]) + abs(fftData1[i]))/ 2);/*
            bufferlist.Add(myData2[i].real());*/
            //
        }
        /*for (int i = 0; i < N ; ++i) {
            pdata->bufferlist.Add((float)pBuf1[i]);
        }*/
        //pdata->bufferlist[0] /= 2;
        pdata->count = ucount;
    }
}

void SetDeviceInfo(AkOutputDeviceID defaultOutputDeviceId, AkChannelConfig& Config, Ak3DAudioSinkCapabilities& SinkCapabilities){
    AK::SoundEngine::GetOutputDeviceConfiguration(defaultOutputDeviceId, Config, SinkCapabilities);
}

//void CaptureCallback(AkAudioBuffer& in_CaptureBuffer, AkOutputDeviceID in_idOutput, void* in_pCookie)
//{
//    const unsigned uSampleCount = static_cast<unsigned>(in_CaptureBuffer.uValidFrames) * in_CaptureBuffer.NumChannels();
//
//    if (!uSampleCount)
//        return;
//
//    UAkAudioSampler::SaveBufferAndChannels(in_CaptureBuffer, in_CaptureBuffer.NumChannels(),uSampleCount);
//}

void UAkAudioSampler::HackerCallback(
    AkAudioBuffer* io_pBufferOut
)
{
    const unsigned uSampleCount = static_cast<unsigned>(io_pBufferOut->uValidFrames) * io_pBufferOut->NumChannels();

    if (!uSampleCount)
        return;

    UAkAudioSampler::SaveBufferAndChannels(io_pBufferOut, io_pBufferOut->NumChannels(), uSampleCount);
}

//void CaptureCallbackVolume(AkSpeakerVolumeMatrixCallbackInfo* in_pCallbackInfo){
//    in_pCallbackInfo.
//
//}
int32 UAkAudioSampler::RegisterCatchBuffer()
{
   //int32 test = (int32) SetAudioBusHackerCallbacks(HackerCallback);


    //GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("Your debug message here"));
    #if PLATFORM_WINDOWS
    if (FAkAudioSamplerModule::SetCallBackFunc) {
        
        return FAkAudioSamplerModule::SetCallBackFunc(HackerCallback);
    }
    else {
        return 1;
    }
    #elif PLATFORM_ANDROID
        SetAudioBusHackerCallbacks(HackerCallback);
        return 3;
    #endif
    //FString platform = TEXT("x64_vc160/Profile/bin/");
    //FString path = IPluginManager::Get().FindPlugin("Wwise")->GetBaseDir();
    //FString dllpath = path + "/ThirdParty/" + platform + "AudioBusHacker.dll";
    //auto PdfDllHandle = FPlatformProcess::GetDllHandle(*dllpath);
    //if (PdfDllHandle) {
    //    FString procName = "SetAudioBusHackerCallbacks"; // The exact name of the DLL function.
    //    SABH SetCallBackFunc = (SABH)FPlatformProcess::GetDllExport(PdfDllHandle, *procName);
    //    if (SetCallBackFunc) {

    //        SetCallBackFunc(HackerCallback);
    //        return 9;
    //    }
    //    return 4;
    //}
    return 5;//test;
}
void UAkAudioSampler::StopEventByID(int32 ID)
{
    auto* SoundEngine = IWwiseSoundEngineAPI::Get();
    SoundEngine->StopPlayingID(ID);
}
//int32 UAkAudioSampler::RegisterCatchBuffer()
//{
//    if (pdata == NULL) {
//        pdata = new Samplerdata();
//    }
//    //��ʼ��Soundengine
//    AkMemSettings mgrSettings;
//    AkStreamMgrSettings streamsettings;
//    AkDeviceSettings deviceSettings;
//    AkInitSettings          initSettings;
//    AkPlatformInitSettings  platformInitSettings;
//
//    AK::MemoryMgr::GetDefaultSettings(mgrSettings);
//    AK::MemoryMgr::Init(& mgrSettings);
//    AK::StreamMgr::GetDefaultSettings(streamsettings);
//    AK::StreamMgr::Create(streamsettings);
//    AK::StreamMgr::GetDefaultDeviceSettings(deviceSettings);
//    initSettings.uMaxNumPaths = 16;
//    AK::SoundEngine::GetDefaultInitSettings(initSettings);
//    AK::SoundEngine::GetDefaultPlatformInitSettings(platformInitSettings);
//    //FAkSoundEngineInitialization::Initialize();
//    auto* SoundEngine = IWwiseSoundEngineAPI::Get();
//    int32 Initcallback = SoundEngine->Init(&initSettings, &platformInitSettings);
//
//    pdata->m_defaultOutputDeviceId = SoundEngine->GetOutputID(AK_INVALID_UNIQUE_ID, 0);
//    SetDeviceInfo(pdata->m_defaultOutputDeviceId, pdata->channelConfig, pdata->audioSinkCapabilities);
//
//    SoundEngine->RenderAudio();
//    //ע�Ჶ��Buffer�ص�
//    AKRESULT RegistCB = SoundEngine->RegisterCaptureCallback(&CaptureCallback, pdata->m_defaultOutputDeviceId, pdata->cookie);
//    //AKRESULT RegistCBvolume = SoundEngine->RegisterBusVolumeCallback(0,, pdata->cookie);
//    return Initcallback;
//}


TArray<float> UAkAudioSampler::UpdateSampleSpecturmCallback(float deltaTime, int32& tick)
{
    //auto* SoundEngine = IWwiseSoundEngineAPI::Get();
    tick = pdata->count;//SoundEngine->GetBufferTick();
    if (pdata->count>0){
        return pdata->bufferlist;
    }
    return TArray<float>{0};
}

//int32 UAkAudioSampler::UnRegisterCatchBuffer()
//{
//    auto* SoundEngine = IWwiseSoundEngineAPI::Get();
//    AKRESULT UnRegistCB = SoundEngine->UnregisterCaptureCallback(&CaptureCallback, pdata->m_defaultOutputDeviceId, pdata->cookie);
//    delete pdata;
//    pdata = NULL;
//    return UnRegistCB;
//}

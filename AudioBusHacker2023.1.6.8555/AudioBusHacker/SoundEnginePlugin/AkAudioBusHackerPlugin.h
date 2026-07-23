#pragma once

#ifndef AudioBusHackerCallbackAPI_H
#define AudioBusHackerCallbackAPI_H

AK_CALLBACK(void, AkAudioBusHackerPluginExecuteCallbackFunc)(
    AkAudioBuffer* io_pBufferOut
    );

extern "C" AK_DLLEXPORT int SetAudioBusHackerCallbacks(
    AkAudioBusHackerPluginExecuteCallbackFunc in_ABHExecCallback
    );

#endif // AudioBusHackerCallbackAPI_H

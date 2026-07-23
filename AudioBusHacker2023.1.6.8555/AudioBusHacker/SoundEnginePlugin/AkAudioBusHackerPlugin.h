#pragma once

AK_CALLBACK(void, AkAudioBusHackerPluginExecuteCallbackFunc)(
	AkAudioBuffer*	io_pBufferOut
	);

extern "C" AK_DLLEXPORT int SetAudioBusHackerCallbacks(
	AkAudioBusHackerPluginExecuteCallbackFunc in_ABHExecCallback
	);


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

  Copyright (c) 2024 Audiokinetic Inc.
*******************************************************************************/

#include "AudioBusHackerPluginGUI.h"
#include <cstring>

AudioBusHackerPluginGUI::AudioBusHackerPluginGUI()
	: m_visualizationData{}
	, m_bHasVisualizationData(false)
	, m_bIsVisualizationRealtime(false)
{
}

void AudioBusHackerPluginGUI::NotifyMonitorData(
	AkTimeMs,
	const AK::Wwise::Plugin::MonitorData* in_pMonitorDataArray,
	unsigned int in_uMonitorDataArraySize,
	bool in_bIsRealtime)
{
	for (unsigned int index = 0; index < in_uMonitorDataArraySize; ++index)
	{
		const auto& monitorData = in_pMonitorDataArray[index];
		if (!monitorData.pData || monitorData.uDataSize < sizeof(AkUInt32) * 2)
			continue;

		const auto* visualizationData =
			static_cast<const AkAudioBusHackerVisualizationData*>(monitorData.pData);
		if (visualizationData->uVersion != AK_AUDIO_BUS_HACKER_VISUALIZATION_VERSION
			|| visualizationData->uStructSize < sizeof(AkUInt32) * 2)
		{
			continue;
		}

		unsigned int copySize = visualizationData->uStructSize;
		if (copySize > monitorData.uDataSize)
			copySize = monitorData.uDataSize;
		if (copySize > sizeof(m_visualizationData))
			copySize = sizeof(m_visualizationData);

		std::memset(&m_visualizationData, 0, sizeof(m_visualizationData));
		std::memcpy(&m_visualizationData, visualizationData, copySize);
		m_bHasVisualizationData = true;
		m_bIsVisualizationRealtime = in_bIsRealtime;
	}
}

ADD_AUDIOPLUGIN_CLASS_TO_CONTAINER(
    AudioBusHacker,            // Name of the plug-in container for this shared library
    AudioBusHackerPluginGUI,   // Authoring plug-in class to add to the plug-in container
    AudioBusHackerFX           // Corresponding Sound Engine plug-in class
);

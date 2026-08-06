#include "WwisePixelStreaming2Module.h"

#include "WwisePixelStreaming2Bridge.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWwisePixelStreaming2Module, Log, All);

class FWwisePixelStreaming2Module final : public IWwisePixelStreaming2Module
{
public:
	virtual void StartupModule() override
	{
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("WwisePixelStreaming2.Status"),
			TEXT("Log the current Wwise to Pixel Streaming 2 audio bridge status."),
			FConsoleCommandDelegate::CreateRaw(this, &FWwisePixelStreaming2Module::LogStatusCommand),
			ECVF_Default);
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("WwisePixelStreaming2.RebindCapture"),
			TEXT("Rebind the Wwise capture callback to the current output device."),
			FConsoleCommandDelegate::CreateRaw(this, &FWwisePixelStreaming2Module::RebindCaptureCommand),
			ECVF_Default);

		const FWwisePixelStreaming2Config Config = FWwisePixelStreaming2Config::Load();
		if (!Config.bEnabled)
		{
			BridgeInactiveReason = TEXT("disabled by [WwisePixelStreaming2] Enabled=false");
			UE_LOG(LogWwisePixelStreaming2Module, Log, TEXT("Plugin is disabled by configuration."));
			return;
		}

		Bridge = MakeUnique<FWwisePixelStreaming2Bridge>(Config);
		if (!Bridge->Start())
		{
			BridgeInactiveReason = TEXT("audio bridge worker thread failed to start");
			UE_LOG(LogWwisePixelStreaming2Module, Error, TEXT("Could not start audio bridge worker thread."));
			Bridge.Reset();
			return;
		}
		BridgeInactiveReason.Reset();

		UE_LOG(LogWwisePixelStreaming2Module, Display,
			TEXT("Plugin started. StreamerId='%s' OutputDeviceId=%llu QueueSlots=%d MaxFrames=%d MaxChannels=%d Gain=%.3f CaptureStallTimeout=%.2fs"),
			Config.StreamerId.IsEmpty() ? TEXT("<default>") : *Config.StreamerId,
			Config.OutputDeviceId,
			Config.QueueSlots,
			Config.MaxFrames,
			Config.MaxChannels,
			Config.Gain,
			Config.CaptureStallTimeoutSeconds);

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FWwisePixelStreaming2Module::Tick),
			0.5f);
	}

	virtual void ShutdownModule() override
	{
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("WwisePixelStreaming2.Status"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("WwisePixelStreaming2.RebindCapture"), false);
		if (TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}

		if (Bridge)
		{
			const FWwisePixelStreaming2Stats Stats = Bridge->GetStats();
			Bridge->StopBridge();
			UE_LOG(LogWwisePixelStreaming2Module, Log,
				TEXT("Stopped. Captured=%llu Pushed=%llu Dropped=%llu Rejected=%llu"),
				Stats.CapturedBuffers, Stats.PushedBuffers, Stats.DroppedBuffers, Stats.RejectedBuffers);
			Bridge.Reset();
		}
	}

	virtual FWwisePixelStreaming2Stats GetStats() const override
	{
		return Bridge ? Bridge->GetStats() : FWwisePixelStreaming2Stats{};
	}

	virtual void LogStatus() const override
	{
		if (Bridge)
		{
			Bridge->LogStatus();
			return;
		}

		UE_LOG(LogWwisePixelStreaming2Module, Display,
			TEXT("Status: BridgeActive=false Reason='%s'."),
			*BridgeInactiveReason);
	}

private:
	bool Tick(float DeltaTime)
	{
		Bridge->TryInitialize();
		StatusLogElapsedSeconds += DeltaTime;
		const float Interval = Bridge->GetStatusLogIntervalSeconds();
		if (Interval > 0.0f && StatusLogElapsedSeconds >= Interval)
		{
			Bridge->LogStatus();
			StatusLogElapsedSeconds = 0.0f;
		}
		return true;
	}

	void LogStatusCommand()
	{
		LogStatus();
	}

	void RebindCaptureCommand()
	{
		if (!Bridge)
		{
			UE_LOG(LogWwisePixelStreaming2Module, Warning,
				TEXT("Cannot rebind capture because the audio bridge is inactive: %s."),
				*BridgeInactiveReason);
			return;
		}
		Bridge->RebindCapture();
	}

	TUniquePtr<FWwisePixelStreaming2Bridge> Bridge;
	FTSTicker::FDelegateHandle TickerHandle;
	float StatusLogElapsedSeconds = 0.0f;
	FString BridgeInactiveReason = TEXT("module startup has not completed");
};

IMPLEMENT_MODULE(FWwisePixelStreaming2Module, WwisePixelStreaming2)

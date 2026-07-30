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
		const FWwisePixelStreaming2Config Config = FWwisePixelStreaming2Config::Load();
		if (!Config.bEnabled)
		{
			UE_LOG(LogWwisePixelStreaming2Module, Log, TEXT("Plugin is disabled by configuration."));
			return;
		}

		Bridge = MakeUnique<FWwisePixelStreaming2Bridge>(Config);
		if (!Bridge->Start())
		{
			UE_LOG(LogWwisePixelStreaming2Module, Error, TEXT("Could not start audio bridge worker thread."));
			Bridge.Reset();
			return;
		}

		UE_LOG(LogWwisePixelStreaming2Module, Display,
			TEXT("Plugin started. StreamerId='%s' OutputDeviceId=%llu QueueSlots=%d MaxFrames=%d MaxChannels=%d Gain=%.3f"),
			Config.StreamerId.IsEmpty() ? TEXT("<default>") : *Config.StreamerId,
			Config.OutputDeviceId,
			Config.QueueSlots,
			Config.MaxFrames,
			Config.MaxChannels,
			Config.Gain);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("WwisePixelStreaming2.Status"),
			TEXT("Log the current Wwise to Pixel Streaming 2 audio bridge status."),
			FConsoleCommandDelegate::CreateRaw(this, &FWwisePixelStreaming2Module::LogStatusCommand),
			ECVF_Default);

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FWwisePixelStreaming2Module::Tick),
			0.5f);
	}

	virtual void ShutdownModule() override
	{
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("WwisePixelStreaming2.Status"), false);
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
		}
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

	TUniquePtr<FWwisePixelStreaming2Bridge> Bridge;
	FTSTicker::FDelegateHandle TickerHandle;
	float StatusLogElapsedSeconds = 0.0f;
};

IMPLEMENT_MODULE(FWwisePixelStreaming2Module, WwisePixelStreaming2)

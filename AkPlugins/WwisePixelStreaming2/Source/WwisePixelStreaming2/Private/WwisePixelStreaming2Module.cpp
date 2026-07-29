#include "WwisePixelStreaming2Module.h"

#include "WwisePixelStreaming2Bridge.h"
#include "Containers/Ticker.h"

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

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FWwisePixelStreaming2Module::Tick),
			0.5f);
	}

	virtual void ShutdownModule() override
	{
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

private:
	bool Tick(float DeltaTime)
	{
		Bridge->TryInitialize();
		return true;
	}

	TUniquePtr<FWwisePixelStreaming2Bridge> Bridge;
	FTSTicker::FDelegateHandle TickerHandle;
};

IMPLEMENT_MODULE(FWwisePixelStreaming2Module, WwisePixelStreaming2)

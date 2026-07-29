#include "AkWwisePixelStreamingModule.h"

#include "AkWwisePixelStreamingBridge.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogAkWwisePixelStreamingModule, Log, All);

class FAkWwisePixelStreamingModule final : public IAkWwisePixelStreamingModule
{
public:
	virtual void StartupModule() override
	{
		const FAkWwisePixelStreamingConfig Config = FAkWwisePixelStreamingConfig::Load();
		if (!Config.bEnabled)
		{
			UE_LOG(LogAkWwisePixelStreamingModule, Log, TEXT("Plugin is disabled by configuration."));
			return;
		}

		Bridge = MakeUnique<FAkWwisePixelStreamingBridge>(Config);
		if (!Bridge->Start())
		{
			UE_LOG(LogAkWwisePixelStreamingModule, Error, TEXT("Could not start audio bridge worker thread."));
			Bridge.Reset();
			return;
		}

		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FAkWwisePixelStreamingModule::Tick),
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
			const FAkWwisePixelStreamingStats Stats = Bridge->GetStats();
			Bridge->StopBridge();
			UE_LOG(LogAkWwisePixelStreamingModule, Log,
				TEXT("Stopped. Captured=%llu Pushed=%llu Dropped=%llu Rejected=%llu"),
				Stats.CapturedBuffers, Stats.PushedBuffers, Stats.DroppedBuffers, Stats.RejectedBuffers);
			Bridge.Reset();
		}
	}

	virtual FAkWwisePixelStreamingStats GetStats() const override
	{
		return Bridge ? Bridge->GetStats() : FAkWwisePixelStreamingStats{};
	}

private:
	bool Tick(float DeltaTime)
	{
		Bridge->TryInitialize();
		return true;
	}

	TUniquePtr<FAkWwisePixelStreamingBridge> Bridge;
	FTSTicker::FDelegateHandle TickerHandle;
};

IMPLEMENT_MODULE(FAkWwisePixelStreamingModule, AkWwisePixelStreaming)

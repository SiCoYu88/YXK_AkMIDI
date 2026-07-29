#if WITH_DEV_AUTOMATION_TESTS

#include "AkWwiseAudioQueue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAkWwiseAudioQueueTest,
	"AkWwisePixelStreaming.AudioQueue.SpscContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAkWwiseAudioQueueTest::RunTest(const FString& Parameters)
{
	FAkWwiseAudioQueue Queue(2, 4, 2);
	const float First[] = { 0.0f, 0.1f, 0.2f, 0.3f };
	const float Second[] = { 0.4f, 0.5f };

	TestTrue(TEXT("first frame is accepted"), Queue.TryPush(First, 2, 2, 48000));
	TestTrue(TEXT("second frame is accepted"), Queue.TryPush(Second, 1, 2, 48000));
	TestFalse(TEXT("full queue rejects without overwriting"), Queue.TryPush(Second, 1, 2, 48000));
	TestFalse(TEXT("oversized frame is rejected"), Queue.TryPush(First, 5, 2, 48000));

	FAkWwiseAudioFrame Frame;
	TestTrue(TEXT("first frame can be read"), Queue.TryPeek(Frame));
	TestEqual(TEXT("frame count is preserved"), Frame.NumFrames, 2);
	TestEqual(TEXT("channel count is preserved"), Frame.NumChannels, 2);
	TestEqual(TEXT("sample rate is preserved"), Frame.SampleRate, 48000);
	TestEqual(TEXT("PCM is copied"), Frame.Data[3], First[3]);
	Queue.Pop();

	TestTrue(TEXT("second frame can be read"), Queue.TryPeek(Frame));
	TestEqual(TEXT("second PCM is preserved"), Frame.Data[1], Second[1]);
	Queue.Pop();
	TestFalse(TEXT("queue is empty after both pops"), Queue.TryPeek(Frame));
	return true;
}

#endif

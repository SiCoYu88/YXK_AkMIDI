#include "../Source/WwiseMediaRuntime/Private/WwiseMediaPcmQueue.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
    auto Queue = std::make_unique<WwiseMedia::FPcmQueue>(3);
    WwiseMedia::FPcmBlock Block;
    Block.Frames = WwiseMedia::FramesPerBlock;
    Block.Epoch = 1;
    for (int Index = 0; Index < 3; ++Index)
    {
        Block.FirstTimeTicks = Index;
        assert(Queue->Push(Block));
    }
    assert(!Queue->Push(Block));
    for (int Index = 0; Index < 3; ++Index)
    {
        assert(Queue->Peek()->FirstTimeTicks == Index);
        Queue->Pop();
    }
    assert(!Queue->Peek());

    // Real SPSC stress: wraps every slot repeatedly and verifies full payloads.
    constexpr std::uint32_t Count = 100000;
    std::thread Producer([&]
    {
        WwiseMedia::FPcmBlock Data;
        Data.Frames = WwiseMedia::FramesPerBlock;
        for (std::uint32_t Index = 0; Index < Count; ++Index)
        {
            Data.Epoch = Index / 13;
            Data.FlushVersion = Index / 17;
            Data.FirstTimeTicks = Index;
            Data.Samples.fill(static_cast<float>(Index));
            while (!Queue->Push(Data)) std::this_thread::yield();
        }
    });
    for (std::uint32_t Index = 0; Index < Count; ++Index)
    {
        const WwiseMedia::FPcmBlock* Data;
        while (!(Data = Queue->Peek())) std::this_thread::yield();
        assert(Data->FirstTimeTicks == Index);
        assert(Data->Epoch == Index / 13);
        assert(Data->FlushVersion == Index / 17);
        for (float Sample : Data->Samples) assert(Sample == static_cast<float>(Index));
        Queue->Pop();
    }
    Producer.join();
    assert(Queue->Num() == 0);
    assert(Queue->Free() == 3);
    std::cout << "PASS: bounded capacity, FIFO, 100000 concurrent blocks, full payload and generation integrity\n";
}

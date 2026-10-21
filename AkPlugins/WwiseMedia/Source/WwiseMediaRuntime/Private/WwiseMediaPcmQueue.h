#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace WwiseMedia
{
constexpr std::uint32_t FramesPerBlock = 512;
constexpr std::uint32_t OutputSampleRate = 48000;
constexpr std::uint32_t MaximumBlocks = 32;

struct FPcmBlock
{
    std::array<float, FramesPerBlock> Samples{};
    std::uint64_t Epoch = 0;
    std::uint32_t FlushVersion = 0;
    std::uint32_t Frames = 0;
    std::int64_t FirstTimeTicks = 0;
    std::int32_t Sequence = 0;
    std::int32_t Loop = 0;
};

// Exactly one producer and one consumer. Only the consumer advances Read.
// An epoch change invalidates blocks without either thread resetting indices.
class FPcmQueue
{
public:
    explicit FPcmQueue(std::uint32_t InCapacity = 8)
        : Capacity(std::clamp(InCapacity, 2u, MaximumBlocks)) {}

    bool Push(const FPcmBlock& Block)
    {
        const auto WriteValue = Write.load(std::memory_order_relaxed);
        if (WriteValue - Read.load(std::memory_order_acquire) >= Capacity)
            return false;
        Blocks[WriteValue % MaximumBlocks] = Block;
        Write.store(WriteValue + 1, std::memory_order_release);
        return true;
    }

    const FPcmBlock* Peek() const
    {
        const auto ReadValue = Read.load(std::memory_order_relaxed);
        if (ReadValue == Write.load(std::memory_order_acquire))
            return nullptr;
        return &Blocks[ReadValue % MaximumBlocks];
    }

    void Pop()
    {
        Read.fetch_add(1, std::memory_order_release);
    }

    std::uint32_t Num() const
    {
        // Read first: the producer cannot advance more than Capacity beyond it.
        const auto ReadValue = Read.load(std::memory_order_acquire);
        const auto WriteValue = Write.load(std::memory_order_acquire);
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(WriteValue - ReadValue, Capacity));
    }

    std::uint32_t Free() const { return Capacity - Num(); }

private:
    std::array<FPcmBlock, MaximumBlocks> Blocks;
    const std::uint32_t Capacity;
    alignas(64) std::atomic<std::uint64_t> Read{0};
    alignas(64) std::atomic<std::uint64_t> Write{0};
};
}

/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * The ingame overlay project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "../src/Windows/WindowsHookEventQueue.h"

#include <array>
#include <cstdlib>
#include <condition_variable>
#include <cstdio>
#include <new>
#include <mutex>
#include <thread>

static bool trackAllocations = false;
static size_t trackedAllocations = 0;

void* operator new(size_t size)
{
    if (trackAllocations)
        ++trackedAllocations;
    if (void* allocation = std::malloc(size))
        return allocation;
    throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept
{
    std::free(allocation);
}

void operator delete(void* allocation, size_t) noexcept
{
    std::free(allocation);
}

using namespace InGameOverlay;

#define Check(condition) do { if (!(condition)) { std::fprintf(stderr, "Check failed at line %d\n", __LINE__); std::abort(); } } while (false)

static void TestConcurrentProducersAndConsumer()
{
    constexpr size_t producerCount = 4;
    constexpr size_t eventsPerProducer = 500;
    constexpr size_t eventCount = producerCount * eventsPerProducer;
    WindowsHookEventQueue_t queue(eventCount);
    std::mutex mutex;
    std::condition_variable condition;
    size_t progress = 0;
    size_t completed = 0;
    std::array<std::array<bool, eventsPerProducer>, producerCount> seen{};
    std::array<size_t, producerCount> next{};
    std::thread consumer([&]()
    {
        size_t observed = 0;
        while (observed < eventCount)
        {
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [&]() { return progress != 0 || completed == producerCount; });
                progress = 0;
            }

            size_t batchSize = 0;
            uint64_t generation = 0;
            Check(!queue.TakeBatch(batchSize, generation));
            Check(batchSize <= eventCount - observed);
            for (size_t i = 0; i < batchSize; ++i)
            {
                const WindowsHookEvent_t& event = queue.GetBatchEvent(i);
                const size_t producer = static_cast<size_t>(event.lParam);
                const size_t sequence = static_cast<size_t>(event.wParam);
                Check(producer < producerCount);
                Check(sequence < eventsPerProducer);
                Check(sequence == next[producer]);
                Check(!seen[producer][sequence]);
                seen[producer][sequence] = true;
                ++next[producer];
                ++observed;
            }
        }
    });

    std::array<std::thread, producerCount> producers;
    for (size_t producer = 0; producer < producerCount; ++producer)
    {
        producers[producer] = std::thread([&, producer]()
        {
            for (size_t sequence = 0; sequence < eventsPerProducer; ++sequence)
            {
                Check(queue.Enqueue(reinterpret_cast<HWND>(1), WM_KEYDOWN,
                    static_cast<WPARAM>(sequence), static_cast<LPARAM>(producer)));
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++progress;
                }
                condition.notify_one();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++completed;
            }
            condition.notify_one();
        });
    }

    for (std::thread& producer : producers)
        producer.join();
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++progress;
    }
    condition.notify_one();
    consumer.join();
    for (size_t producer = 0; producer < producerCount; ++producer)
    {
        Check(next[producer] == eventsPerProducer);
        for (bool wasSeen : seen[producer])
            Check(wasSeen);
    }
}

static void TestOverflowAndInvalidation()
{
    const HWND window = reinterpret_cast<HWND>(1);
    WindowsHookEventQueue_t overflowQueue(2);
    Check(overflowQueue.Enqueue(window, WM_KEYDOWN, 'A', 0));
    Check(overflowQueue.Enqueue(window, WM_KEYUP, 'A', 0));
    Check(!overflowQueue.Enqueue(window, WM_KEYDOWN, 'B', 0));
    size_t batchSize = 0;
    uint64_t generation = 0;
    Check(overflowQueue.TakeBatch(batchSize, generation));
    Check(batchSize == 0);

    WindowsHookEventQueue_t beforeExtraction(2);
    Check(beforeExtraction.Enqueue(window, WM_KEYDOWN, 'C', 0));
    beforeExtraction.Invalidate();
    Check(beforeExtraction.TakeBatch(batchSize, generation));
    Check(batchSize == 0);

    WindowsHookEventQueue_t afterExtraction(2);
    Check(afterExtraction.Enqueue(window, WM_KEYDOWN, 'D', 0));
    Check(!afterExtraction.TakeBatch(batchSize, generation));
    Check(batchSize == 1);
    afterExtraction.Invalidate();
    Check(!afterExtraction.IsCurrent(generation));
    bool keyDown = afterExtraction.GetBatchEvent(0).msg == WM_KEYDOWN;
    if (!afterExtraction.IsCurrent(generation))
        keyDown = false;
    Check(!keyDown);
    Check(afterExtraction.TakeBatch(batchSize, generation));
    Check(batchSize == 0);
}

static void TestInvalidationDuringConsumptionAndReentrantEnqueue()
{
    const HWND window = reinterpret_cast<HWND>(1);
    WindowsHookEventQueue_t queue(2);
    Check(queue.Enqueue(window, WM_KEYDOWN, 'E', 0));
    size_t batchSize = 0;
    uint64_t generation = 0;
    Check(!queue.TakeBatch(batchSize, generation));
    Check(batchSize == 1);

    std::mutex mutex;
    std::condition_variable condition;
    bool processing = false;
    bool resetComplete = false;
    bool keyDown = false;
    std::thread invalidator([&]()
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]() { return processing; });
        lock.unlock();
        queue.Invalidate();
        lock.lock();
        resetComplete = true;
        lock.unlock();
        condition.notify_one();
    });

    // This callback both enqueues and coordinates invalidation during
    // consumption. It deadlocks if the queue mutex is held during handling.
    auto consumeEvent = [&]()
    {
        keyDown = queue.GetBatchEvent(0).msg == WM_KEYDOWN;
        Check(queue.Enqueue(window, WM_KEYUP, 'E', 0));
        {
            std::lock_guard<std::mutex> lock(mutex);
            processing = true;
        }
        condition.notify_one();
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&]() { return resetComplete; });
    };
    consumeEvent();
    if (!queue.IsCurrent(generation))
        keyDown = false;
    Check(!keyDown);
    invalidator.join();

    Check(queue.TakeBatch(batchSize, generation));
    Check(batchSize == 0);
}

static void TestSwapReuseAndStableBatch()
{
    const HWND window = reinterpret_cast<HWND>(1);
    constexpr size_t capacity = 4;
    WindowsHookEventQueue_t queue(capacity);
    size_t batchSize = 0;
    uint64_t generation = 0;

    trackedAllocations = 0;
    trackAllocations = true;
    for (size_t iteration = 0; iteration < 20; ++iteration)
    {
        const size_t count = iteration % (capacity + 1);
        for (size_t i = 0; i < count; ++i)
            Check(queue.Enqueue(window, WM_KEYDOWN, static_cast<WPARAM>(iteration * capacity + i), 0));
        Check(!queue.TakeBatch(batchSize, generation));
        Check(batchSize == count);

        // Producers append to the receiving vector while this batch remains
        // owned by the consumer and readable.
        Check(queue.Enqueue(window, WM_KEYUP, static_cast<WPARAM>(iteration), 0));
        for (size_t i = 0; i < batchSize; ++i)
        {
            const WindowsHookEvent_t& event = queue.GetBatchEvent(i);
            Check(event.msg == WM_KEYDOWN);
            Check(event.wParam == static_cast<WPARAM>(iteration * capacity + i));
        }
        Check(!queue.TakeBatch(batchSize, generation));
        Check(batchSize == 1);
        Check(queue.GetBatchEvent(0).msg == WM_KEYUP);
    }

    // Overflow purges only the receiving side; the current batch stays stable
    // while the generation change invalidates its processing.
    Check(queue.Enqueue(window, WM_KEYDOWN, 'A', 0));
    Check(!queue.TakeBatch(batchSize, generation));
    Check(batchSize == 1);
    Check(queue.Enqueue(window, WM_KEYUP, 'A', 0));
    Check(queue.Enqueue(window, WM_KEYDOWN, 'B', 0));
    Check(queue.Enqueue(window, WM_KEYUP, 'B', 0));
    Check(queue.Enqueue(window, WM_KEYDOWN, 'C', 0));
    Check(!queue.Enqueue(window, WM_KEYUP, 'C', 0));
    Check(!queue.IsCurrent(generation));
    Check(queue.GetBatchEvent(0).msg == WM_KEYDOWN);
    Check(queue.GetBatchEvent(0).wParam == 'A');
    Check(queue.TakeBatch(batchSize, generation));
    Check(batchSize == 0);
    trackAllocations = false;
    Check(trackedAllocations == 0);
}

int main()
{
    TestConcurrentProducersAndConsumer();
    TestOverflowAndInvalidation();
    TestInvalidationDuringConsumptionAndReentrantEnqueue();
    TestSwapReuseAndStableBatch();
    return 0;
}

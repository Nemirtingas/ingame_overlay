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

#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace InGameOverlay {

struct WindowsHookEvent_t
{
    HWND hWnd;
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;

    WindowsHookEvent_t() = default;
    WindowsHookEvent_t(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) :
        hWnd(hWnd), msg(msg), wParam(wParam), lParam(lParam)
    {}
};

class WindowsHookEventQueue_t
{
public:
    explicit WindowsHookEventQueue_t(size_t capacity) :
        _Events(),
        _Batch(),
        _Capacity(capacity),
        _Generation(0),
        _ResetPending(false)
    {
        _Events.reserve(_Capacity);
        _Batch.reserve(_Capacity);
    }

    bool Enqueue(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        if (_Events.size() == _Capacity)
        {
            _InvalidateLocked();
            return false;
        }

        _Events.emplace_back(hWnd, msg, wParam, lParam);
        return true;
    }

    void Invalidate()
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        _InvalidateLocked();
    }

    bool TakeBatch(size_t& size, uint64_t& generation)
    {
        // The single consumer owns _Batch until it calls TakeBatch again.
        // Clear it outside the lock before receiving the next producer batch.
        _Batch.clear();
        std::lock_guard<std::mutex> lock(_Mutex);
        const bool resetPending = _ResetPending;
        _ResetPending = false;
        generation = _Generation;
        _Events.swap(_Batch);
        size = _Batch.size();
        return resetPending;
    }

    const WindowsHookEvent_t& GetBatchEvent(size_t index) const
    {
        return _Batch[index];
    }

    bool IsCurrent(uint64_t generation) const
    {
        std::lock_guard<std::mutex> lock(_Mutex);
        return generation == _Generation;
    }

private:
    void _InvalidateLocked()
    {
        ++_Generation;
        _Events.clear();
        _ResetPending = true;
    }

    std::vector<WindowsHookEvent_t> _Events;
    std::vector<WindowsHookEvent_t> _Batch;
    const size_t _Capacity;
    uint64_t _Generation;
    bool _ResetPending;
    mutable std::mutex _Mutex;
};

} // namespace InGameOverlay

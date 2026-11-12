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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <utility>
#include <vector>

namespace InGameOverlay {

template<typename T>
class RendererUploadBatchState_t
{
public:
    enum class Status_e
    {
        Ready,
        Pending,
        Failed
    };

    bool CanSubmit() const
    {
        return _Status == Status_e::Ready && _Resources.empty();
    }

    bool HasResources() const
    {
        return !_Resources.empty();
    }

    const std::vector<std::shared_ptr<T>>& Resources() const
    {
        return _Resources;
    }

    void Retain(std::shared_ptr<T> resource)
    {
        _Resources.push_back(std::move(resource));
    }

    void ClearResources()
    {
        _Resources.clear();
    }

    Status_e GetStatus() const
    {
        return _Status;
    }

    void Submitted(bool fenceValid)
    {
        _Status = fenceValid ? Status_e::Pending : Status_e::Failed;
    }

    // Returns true only when the batch has a completion proof and may be released.
    bool Poll(bool completed, bool error)
    {
        if (_Status != Status_e::Pending)
            return false;
        if (error)
        {
            _Status = Status_e::Failed;
            return false;
        }
        if (!completed)
            return false;
        _Status = Status_e::Ready;
        return true;
    }

    void Fail()
    {
        _Status = Status_e::Failed;
    }

    void Reset()
    {
        _Status = Status_e::Ready;
    }

private:
    Status_e _Status = Status_e::Ready;
    std::vector<std::shared_ptr<T>> _Resources;
};

} // namespace InGameOverlay

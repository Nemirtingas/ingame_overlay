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
#include <Unknwn.h>

#include <atomic>
#include <cstring>
#include <random>

#include <cstdint>

namespace InGameOverlay {

// Private data key used to store a ComDestructionNotifier_t in an application COM object.
// It is random and generated once per module, so two copies of this library loaded in the same process
// don't replace (and therefore trigger) each other's notifier.
inline GUID const& GetComDestructionNotifierGuid()
{
    static const GUID guid = []()
    {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        const uint64_t data[2] = { dis(gen), dis(gen) };
        GUID result;
        std::memcpy(&result, data, sizeof(result));

        // Mark it as a random GUID (version 4, RFC 4122 variant).
        result.Data3 = (result.Data3 & 0x0fff) | 0x4000;
        result.Data4[0] = (result.Data4[0] & 0x3f) | 0x80;
        return result;
    }();

    return guid;
}

// Stored as private data in an application object (for example a DXGI swapchain): the object releases it
// when it is destroyed, which tells the hook to release every resource holding a reference on the application device.
class ComDestructionNotifier_t : public IUnknown
{
    std::atomic<ULONG> _RefCount;
    void (*_Callback)(void*);
    void* _UserData;

public:
    ComDestructionNotifier_t(void (*callback)(void*), void* userData) :
        _RefCount(1),
        _Callback(callback),
        _UserData(userData)
    {}

    // Called when we remove the notifier ourselves, the callback must not run in that case.
    void Detach()
    {
        _Callback = nullptr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid != __uuidof(IUnknown))
        {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        *ppvObject = static_cast<IUnknown*>(this);
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++_RefCount;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG refCount = --_RefCount;
        if (refCount == 0)
        {
            if (_Callback != nullptr)
                _Callback(_UserData);

            delete this;
        }
        return refCount;
    }
};

}// namespace InGameOverlay

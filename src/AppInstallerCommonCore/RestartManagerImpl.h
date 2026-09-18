// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once

#include <winget/RestartManager.h>
#include <RestartManager.h>

namespace AppInstaller::RestartManager::details
{
    struct IRestartManagerApi
    {
        virtual ~IRestartManagerApi() = default;

        virtual DWORD StartSession(DWORD* sessionHandle, DWORD sessionFlags, WCHAR* sessionKey) noexcept = 0;
        virtual DWORD EndSession(DWORD sessionHandle) noexcept = 0;
        virtual DWORD RegisterResources(
            DWORD sessionHandle,
            UINT fileCount,
            LPCWSTR* fileNames,
            UINT applicationCount,
            RM_UNIQUE_PROCESS* applications,
            UINT serviceCount,
            LPCWSTR* serviceNames) noexcept = 0;
        virtual DWORD GetList(
            DWORD sessionHandle,
            UINT* processInfoNeeded,
            UINT* processInfoCount,
            RM_PROCESS_INFO* affectedApplications,
            DWORD* rebootReasons) noexcept = 0;
        virtual DWORD Shutdown(DWORD sessionHandle, ULONG actionFlags, RM_WRITE_STATUS_CALLBACK callback) noexcept = 0;
    };
}

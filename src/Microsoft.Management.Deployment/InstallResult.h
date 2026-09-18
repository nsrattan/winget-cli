// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "InstallResult.g.h"

namespace winrt::Microsoft::Management::Deployment::implementation
{
    struct InstallResult : InstallResultT<InstallResult>
    {
        InstallResult() = default;

#if !defined(INCLUDE_ONLY_INTERFACE_METHODS)
        void Initialize(
            winrt::Microsoft::Management::Deployment::InstallResultStatus status,
            winrt::hresult extendedErrorCode,
            uint32_t installerErrorCode,
            hstring const& correlationData, 
            bool rebootRequired);
        void InitializeAppInUseResult(winrt::Microsoft::Management::Deployment::AppInUseResult const& value);
#endif

        hstring CorrelationData();
        bool RebootRequired();
        winrt::Microsoft::Management::Deployment::InstallResultStatus Status();
        winrt::hresult ExtendedErrorCode();
        uint32_t InstallerErrorCode();
        winrt::Microsoft::Management::Deployment::AppInUseResult AppInUseResult();

#if !defined(INCLUDE_ONLY_INTERFACE_METHODS)
    private:
        std::wstring m_correlationData = L"";
        bool m_rebootRequired = false;
        winrt::Microsoft::Management::Deployment::InstallResultStatus m_status = winrt::Microsoft::Management::Deployment::InstallResultStatus::Ok;
        winrt::hresult m_extendedErrorCode = S_OK;
        uint32_t m_installerErrorCode = 0;
        winrt::Microsoft::Management::Deployment::AppInUseResult m_appInUseResult{ nullptr };
#endif
    };
}

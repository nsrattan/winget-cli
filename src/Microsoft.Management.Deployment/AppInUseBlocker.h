// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "AppInUseBlocker.g.h"
#include <winget/RestartManager.h>

namespace winrt::Microsoft::Management::Deployment::implementation
{
    struct AppInUseBlocker : AppInUseBlockerT<AppInUseBlocker>
    {
        AppInUseBlocker() = default;

#if !defined(INCLUDE_ONLY_INTERFACE_METHODS)
        void Initialize(const AppInstaller::RestartManager::Blocker& blocker);
#endif

        uint32_t ProcessId();
        uint64_t ProcessStartTime();
        hstring ApplicationName();
        hstring ServiceName();
        winrt::Microsoft::Management::Deployment::AppInUseApplicationType ApplicationType();
        winrt::Microsoft::Management::Deployment::AppInUseApplicationStatus Status();
        uint32_t SessionId();
        bool IsRestartable();
        bool IsCritical();
        winrt::Windows::Foundation::Collections::IVectorView<hstring> WindowTitles();

#if !defined(INCLUDE_ONLY_INTERFACE_METHODS)
    private:
        uint32_t m_processId = 0;
        uint64_t m_processStartTime = 0;
        std::wstring m_applicationName;
        std::wstring m_serviceName;
        winrt::Microsoft::Management::Deployment::AppInUseApplicationType m_applicationType =
            winrt::Microsoft::Management::Deployment::AppInUseApplicationType::Unknown;
        winrt::Microsoft::Management::Deployment::AppInUseApplicationStatus m_status =
            winrt::Microsoft::Management::Deployment::AppInUseApplicationStatus::Unknown;
        uint32_t m_sessionId = 0;
        bool m_isRestartable = false;
        bool m_isCritical = false;
        winrt::Windows::Foundation::Collections::IVector<hstring> m_windowTitles{ winrt::single_threaded_vector<hstring>() };
#endif
    };
}

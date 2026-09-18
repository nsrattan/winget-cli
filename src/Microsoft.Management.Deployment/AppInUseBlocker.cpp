// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "AppInUseBlocker.h"
#include "AppInUseBlocker.g.cpp"
#include <wil\cppwinrt_wrl.h>

namespace winrt::Microsoft::Management::Deployment::implementation
{
    void AppInUseBlocker::Initialize(const AppInstaller::RestartManager::Blocker& blocker)
    {
        m_processId = blocker.ProcessId;
        m_processStartTime = blocker.ProcessStartTime;
        m_applicationName = blocker.ApplicationName;
        m_serviceName = blocker.ServiceName;
        m_applicationType = static_cast<winrt::Microsoft::Management::Deployment::AppInUseApplicationType>(blocker.Type);
        m_status = static_cast<winrt::Microsoft::Management::Deployment::AppInUseApplicationStatus>(blocker.Status);
        m_sessionId = blocker.SessionId;
        m_isRestartable = blocker.IsRestartable;
        m_isCritical = blocker.IsCritical;

        for (const auto& title : blocker.WindowTitles)
        {
            m_windowTitles.Append(title);
        }
    }

    uint32_t AppInUseBlocker::ProcessId() { return m_processId; }
    uint64_t AppInUseBlocker::ProcessStartTime() { return m_processStartTime; }
    hstring AppInUseBlocker::ApplicationName() { return hstring{ m_applicationName }; }
    hstring AppInUseBlocker::ServiceName() { return hstring{ m_serviceName }; }
    winrt::Microsoft::Management::Deployment::AppInUseApplicationType AppInUseBlocker::ApplicationType() { return m_applicationType; }
    winrt::Microsoft::Management::Deployment::AppInUseApplicationStatus AppInUseBlocker::Status() { return m_status; }
    uint32_t AppInUseBlocker::SessionId() { return m_sessionId; }
    bool AppInUseBlocker::IsRestartable() { return m_isRestartable; }
    bool AppInUseBlocker::IsCritical() { return m_isCritical; }
    winrt::Windows::Foundation::Collections::IVectorView<hstring> AppInUseBlocker::WindowTitles() { return m_windowTitles.GetView(); }
}

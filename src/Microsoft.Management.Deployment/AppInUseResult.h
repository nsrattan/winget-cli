// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "AppInUseResult.g.h"
#include <winget/RestartManager.h>

namespace winrt::Microsoft::Management::Deployment::implementation
{
    struct AppInUseResult : AppInUseResultT<AppInUseResult>
    {
        AppInUseResult() = default;

#if !defined(INCLUDE_ONLY_INTERFACE_METHODS)
        void Initialize(
            winrt::Microsoft::Management::Deployment::AppInUseResultStatus status,
            winrt::Microsoft::Management::Deployment::AppInUseResultReason reason,
            winrt::hresult extendedErrorCode);
        void Initialize(
            winrt::Microsoft::Management::Deployment::AppInUseResultStatus status,
            winrt::Microsoft::Management::Deployment::AppInUseResultReason reason,
            winrt::hresult extendedErrorCode,
            const AppInstaller::RestartManager::ProbeResult& result);
        void Initialize(
            winrt::Microsoft::Management::Deployment::AppInUseResultStatus status,
            winrt::Microsoft::Management::Deployment::AppInUseResultReason reason,
            winrt::hresult extendedErrorCode,
            const AppInstaller::RestartManager::ShutdownResult& result);
        void Initialize(const AppInstaller::RestartManager::ProbeResult& result);
        void Initialize(const AppInstaller::RestartManager::ShutdownResult& result);
        void SetProbeResult(const AppInstaller::RestartManager::ProbeResult& result);
        void SetShutdownResult(const AppInstaller::RestartManager::ShutdownResult& result);
#endif

        winrt::Microsoft::Management::Deployment::AppInUseResultStatus Status();
        winrt::Microsoft::Management::Deployment::AppInUseResultReason Reason();
        winrt::hresult ExtendedErrorCode();
        winrt::Windows::Foundation::Collections::IVectorView<winrt::Microsoft::Management::Deployment::AppInUseBlocker> InitialBlockers();
        winrt::Windows::Foundation::Collections::IVectorView<winrt::Microsoft::Management::Deployment::AppInUseBlocker> RemainingBlockers();
        winrt::Microsoft::Management::Deployment::AppInUseRebootReason InitialRebootReasons();
        winrt::Microsoft::Management::Deployment::AppInUseRebootReason RemainingRebootReasons();
        winrt::Microsoft::Management::Deployment::AppInUseShutdownStatus ShutdownStatus();
        uint32_t ShutdownErrorCode();
        bool ShutdownAttempted();

#if !defined(INCLUDE_ONLY_INTERFACE_METHODS)
    private:
        void PopulateBlockers(
            const std::vector<AppInstaller::RestartManager::Blocker>& source,
            winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::Management::Deployment::AppInUseBlocker> destination);
        void ResetRestartManagerData();

        winrt::Microsoft::Management::Deployment::AppInUseResultStatus m_status =
            winrt::Microsoft::Management::Deployment::AppInUseResultStatus::NotApplicable;
        winrt::Microsoft::Management::Deployment::AppInUseResultReason m_reason =
            winrt::Microsoft::Management::Deployment::AppInUseResultReason::None;
        winrt::hresult m_extendedErrorCode = S_OK;
        winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::Management::Deployment::AppInUseBlocker> m_initialBlockers{
            winrt::single_threaded_vector<winrt::Microsoft::Management::Deployment::AppInUseBlocker>() };
        winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::Management::Deployment::AppInUseBlocker> m_remainingBlockers{
            winrt::single_threaded_vector<winrt::Microsoft::Management::Deployment::AppInUseBlocker>() };
        winrt::Microsoft::Management::Deployment::AppInUseRebootReason m_initialRebootReasons =
            winrt::Microsoft::Management::Deployment::AppInUseRebootReason::None;
        winrt::Microsoft::Management::Deployment::AppInUseRebootReason m_remainingRebootReasons =
            winrt::Microsoft::Management::Deployment::AppInUseRebootReason::None;
        winrt::Microsoft::Management::Deployment::AppInUseShutdownStatus m_shutdownStatus =
            winrt::Microsoft::Management::Deployment::AppInUseShutdownStatus::NotNeeded;
        uint32_t m_shutdownErrorCode = 0;
        bool m_shutdownAttempted = false;
#endif
    };
}

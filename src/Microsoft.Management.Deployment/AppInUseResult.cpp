// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "AppInUseResult.h"
#include "AppInUseResult.g.cpp"
#include "AppInUseBlocker.h"
#include <wil\cppwinrt_wrl.h>

namespace winrt::Microsoft::Management::Deployment::implementation
{
    namespace
    {
        winrt::hresult GetShutdownExtendedErrorCode(const AppInstaller::RestartManager::ShutdownResult& result)
        {
            return result.ShutdownError ? winrt::hresult{ HRESULT_FROM_WIN32(result.ShutdownError) } : winrt::hresult{ S_OK };
        }
    }

    void AppInUseResult::PopulateBlockers(
        const std::vector<AppInstaller::RestartManager::Blocker>& source,
        winrt::Windows::Foundation::Collections::IVector<winrt::Microsoft::Management::Deployment::AppInUseBlocker> destination)
    {
        for (const auto& blocker : source)
        {
            auto projected = winrt::make_self<wil::details::module_count_wrapper<AppInUseBlocker>>();
            projected->Initialize(blocker);
            destination.Append(*projected);
        }
    }

    void AppInUseResult::ResetRestartManagerData()
    {
        m_initialBlockers.Clear();
        m_remainingBlockers.Clear();
        m_initialRebootReasons = winrt::Microsoft::Management::Deployment::AppInUseRebootReason::None;
        m_remainingRebootReasons = winrt::Microsoft::Management::Deployment::AppInUseRebootReason::None;
        m_shutdownStatus = winrt::Microsoft::Management::Deployment::AppInUseShutdownStatus::NotNeeded;
        m_shutdownErrorCode = 0;
        m_shutdownAttempted = false;
    }

    void AppInUseResult::Initialize(
        winrt::Microsoft::Management::Deployment::AppInUseResultStatus status,
        winrt::Microsoft::Management::Deployment::AppInUseResultReason reason,
        winrt::hresult extendedErrorCode)
    {
        m_status = status;
        m_reason = reason;
        m_extendedErrorCode = extendedErrorCode;
        ResetRestartManagerData();
    }

    void AppInUseResult::Initialize(
        winrt::Microsoft::Management::Deployment::AppInUseResultStatus status,
        winrt::Microsoft::Management::Deployment::AppInUseResultReason reason,
        winrt::hresult extendedErrorCode,
        const AppInstaller::RestartManager::ProbeResult& result)
    {
        Initialize(status, reason, extendedErrorCode);
        SetProbeResult(result);
    }

    void AppInUseResult::Initialize(
        winrt::Microsoft::Management::Deployment::AppInUseResultStatus status,
        winrt::Microsoft::Management::Deployment::AppInUseResultReason reason,
        winrt::hresult extendedErrorCode,
        const AppInstaller::RestartManager::ShutdownResult& result)
    {
        Initialize(status, reason, extendedErrorCode);
        SetShutdownResult(result);
    }

    void AppInUseResult::Initialize(const AppInstaller::RestartManager::ProbeResult& result)
    {
        Initialize(
            result.IsInUse() ?
                winrt::Microsoft::Management::Deployment::AppInUseResultStatus::InUse :
                winrt::Microsoft::Management::Deployment::AppInUseResultStatus::NotInUse,
            winrt::Microsoft::Management::Deployment::AppInUseResultReason::None,
            S_OK);
        SetProbeResult(result);
    }

    void AppInUseResult::SetProbeResult(const AppInstaller::RestartManager::ProbeResult& result)
    {
        ResetRestartManagerData();
        PopulateBlockers(result.Blockers, m_initialBlockers);
        PopulateBlockers(result.Blockers, m_remainingBlockers);
        m_initialRebootReasons = static_cast<winrt::Microsoft::Management::Deployment::AppInUseRebootReason>(result.RebootReasons);
        m_remainingRebootReasons = m_initialRebootReasons;
    }

    void AppInUseResult::Initialize(const AppInstaller::RestartManager::ShutdownResult& result)
    {
        using ResultStatus = winrt::Microsoft::Management::Deployment::AppInUseResultStatus;

        ResultStatus status = ResultStatus::ShutdownIncomplete;
        if (result.Status == AppInstaller::RestartManager::ShutdownStatus::Completed)
        {
            status = ResultStatus::ShutdownCompleted;
        }
        else if (result.Status == AppInstaller::RestartManager::ShutdownStatus::NotNeeded)
        {
            status = result.Initial.IsInUse() ? ResultStatus::ShutdownCompleted : ResultStatus::NotInUse;
        }

        Initialize(status, winrt::Microsoft::Management::Deployment::AppInUseResultReason::None, GetShutdownExtendedErrorCode(result));
        SetShutdownResult(result);
    }

    void AppInUseResult::SetShutdownResult(const AppInstaller::RestartManager::ShutdownResult& result)
    {
        ResetRestartManagerData();
        PopulateBlockers(result.Initial.Blockers, m_initialBlockers);
        PopulateBlockers(result.Final.Blockers, m_remainingBlockers);
        m_initialRebootReasons = static_cast<winrt::Microsoft::Management::Deployment::AppInUseRebootReason>(result.Initial.RebootReasons);
        m_remainingRebootReasons = static_cast<winrt::Microsoft::Management::Deployment::AppInUseRebootReason>(result.Final.RebootReasons);
        m_shutdownStatus = static_cast<winrt::Microsoft::Management::Deployment::AppInUseShutdownStatus>(result.Status);
        m_shutdownErrorCode = result.ShutdownError;
        m_shutdownAttempted = result.ShutdownAttempted;
    }

    winrt::Microsoft::Management::Deployment::AppInUseResultStatus AppInUseResult::Status() { return m_status; }
    winrt::Microsoft::Management::Deployment::AppInUseResultReason AppInUseResult::Reason() { return m_reason; }
    winrt::hresult AppInUseResult::ExtendedErrorCode() { return m_extendedErrorCode; }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::Microsoft::Management::Deployment::AppInUseBlocker> AppInUseResult::InitialBlockers() { return m_initialBlockers.GetView(); }
    winrt::Windows::Foundation::Collections::IVectorView<winrt::Microsoft::Management::Deployment::AppInUseBlocker> AppInUseResult::RemainingBlockers() { return m_remainingBlockers.GetView(); }
    winrt::Microsoft::Management::Deployment::AppInUseRebootReason AppInUseResult::InitialRebootReasons() { return m_initialRebootReasons; }
    winrt::Microsoft::Management::Deployment::AppInUseRebootReason AppInUseResult::RemainingRebootReasons() { return m_remainingRebootReasons; }
    winrt::Microsoft::Management::Deployment::AppInUseShutdownStatus AppInUseResult::ShutdownStatus() { return m_shutdownStatus; }
    uint32_t AppInUseResult::ShutdownErrorCode() { return m_shutdownErrorCode; }
    bool AppInUseResult::ShutdownAttempted() { return m_shutdownAttempted; }
}

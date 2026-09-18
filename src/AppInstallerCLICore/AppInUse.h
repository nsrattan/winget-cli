// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once

#include <winget/RestartManager.h>
#include <optional>

namespace AppInstaller::CLI
{
    enum class AppInUsePolicy
    {
        Disabled = 0,
        Probe,
        Shutdown,
        ForceShutdown,
    };

    enum class AppInUseResultStatus
    {
        NotInUse = 0,
        InUse,
        NotApplicable,
        MetadataUnavailable,
        SourceError,
        InvalidMetadata,
        ProbeError,
        ShutdownIncomplete,
        ShutdownCompleted,
        DelegatedToInstaller,
    };

    enum class AppInUseResultReason
    {
        None = 0,
        InstallerTypeUsesPlatformDeployment,
        SideEffectFreeProbeUnavailable,
        GracefulShutdownUnavailable,
        InstallerTypeHasNoAppInUseSemantics,
        UnsupportedInstallerType,
        SourceIneligible,
        OfferingUnavailable,
        TransitionUnavailable,
    };

    struct AppInUseResult
    {
        AppInUseResultStatus Status = AppInUseResultStatus::NotApplicable;
        AppInUseResultReason Reason = AppInUseResultReason::None;
        HRESULT ExtendedErrorCode = S_OK;
        std::optional<RestartManager::ProbeResult> Probe;
        std::optional<RestartManager::ShutdownResult> Shutdown;
    };
}

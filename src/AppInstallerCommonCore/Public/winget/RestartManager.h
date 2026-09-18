// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AppInstaller::RestartManager
{
    enum class ApplicationType : std::uint32_t
    {
        Unknown = 0,
        MainWindow = 1,
        OtherWindow = 2,
        Service = 3,
        Explorer = 4,
        Console = 5,
        Critical = 1000,
    };

    enum class ShutdownType
    {
        Graceful,
        Force,
    };

    enum class ApplicationStatus : std::uint32_t
    {
        Unknown = 0x0,
        Running = 0x1,
        Stopped = 0x2,
        StoppedByOther = 0x4,
        Restarted = 0x8,
        ErrorOnStop = 0x10,
        ErrorOnRestart = 0x20,
        ShutdownMasked = 0x40,
        RestartMasked = 0x80,
    };

    enum class RebootReason : std::uint32_t
    {
        None = 0x0,
        PermissionDenied = 0x1,
        SessionMismatch = 0x2,
        CriticalProcess = 0x4,
        CriticalService = 0x8,
        DetectedSelf = 0x10,
    };

    enum class ShutdownStatus
    {
        NotNeeded,
        Completed,
        BlockedByCriticalApplication,
        BlockedByCurrentProcess,
        Incomplete,
        Failed,
    };

    struct ResourceSet
    {
        std::vector<std::filesystem::path> Files;
        std::vector<std::wstring> ServiceNames;
    };

    struct Blocker
    {
        std::uint32_t ProcessId = 0;
        std::uint64_t ProcessStartTime = 0;
        std::wstring ApplicationName;
        std::wstring ServiceName;
        ApplicationType Type = ApplicationType::Unknown;
        ApplicationStatus Status = ApplicationStatus::Unknown;
        std::uint32_t SessionId = 0;
        bool IsRestartable = false;
        bool IsCritical = false;
        std::vector<std::wstring> WindowTitles;
    };

    struct ProbeResult
    {
        std::vector<Blocker> Blockers;
        RebootReason RebootReasons = RebootReason::None;

        bool IsInUse() const noexcept
        {
            return !Blockers.empty();
        }
    };

    struct ShutdownResult
    {
        ProbeResult Initial;
        ProbeResult Final;
        ShutdownStatus Status = ShutdownStatus::NotNeeded;
        std::uint32_t ShutdownError = 0;
        bool ShutdownAttempted = false;
    };

    namespace details
    {
        struct IRestartManagerApi;
    }

    class Coordinator
    {
    public:
        Coordinator();

#ifndef AICLI_DISABLE_TEST_HOOKS
        explicit Coordinator(std::shared_ptr<details::IRestartManagerApi> api);
#endif

        ~Coordinator();

        Coordinator(const Coordinator&) = delete;
        Coordinator& operator=(const Coordinator&) = delete;
        Coordinator(Coordinator&&) noexcept;
        Coordinator& operator=(Coordinator&&) noexcept;

        ProbeResult Probe(const ResourceSet& resources) const;
        ShutdownResult Shutdown(const ResourceSet& resources, ShutdownType type) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}

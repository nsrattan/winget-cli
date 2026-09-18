// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "RestartManagerImpl.h"
#include <AppInstallerLogging.h>

#pragma comment(lib, "Rstrtmgr.lib")

namespace AppInstaller::RestartManager
{
    namespace
    {
        constexpr std::size_t MaximumFileResources = 2048;
        constexpr std::size_t MaximumServiceResources = 256;
        constexpr std::size_t MaximumBlockers = 4096;
        constexpr std::size_t MaximumGetListAttempts = 8;
        constexpr std::size_t MaximumWindowTitles = 8;
        constexpr int MaximumWindowTitleLength = 512;

        struct CaseInsensitiveLess
        {
            bool operator()(const std::wstring& left, const std::wstring& right) const noexcept
            {
                return _wcsicmp(left.c_str(), right.c_str()) < 0;
            }
        };

        class RestartManagerApi : public details::IRestartManagerApi
        {
        public:
            DWORD StartSession(DWORD* sessionHandle, DWORD sessionFlags, WCHAR* sessionKey) noexcept override
            {
                UNREFERENCED_PARAMETER(sessionFlags);
                return RmStartSession(sessionHandle, 0, sessionKey);
            }

            DWORD EndSession(DWORD sessionHandle) noexcept override
            {
                return RmEndSession(sessionHandle);
            }

            DWORD RegisterResources(
                DWORD sessionHandle,
                UINT fileCount,
                LPCWSTR* fileNames,
                UINT applicationCount,
                RM_UNIQUE_PROCESS* applications,
                UINT serviceCount,
                LPCWSTR* serviceNames) noexcept override
            {
                return RmRegisterResources(
                    sessionHandle,
                    fileCount,
                    fileNames,
                    applicationCount,
                    applications,
                    serviceCount,
                    serviceNames);
            }

            DWORD GetList(
                DWORD sessionHandle,
                UINT* processInfoNeeded,
                UINT* processInfoCount,
                RM_PROCESS_INFO* affectedApplications,
                DWORD* rebootReasons) noexcept override
            {
                return RmGetList(sessionHandle, processInfoNeeded, processInfoCount, affectedApplications, rebootReasons);
            }

            DWORD Shutdown(DWORD sessionHandle, ULONG actionFlags, RM_WRITE_STATUS_CALLBACK callback) noexcept override
            {
                return RmShutdown(sessionHandle, actionFlags, callback);
            }
        };

        void ThrowOnError(DWORD result)
        {
            if (result != ERROR_SUCCESS)
            {
                THROW_WIN32(result);
            }
        }

        void ValidateResources(const ResourceSet& resources)
        {
            THROW_HR_IF(E_INVALIDARG, resources.Files.empty() && resources.ServiceNames.empty());
            THROW_HR_IF(E_INVALIDARG, resources.Files.size() > MaximumFileResources);
            THROW_HR_IF(E_INVALIDARG, resources.ServiceNames.size() > MaximumServiceResources);

            for (const auto& file : resources.Files)
            {
                THROW_HR_IF(E_INVALIDARG, file.empty() || !file.is_absolute());
                THROW_HR_IF(E_INVALIDARG, file.native().size() >= std::numeric_limits<std::uint16_t>::max());
            }

            for (const auto& service : resources.ServiceNames)
            {
                THROW_HR_IF(E_INVALIDARG, service.empty() || service.size() > 256);
            }
        }

        ResourceSet NormalizeAndDeduplicateResources(const ResourceSet& resources)
        {
            ResourceSet result;
            std::set<std::wstring, CaseInsensitiveLess> files;
            std::set<std::wstring, CaseInsensitiveLess> services;

            for (const auto& file : resources.Files)
            {
                std::filesystem::path normalized = file.lexically_normal();
                if (files.emplace(normalized.native()).second)
                {
                    result.Files.emplace_back(std::move(normalized));
                }
            }

            for (const auto& service : resources.ServiceNames)
            {
                if (services.emplace(service).second)
                {
                    result.ServiceNames.emplace_back(service);
                }
            }

            return result;
        }

        class Session
        {
        public:
            explicit Session(std::shared_ptr<details::IRestartManagerApi> api) : m_api(std::move(api))
            {
                WCHAR sessionKey[CCH_RM_SESSION_KEY + 1]{};
                ThrowOnError(m_api->StartSession(&m_handle, 0, sessionKey));
                m_started = true;
            }

            ~Session()
            {
                if (m_started)
                {
                    const DWORD result = m_api->EndSession(m_handle);
                    if (result != ERROR_SUCCESS)
                    {
                        AICLI_LOG(Core, Warning, << "RmEndSession failed with error " << result);
                    }
                }
            }

            Session(const Session&) = delete;
            Session& operator=(const Session&) = delete;

            DWORD Handle() const noexcept
            {
                return m_handle;
            }

        private:
            std::shared_ptr<details::IRestartManagerApi> m_api;
            DWORD m_handle = 0;
            bool m_started = false;
        };

        std::uint64_t FileTimeToUInt64(const FILETIME& value) noexcept
        {
            ULARGE_INTEGER result{};
            result.LowPart = value.dwLowDateTime;
            result.HighPart = value.dwHighDateTime;
            return result.QuadPart;
        }

        bool HasMatchingProcessStartTime(DWORD processId, std::uint64_t expectedStartTime)
        {
            wil::unique_handle process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId) };
            if (!process)
            {
                return false;
            }

            FILETIME creationTime{};
            FILETIME exitTime{};
            FILETIME kernelTime{};
            FILETIME userTime{};
            return GetProcessTimes(process.get(), &creationTime, &exitTime, &kernelTime, &userTime) &&
                FileTimeToUInt64(creationTime) == expectedStartTime;
        }

        struct WindowEnumerationContext
        {
            DWORD ProcessId;
            std::vector<std::wstring>* Titles;
        };

        BOOL CALLBACK EnumerateProcessWindow(HWND window, LPARAM parameter)
        {
            auto* context = reinterpret_cast<WindowEnumerationContext*>(parameter);
            if (context->Titles->size() >= MaximumWindowTitles)
            {
                return TRUE;
            }

            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId != context->ProcessId || !IsWindowVisible(window))
            {
                return TRUE;
            }

            const int titleLength = std::min(GetWindowTextLengthW(window), MaximumWindowTitleLength);
            if (titleLength <= 0)
            {
                return TRUE;
            }

            std::wstring title(static_cast<std::size_t>(titleLength) + 1, L'\0');
            const int copied = GetWindowTextW(window, title.data(), titleLength + 1);
            if (copied > 0)
            {
                title.resize(copied);
                context->Titles->emplace_back(std::move(title));
            }

            return TRUE;
        }

        void EnrichWindowTitles(Blocker& blocker)
        {
            if (blocker.ProcessId == 0 || blocker.ProcessStartTime == 0)
            {
                return;
            }

            DWORD currentSessionId = 0;
            if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId) ||
                blocker.SessionId != currentSessionId ||
                !HasMatchingProcessStartTime(blocker.ProcessId, blocker.ProcessStartTime))
            {
                return;
            }

            WindowEnumerationContext context{ blocker.ProcessId, &blocker.WindowTitles };
            EnumWindows(EnumerateProcessWindow, reinterpret_cast<LPARAM>(&context));

            if (!HasMatchingProcessStartTime(blocker.ProcessId, blocker.ProcessStartTime))
            {
                blocker.WindowTitles.clear();
            }
        }

        Blocker ConvertBlocker(const RM_PROCESS_INFO& process)
        {
            Blocker result;
            result.ProcessId = process.Process.dwProcessId;
            result.ProcessStartTime = FileTimeToUInt64(process.Process.ProcessStartTime);
            result.ApplicationName = process.strAppName;
            result.ServiceName = process.strServiceShortName;
            result.Type = static_cast<ApplicationType>(process.ApplicationType);
            result.Status = static_cast<ApplicationStatus>(process.AppStatus);
            result.SessionId = process.TSSessionId;
            result.IsRestartable = process.bRestartable != FALSE;
            result.IsCritical = process.ApplicationType == RmCritical;
            EnrichWindowTitles(result);
            return result;
        }

        void RegisterResources(
            details::IRestartManagerApi& api,
            DWORD sessionHandle,
            const ResourceSet& resources)
        {
            std::vector<LPCWSTR> files;
            files.reserve(resources.Files.size());
            for (const auto& file : resources.Files)
            {
                files.emplace_back(file.c_str());
            }

            std::vector<LPCWSTR> services;
            services.reserve(resources.ServiceNames.size());
            for (const auto& service : resources.ServiceNames)
            {
                services.emplace_back(service.c_str());
            }

            ThrowOnError(api.RegisterResources(
                sessionHandle,
                static_cast<UINT>(files.size()),
                files.empty() ? nullptr : files.data(),
                0,
                nullptr,
                static_cast<UINT>(services.size()),
                services.empty() ? nullptr : services.data()));
        }

        ProbeResult GetAffectedApplications(details::IRestartManagerApi& api, DWORD sessionHandle)
        {
            UINT processInfoNeeded = 0;
            UINT processInfoCount = 0;
            DWORD rebootReasons = RmRebootReasonNone;
            DWORD result = api.GetList(sessionHandle, &processInfoNeeded, &processInfoCount, nullptr, &rebootReasons);

            for (std::size_t attempt = 0; attempt < MaximumGetListAttempts; ++attempt)
            {
                if (result == ERROR_SUCCESS && processInfoNeeded == 0)
                {
                    return { {}, static_cast<RebootReason>(rebootReasons) };
                }

                if (result != ERROR_MORE_DATA && result != ERROR_SUCCESS)
                {
                    ThrowOnError(result);
                }

                THROW_WIN32_IF(ERROR_BUFFER_OVERFLOW, processInfoNeeded > MaximumBlockers);

                std::vector<RM_PROCESS_INFO> affectedApplications(processInfoNeeded);
                processInfoCount = processInfoNeeded;
                result = api.GetList(
                    sessionHandle,
                    &processInfoNeeded,
                    &processInfoCount,
                    affectedApplications.data(),
                    &rebootReasons);

                if (result == ERROR_SUCCESS)
                {
                    THROW_WIN32_IF(
                        ERROR_INVALID_DATA,
                        processInfoCount > static_cast<UINT>(affectedApplications.size()));
                    affectedApplications.resize(processInfoCount);

                    ProbeResult probeResult;
                    probeResult.RebootReasons = static_cast<RebootReason>(rebootReasons);
                    probeResult.Blockers.reserve(affectedApplications.size());
                    for (const auto& process : affectedApplications)
                    {
                        probeResult.Blockers.emplace_back(ConvertBlocker(process));
                    }

                    return probeResult;
                }
            }

            THROW_WIN32(ERROR_RETRY);
        }
    }

    struct Coordinator::Impl
    {
        explicit Impl(std::shared_ptr<details::IRestartManagerApi> api) : Api(std::move(api))
        {
            THROW_HR_IF(E_INVALIDARG, !Api);
        }

        std::shared_ptr<details::IRestartManagerApi> Api;
    };

    Coordinator::Coordinator() : m_impl(std::make_unique<Impl>(std::make_shared<RestartManagerApi>()))
    {
    }

#ifndef AICLI_DISABLE_TEST_HOOKS
    Coordinator::Coordinator(std::shared_ptr<details::IRestartManagerApi> api) :
        m_impl(std::make_unique<Impl>(std::move(api)))
    {
    }
#endif

    Coordinator::~Coordinator() = default;
    Coordinator::Coordinator(Coordinator&&) noexcept = default;
    Coordinator& Coordinator::operator=(Coordinator&&) noexcept = default;

    ProbeResult Coordinator::Probe(const ResourceSet& resources) const
    {
        THROW_HR_IF(E_UNEXPECTED, !m_impl);
        ResourceSet normalizedResources = NormalizeAndDeduplicateResources(resources);
        ValidateResources(normalizedResources);

        Session session{ m_impl->Api };
        RegisterResources(*m_impl->Api, session.Handle(), normalizedResources);
        return GetAffectedApplications(*m_impl->Api, session.Handle());
    }

    ShutdownResult Coordinator::Shutdown(const ResourceSet& resources, ShutdownType type) const
    {
        THROW_HR_IF(E_UNEXPECTED, !m_impl);
        ResourceSet normalizedResources = NormalizeAndDeduplicateResources(resources);
        ValidateResources(normalizedResources);

        ShutdownResult result;
        {
            Session shutdownSession{ m_impl->Api };
            RegisterResources(*m_impl->Api, shutdownSession.Handle(), normalizedResources);

            result.Initial = GetAffectedApplications(*m_impl->Api, shutdownSession.Handle());
            if (!result.Initial.IsInUse())
            {
                result.Final = result.Initial;
                return result;
            }

            const DWORD currentProcessId = GetCurrentProcessId();
            if (std::any_of(
                    result.Initial.Blockers.begin(),
                    result.Initial.Blockers.end(),
                    [currentProcessId](const Blocker& blocker) { return blocker.ProcessId == currentProcessId; }))
            {
                result.Status = ShutdownStatus::BlockedByCurrentProcess;
                result.Final = result.Initial;
                return result;
            }

            if (std::any_of(
                    result.Initial.Blockers.begin(),
                    result.Initial.Blockers.end(),
                    [](const Blocker& blocker) { return blocker.IsCritical; }))
            {
                result.Status = ShutdownStatus::BlockedByCriticalApplication;
                result.Final = result.Initial;
                return result;
            }

            const ULONG flags = type == ShutdownType::Force ? RmForceShutdown : 0;
            result.ShutdownAttempted = true;
            result.ShutdownError = m_impl->Api->Shutdown(shutdownSession.Handle(), flags, nullptr);
        }

        // The shutdown session retains affected applications and updates their AppStatus.
        // Use a new session to determine which processes still hold the resources.
        Session verificationSession{ m_impl->Api };
        RegisterResources(*m_impl->Api, verificationSession.Handle(), normalizedResources);
        result.Final = GetAffectedApplications(*m_impl->Api, verificationSession.Handle());

        if (result.ShutdownError != ERROR_SUCCESS)
        {
            result.Status = ShutdownStatus::Failed;
        }
        else if (result.Final.IsInUse())
        {
            result.Status = ShutdownStatus::Incomplete;
        }
        else
        {
            result.Status = ShutdownStatus::Completed;
        }

        return result;
    }
}

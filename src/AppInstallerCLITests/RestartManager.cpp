// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "TestCommon.h"
#include <winget/RestartManager.h>
#include "RestartManagerImpl.h"

using namespace AppInstaller::RestartManager;

namespace
{
    struct GetListResponse
    {
        DWORD Result = ERROR_SUCCESS;
        std::vector<RM_PROCESS_INFO> Processes;
        DWORD RebootReasons = RmRebootReasonNone;
        UINT NeededOverride = 0;
    };

    struct TestRestartManagerApi : public details::IRestartManagerApi
    {
        DWORD StartSession(DWORD* sessionHandle, DWORD, WCHAR*) noexcept override
        {
            ++StartCalls;
            if (StartResult == ERROR_SUCCESS)
            {
                *sessionHandle = SessionHandle;
            }
            return StartResult;
        }

        DWORD EndSession(DWORD sessionHandle) noexcept override
        {
            ++EndCalls;
            LastEndedHandle = sessionHandle;
            return EndResult;
        }

        DWORD RegisterResources(
            DWORD sessionHandle,
            UINT fileCount,
            LPCWSTR* fileNames,
            UINT,
            RM_UNIQUE_PROCESS*,
            UINT serviceCount,
            LPCWSTR* serviceNames) noexcept override
        {
            ++RegisterCalls;
            LastRegisteredHandle = sessionHandle;
            for (UINT i = 0; i < fileCount; ++i)
            {
                RegisteredFiles.emplace_back(fileNames[i]);
            }
            for (UINT i = 0; i < serviceCount; ++i)
            {
                RegisteredServices.emplace_back(serviceNames[i]);
            }
            return RegisterResult;
        }

        DWORD GetList(
            DWORD,
            UINT* processInfoNeeded,
            UINT* processInfoCount,
            RM_PROCESS_INFO* affectedApplications,
            DWORD* rebootReasons) noexcept override
        {
            ++GetListCalls;
            if (GetListResponses.empty())
            {
                return ERROR_INVALID_STATE;
            }

            const GetListResponse response = GetListResponses.front();
            GetListResponses.erase(GetListResponses.begin());
            *processInfoNeeded = response.NeededOverride != 0 ?
                response.NeededOverride : static_cast<UINT>(response.Processes.size());
            *rebootReasons = response.RebootReasons;

            if (response.Result == ERROR_SUCCESS && affectedApplications)
            {
                const UINT copyCount = std::min(*processInfoCount, static_cast<UINT>(response.Processes.size()));
                std::copy_n(response.Processes.begin(), copyCount, affectedApplications);
                *processInfoCount = copyCount;
            }

            return response.Result;
        }

        DWORD Shutdown(DWORD sessionHandle, ULONG actionFlags, RM_WRITE_STATUS_CALLBACK) noexcept override
        {
            ++ShutdownCalls;
            LastShutdownHandle = sessionHandle;
            LastShutdownFlags = actionFlags;
            return ShutdownResult;
        }

        DWORD StartResult = ERROR_SUCCESS;
        DWORD EndResult = ERROR_SUCCESS;
        DWORD RegisterResult = ERROR_SUCCESS;
        DWORD ShutdownResult = ERROR_SUCCESS;
        DWORD SessionHandle = 42;
        DWORD LastEndedHandle = 0;
        DWORD LastRegisteredHandle = 0;
        DWORD LastShutdownHandle = 0;
        ULONG LastShutdownFlags = 0;
        std::size_t StartCalls = 0;
        std::size_t EndCalls = 0;
        std::size_t RegisterCalls = 0;
        std::size_t GetListCalls = 0;
        std::size_t ShutdownCalls = 0;
        std::vector<std::wstring> RegisteredFiles;
        std::vector<std::wstring> RegisteredServices;
        std::vector<GetListResponse> GetListResponses;
    };

    RM_PROCESS_INFO CreateProcessInfo(
        DWORD processId,
        std::uint64_t processStartTime,
        RM_APP_TYPE type,
        DWORD status,
        DWORD sessionId,
        bool restartable,
        const std::wstring& applicationName,
        const std::wstring& serviceName = {})
    {
        RM_PROCESS_INFO result{};
        result.Process.dwProcessId = processId;
        ULARGE_INTEGER startTime{};
        startTime.QuadPart = processStartTime;
        result.Process.ProcessStartTime.dwLowDateTime = startTime.LowPart;
        result.Process.ProcessStartTime.dwHighDateTime = startTime.HighPart;
        result.ApplicationType = type;
        result.AppStatus = status;
        result.TSSessionId = sessionId;
        result.bRestartable = restartable ? TRUE : FALSE;
        wcsncpy_s(result.strAppName, applicationName.c_str(), _TRUNCATE);
        wcsncpy_s(result.strServiceShortName, serviceName.c_str(), _TRUNCATE);
        return result;
    }

    ResourceSet CreateResources()
    {
        ResourceSet result;
        result.Files.emplace_back(L"C:\\Program Files\\Contoso\\Editor.exe");
        result.ServiceNames.emplace_back(L"ContosoUpdater");
        return result;
    }
}

TEST_CASE("RestartManager_RejectsEmptyResources", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    Coordinator coordinator{ api };

    REQUIRE_THROWS_HR(coordinator.Probe({}), E_INVALIDARG);
    REQUIRE(api->StartCalls == 0);
}

TEST_CASE("RestartManager_RejectsInvalidResources", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    Coordinator coordinator{ api };

    ResourceSet relativeFile;
    relativeFile.Files.emplace_back(L"Contoso\\Editor.exe");
    REQUIRE_THROWS_HR(coordinator.Probe(relativeFile), E_INVALIDARG);

    ResourceSet emptyService;
    emptyService.ServiceNames.emplace_back();
    REQUIRE_THROWS_HR(coordinator.Probe(emptyService), E_INVALIDARG);
    REQUIRE(api->StartCalls == 0);
}

TEST_CASE("RestartManager_PropagatesStartSessionFailure", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    api->StartResult = ERROR_ACCESS_DENIED;
    Coordinator coordinator{ api };

    REQUIRE_THROWS_HR(coordinator.Probe(CreateResources()), HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(api->StartCalls == 1);
    REQUIRE(api->EndCalls == 0);
}

TEST_CASE("RestartManager_ProbeMapsBlockers", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(
        1234,
        0x1020304050607080,
        RmCritical,
        RmStatusRunning,
        7,
        true,
        L"Contoso Editor",
        L"ContosoUpdater");

    api->GetListResponses = {
        { ERROR_MORE_DATA, { process }, RmRebootReasonPermissionDenied },
        { ERROR_SUCCESS, { process }, RmRebootReasonPermissionDenied },
    };

    Coordinator coordinator{ api };
    ProbeResult result = coordinator.Probe(CreateResources());

    REQUIRE(result.IsInUse());
    REQUIRE(result.RebootReasons == RebootReason::PermissionDenied);
    REQUIRE(result.Blockers.size() == 1);
    REQUIRE(result.Blockers[0].ProcessId == 1234);
    REQUIRE(result.Blockers[0].ProcessStartTime == 0x1020304050607080);
    REQUIRE(result.Blockers[0].ApplicationName == L"Contoso Editor");
    REQUIRE(result.Blockers[0].ServiceName == L"ContosoUpdater");
    REQUIRE(result.Blockers[0].Type == AppInstaller::RestartManager::ApplicationType::Critical);
    REQUIRE(result.Blockers[0].Status == AppInstaller::RestartManager::ApplicationStatus::Running);
    REQUIRE(result.Blockers[0].SessionId == 7);
    REQUIRE(result.Blockers[0].IsRestartable);
    REQUIRE(result.Blockers[0].IsCritical);
    REQUIRE(api->RegisteredFiles == std::vector<std::wstring>{ L"C:\\Program Files\\Contoso\\Editor.exe" });
    REQUIRE(api->RegisteredServices == std::vector<std::wstring>{ L"ContosoUpdater" });
    REQUIRE(api->EndCalls == 1);
    REQUIRE(api->LastEndedHandle == api->SessionHandle);
}

TEST_CASE("RestartManager_ProbeRetriesChangingList", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO first = CreateProcessInfo(1, 1, RmMainWindow, RmStatusRunning, 1, true, L"First");
    RM_PROCESS_INFO second = CreateProcessInfo(2, 2, RmConsole, RmStatusRunning, 1, false, L"Second");

    api->GetListResponses = {
        { ERROR_MORE_DATA, { first }, RmRebootReasonNone },
        { ERROR_MORE_DATA, { first, second }, RmRebootReasonNone },
        { ERROR_SUCCESS, { first, second }, RmRebootReasonNone },
    };

    Coordinator coordinator{ api };
    ProbeResult result = coordinator.Probe(CreateResources());

    REQUIRE(result.Blockers.size() == 2);
    REQUIRE(api->GetListCalls == 3);
}

TEST_CASE("RestartManager_PropagatesGetListFailure", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    api->GetListResponses = {
        { ERROR_ACCESS_DENIED, {}, RmRebootReasonNone },
    };

    Coordinator coordinator{ api };
    REQUIRE_THROWS_HR(coordinator.Probe(CreateResources()), HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(api->EndCalls == 1);
}

TEST_CASE("RestartManager_BoundsGetListRetries", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(1234, 1, RmMainWindow, RmStatusRunning, 1, false, L"Contoso");
    api->GetListResponses.resize(9, { ERROR_MORE_DATA, { process }, RmRebootReasonNone });

    Coordinator coordinator{ api };
    REQUIRE_THROWS_HR(coordinator.Probe(CreateResources()), HRESULT_FROM_WIN32(ERROR_RETRY));
    REQUIRE(api->GetListCalls == 9);
    REQUIRE(api->EndCalls == 1);
}

TEST_CASE("RestartManager_DeduplicatesResourcesCaseInsensitively", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    api->GetListResponses = {
        { ERROR_SUCCESS, {}, RmRebootReasonNone },
    };

    ResourceSet resources = CreateResources();
    resources.Files.emplace_back(L"c:\\program files\\contoso\\EDITOR.exe");
    resources.ServiceNames.emplace_back(L"contosoupdater");

    Coordinator coordinator{ api };
    ProbeResult result = coordinator.Probe(resources);

    REQUIRE_FALSE(result.IsInUse());
    REQUIRE(api->RegisteredFiles.size() == 1);
    REQUIRE(api->RegisteredServices.size() == 1);
}

TEST_CASE("RestartManager_GracefulShutdownReprobes", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(1234, 1, RmMainWindow, RmStatusRunning, 1, true, L"Contoso Editor");
    api->GetListResponses = {
        { ERROR_MORE_DATA, { process }, RmRebootReasonNone },
        { ERROR_SUCCESS, { process }, RmRebootReasonNone },
        { ERROR_SUCCESS, {}, RmRebootReasonNone },
    };

    Coordinator coordinator{ api };
    ShutdownResult result = coordinator.Shutdown(
        CreateResources(), AppInstaller::RestartManager::ShutdownType::Graceful);

    REQUIRE(result.Initial.IsInUse());
    REQUIRE_FALSE(result.Final.IsInUse());
    REQUIRE(result.ShutdownAttempted);
    REQUIRE(result.Status == ShutdownStatus::Completed);
    REQUIRE(result.ShutdownError == ERROR_SUCCESS);
    REQUIRE(api->StartCalls == 2);
    REQUIRE(api->ShutdownCalls == 1);
    REQUIRE(api->LastShutdownFlags == 0);
    REQUIRE(api->EndCalls == 2);
}

TEST_CASE("RestartManager_ForceShutdownUsesForceFlagAndReturnsSurvivors", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(1234, 1, RmMainWindow, RmStatusRunning, 1, false, L"Contoso Editor");
    api->GetListResponses = {
        { ERROR_MORE_DATA, { process }, RmRebootReasonNone },
        { ERROR_SUCCESS, { process }, RmRebootReasonNone },
        { ERROR_MORE_DATA, { process }, RmRebootReasonNone },
        { ERROR_SUCCESS, { process }, RmRebootReasonNone },
    };

    Coordinator coordinator{ api };
    ShutdownResult result = coordinator.Shutdown(
        CreateResources(), AppInstaller::RestartManager::ShutdownType::Force);

    REQUIRE(result.Initial.IsInUse());
    REQUIRE(result.Final.IsInUse());
    REQUIRE(result.ShutdownAttempted);
    REQUIRE(result.Status == ShutdownStatus::Incomplete);
    REQUIRE(api->StartCalls == 2);
    REQUIRE(api->ShutdownCalls == 1);
    REQUIRE(api->LastShutdownFlags == RmForceShutdown);
    REQUIRE(api->EndCalls == 2);
}

TEST_CASE("RestartManager_ShutdownFailureReturnsSurvivors", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(1234, 1, RmMainWindow, RmStatusRunning, 1, false, L"Contoso Editor");
    api->ShutdownResult = ERROR_FAIL_SHUTDOWN;
    api->GetListResponses = {
        { ERROR_MORE_DATA, { process }, RmRebootReasonNone },
        { ERROR_SUCCESS, { process }, RmRebootReasonNone },
        { ERROR_MORE_DATA, { process }, RmRebootReasonNone },
        { ERROR_SUCCESS, { process }, RmRebootReasonNone },
    };

    Coordinator coordinator{ api };
    ShutdownResult result = coordinator.Shutdown(
        CreateResources(), AppInstaller::RestartManager::ShutdownType::Graceful);

    REQUIRE(result.ShutdownAttempted);
    REQUIRE(result.Status == ShutdownStatus::Failed);
    REQUIRE(result.ShutdownError == ERROR_FAIL_SHUTDOWN);
    REQUIRE(result.Final.IsInUse());
    REQUIRE(api->StartCalls == 2);
    REQUIRE(api->EndCalls == 2);
}

TEST_CASE("RestartManager_DoesNotShutdownCriticalApplication", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(1234, 1, RmCritical, RmStatusRunning, 1, false, L"Critical");
    api->GetListResponses = {
        { ERROR_MORE_DATA, { process }, RmRebootReasonCriticalProcess },
        { ERROR_SUCCESS, { process }, RmRebootReasonCriticalProcess },
    };

    Coordinator coordinator{ api };
    ShutdownResult result = coordinator.Shutdown(
        CreateResources(), AppInstaller::RestartManager::ShutdownType::Force);

    REQUIRE_FALSE(result.ShutdownAttempted);
    REQUIRE(result.Status == ShutdownStatus::BlockedByCriticalApplication);
    REQUIRE(result.Final.IsInUse());
    REQUIRE(api->ShutdownCalls == 0);
}

TEST_CASE("RestartManager_DoesNotShutdownCurrentProcess", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    RM_PROCESS_INFO process = CreateProcessInfo(
        GetCurrentProcessId(), 1, RmMainWindow, RmStatusRunning, 1, false, L"WinGet");
    api->GetListResponses = {
        { ERROR_MORE_DATA, { process }, RmRebootReasonDetectedSelf },
        { ERROR_SUCCESS, { process }, RmRebootReasonDetectedSelf },
    };

    Coordinator coordinator{ api };
    ShutdownResult result = coordinator.Shutdown(
        CreateResources(), AppInstaller::RestartManager::ShutdownType::Force);

    REQUIRE_FALSE(result.ShutdownAttempted);
    REQUIRE(result.Status == ShutdownStatus::BlockedByCurrentProcess);
    REQUIRE(result.Final.IsInUse());
    REQUIRE(api->ShutdownCalls == 0);
}

TEST_CASE("RestartManager_EndsSessionWhenRegistrationFails", "[restartManager]")
{
    auto api = std::make_shared<TestRestartManagerApi>();
    api->RegisterResult = ERROR_ACCESS_DENIED;
    Coordinator coordinator{ api };

    REQUIRE_THROWS_HR(coordinator.Probe(CreateResources()), HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(api->EndCalls == 1);
}

TEST_CASE("RestartManager_DetectsLockedFile", "[restartManager][integration]")
{
    TestCommon::TempFile lockedFile{ "RestartManagerLockedFile.txt" };
    {
        std::ofstream stream{ lockedFile.GetPath() };
        stream << "locked";
    }

    wil::unique_hfile fileHandle{ CreateFileW(
        lockedFile.GetPath().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr) };
    REQUIRE(fileHandle);

    ResourceSet resources;
    resources.Files.emplace_back(lockedFile.GetPath());

    Coordinator coordinator;
    ProbeResult result = coordinator.Probe(resources);

    REQUIRE(result.IsInUse());
    REQUIRE(std::any_of(
        result.Blockers.begin(),
        result.Blockers.end(),
        [](const Blocker& blocker) { return blocker.ProcessId == GetCurrentProcessId(); }));
}

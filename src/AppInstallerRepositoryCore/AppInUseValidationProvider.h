// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "Public/winget/RepositorySource.h"

#ifndef AICLI_DISABLE_TEST_HOOKS

namespace AppInstaller::Repository::AppInUseValidation
{
    inline constexpr std::wstring_view FixtureRoot = L"C:\\TEMP\\WinGET\\EM";

    bool IsSourceEligible(const SourceDetails& details);

    AppInUseCapability GetCapability(const SourceDetails& details, AppInUseOperation operation);

    AppInUseResult GetMetadata(
        const SourceDetails& details,
        const AppInUseBinding& binding,
        const std::filesystem::path& fixtureRoot = FixtureRoot);
}

#endif

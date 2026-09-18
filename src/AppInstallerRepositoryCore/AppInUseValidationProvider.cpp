// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "AppInUseValidationProvider.h"

#ifndef AICLI_DISABLE_TEST_HOOKS

#include "Microsoft/PreIndexedPackageSourceFactory.h"
#include "Rest/Schema/1_29/Interface.h"
#include "SourceList.h"
#include <AppInstallerStrings.h>
#include <winget/JsonUtil.h>

namespace AppInstaller::Repository::AppInUseValidation
{
    namespace
    {
        constexpr size_t MaximumFixtureBytes = 1024 * 1024;
        constexpr size_t MaximumPackageIdentifierLength = 255;

        bool IsReservedDeviceName(std::string_view packageIdentifier)
        {
            const size_t firstDot = packageIdentifier.find('.');
            std::string name = Utility::ToLower(packageIdentifier.substr(0, firstDot));

            if (name == "con" || name == "prn" || name == "aux" || name == "nul" || name == "clock$")
            {
                return true;
            }

            return name.size() == 4 &&
                (name.compare(0, 3, "com") == 0 || name.compare(0, 3, "lpt") == 0) &&
                name[3] >= '1' && name[3] <= '9';
        }

        std::filesystem::path GetFixturePath(
            const std::filesystem::path& fixtureRoot,
            std::string_view packageIdentifier)
        {
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                packageIdentifier.empty() ||
                packageIdentifier.size() > MaximumPackageIdentifierLength ||
                packageIdentifier.find("..") != std::string_view::npos ||
                IsReservedDeviceName(packageIdentifier) ||
                !std::all_of(packageIdentifier.begin(), packageIdentifier.end(), [](char c)
                    {
                        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_';
                    }));

            std::filesystem::path normalizedRoot = fixtureRoot.lexically_normal();
            std::filesystem::path result =
                (normalizedRoot / (Utility::ConvertToUTF16(packageIdentifier) + L".json")).lexically_normal();
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, result.parent_path() != normalizedRoot);
            return result;
        }
    }

    bool IsSourceEligible(const SourceDetails& details)
    {
        return details.Origin == SourceOrigin::Default &&
            details.Name == GetWellKnownSourceName(WellKnownSource::WinGet) &&
            details.Arg == GetWellKnownSourceArg(WellKnownSource::WinGet) &&
            details.Data == GetWellKnownSourceIdentifier(WellKnownSource::WinGet) &&
            details.Identifier == GetWellKnownSourceIdentifier(WellKnownSource::WinGet) &&
            details.Type == Microsoft::PreIndexedPackageSourceFactory::Type();
    }

    AppInUseCapability GetCapability(const SourceDetails& details, AppInUseOperation)
    {
        AppInUseCapability result;
        result.SourceEligible = IsSourceEligible(details);
        result.ContractSupported = result.SourceEligible;
        result.OfferingEnabled = result.SourceEligible;
        result.SchemaSupported = result.SourceEligible;
        result.OperationSupported = result.SourceEligible;
        return result;
    }

    AppInUseResult GetMetadata(
        const SourceDetails& details,
        const AppInUseBinding& binding,
        const std::filesystem::path& fixtureRoot)
    {
        AppInUseResult unavailable;
        unavailable.Binding = binding;

        if (!GetCapability(details, binding.Operation).IsAvailable())
        {
            return unavailable;
        }

        const std::filesystem::path fixturePath = GetFixturePath(fixtureRoot, binding.PackageIdentifier);
        std::error_code error;
        bool exists = std::filesystem::exists(fixturePath, error);
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED, error);
        if (!exists)
        {
            unavailable.UnavailableReason = AppInUseUnavailableReason::PackageNotFound;
            return unavailable;
        }

        uintmax_t size = std::filesystem::file_size(fixturePath, error);
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED, error);
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, size > MaximumFixtureBytes);

        std::ifstream stream{ fixturePath, std::ios_base::in | std::ios_base::binary };
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED, !stream);

        std::string contents(MaximumFixtureBytes + 1, '\0');
        stream.read(contents.data(), contents.size());
        contents.resize(static_cast<size_t>(stream.gcount()));
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED, stream.bad());
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, contents.size() > MaximumFixtureBytes);

        try
        {
            return Rest::Schema::V1_29::Interface::DeserializeAppInUseResponse(
                web::json::value::parse(JSON::GetUtilityString(contents)),
                binding);
        }
        catch (const wil::ResultException&)
        {
            throw;
        }
        catch (...)
        {
            THROW_HR(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
        }
    }
}

#endif

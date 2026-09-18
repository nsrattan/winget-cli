// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "Rest/Schema/1_29/Interface.h"
#include "Rest/Schema/CommonRestConstants.h"
#include <winget/HttpClientHelper.h>
#include <winget/JsonUtil.h>
#include <winget/Rest.h>

namespace AppInstaller::Repository::Rest::Schema::V1_29
{
    namespace
    {
        constexpr std::string_view Status = "Status"sv;
        constexpr std::string_view Available = "Available"sv;
        constexpr std::string_view Unavailable = "Unavailable"sv;
        constexpr std::string_view Error = "Error"sv;
        constexpr std::string_view Binding = "Binding"sv;
        constexpr std::string_view PackageIdentifier = "PackageIdentifier"sv;
        constexpr std::string_view Operation = "Operation"sv;
        constexpr std::string_view CurrentVersion = "CurrentVersion"sv;
        constexpr std::string_view CurrentChannel = "CurrentChannel"sv;
        constexpr std::string_view CurrentScope = "CurrentScope"sv;
        constexpr std::string_view TargetVersion = "TargetVersion"sv;
        constexpr std::string_view TargetChannel = "TargetChannel"sv;
        constexpr std::string_view SelectedInstaller = "SelectedInstaller"sv;
        constexpr std::string_view Sha256 = "Sha256"sv;
        constexpr std::string_view FileName = "FileName"sv;
        constexpr std::string_view Architecture = "Architecture"sv;
        constexpr std::string_view EffectiveInstallerType = "EffectiveInstallerType"sv;
        constexpr std::string_view Scope = "Scope"sv;
        constexpr std::string_view Locale = "Locale"sv;
        constexpr std::string_view UnavailableReason = "UnavailableReason"sv;
        constexpr std::string_view GeneratedAt = "GeneratedAt"sv;
        constexpr std::string_view ValidUntil = "ValidUntil"sv;
        constexpr std::string_view Generation = "Generation"sv;
        constexpr std::string_view Resources = "Resources"sv;
        constexpr std::string_view Files = "Files"sv;
        constexpr std::string_view Services = "Services"sv;
        constexpr std::string_view Path = "Path"sv;
        constexpr std::string_view Kind = "Kind"sv;
        constexpr std::string_view Name = "Name"sv;
        constexpr std::string_view Errors = "Errors"sv;
        constexpr std::string_view Code = "Code"sv;
        constexpr std::string_view Message = "Message"sv;

        constexpr size_t MaximumResponseBytes = 1024 * 1024;
        constexpr size_t MaximumPackageIdentifierLength = 255;
        constexpr size_t MaximumVersionLength = 128;
        constexpr size_t MaximumChannelLength = 128;
        constexpr size_t MaximumScopeLength = 32;
        constexpr size_t MaximumFileNameLength = 260;
        constexpr size_t MaximumArchitectureLength = 32;
        constexpr size_t MaximumInstallerTypeLength = 64;
        constexpr size_t MaximumLocaleLength = 85;
        constexpr size_t MaximumFileCount = 2048;
        constexpr size_t MaximumFilePathLength = 32767;
        constexpr size_t MaximumFileKindLength = 64;
        constexpr size_t MaximumServiceCount = 256;
        constexpr size_t MaximumServiceNameLength = 256;
        constexpr size_t MaximumErrorCount = 16;
        constexpr size_t MaximumErrorCodeLength = 128;
        constexpr size_t MaximumErrorMessageLength = 4096;
        constexpr uint64_t DateTimeTicksPerSecond = 10000000;
        constexpr uint64_t MaximumValidityTicks = 60 * 60 * DateTimeTicksPerSecond;
        constexpr uint64_t ClockSkewAllowanceTicks = 5 * 60 * DateTimeTicksPerSecond;

        bool IsValidRequiredString(const std::optional<std::string>& value, size_t maximumLength)
        {
            return value && !value->empty() && value->size() <= maximumLength;
        }

        bool IsValidOptionalString(const std::optional<std::string>& value, size_t maximumLength)
        {
            return !value || (!value->empty() && value->size() <= maximumLength);
        }

        std::string_view OperationToString(AppInUseOperation operation)
        {
            switch (operation)
            {
            case AppInUseOperation::Install: return "Install"sv;
            case AppInUseOperation::Upgrade: return "Upgrade"sv;
            case AppInUseOperation::Repair: return "Repair"sv;
            case AppInUseOperation::Uninstall: return "Uninstall"sv;
            }

            THROW_HR(E_INVALIDARG);
        }

        std::optional<AppInUseOperation> ConvertToOperation(std::string_view value)
        {
            if (value == "Install") return AppInUseOperation::Install;
            if (value == "Upgrade") return AppInUseOperation::Upgrade;
            if (value == "Repair") return AppInUseOperation::Repair;
            if (value == "Uninstall") return AppInUseOperation::Uninstall;
            return {};
        }

        void ValidateSelectedInstaller(const AppInUseSelectedInstaller& installer)
        {
            THROW_HR_IF(E_INVALIDARG, installer.Sha256.size() != 64 ||
                !std::all_of(installer.Sha256.begin(), installer.Sha256.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }));
            THROW_HR_IF(E_INVALIDARG, installer.FileName.empty() || installer.FileName.size() > MaximumFileNameLength ||
                installer.FileName.find_first_of("\\/") != std::string::npos);
            THROW_HR_IF(E_INVALIDARG, installer.Architecture.empty() || installer.Architecture.size() > MaximumArchitectureLength);
            THROW_HR_IF(E_INVALIDARG, installer.EffectiveInstallerType.empty() || installer.EffectiveInstallerType.size() > MaximumInstallerTypeLength);
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(installer.Scope, MaximumScopeLength));
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(installer.Locale, MaximumLocaleLength));
        }

        void ValidateBinding(const AppInUseBinding& binding)
        {
            THROW_HR_IF(E_INVALIDARG, binding.PackageIdentifier.empty() || binding.PackageIdentifier.size() > MaximumPackageIdentifierLength);
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(binding.CurrentVersion, MaximumVersionLength));
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(binding.CurrentChannel, MaximumChannelLength));
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(binding.CurrentScope, MaximumScopeLength));
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(binding.TargetVersion, MaximumVersionLength));
            THROW_HR_IF(E_INVALIDARG, !IsValidOptionalString(binding.TargetChannel, MaximumChannelLength));
            THROW_HR_IF(E_INVALIDARG, !binding.CurrentVersion && (binding.CurrentChannel || binding.CurrentScope));
            THROW_HR_IF(E_INVALIDARG, !binding.TargetVersion && binding.TargetChannel);

            switch (binding.Operation)
            {
            case AppInUseOperation::Install:
                THROW_HR_IF(E_INVALIDARG, binding.CurrentVersion || !binding.TargetVersion || !binding.SelectedInstaller);
                break;
            case AppInUseOperation::Upgrade:
                THROW_HR_IF(E_INVALIDARG, !binding.CurrentVersion || !binding.TargetVersion || !binding.SelectedInstaller);
                break;
            case AppInUseOperation::Repair:
                THROW_HR_IF(E_INVALIDARG, !binding.CurrentVersion || binding.TargetVersion || !binding.SelectedInstaller);
                break;
            case AppInUseOperation::Uninstall:
                THROW_HR_IF(E_INVALIDARG, !binding.CurrentVersion || binding.TargetVersion || binding.SelectedInstaller);
                break;
            default:
                THROW_HR(E_INVALIDARG);
            }

            if (binding.SelectedInstaller)
            {
                ValidateSelectedInstaller(*binding.SelectedInstaller);
            }
        }

        void AddOptionalString(web::json::value& target, std::string_view name, const std::optional<std::string>& value)
        {
            if (value)
            {
                target[JSON::GetUtilityString(name)] = JSON::GetStringValue(*value);
            }
        }

        web::json::value SerializeSelectedInstaller(const AppInUseSelectedInstaller& installer)
        {
            web::json::value result = web::json::value::object();
            result[JSON::GetUtilityString(Sha256)] = JSON::GetStringValue(installer.Sha256);
            result[JSON::GetUtilityString(FileName)] = JSON::GetStringValue(installer.FileName);
            result[JSON::GetUtilityString(Architecture)] = JSON::GetStringValue(installer.Architecture);
            result[JSON::GetUtilityString(EffectiveInstallerType)] = JSON::GetStringValue(installer.EffectiveInstallerType);
            AddOptionalString(result, Scope, installer.Scope);
            AddOptionalString(result, Locale, installer.Locale);
            return result;
        }

        web::json::value SerializeBinding(const AppInUseBinding& binding)
        {
            web::json::value result = web::json::value::object();
            result[JSON::GetUtilityString(PackageIdentifier)] = JSON::GetStringValue(binding.PackageIdentifier);
            result[JSON::GetUtilityString(Operation)] = JSON::GetStringValue(OperationToString(binding.Operation));
            AddOptionalString(result, CurrentVersion, binding.CurrentVersion);
            AddOptionalString(result, CurrentChannel, binding.CurrentChannel);
            AddOptionalString(result, CurrentScope, binding.CurrentScope);
            AddOptionalString(result, TargetVersion, binding.TargetVersion);
            AddOptionalString(result, TargetChannel, binding.TargetChannel);
            if (binding.SelectedInstaller)
            {
                result[JSON::GetUtilityString(SelectedInstaller)] = SerializeSelectedInstaller(*binding.SelectedInstaller);
            }
            return result;
        }

        std::optional<std::string> GetOptionalStringStrict(const web::json::value& value, std::string_view name, size_t maximumLength)
        {
            const auto key = JSON::GetUtilityString(name);
            if (!value.has_field(key))
            {
                return {};
            }

            auto result = JSON::GetRawStringValueFromJsonNode(value, key);
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !IsValidRequiredString(result, maximumLength));
            return result;
        }

        AppInUseSelectedInstaller DeserializeSelectedInstaller(const web::json::value& value)
        {
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !value.is_object());

            auto sha256 = JSON::GetRawStringValueFromJsonNode(value, JSON::GetUtilityString(Sha256));
            auto fileName = JSON::GetRawStringValueFromJsonNode(value, JSON::GetUtilityString(FileName));
            auto architecture = JSON::GetRawStringValueFromJsonNode(value, JSON::GetUtilityString(Architecture));
            auto installerType = JSON::GetRawStringValueFromJsonNode(value, JSON::GetUtilityString(EffectiveInstallerType));
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                !sha256 || !fileName || !architecture || !installerType);

            AppInUseSelectedInstaller result{
                std::move(sha256).value(),
                std::move(fileName).value(),
                std::move(architecture).value(),
                std::move(installerType).value(),
                GetOptionalStringStrict(value, Scope, MaximumScopeLength),
                GetOptionalStringStrict(value, Locale, MaximumLocaleLength) };

            try
            {
                ValidateSelectedInstaller(result);
            }
            catch (...)
            {
                THROW_HR(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
            }
            return result;
        }

        AppInUseBinding DeserializeBinding(const web::json::value& value)
        {
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !value.is_object());

            auto packageIdentifier = JSON::GetRawStringValueFromJsonNode(value, JSON::GetUtilityString(PackageIdentifier));
            auto operationString = JSON::GetRawStringValueFromJsonNode(value, JSON::GetUtilityString(Operation));
            auto operation = operationString ? ConvertToOperation(*operationString) : std::nullopt;
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !packageIdentifier || !operation);

            AppInUseBinding result;
            result.PackageIdentifier = std::move(packageIdentifier).value();
            result.Operation = *operation;
            result.CurrentVersion = GetOptionalStringStrict(value, CurrentVersion, MaximumVersionLength);
            result.CurrentChannel = GetOptionalStringStrict(value, CurrentChannel, MaximumChannelLength);
            result.CurrentScope = GetOptionalStringStrict(value, CurrentScope, MaximumScopeLength);
            result.TargetVersion = GetOptionalStringStrict(value, TargetVersion, MaximumVersionLength);
            result.TargetChannel = GetOptionalStringStrict(value, TargetChannel, MaximumChannelLength);

            auto selectedInstaller = JSON::GetJsonValueFromNode(value, JSON::GetUtilityString(SelectedInstaller));
            if (selectedInstaller)
            {
                result.SelectedInstaller = DeserializeSelectedInstaller(selectedInstaller->get());
            }

            try
            {
                ValidateBinding(result);
            }
            catch (...)
            {
                THROW_HR(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
            }
            return result;
        }

        AppInUseUnavailableReason ConvertToUnavailableReason(std::string_view value)
        {
            if (value == "NotSupported") return AppInUseUnavailableReason::NotSupported;
            if (value == "PackageNotFound") return AppInUseUnavailableReason::PackageNotFound;
            if (value == "CurrentVersionNotFound") return AppInUseUnavailableReason::CurrentVersionNotFound;
            if (value == "TargetVersionNotFound") return AppInUseUnavailableReason::TargetVersionNotFound;
            if (value == "InstallerNotMatched") return AppInUseUnavailableReason::InstallerNotMatched;
            if (value == "TransitionNotAnalyzed") return AppInUseUnavailableReason::TransitionNotAnalyzed;
            if (value == "OperationNotSupportedForPackage") return AppInUseUnavailableReason::OperationNotSupportedForPackage;
            THROW_HR(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
        }

        void DeserializeResources(
            const web::json::value& data,
            std::vector<AppInUseFileResource>& filesResult,
            std::vector<AppInUseServiceResource>& servicesResult)
        {
            auto resources = JSON::GetJsonValueFromNode(data, JSON::GetUtilityString(Resources));
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !resources || !resources->get().is_object());

            auto files = JSON::GetRawJsonArrayFromJsonNode(resources->get(), JSON::GetUtilityString(Files));
            auto services = JSON::GetRawJsonArrayFromJsonNode(resources->get(), JSON::GetUtilityString(Services));
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                !files || !services || files->get().size() > MaximumFileCount || services->get().size() > MaximumServiceCount);

            std::set<std::wstring> seenFiles;
            for (const auto& fileValue : files->get())
            {
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !fileValue.is_object());
                auto pathString = JSON::GetRawStringValueFromJsonNode(fileValue, JSON::GetUtilityString(Path));
                auto kind = JSON::GetRawStringValueFromJsonNode(fileValue, JSON::GetUtilityString(Kind));
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                    !IsValidRequiredString(pathString, MaximumFilePathLength) || !IsValidRequiredString(kind, MaximumFileKindLength));

                std::filesystem::path path{ Utility::ConvertToUTF16(*pathString) };
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !path.is_absolute() || !path.has_filename());

                std::wstring normalized = Utility::ToLower(path.native());
                if (seenFiles.emplace(std::move(normalized)).second)
                {
                    filesResult.emplace_back(AppInUseFileResource{ std::move(path), std::move(kind).value() });
                }
            }

            std::set<std::string> seenServices;
            for (const auto& serviceValue : services->get())
            {
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !serviceValue.is_object());
                auto name = JSON::GetRawStringValueFromJsonNode(serviceValue, JSON::GetUtilityString(Name));
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                    !IsValidRequiredString(name, MaximumServiceNameLength) || name->find_first_of("\\/") != std::string::npos);

                if (seenServices.emplace(Utility::ToLower(*name)).second)
                {
                    servicesResult.emplace_back(AppInUseServiceResource{ std::move(name).value() });
                }
            }
        }

        std::vector<AppInUseError> DeserializeErrors(const web::json::value& data)
        {
            auto errors = JSON::GetRawJsonArrayFromJsonNode(data, JSON::GetUtilityString(Errors));
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                !errors || errors->get().size() == 0 || errors->get().size() > MaximumErrorCount);

            std::vector<AppInUseError> result;
            result.reserve(errors->get().size());
            for (const auto& errorValue : errors->get())
            {
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !errorValue.is_object());
                auto code = JSON::GetRawStringValueFromJsonNode(errorValue, JSON::GetUtilityString(Code));
                auto message = JSON::GetRawStringValueFromJsonNode(errorValue, JSON::GetUtilityString(Message));
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                    !IsValidRequiredString(code, MaximumErrorCodeLength) || !IsValidRequiredString(message, MaximumErrorMessageLength));
                result.emplace_back(AppInUseError{ std::move(code).value(), std::move(message).value() });
            }
            return result;
        }

        void ValidateValidityWindow(std::string_view generatedAtString, std::string_view validUntilString)
        {
            try
            {
                auto generatedAt = utility::datetime::from_string(JSON::GetUtilityString(generatedAtString), utility::datetime::ISO_8601);
                auto validUntil = utility::datetime::from_string(JSON::GetUtilityString(validUntilString), utility::datetime::ISO_8601);
                auto generatedTicks = generatedAt.to_interval();
                auto validUntilTicks = validUntil.to_interval();
                auto nowTicks = utility::datetime::utc_now().to_interval();
                THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                    validUntilTicks <= generatedTicks ||
                    validUntilTicks - generatedTicks > MaximumValidityTicks ||
                    (generatedTicks > nowTicks && generatedTicks - nowTicks > ClockSkewAllowanceTicks) ||
                    (validUntilTicks < nowTicks && nowTicks - validUntilTicks > ClockSkewAllowanceTicks));
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

    Interface::Interface(
        const std::string& restApi,
        const Http::HttpClientHelper& httpClientHelper,
        IRestClient::Information information,
        const Http::HttpClientHelper::HttpRequestHeaders& additionalHeaders,
        Authentication::AuthenticationArguments authArgs) :
        V1_28::Interface(restApi, httpClientHelper, std::move(information), additionalHeaders, std::move(authArgs)),
        m_appInUseEndpoint(AppInstaller::Rest::AppendPathToUri(JSON::GetUtilityString(restApi), JSON::GetUtilityString(AppInUseExtendedMetadataPostEndpoint))),
        m_httpClientHelper(httpClientHelper)
    {
        m_requiredRestApiHeaders[JSON::GetUtilityString(ContractVersion)] = JSON::GetUtilityString(Version_1_29_0.ToString());
    }

    Utility::Version Interface::GetVersion() const
    {
        return Version_1_29_0;
    }

    bool Interface::IsOperationSupported(AppInUseOperation) const
    {
        return true;
    }

    AppInUseResult Interface::GetAppInUseMetadata(const AppInUseBinding& binding) const
    {
        ValidateBinding(binding);

        web::json::value requestBody = web::json::value::object();
        requestBody[JSON::GetUtilityString(Binding)] = SerializeBinding(binding);

        auto response = m_httpClientHelper.HandlePost(
            m_appInUseEndpoint,
            requestBody,
            m_requiredRestApiHeaders,
            GetAuthHeaders(),
            [](const web::http::http_response& httpResponse)
            {
                if (httpResponse.headers().has(web::http::header_names::content_length))
                {
                    THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, httpResponse.headers().content_length() > MaximumResponseBytes);
                }
                return Http::HttpClientHelper::HttpResponseHandlerResult{ std::nullopt, true };
            });
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED, !response);
        return DeserializeAppInUseResponse(response.value(), binding);
    }

    AppInUseResult Interface::DeserializeAppInUseResponse(
        const web::json::value& response,
        const AppInUseBinding& binding)
    {
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
            utility::conversions::to_utf8string(response.serialize()).size() > MaximumResponseBytes);

        auto data = JSON::GetJsonValueFromNode(response, JSON::GetUtilityString(Data));
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !data || !data->get().is_object());

        auto status = JSON::GetRawStringValueFromJsonNode(data->get(), JSON::GetUtilityString(Status));
        auto responseBinding = JSON::GetJsonValueFromNode(data->get(), JSON::GetUtilityString(Binding));
        THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !status || !responseBinding);

        AppInUseResult result;
        result.Binding = DeserializeBinding(responseBinding->get());
        if (!(result.Binding == binding))
        {
            bool installerSha256Matches = result.Binding.SelectedInstaller && binding.SelectedInstaller &&
                result.Binding.SelectedInstaller->Sha256 == binding.SelectedInstaller->Sha256;
            bool installerFileNameMatches = result.Binding.SelectedInstaller && binding.SelectedInstaller &&
                result.Binding.SelectedInstaller->FileName == binding.SelectedInstaller->FileName;
            bool installerArchitectureMatches = result.Binding.SelectedInstaller && binding.SelectedInstaller &&
                result.Binding.SelectedInstaller->Architecture == binding.SelectedInstaller->Architecture;
            bool installerTypeMatches = result.Binding.SelectedInstaller && binding.SelectedInstaller &&
                result.Binding.SelectedInstaller->EffectiveInstallerType == binding.SelectedInstaller->EffectiveInstallerType;
            bool installerScopeMatches = result.Binding.SelectedInstaller && binding.SelectedInstaller &&
                result.Binding.SelectedInstaller->Scope == binding.SelectedInstaller->Scope;
            bool installerLocaleMatches = result.Binding.SelectedInstaller && binding.SelectedInstaller &&
                result.Binding.SelectedInstaller->Locale == binding.SelectedInstaller->Locale;

            AICLI_LOG(Repo, Error,
                << "App-in-use metadata binding mismatch"
                << " [PackageIdentifier=" << (result.Binding.PackageIdentifier == binding.PackageIdentifier)
                << ", Operation=" << (result.Binding.Operation == binding.Operation)
                << ", CurrentVersion=" << (result.Binding.CurrentVersion == binding.CurrentVersion)
                << ", CurrentChannel=" << (result.Binding.CurrentChannel == binding.CurrentChannel)
                << ", CurrentScope=" << (result.Binding.CurrentScope == binding.CurrentScope)
                << ", TargetVersion=" << (result.Binding.TargetVersion == binding.TargetVersion)
                << ", TargetChannel=" << (result.Binding.TargetChannel == binding.TargetChannel)
                << ", SelectedInstaller=" << (result.Binding.SelectedInstaller == binding.SelectedInstaller)
                << ", InstallerSha256=" << installerSha256Matches
                << ", InstallerFileName=" << installerFileNameMatches
                << ", InstallerArchitecture=" << installerArchitectureMatches
                << ", InstallerType=" << installerTypeMatches
                << ", InstallerScope=" << installerScopeMatches
                << ", InstallerLocale=" << installerLocaleMatches
                << ']');
            THROW_HR(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
        }

        if (*status == Available)
        {
            auto generatedAt = JSON::GetRawStringValueFromJsonNode(data->get(), JSON::GetUtilityString(GeneratedAt));
            auto validUntil = JSON::GetRawStringValueFromJsonNode(data->get(), JSON::GetUtilityString(ValidUntil));
            auto generation = JSON::GetRawUInt64ValueFromJsonNode(data->get(), JSON::GetUtilityString(Generation));
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !generatedAt || !validUntil || !generation);
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                data->get().has_field(JSON::GetUtilityString(UnavailableReason)) || data->get().has_field(JSON::GetUtilityString(Errors)));

            ValidateValidityWindow(*generatedAt, *validUntil);
            result.Status = AppInUseStatus::Available;
            result.UnavailableReason = AppInUseUnavailableReason::None;
            result.GeneratedAt = std::move(generatedAt);
            result.ValidUntil = std::move(validUntil);
            result.Generation = *generation;
            DeserializeResources(data->get(), result.Files, result.Services);
        }
        else if (*status == Unavailable)
        {
            auto reason = JSON::GetRawStringValueFromJsonNode(data->get(), JSON::GetUtilityString(UnavailableReason));
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID, !reason ||
                data->get().has_field(JSON::GetUtilityString(Resources)) ||
                data->get().has_field(JSON::GetUtilityString(Errors)) ||
                data->get().has_field(JSON::GetUtilityString(GeneratedAt)) ||
                data->get().has_field(JSON::GetUtilityString(ValidUntil)) ||
                data->get().has_field(JSON::GetUtilityString(Generation)));
            result.Status = AppInUseStatus::Unavailable;
            result.UnavailableReason = ConvertToUnavailableReason(*reason);
        }
        else if (*status == Error)
        {
            THROW_HR_IF(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID,
                data->get().has_field(JSON::GetUtilityString(Resources)) ||
                data->get().has_field(JSON::GetUtilityString(UnavailableReason)) ||
                data->get().has_field(JSON::GetUtilityString(GeneratedAt)) ||
                data->get().has_field(JSON::GetUtilityString(ValidUntil)) ||
                data->get().has_field(JSON::GetUtilityString(Generation)));
            result.Status = AppInUseStatus::Error;
            result.UnavailableReason = AppInUseUnavailableReason::None;
            result.Errors = DeserializeErrors(data->get());
        }
        else
        {
            THROW_HR(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
        }

        return result;
    }
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "Rest/Schema/IRestClient.h"
#include <winget/JsonUtil.h>
#include "Rest/Schema/CommonRestConstants.h"
#include "AuthenticationInfoParser.h"
#include "InformationResponseDeserializer.h"

namespace AppInstaller::Repository::Rest::Schema
{
    namespace
    {
        // Information response constants
        constexpr std::string_view SourceIdentifier = "SourceIdentifier"sv;
        constexpr std::string_view ServerSupportedVersions = "ServerSupportedVersions"sv;

        constexpr std::string_view SourceAgreements = "SourceAgreements"sv;
        constexpr std::string_view SourceAgreementsIdentifier = "AgreementsIdentifier"sv;
        constexpr std::string_view SourceAgreementsContent = "Agreements"sv;
        constexpr std::string_view SourceAgreementLabel = "AgreementLabel"sv;
        constexpr std::string_view SourceAgreementText = "Agreement"sv;
        constexpr std::string_view SourceAgreementUrl = "AgreementUrl"sv;

        constexpr std::string_view UnsupportedPackageMatchFields = "UnsupportedPackageMatchFields"sv;
        constexpr std::string_view RequiredPackageMatchFields = "RequiredPackageMatchFields"sv;
        constexpr std::string_view UnsupportedQueryParameters = "UnsupportedQueryParameters"sv;
        constexpr std::string_view RequiredQueryParameters = "RequiredQueryParameters"sv;

        constexpr std::string_view ServiceOfferings = "ServiceOfferings"sv;
        constexpr std::string_view ExtendedMetadata = "ExtendedMetadata"sv;
        constexpr std::string_view AppInUse = "AppInUse"sv;
        constexpr std::string_view State = "State"sv;
        constexpr std::string_view SchemaVersions = "SchemaVersions"sv;
        constexpr std::string_view Operations = "Operations"sv;
        constexpr std::string_view VersionRoles = "VersionRoles"sv;
        constexpr std::string_view ResourceTypes = "ResourceTypes"sv;

        std::optional<std::vector<std::string>> GetRequiredStringArray(
            const web::json::value& value,
            std::string_view name,
            size_t maximumCount)
        {
            auto array = JSON::GetRawJsonArrayFromJsonNode(value, JSON::GetUtilityString(name));
            if (!array || array->get().size() == 0 || array->get().size() > maximumCount)
            {
                return {};
            }

            std::vector<std::string> result;
            result.reserve(array->get().size());
            for (const auto& itemValue : array->get())
            {
                auto item = JSON::GetRawStringValueFromJsonValue(itemValue);
                if (!item || item->empty() || item->size() > 64)
                {
                    return {};
                }

                result.emplace_back(std::move(item).value());
            }

            return result;
        }

        std::optional<AppInUseOperation> ConvertToAppInUseOperation(std::string_view value)
        {
            if (value == "Install") return AppInUseOperation::Install;
            if (value == "Upgrade") return AppInUseOperation::Upgrade;
            if (value == "Repair") return AppInUseOperation::Repair;
            if (value == "Uninstall") return AppInUseOperation::Uninstall;
            return {};
        }
    }

    IRestClient::Information InformationResponseDeserializer::Deserialize(const web::json::value& dataObject) const
    {
        // Get information result from json output.
        std::optional<IRestClient::Information> information = DeserializeInformation(dataObject);

        THROW_HR_IF(APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE, !information);

        return information.value();
    }

    std::optional<IRestClient::Information> InformationResponseDeserializer::DeserializeInformation(const web::json::value& dataObject) const
    {
        try
        {
            if (dataObject.is_null())
            {
                AICLI_LOG(Repo, Error, << "Missing json object.");
                return {};
            }

            std::optional<std::reference_wrapper<const web::json::value>> data = JSON::GetJsonValueFromNode(dataObject, JSON::GetUtilityString(Data));
            if (!data)
            {
                AICLI_LOG(Repo, Error, << "Missing data");
                return {};
            }

            const auto& dataValue = data.value().get();
            std::optional<std::string> sourceId = JSON::GetRawStringValueFromJsonNode(dataValue, JSON::GetUtilityString(SourceIdentifier));
            if (!JSON::IsValidNonEmptyStringValue(sourceId))
            {
                AICLI_LOG(Repo, Error, << "Missing source identifier");
                return {};
            }

            std::vector<std::string> allVersions = JSON::GetRawStringArrayFromJsonNode(dataValue, JSON::GetUtilityString(ServerSupportedVersions));
            if (allVersions.size() == 0)
            {
                AICLI_LOG(Repo, Error, << "Missing supported versions.");
                return {};
            }

            IRestClient::Information info{ std::move(sourceId.value()), std::move(allVersions) };

            auto agreements = JSON::GetJsonValueFromNode(dataValue, JSON::GetUtilityString(SourceAgreements));
            if (agreements)
            {
                const auto& agreementsValue = agreements.value().get();

                auto agreementsIdentifier = JSON::GetRawStringValueFromJsonNode(agreementsValue, JSON::GetUtilityString(SourceAgreementsIdentifier));
                if (!JSON::IsValidNonEmptyStringValue(agreementsIdentifier))
                {
                    AICLI_LOG(Repo, Error, << "SourceAgreements node exists but AgreementsIdentifier is missing.");
                    return {};
                }

                info.SourceAgreementsIdentifier = std::move(agreementsIdentifier.value());

                auto agreementsContent = JSON::GetRawJsonArrayFromJsonNode(agreementsValue, JSON::GetUtilityString(SourceAgreementsContent));
                if (agreementsContent)
                {
                    for (auto const& agreementNode : agreementsContent.value().get())
                    {
                        IRestClient::SourceAgreementEntry agreementEntry;

                        std::optional<std::string> label = JSON::GetRawStringValueFromJsonNode(agreementNode, JSON::GetUtilityString(SourceAgreementLabel));
                        if (JSON::IsValidNonEmptyStringValue(label))
                        {
                            agreementEntry.Label = std::move(label.value());
                        }

                        std::optional<std::string> text = JSON::GetRawStringValueFromJsonNode(agreementNode, JSON::GetUtilityString(SourceAgreementText));
                        if (JSON::IsValidNonEmptyStringValue(text))
                        {
                            agreementEntry.Text = std::move(text.value());
                        }

                        std::optional<std::string> url = JSON::GetRawStringValueFromJsonNode(agreementNode, JSON::GetUtilityString(SourceAgreementUrl));
                        if (JSON::IsValidNonEmptyStringValue(url))
                        {
                            agreementEntry.Url = std::move(url.value());
                        }

                        if (!agreementEntry.Label.empty() || !agreementEntry.Text.empty() || !agreementEntry.Url.empty())
                        {
                            info.SourceAgreements.emplace_back(std::move(agreementEntry));
                        }
                    }
                }
            }

            info.RequiredPackageMatchFields = JSON::GetRawStringArrayFromJsonNode(dataValue, JSON::GetUtilityString(RequiredPackageMatchFields));
            info.UnsupportedPackageMatchFields = JSON::GetRawStringArrayFromJsonNode(dataValue, JSON::GetUtilityString(UnsupportedPackageMatchFields));
            info.RequiredQueryParameters = JSON::GetRawStringArrayFromJsonNode(dataValue, JSON::GetUtilityString(RequiredQueryParameters));
            info.UnsupportedQueryParameters = JSON::GetRawStringArrayFromJsonNode(dataValue, JSON::GetUtilityString(UnsupportedQueryParameters));

            info.Authentication = ParseAuthenticationInfo(dataValue, ParseAuthenticationInfoType::Source);

            auto serviceOfferings = JSON::GetJsonValueFromNode(dataValue, JSON::GetUtilityString(ServiceOfferings));
            if (serviceOfferings)
            {
                const auto& serviceOfferingsValue = serviceOfferings->get();
                if (!serviceOfferingsValue.is_object())
                {
                    AICLI_LOG(Repo, Error, << "ServiceOfferings must be an object.");
                    return {};
                }

                auto extendedMetadata = JSON::GetJsonValueFromNode(serviceOfferingsValue, JSON::GetUtilityString(ExtendedMetadata));
                if (extendedMetadata)
                {
                    const auto& extendedMetadataValue = extendedMetadata->get();
                    if (!extendedMetadataValue.is_object())
                    {
                        AICLI_LOG(Repo, Error, << "ServiceOfferings.ExtendedMetadata must be an object.");
                        return {};
                    }

                    const auto appInUseKey = JSON::GetUtilityString(AppInUse);
                    if (extendedMetadataValue.has_field(appInUseKey))
                    {
                        const auto& appInUseValue = extendedMetadataValue.at(appInUseKey);
                        if (!appInUseValue.is_object())
                        {
                            AICLI_LOG(Repo, Error, << "ServiceOfferings.ExtendedMetadata.AppInUse must be an object.");
                            return {};
                        }

                        auto state = JSON::GetRawStringValueFromJsonNode(appInUseValue, JSON::GetUtilityString(State));
                        auto schemaVersions = GetRequiredStringArray(appInUseValue, SchemaVersions, 16);
                        auto operations = GetRequiredStringArray(appInUseValue, Operations, 4);
                        auto versionRoles = GetRequiredStringArray(appInUseValue, VersionRoles, 2);
                        auto resourceTypes = GetRequiredStringArray(appInUseValue, ResourceTypes, 2);
                        if (!state || !schemaVersions || !operations || !versionRoles || !resourceTypes)
                        {
                            AICLI_LOG(Repo, Error, << "AppInUse service offering is missing required fields.");
                            return {};
                        }

                        IRestClient::AppInUseOffering offering;
                        if (*state == "Enabled")
                        {
                            offering.State = IRestClient::ServiceOfferingState::Enabled;
                        }
                        else if (*state == "Disabled")
                        {
                            offering.State = IRestClient::ServiceOfferingState::Disabled;
                        }
                        else
                        {
                            AICLI_LOG(Repo, Error, << "AppInUse service offering has an invalid State.");
                            return {};
                        }

                        offering.SchemaVersions = std::move(schemaVersions).value();
                        offering.VersionRoles = std::move(versionRoles).value();
                        offering.ResourceTypes = std::move(resourceTypes).value();
                        for (const auto& operationString : operations.value())
                        {
                            auto operation = ConvertToAppInUseOperation(operationString);
                            if (!operation)
                            {
                                AICLI_LOG(Repo, Error, << "AppInUse service offering has an invalid operation.");
                                return {};
                            }
                            offering.Operations.emplace_back(*operation);
                        }

                        info.ExtendedMetadata.AppInUse = std::move(offering);
                    }
                }
            }

            return info;
        }
        catch (const std::exception& e)
        {
            AICLI_LOG(Repo, Error, << "Error encountered while deserializing Information. Reason: " << e.what());
        }
        catch (...)
        {
            AICLI_LOG(Repo, Error, << "Received invalid information.");
        }

        return {};
    }
}

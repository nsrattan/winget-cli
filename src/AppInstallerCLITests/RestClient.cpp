// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "TestCommon.h"
#include "TestHooks.h"
#include "TestRestRequestHandler.h"
#include <AppInUseValidationProvider.h>
#include <Microsoft/PreIndexedPackageSourceFactory.h>
#include <Rest/RestClient.h>
#include <Rest/RestInformationCache.h>
#include <Rest/RestSource.h>
#include <Rest/Schema/IRestClient.h>
#include <Rest/Schema/InformationResponseDeserializer.h>
#include <SourceList.h>
#include <AppInstallerVersions.h>
#include <AppInstallerErrors.h>
#include <AppInstallerRuntime.h>
#include <winget/JsonUtil.h>

using namespace AppInstaller;
using namespace AppInstaller::Http;
using namespace AppInstaller::Utility;
using namespace AppInstaller::Repository;
using namespace AppInstaller::Repository::Rest;
using namespace AppInstaller::Repository::Rest::Schema;

const std::string TestRestUri = "http://restsource.net";

RestClient CreateRestClient(
    const std::string& restApi,
    const std::optional<std::string>& customHeader,
    std::string_view caller,
    const Http::HttpClientHelper& helper,
    const Authentication::AuthenticationArguments& authArgs = {})
{
    return RestClient::Create(restApi, customHeader, caller, helper, RestClient::GetInformation(restApi, customHeader, caller, helper), authArgs);
}

TEST_CASE("GetLatestCommonVersion", "[RestSource]")
{
    std::set<AppInstaller::Utility::Version> wingetSupportedContracts = { Version {"1.0.0"}, Version {"1.2.0"} };
    std::vector<std::string> versions{ "1.0.0", "2.0.0", "1.2.0" };
    std::optional<Version> actual = RestClient::GetLatestCommonVersion(versions, wingetSupportedContracts);
    REQUIRE(actual);
    REQUIRE(actual.value().ToString() == "1.2.0");
}

TEST_CASE("GetLatestCommonVersion_OnlyMajorMinorVersionMatched", "[RestSource]")
{
    std::set<AppInstaller::Utility::Version> wingetSupportedContracts = { Version {"1.0.0"}, Version {"1.2.0"} };
    std::vector<std::string> versions{ "1.0.0", "2.0.0", "1.2.1" };
    std::optional<Version> actual = RestClient::GetLatestCommonVersion(versions, wingetSupportedContracts);
    REQUIRE(actual);
    REQUIRE(actual.value().ToString() == "1.2.0");
}

TEST_CASE("GetLatestCommonVersion_UnsupportedVersion", "[RestSource]")
{
    std::set<AppInstaller::Utility::Version> wingetSupportedContracts = { Version {"3.0.0"}, Version {"4.2.0"} };
    std::vector<std::string> versions{ "1.0.0", "2.0.0" };
    std::optional<Version> actual = RestClient::GetLatestCommonVersion(versions, wingetSupportedContracts);
    REQUIRE(!actual);
}

TEST_CASE("GetSupportedInterface", "[RestSource]")
{
    IRestClient::Information info{ "TestId", { "1.0.0" } };

    Version version{ "1.0.0" };
    REQUIRE(RestClient::GetSupportedInterface(TestRestUri, {}, info, {}, version, {})->GetVersion() == version);

    // Update this test to next version so that we don't forget to add to supported versions before rest e2e tests are available.
    Version invalid{ "1.30.0" };
    REQUIRE_THROWS_HR(RestClient::GetSupportedInterface(TestRestUri, {}, info, {}, invalid, {}), APPINSTALLER_CLI_ERROR_RESTSOURCE_INVALID_VERSION);

    Authentication::AuthenticationArguments authArgs;
    authArgs.Mode = Authentication::AuthenticationMode::Silent;
    Version version_1_7{ "1.7.0" };

    // GetSupportedInterface throws on unknown authentication type.
    IRestClient::Information infoWithUnknownAuthenticationType{ "TestId", { "1.7.0" } };
    infoWithUnknownAuthenticationType.Authentication.Type = Authentication::AuthenticationType::Unknown;
    REQUIRE_THROWS_HR(RestClient::GetSupportedInterface(TestRestUri, {}, infoWithUnknownAuthenticationType, authArgs, version_1_7, {}), APPINSTALLER_CLI_ERROR_AUTHENTICATION_TYPE_NOT_SUPPORTED);

    // GetSupportedInterface throws on invalid authentication info.
    IRestClient::Information infoWithInvalidAuthenticationInfo{ "TestId", { "1.7.0" } };
    infoWithInvalidAuthenticationInfo.Authentication.Type = Authentication::AuthenticationType::MicrosoftEntraId;
    REQUIRE_THROWS_HR(RestClient::GetSupportedInterface(TestRestUri, {}, infoWithInvalidAuthenticationInfo, authArgs, version_1_7, {}), APPINSTALLER_CLI_ERROR_INVALID_AUTHENTICATION_INFO);
}

TEST_CASE("GetInformation_Success", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.0.0",
                "1.1.0"
               ],
              "SourceAgreements": {
                "AgreementsIdentifier": "agreementV1",
                "Agreements": [{
                    "AgreementLabel": "EULA",
                    "Agreement": "this is store agreement",
                    "AgreementUrl": "https://store.agreement"
                  }
                ]
              },
              "RequiredQueryParameters": [
                "Market"
              ],
              "RequiredPackageMatchFields": [
                "Market"
              ],
              "UnsupportedQueryParameters": [
                "Moniker"
              ],
              "UnsupportedPackageMatchFields": [
                "Moniker"
              ]
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    IRestClient::Information information = RestClient::GetInformation(TestRestUri, {}, {}, helper);
    REQUIRE(information.SourceIdentifier == "Source123");
    REQUIRE(information.ServerSupportedVersions.size() == 2);
    REQUIRE(information.ServerSupportedVersions.at(0) == "1.0.0");
    REQUIRE(information.ServerSupportedVersions.at(1) == "1.1.0");
    REQUIRE(information.SourceAgreementsIdentifier == "agreementV1");
    REQUIRE(information.SourceAgreements.size() == 1);
    REQUIRE(information.SourceAgreements.at(0).Label == "EULA");
    REQUIRE(information.SourceAgreements.at(0).Text == "this is store agreement");
    REQUIRE(information.SourceAgreements.at(0).Url == "https://store.agreement");
    REQUIRE(information.RequiredQueryParameters.size() == 1);
    REQUIRE(information.RequiredQueryParameters.at(0) == "Market");
    REQUIRE(information.RequiredPackageMatchFields.size() == 1);
    REQUIRE(information.RequiredPackageMatchFields.at(0) == "Market");
    REQUIRE(information.UnsupportedQueryParameters.size() == 1);
    REQUIRE(information.UnsupportedQueryParameters.at(0) == "Moniker");
    REQUIRE(information.UnsupportedPackageMatchFields.size() == 1);
    REQUIRE(information.UnsupportedPackageMatchFields.at(0) == "Moniker");
    REQUIRE(information.Authentication.Type == Authentication::AuthenticationType::None);
    REQUIRE_FALSE(information.Authentication.MicrosoftEntraIdInfo.has_value());
}

TEST_CASE("GetInformation_WithAuthenticationInfo_Success", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "SourceAgreements": {
                "AgreementsIdentifier": "agreementV1",
                "Agreements": [{
                    "AgreementLabel": "EULA",
                    "Agreement": "this is store agreement",
                    "AgreementUrl": "https://store.agreement"
                  }
                ]
              },
              "RequiredQueryParameters": [
                "Market"
              ],
              "RequiredPackageMatchFields": [
                "Market"
              ],
              "UnsupportedQueryParameters": [
                "Moniker"
              ],
              "UnsupportedPackageMatchFields": [
                "Moniker"
              ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraId",
                "MicrosoftEntraIdAuthenticationInfo" : {
                  "Resource": "GUID",
                  "Scope" : "test"
                }
              }
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    IRestClient::Information information = RestClient::GetInformation(TestRestUri, {}, {}, helper);
    REQUIRE(information.SourceIdentifier == "Source123");
    REQUIRE(information.ServerSupportedVersions.size() == 1);
    REQUIRE(information.ServerSupportedVersions.at(0) == "1.7.0");
    REQUIRE(information.SourceAgreementsIdentifier == "agreementV1");
    REQUIRE(information.SourceAgreements.size() == 1);
    REQUIRE(information.SourceAgreements.at(0).Label == "EULA");
    REQUIRE(information.SourceAgreements.at(0).Text == "this is store agreement");
    REQUIRE(information.SourceAgreements.at(0).Url == "https://store.agreement");
    REQUIRE(information.RequiredQueryParameters.size() == 1);
    REQUIRE(information.RequiredQueryParameters.at(0) == "Market");
    REQUIRE(information.RequiredPackageMatchFields.size() == 1);
    REQUIRE(information.RequiredPackageMatchFields.at(0) == "Market");
    REQUIRE(information.UnsupportedQueryParameters.size() == 1);
    REQUIRE(information.UnsupportedQueryParameters.at(0) == "Moniker");
    REQUIRE(information.UnsupportedPackageMatchFields.size() == 1);
    REQUIRE(information.UnsupportedPackageMatchFields.at(0) == "Moniker");
    REQUIRE(information.Authentication.Type == Authentication::AuthenticationType::MicrosoftEntraId);
    REQUIRE(information.Authentication.MicrosoftEntraIdInfo.has_value());
    REQUIRE(information.Authentication.MicrosoftEntraIdInfo->Resource == "GUID");
    REQUIRE(information.Authentication.MicrosoftEntraIdInfo->Scope == "test");
}

#if 0
TEST_CASE("GetInformation_ExtendedMetadataOffering",
          "[RestSource][ExtendedMetadata]")
{
    auto response = web::json::value::parse(_XPLATSTR(R"({
                          "Data": {
                              "SourceIdentifier": "Source123",
                              "ServerSupportedVersions": [ "1.29.0" ],
                              "ServiceOfferings": {
                                  "ExtendedMetadata": {
                                      "AppInUse": true
                                  }
                              }
                          }
                      })"));

    InformationResponseDeserializer deserializer;
    auto information = deserializer.Deserialize(response);
    REQUIRE(information.ExtendedMetadata.AppInUse == true);

    response[_XPLATSTR("Data")][_XPLATSTR("ServiceOfferings")]
            [_XPLATSTR("ExtendedMetadata")][_XPLATSTR("AppInUse")] =
                web::json::value::boolean(false);
    information = deserializer.Deserialize(response);
    REQUIRE(information.ExtendedMetadata.AppInUse == false);

    response[_XPLATSTR("Data")].erase(_XPLATSTR("ServiceOfferings"));
    information = deserializer.Deserialize(response);
    REQUIRE_FALSE(information.ExtendedMetadata.AppInUse.has_value());
}

TEST_CASE("GetInformation_ExtendedMetadataOffering_Invalid",
          "[RestSource][ExtendedMetadata]")
{
    auto response = web::json::value::parse(_XPLATSTR(R"({
                          "Data": {
                              "SourceIdentifier": "Source123",
                              "ServerSupportedVersions": [ "1.29.0" ],
                              "ServiceOfferings": {
                                  "ExtendedMetadata": {
                                      "AppInUse": "true"
                                  }
                              }
                          }
                      })"));

    InformationResponseDeserializer deserializer;
    REQUIRE_THROWS_HR(deserializer.Deserialize(response),
                      APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);
}

TEST_CASE("RestClient_ExtendedMetadataCapabilitiesAreIndependent",
          "[RestSource][ExtendedMetadata]")
{
    IRestClient::Information oldContractInformation{"Source123", {"1.28.0"}};
    oldContractInformation.ExtendedMetadata.AppInUse = true;
    RestClient oldContractClient =
        RestClient::Create(TestRestUri, {}, {}, {}, oldContractInformation);
    auto oldContractCapability = oldContractClient.GetExtendedMetadataCapability(
        IRestClient::ExtendedMetadataOperation::AppInUse);
    REQUIRE_FALSE(oldContractCapability.ContractSupported);
    REQUIRE(oldContractCapability.ServiceOfferingAvailable);
    REQUIRE_FALSE(oldContractCapability.IsAvailable());

    IRestClient::Information noOfferingInformation{"Source123", {"1.29.0"}};
    RestClient noOfferingClient =
        RestClient::Create(TestRestUri, {}, {}, {}, noOfferingInformation);
    auto noOfferingCapability = noOfferingClient.GetExtendedMetadataCapability(
        IRestClient::ExtendedMetadataOperation::AppInUse);
    REQUIRE(noOfferingCapability.ContractSupported);
    REQUIRE_FALSE(noOfferingCapability.ServiceOfferingAvailable);
    REQUIRE_FALSE(noOfferingCapability.IsAvailable());
    REQUIRE(noOfferingClient.GetAppInUseMetadata({{"Contoso.App", "1.0", "2.0"}})
                .Status ==
            IRestClient::ExtendedMetadataResultStatus::Unavailable);
}

TEST_CASE("RestClient_AppInUseExtendedMetadata_Available",
          "[RestSource][ExtendedMetadata]")
{
    auto handler = std::make_shared<TestRestRequestHandler>(
        [](web::http::http_request request)
            -> pplx::task<web::http::http_response> {
            REQUIRE(request.method() == web::http::methods::POST);
            REQUIRE(request.headers()[_XPLATSTR("Version")] == _XPLATSTR("1.29.0"));
            REQUIRE(request.headers()[_XPLATSTR("Windows-Package-Manager")] ==
                    _XPLATSTR("TestHeader"));

            auto requestBody = request.extract_json().get();
            const auto &transition = requestBody.at(_XPLATSTR("Transition"));
            REQUIRE(transition.at(_XPLATSTR("PackageIdentifier")).as_string() ==
                    _XPLATSTR("Contoso.App"));
            REQUIRE(transition.at(_XPLATSTR("InstalledVersion")).as_string() ==
                    _XPLATSTR("1.0"));
            REQUIRE(transition.at(_XPLATSTR("TargetVersion")).as_string() ==
                    _XPLATSTR("2.0"));

            web::http::http_response response{web::http::status_codes::OK};
            response.headers().set_content_type(
                web::http::details::mime_types::application_json);
            response.set_body(web::json::value::parse(_XPLATSTR(R"({
                                  "Data": {
                                      "Status": "Available",
                                      "Transition": {
                                          "PackageIdentifier": "Contoso.App",
                                          "InstalledVersion": "1.0",
                                          "TargetVersion": "2.0"
                                      },
                                      "Resources": [ "Contoso.exe", "Contoso.Service" ]
                                  }
                              })")));
            return pplx::task_from_result(response);
        });

    IRestClient::Information information{"Source123", {"1.29.0"}};
    information.ExtendedMetadata.AppInUse = true;
    HttpClientHelper helper{handler};
    RestClient client = RestClient::Create(TestRestUri + "/api", "TestHeader", {},
                                           helper, information);

    auto result = client.GetAppInUseMetadata({{"Contoso.App", "1.0", "2.0"}});
    REQUIRE(result.Status ==
            IRestClient::ExtendedMetadataResultStatus::Available);
    REQUIRE((result.Transition == IRestClient::AppInUseTransition{
                                      "Contoso.App", "1.0", "2.0"}));
    REQUIRE((result.Resources ==
             std::vector<std::string>{"Contoso.exe", "Contoso.Service"}));
    REQUIRE(result.Errors.empty());
}

TEST_CASE("RestClient_AppInUseExtendedMetadata_AuthenticationHeader",
          "[RestSource][ExtendedMetadata]")
{
    Authentication::AuthenticationResult authenticationResult;
    authenticationResult.Status = S_OK;
    authenticationResult.Token = "TestToken";
    TestHook::SetAuthenticationResult_Override authenticationOverride{
        authenticationResult};

    auto handler = std::make_shared<TestRestRequestHandler>(
        [](web::http::http_request request)
            -> pplx::task<web::http::http_response> {
            REQUIRE(request.headers()[web::http::header_names::authorization] ==
                    _XPLATSTR("Bearer TestToken"));

            web::http::http_response response{web::http::status_codes::OK};
            response.headers().set_content_type(
                web::http::details::mime_types::application_json);
            response.set_body(web::json::value::parse(
                _XPLATSTR(R"({ "Data": { "Status": "Unavailable" } })")));
            return pplx::task_from_result(response);
        });

    IRestClient::Information information{"Source123", {"1.29.0"}};
    information.ExtendedMetadata.AppInUse = true;
    information.Authentication.Type =
        Authentication::AuthenticationType::MicrosoftEntraId;
    information.Authentication.MicrosoftEntraIdInfo =
        Authentication::MicrosoftEntraIdAuthenticationInfo{"Resource", "Scope"};
    Authentication::AuthenticationArguments authenticationArguments;
    authenticationArguments.Mode = Authentication::AuthenticationMode::Silent;

    HttpClientHelper helper{handler};
    RestClient client = RestClient::Create(TestRestUri, {}, {}, helper,
                                           information, authenticationArguments);
    auto result = client.GetAppInUseMetadata({{"Contoso.App", "1.0", "2.0"}});
    REQUIRE(result.Status ==
            IRestClient::ExtendedMetadataResultStatus::Unavailable);
}

TEST_CASE("RestClient_AppInUseExtendedMetadata_Error",
          "[RestSource][ExtendedMetadata]")
{
    auto response = _XPLATSTR(R"({
                          "Data": {
                              "Status": "Error",
                              "Errors": [
                                  { "Code": "MetadataNotFound", "Message": "No metadata was found." }
                              ]
                          }
                      })");

    IRestClient::Information information{"Source123", {"1.29.0"}};
    information.ExtendedMetadata.AppInUse = true;
    HttpClientHelper helper{
        GetTestRestRequestHandler(web::http::status_codes::OK, response)};
    RestClient client =
        RestClient::Create(TestRestUri, {}, {}, helper, information);

    auto result = client.GetAppInUseMetadata({{"Contoso.App", "1.0", "2.0"}});
    REQUIRE(result.Status == IRestClient::ExtendedMetadataResultStatus::Error);
    REQUIRE(result.Errors.size() == 1);
    REQUIRE(result.Errors[0].Code == "MetadataNotFound");
    REQUIRE(result.Errors[0].Message == "No metadata was found.");
}

TEST_CASE("RestClient_AppInUseExtendedMetadata_StrictValidation",
          "[RestSource][ExtendedMetadata]")
{
    IRestClient::Information information{"Source123", {"1.29.0"}};
    information.ExtendedMetadata.AppInUse = true;

    SECTION("Invalid request")
    {
        RestClient client =
            RestClient::Create(TestRestUri, {}, {}, {}, information);
        REQUIRE_THROWS_HR(client.GetAppInUseMetadata({{"", "1.0", "2.0"}}),
                          E_INVALIDARG);
        REQUIRE_THROWS_HR(client.GetAppInUseMetadata(
                              {{"Contoso.App", std::string(129, '1'), "2.0"}}),
                          E_INVALIDARG);
    }

    SECTION("Mismatched transition binding")
    {
        auto response = _XPLATSTR(R"({
                              "Data": {
                                  "Status": "Available",
                                  "Transition": {
                                      "PackageIdentifier": "Other.App",
                                      "InstalledVersion": "1.0",
                                      "TargetVersion": "2.0"
                                  },
                                  "Resources": [ "Contoso.exe" ]
                              }
                          })");
        HttpClientHelper helper{
            GetTestRestRequestHandler(web::http::status_codes::OK, response)};
        RestClient client =
            RestClient::Create(TestRestUri, {}, {}, helper, information);
        REQUIRE_THROWS_HR(
            client.GetAppInUseMetadata({{"Contoso.App", "1.0", "2.0"}}),
            APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
    }

    SECTION("Oversized resources")
    {
        auto response = web::json::value::parse(_XPLATSTR(R"({
                              "Data": {
                                  "Status": "Available",
                                  "Transition": {
                                      "PackageIdentifier": "Contoso.App",
                                      "InstalledVersion": "1.0",
                                      "TargetVersion": "2.0"
                                  },
                                  "Resources": []
                              }
                          })"));
        auto &resources = response[_XPLATSTR("Data")][_XPLATSTR("Resources")];
        for (size_t i = 0; i != 1025; ++i)
        {
            resources[i] = web::json::value::string(_XPLATSTR("resource"));
        }

        HttpClientHelper helper{GetTestRestRequestHandler(
            web::http::status_codes::OK, response.serialize())};
        RestClient client =
            RestClient::Create(TestRestUri, {}, {}, helper, information);
        REQUIRE_THROWS_HR(
            client.GetAppInUseMetadata({{"Contoso.App", "1.0", "2.0"}}),
            APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
    }
}

#endif

namespace
{
    IRestClient::Information CreateAppInUseInformation(std::string version = "1.29.0")
    {
        IRestClient::Information result{ "Source123", { std::move(version) } };
        IRestClient::AppInUseOffering offering;
        offering.State = IRestClient::ServiceOfferingState::Enabled;
        offering.SchemaVersions = { "1.0" };
        offering.Operations = { AppInUseOperation::Install, AppInUseOperation::Upgrade, AppInUseOperation::Repair, AppInUseOperation::Uninstall };
        offering.VersionRoles = { "Current", "Target" };
        offering.ResourceTypes = { "File", "Service" };
        result.ExtendedMetadata.AppInUse = std::move(offering);
        return result;
    }

    AppInUseBinding CreateInstallBinding()
    {
        AppInUseBinding result;
        result.PackageIdentifier = "Contoso.App";
        result.Operation = AppInUseOperation::Install;
        result.TargetVersion = "2.0";
        result.TargetChannel = "stable";
        result.SelectedInstaller = {
            std::string(64, 'a'), "ContosoInstaller.exe", "x64", "exe", "machine", "en-US"
        };
        return result;
    }

    utility::string_t GetInstallBindingJson(std::string_view packageIdentifier = "Contoso.App")
    {
        return JSON::GetUtilityString(R"("Binding": {
            "PackageIdentifier": ")" + std::string{ packageIdentifier } + R"(",
            "Operation": "Install",
            "TargetVersion": "2.0",
            "TargetChannel": "stable",
            "SelectedInstaller": {
                "Sha256": ")" + std::string(64, 'a') + R"(",
                "FileName": "ContosoInstaller.exe",
                "Architecture": "x64",
                "EffectiveInstallerType": "exe",
                "Scope": "machine",
                "Locale": "en-US"
            }
        })");
    }

    utility::string_t CreateAppInUseResponse(std::string_view fields)
    {
        return JSON::GetUtilityString(R"({ "Data": { )") + GetInstallBindingJson() +
            _XPLATSTR(", ") + JSON::GetUtilityString(fields) + _XPLATSTR(" } }");
    }

    std::string GetCurrentRelativeTimestamp(int64_t offsetSeconds)
    {
        constexpr uint64_t TicksPerSecond = 10000000;
        uint64_t interval = utility::datetime::utc_now().to_interval();
        if (offsetSeconds < 0)
        {
            interval -= static_cast<uint64_t>(-offsetSeconds) * TicksPerSecond;
        }
        else
        {
            interval += static_cast<uint64_t>(offsetSeconds) * TicksPerSecond;
        }

        return utility::conversions::to_utf8string(
            utility::datetime::from_interval(interval).to_string(utility::datetime::ISO_8601));
    }

    utility::string_t CreateAvailableAppInUseResponse(
        int64_t generatedAtOffsetSeconds,
        int64_t validUntilOffsetSeconds,
        std::string_view resources)
    {
        std::string fields =
            R"("Status": "Available", "GeneratedAt": ")" +
            GetCurrentRelativeTimestamp(generatedAtOffsetSeconds) +
            R"(", "ValidUntil": ")" +
            GetCurrentRelativeTimestamp(validUntilOffsetSeconds) +
            R"(", "Generation": 7, "Resources": )" +
            std::string{ resources };
        return CreateAppInUseResponse(fields);
    }
}

TEST_CASE("GetInformation_StructuredAppInUseOffering", "[RestSource][ExtendedMetadata]")
{
    auto response = web::json::value::parse(_XPLATSTR(R"({
        "Data": {
            "SourceIdentifier": "Source123",
            "ServerSupportedVersions": [ "1.29.0" ],
            "ServiceOfferings": {
                "ExtendedMetadata": {
                    "AppInUse": {
                        "State": "Enabled",
                        "SchemaVersions": [ "1.0" ],
                        "Operations": [ "Install", "Upgrade", "Repair", "Uninstall" ],
                        "VersionRoles": [ "Current", "Target" ],
                        "ResourceTypes": [ "File", "Service" ]
                    }
                }
            }
        }
    })"));

    InformationResponseDeserializer deserializer;
    auto information = deserializer.Deserialize(response);
    REQUIRE(information.ExtendedMetadata.AppInUse);
    REQUIRE(information.ExtendedMetadata.AppInUse->State == IRestClient::ServiceOfferingState::Enabled);
    REQUIRE(information.ExtendedMetadata.AppInUse->SchemaVersions == std::vector<std::string>{ "1.0" });
    REQUIRE(information.ExtendedMetadata.AppInUse->Operations.size() == 4);

    response[_XPLATSTR("Data")].erase(_XPLATSTR("ServiceOfferings"));
    information = deserializer.Deserialize(response);
    REQUIRE_FALSE(information.ExtendedMetadata.AppInUse);

    response[_XPLATSTR("Data")][_XPLATSTR("ServiceOfferings")][_XPLATSTR("ExtendedMetadata")][_XPLATSTR("AppInUse")] =
        web::json::value::boolean(true);
    REQUIRE_THROWS_HR(deserializer.Deserialize(response), APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);
}

TEST_CASE("RestClient_AppInUseCapabilityRequiresMutualSupport", "[RestSource][ExtendedMetadata]")
{
    auto oldInformation = CreateAppInUseInformation("1.28.0");
    RestClient oldClient = RestClient::Create(TestRestUri, {}, {}, {}, oldInformation);
    auto capability = oldClient.GetAppInUseCapability(AppInUseOperation::Install);
    REQUIRE_FALSE(capability.ContractSupported);
    REQUIRE(capability.OfferingEnabled);
    REQUIRE(capability.SchemaSupported);
    REQUIRE(capability.OperationSupported);
    REQUIRE_FALSE(capability.IsAvailable());

    auto information = CreateAppInUseInformation();
    information.ExtendedMetadata.AppInUse->Operations = { AppInUseOperation::Upgrade };
    RestClient client = RestClient::Create(TestRestUri, {}, {}, {}, information);
    capability = client.GetAppInUseCapability(AppInUseOperation::Install);
    REQUIRE(capability.ContractSupported);
    REQUIRE_FALSE(capability.OperationSupported);
    REQUIRE_FALSE(capability.IsAvailable());

    auto binding = CreateInstallBinding();
    auto result = client.GetAppInUseMetadata(binding);
    REQUIRE(result.Status == AppInUseStatus::Unavailable);
    REQUIRE(result.Binding == binding);

    Source unopenedSource;
    REQUIRE_FALSE(unopenedSource.GetAppInUseCapability(AppInUseOperation::Install).IsAvailable());
    REQUIRE(unopenedSource.GetAppInUseMetadata(binding).Binding == binding);
}

TEST_CASE("RestSource_AppInUseIsFailClosed", "[RestSource][ExtendedMetadata]")
{
    bool requestSent = false;
    auto handler = std::make_shared<TestRestRequestHandler>(
        [&requestSent](web::http::http_request) -> pplx::task<web::http::http_response>
        {
            requestSent = true;
            return pplx::task_from_result(web::http::http_response{ web::http::status_codes::InternalError });
        });

    HttpClientHelper helper{ handler };
    RestClient restClient = RestClient::Create(TestRestUri, {}, {}, helper, CreateAppInUseInformation());
    SourceDetails details;
    details.Identifier = "NormalRestSource";
    Source source{ std::make_shared<RestSource>(details, SourceInformation{}, std::move(restClient)) };

    auto capability = source.GetAppInUseCapability(AppInUseOperation::Install);
    REQUIRE_FALSE(capability.SourceEligible);
    REQUIRE(capability.ContractSupported);
    REQUIRE(capability.OfferingEnabled);
    REQUIRE(capability.SchemaSupported);
    REQUIRE(capability.OperationSupported);
    REQUIRE_FALSE(capability.IsAvailable());

    auto binding = CreateInstallBinding();
    auto result = source.GetAppInUseMetadata(binding);
    REQUIRE(result.Status == AppInUseStatus::Unavailable);
    REQUIRE(result.UnavailableReason == AppInUseUnavailableReason::NotSupported);
    REQUIRE(result.Binding == binding);
    REQUIRE_FALSE(requestSent);
}

#ifndef AICLI_DISABLE_TEST_HOOKS
TEST_CASE("AppInUseValidationProvider_ExactBuiltInWingetOnly", "[ExtendedMetadata][ValidationProvider]")
{
    SourceDetails details;
    details.Name = std::string{ GetWellKnownSourceName(WellKnownSource::WinGet) };
    details.Type = std::string{ AppInstaller::Repository::Microsoft::PreIndexedPackageSourceFactory::Type() };
    details.Arg = std::string{ GetWellKnownSourceArg(WellKnownSource::WinGet) };
    details.Data = std::string{ GetWellKnownSourceIdentifier(WellKnownSource::WinGet) };
    details.Identifier = details.Data;
    details.Origin = SourceOrigin::Default;

    REQUIRE(AppInUseValidation::GetCapability(details, AppInUseOperation::Install).IsAvailable());

    SourceDetails lookalike = details;
    lookalike.Origin = SourceOrigin::User;
    REQUIRE_FALSE(AppInUseValidation::GetCapability(lookalike, AppInUseOperation::Install).IsAvailable());

    lookalike = details;
    lookalike.Name = std::string{ GetWellKnownSourceName(WellKnownSource::WinGetFont) };
    lookalike.Arg = std::string{ GetWellKnownSourceArg(WellKnownSource::WinGetFont) };
    lookalike.Data = std::string{ GetWellKnownSourceIdentifier(WellKnownSource::WinGetFont) };
    lookalike.Identifier = lookalike.Data;
    REQUIRE_FALSE(AppInUseValidation::GetCapability(lookalike, AppInUseOperation::Install).IsAvailable());

    lookalike = details;
    lookalike.Name = std::string{ GetWellKnownSourceName(WellKnownSource::MicrosoftStore) };
    lookalike.Arg = std::string{ GetWellKnownSourceArg(WellKnownSource::MicrosoftStore) };
    lookalike.Data = std::string{ GetWellKnownSourceIdentifier(WellKnownSource::MicrosoftStore) };
    lookalike.Identifier = lookalike.Data;
    REQUIRE_FALSE(AppInUseValidation::GetCapability(lookalike, AppInUseOperation::Install).IsAvailable());
}

TEST_CASE("AppInUseValidationProvider_ReadsBoundRestFixture", "[ExtendedMetadata][ValidationProvider]")
{
    SourceDetails details;
    details.Name = std::string{ GetWellKnownSourceName(WellKnownSource::WinGet) };
    details.Type = std::string{ AppInstaller::Repository::Microsoft::PreIndexedPackageSourceFactory::Type() };
    details.Arg = std::string{ GetWellKnownSourceArg(WellKnownSource::WinGet) };
    details.Data = std::string{ GetWellKnownSourceIdentifier(WellKnownSource::WinGet) };
    details.Identifier = details.Data;
    details.Origin = SourceOrigin::Default;

    TestCommon::TempDirectory fixtureRoot{ "AppInUseValidationProvider" };
    auto binding = CreateInstallBinding();
    std::filesystem::path fixturePath = fixtureRoot.GetPath() / "Contoso.App.json";
    std::ofstream stream{ fixturePath, std::ios_base::out | std::ios_base::binary };
    stream << utility::conversions::to_utf8string(CreateAvailableAppInUseResponse(
        -60,
        30 * 60,
        R"({
            "Files": [ { "Path": "C:\\Program Files\\Contoso\\Contoso.exe", "Kind": "Executable" } ],
            "Services": [ { "Name": "ContosoSvc" } ]
        })"));
    stream.close();

    auto result = AppInUseValidation::GetMetadata(details, binding, fixtureRoot.GetPath());
    REQUIRE(result.Status == AppInUseStatus::Available);
    REQUIRE(result.Binding == binding);
    REQUIRE(result.Files.size() == 1);
    REQUIRE(result.Services.size() == 1);

    binding.TargetVersion = "2.0.0";
    REQUIRE_THROWS_HR(
        AppInUseValidation::GetMetadata(details, binding, fixtureRoot.GetPath()),
        APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);

    binding.PackageIdentifier = "Missing.App";
    result = AppInUseValidation::GetMetadata(details, binding, fixtureRoot.GetPath());
    REQUIRE(result.Status == AppInUseStatus::Unavailable);
    REQUIRE(result.UnavailableReason == AppInUseUnavailableReason::PackageNotFound);

    binding = CreateInstallBinding();
    stream.open(fixturePath, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    stream << "{}";
    stream.close();
    REQUIRE_THROWS_HR(
        AppInUseValidation::GetMetadata(details, binding, fixtureRoot.GetPath()),
        APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);

    binding.PackageIdentifier = "..\\Contoso.App";
    REQUIRE_THROWS_HR(
        AppInUseValidation::GetMetadata(details, binding, fixtureRoot.GetPath()),
        APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
}
#endif

TEST_CASE("RestClient_AppInUseAvailableResponse", "[RestSource][ExtendedMetadata]")
{
    auto handler = std::make_shared<TestRestRequestHandler>(
        [](web::http::http_request request) -> pplx::task<web::http::http_response>
        {
            REQUIRE(request.method() == web::http::methods::POST);
            REQUIRE(request.headers()[_XPLATSTR("Version")] == _XPLATSTR("1.29.0"));
            REQUIRE(request.headers()[_XPLATSTR("Windows-Package-Manager")] == _XPLATSTR("TestHeader"));

            auto requestBody = request.extract_json().get();
            const auto& binding = requestBody.at(_XPLATSTR("Binding"));
            REQUIRE(binding.at(_XPLATSTR("Operation")).as_string() == _XPLATSTR("Install"));
            REQUIRE(binding.at(_XPLATSTR("SelectedInstaller")).at(_XPLATSTR("Sha256")).as_string() ==
                utility::conversions::to_string_t(std::string(64, 'a')));

            web::http::http_response response{ web::http::status_codes::OK };
            response.headers().set_content_type(web::http::details::mime_types::application_json);
            response.set_body(web::json::value::parse(CreateAvailableAppInUseResponse(
                -60,
                30 * 60,
                R"({
                    "Files": [
                        { "Path": "C:\\Program Files\\Contoso\\Contoso.exe", "Kind": "Executable" },
                        { "Path": "c:\\program files\\contoso\\contoso.exe", "Kind": "Executable" }
                    ],
                    "Services": [ { "Name": "ContosoSvc" }, { "Name": "contososvc" } ]
                })")));
            return pplx::task_from_result(response);
        });

    HttpClientHelper helper{ handler };
    RestClient client = RestClient::Create(TestRestUri + "/api", "TestHeader", {}, helper, CreateAppInUseInformation());
    auto binding = CreateInstallBinding();
    auto result = client.GetAppInUseMetadata(binding);

    REQUIRE(result.Status == AppInUseStatus::Available);
    REQUIRE(result.Binding == binding);
    REQUIRE(result.Generation == 7);
    REQUIRE(result.Files.size() == 1);
    REQUIRE(result.Services.size() == 1);
}

TEST_CASE("RestClient_AppInUseStatusesReuseAuthentication", "[RestSource][ExtendedMetadata]")
{
    Authentication::AuthenticationResult authenticationResult;
    authenticationResult.Status = S_OK;
    authenticationResult.Token = "TestToken";
    TestHook::SetAuthenticationResult_Override authenticationOverride{ authenticationResult };

    auto handler = std::make_shared<TestRestRequestHandler>(
        [](web::http::http_request request) -> pplx::task<web::http::http_response>
        {
            REQUIRE(request.headers().has(web::http::header_names::authorization));
            REQUIRE_FALSE(request.headers()[web::http::header_names::authorization].empty());
            web::http::http_response response{ web::http::status_codes::OK };
            response.headers().set_content_type(web::http::details::mime_types::application_json);
            response.set_body(web::json::value::parse(CreateAppInUseResponse(
                R"("Status": "Unavailable", "UnavailableReason": "TransitionNotAnalyzed")")));
            return pplx::task_from_result(response);
        });

    auto information = CreateAppInUseInformation();
    information.Authentication.Type = Authentication::AuthenticationType::MicrosoftEntraId;
    information.Authentication.MicrosoftEntraIdInfo =
        Authentication::MicrosoftEntraIdAuthenticationInfo{ "Resource", "Scope" };
    Authentication::AuthenticationArguments authenticationArguments;
    authenticationArguments.Mode = Authentication::AuthenticationMode::Silent;
    HttpClientHelper helper{ handler };
    RestClient client = RestClient::Create(TestRestUri, {}, {}, helper, information, authenticationArguments);

    auto result = client.GetAppInUseMetadata(CreateInstallBinding());
    REQUIRE(result.Status == AppInUseStatus::Unavailable);
    REQUIRE(result.UnavailableReason == AppInUseUnavailableReason::TransitionNotAnalyzed);

    auto errorResponse = CreateAppInUseResponse(
        R"("Status": "Error", "Errors": [ { "Code": "MetadataNotFound", "Message": "No metadata was found." } ])");
    HttpClientHelper errorHelper{ GetTestRestRequestHandler(web::http::status_codes::OK, errorResponse) };
    RestClient errorClient = RestClient::Create(TestRestUri, {}, {}, errorHelper, CreateAppInUseInformation());
    result = errorClient.GetAppInUseMetadata(CreateInstallBinding());
    REQUIRE(result.Status == AppInUseStatus::Error);
    REQUIRE(result.Errors.size() == 1);
    REQUIRE(result.Errors[0].Code == "MetadataNotFound");
}

TEST_CASE("RestClient_AppInUseUnavailableReasonsAreTyped", "[RestSource][ExtendedMetadata]")
{
    const std::vector<std::pair<std::string, AppInUseUnavailableReason>> reasons{
        { "NotSupported", AppInUseUnavailableReason::NotSupported },
        { "PackageNotFound", AppInUseUnavailableReason::PackageNotFound },
        { "CurrentVersionNotFound", AppInUseUnavailableReason::CurrentVersionNotFound },
        { "TargetVersionNotFound", AppInUseUnavailableReason::TargetVersionNotFound },
        { "InstallerNotMatched", AppInUseUnavailableReason::InstallerNotMatched },
        { "TransitionNotAnalyzed", AppInUseUnavailableReason::TransitionNotAnalyzed },
        { "OperationNotSupportedForPackage", AppInUseUnavailableReason::OperationNotSupportedForPackage },
    };

    for (const auto& [wireValue, expected] : reasons)
    {
        auto response = CreateAppInUseResponse(
            R"("Status": "Unavailable", "UnavailableReason": ")" + wireValue + R"(")");
        HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, response) };
        RestClient client = RestClient::Create(TestRestUri, {}, {}, helper, CreateAppInUseInformation());
        REQUIRE(client.GetAppInUseMetadata(CreateInstallBinding()).UnavailableReason == expected);
    }
}

TEST_CASE("RestClient_AppInUseOperationValidation", "[RestSource][ExtendedMetadata]")
{
    auto handler = std::make_shared<TestRestRequestHandler>(
        [](web::http::http_request request) -> pplx::task<web::http::http_response>
        {
            auto requestBody = request.extract_json().get();
            web::json::value data = web::json::value::object();
            data[_XPLATSTR("Binding")] = requestBody.at(_XPLATSTR("Binding"));
            data[_XPLATSTR("Status")] = web::json::value::string(_XPLATSTR("Unavailable"));
            data[_XPLATSTR("UnavailableReason")] = web::json::value::string(_XPLATSTR("TransitionNotAnalyzed"));
            web::json::value body = web::json::value::object();
            body[_XPLATSTR("Data")] = std::move(data);
            web::http::http_response response{ web::http::status_codes::OK };
            response.headers().set_content_type(web::http::details::mime_types::application_json);
            response.set_body(std::move(body));
            return pplx::task_from_result(response);
        });
    HttpClientHelper helper{ handler };
    RestClient client = RestClient::Create(TestRestUri, {}, {}, helper, CreateAppInUseInformation());
    auto binding = CreateInstallBinding();
    binding.CurrentVersion = "1.0";
    REQUIRE_THROWS_HR(client.GetAppInUseMetadata(binding), E_INVALIDARG);

    binding.Operation = AppInUseOperation::Upgrade;
    REQUIRE_NOTHROW(client.GetAppInUseMetadata(binding));
    binding.Operation = AppInUseOperation::Repair;
    binding.TargetVersion.reset();
    binding.TargetChannel.reset();
    REQUIRE_NOTHROW(client.GetAppInUseMetadata(binding));
    binding.Operation = AppInUseOperation::Uninstall;
    binding.SelectedInstaller.reset();
    REQUIRE_NOTHROW(client.GetAppInUseMetadata(binding));
    binding.TargetVersion = "2.0";
    REQUIRE_THROWS_HR(client.GetAppInUseMetadata(binding), E_INVALIDARG);
}

TEST_CASE("RestClient_AppInUseStrictResponseValidation", "[RestSource][ExtendedMetadata]")
{
    auto validateInvalidResponse = [](utility::string_t response)
    {
        HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, response) };
        RestClient client = RestClient::Create(TestRestUri, {}, {}, helper, CreateAppInUseInformation());
        REQUIRE_THROWS_HR(client.GetAppInUseMetadata(CreateInstallBinding()), APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
    };

    SECTION("Full binding must match")
    {
        validateInvalidResponse(JSON::GetUtilityString(R"({ "Data": { )") + GetInstallBindingJson("Other.App") +
            JSON::GetUtilityString(R"(, "Status": "Unavailable", "UnavailableReason": "PackageNotFound" } })"));
    }

    SECTION("Validity cannot exceed one hour")
    {
        validateInvalidResponse(CreateAvailableAppInUseResponse(
            -60, 60 * 60, R"({ "Files": [], "Services": [] })"));
    }

    SECTION("Expired metadata is rejected")
    {
        validateInvalidResponse(CreateAvailableAppInUseResponse(
            -15 * 60, -6 * 60, R"({ "Files": [], "Services": [] })"));
    }

    SECTION("Metadata generated too far in the future is rejected")
    {
        validateInvalidResponse(CreateAvailableAppInUseResponse(
            6 * 60, 30 * 60, R"({ "Files": [], "Services": [] })"));
    }

    SECTION("Files must be absolute")
    {
        validateInvalidResponse(CreateAvailableAppInUseResponse(
            -60,
            30 * 60,
            R"({
                "Files": [ { "Path": "relative\\Contoso.exe", "Kind": "Executable" } ],
                "Services": []
            })"));
    }

    SECTION("Resource bounds are enforced")
    {
        auto response = web::json::value::parse(CreateAvailableAppInUseResponse(
            -60, 30 * 60, R"({ "Files": [], "Services": [] })"));
        auto& services = response[_XPLATSTR("Data")][_XPLATSTR("Resources")][_XPLATSTR("Services")];
        for (size_t i = 0; i != 257; ++i)
        {
            services[i][_XPLATSTR("Name")] =
                web::json::value::string(_XPLATSTR("Service") + utility::conversions::to_string_t(std::to_string(i)));
        }
        validateInvalidResponse(response.serialize());
    }

    SECTION("Declared responses over one MiB are rejected")
    {
        auto handler = std::make_shared<TestRestRequestHandler>(
            [](web::http::http_request) -> pplx::task<web::http::http_response>
            {
                web::http::http_response response{ web::http::status_codes::OK };
                response.headers().set_content_type(web::http::details::mime_types::application_json);
                response.set_body(web::json::value::parse(CreateAppInUseResponse(
                    R"("Status": "Unavailable", "UnavailableReason": "PackageNotFound")")));
                response.headers().set_content_length(1024 * 1024 + 1);
                return pplx::task_from_result(response);
            });
        HttpClientHelper helper{ handler };
        RestClient client = RestClient::Create(TestRestUri, {}, {}, helper, CreateAppInUseInformation());
        REQUIRE_THROWS_HR(
            client.GetAppInUseMetadata(CreateInstallBinding()),
            APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
    }
}

TEST_CASE("GetInformation_Fail_AgreementsWithoutIdentifier", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.0.0",
                "1.1.0"],
              "SourceAgreements": {
                "Agreements": [{
                    "AgreementLabel": "EULA",
                    "Agreement": "this is store agreement",
                    "AgreementUrl": "https://store.agreement"
                  }
                ]
              }
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    REQUIRE_THROWS_HR(RestClient::GetInformation(TestRestUri, {}, {}, helper), APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);
}

TEST_CASE("GetInformation_Fail_InvalidMicrosoftEntraIdInfo", "[RestSource]")
{
    utility::string_t sample1 = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraId"
              }
        }})delimiter");

    HttpClientHelper helper1{ GetTestRestRequestHandler(web::http::status_codes::OK, sample1) };
    REQUIRE_THROWS_HR(RestClient::GetInformation(TestRestUri, {}, {}, helper1), APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);

    utility::string_t sample2 = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraId",
                "MicrosoftEntraIdAuthenticationInfo" : {
                  "Resource": "",
                  "Scope" : "test"
                }
              }
        }})delimiter");

    HttpClientHelper helper2{ GetTestRestRequestHandler(web::http::status_codes::OK, sample2) };
    REQUIRE_THROWS_HR(RestClient::GetInformation(TestRestUri, {}, {}, helper2), APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);

    utility::string_t sample3 = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraId",
                "MicrosoftEntraIdAuthenticationInfo" : {
                  "Scope" : "test"
                }
              }
        }})delimiter");

    HttpClientHelper helper3{ GetTestRestRequestHandler(web::http::status_codes::OK, sample3) };
    REQUIRE_THROWS_HR(RestClient::GetInformation(TestRestUri, {}, {}, helper3), APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);

    utility::string_t sample4 = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraIdForAzureBlobStorage"
              }
        }})delimiter");

    HttpClientHelper helper4{ GetTestRestRequestHandler(web::http::status_codes::OK, sample4) };
    Authentication::AuthenticationArguments authArgs;
    authArgs.Mode = Authentication::AuthenticationMode::Silent;
    Version version_1_7{ "1.7.0" };
    REQUIRE_THROWS_HR(RestClient::GetSupportedInterface(TestRestUri, {}, RestClient::GetInformation(TestRestUri, {}, {}, helper4), authArgs, version_1_7, {}), APPINSTALLER_CLI_ERROR_AUTHENTICATION_TYPE_NOT_SUPPORTED);
}

TEST_CASE("RestClientCreate_UnsupportedVersion", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.2.0",
                "2.0.0"]
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    REQUIRE_THROWS_HR(CreateRestClient("https://restsource.com/api", {}, {}, helper), APPINSTALLER_CLI_ERROR_UNSUPPORTED_RESTSOURCE);
}

TEST_CASE("RestClientCreate_UnsupportedAuthenticationMethod", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "Authentication": {
                "AuthenticationType": "unknown"
              }
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    Authentication::AuthenticationArguments authArgs;
    authArgs.Mode = Authentication::AuthenticationMode::Silent;
    REQUIRE_THROWS_HR(CreateRestClient("https://restsource.com/api", {}, {}, helper, authArgs), APPINSTALLER_CLI_ERROR_AUTHENTICATION_TYPE_NOT_SUPPORTED);
}

TEST_CASE("RestClientCreate_InvalidAuthenticationArguments", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraId",
                "MicrosoftEntraIdAuthenticationInfo" : {
                  "Resource" : "test"
                }
              }
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    Authentication::AuthenticationArguments authArgs;
    authArgs.Mode = Authentication::AuthenticationMode::Unknown;
    REQUIRE_THROWS_HR(CreateRestClient("https://restsource.com/api", {}, {}, helper, authArgs), E_UNEXPECTED);
}

TEST_CASE("RestClientCreate_1.0_Success", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.0.0",
                "2.0.0"]
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    RestClient client = CreateRestClient(TestRestUri, {}, {}, helper);
    REQUIRE(client.GetSourceIdentifier() == "Source123");
}

TEST_CASE("RestClientCreate_1.1_Success", "[RestSource]")
{
    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.0.0",
                "1.1.0"],
              "SourceAgreements": {
                "AgreementsIdentifier": "agreementV1",
                "Agreements": [{
                    "AgreementLabel": "EULA",
                    "Agreement": "this is store agreement",
                    "AgreementUrl": "https://store.agreement"
                  }
                ]
              },
              "RequiredQueryParameters": [
                "Market"
              ],
              "RequiredPackageMatchFields": [
                "Market"
              ],
              "UnsupportedQueryParameters": [
                "Moniker"
              ],
              "UnsupportedPackageMatchFields": [
                "Moniker"
              ]
        }})delimiter");

    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    RestClient client = CreateRestClient(TestRestUri, {}, {}, helper);
    REQUIRE(client.GetSourceIdentifier() == "Source123");
    auto information = client.GetSourceInformation();
    REQUIRE(information.SourceAgreementsIdentifier == "agreementV1");
    REQUIRE(information.SourceAgreements.size() == 1);
    REQUIRE(information.SourceAgreements.at(0).Label == "EULA");
    REQUIRE(information.SourceAgreements.at(0).Text == "this is store agreement");
    REQUIRE(information.SourceAgreements.at(0).Url == "https://store.agreement");
    REQUIRE(information.RequiredQueryParameters.size() == 1);
    REQUIRE(information.RequiredQueryParameters.at(0) == "Market");
    REQUIRE(information.RequiredPackageMatchFields.size() == 1);
    REQUIRE(information.RequiredPackageMatchFields.at(0) == "Market");
    REQUIRE(information.UnsupportedQueryParameters.size() == 1);
    REQUIRE(information.UnsupportedQueryParameters.at(0) == "Moniker");
    REQUIRE(information.UnsupportedPackageMatchFields.size() == 1);
    REQUIRE(information.UnsupportedPackageMatchFields.at(0) == "Moniker");
}

TEST_CASE("RestClientCreate_1.7_Success", "[RestSource]")
{
    if (Runtime::IsRunningAsSystem())
    {
        WARN("Test does not support running as system. Skipped.");
        return;
    }

    utility::string_t sample = _XPLATSTR(
        R"delimiter({
            "Data" : {
              "SourceIdentifier": "Source123",
              "ServerSupportedVersions": [
                "1.7.0"
               ],
              "SourceAgreements": {
                "AgreementsIdentifier": "agreementV1",
                "Agreements": [{
                    "AgreementLabel": "EULA",
                    "Agreement": "this is store agreement",
                    "AgreementUrl": "https://store.agreement"
                  }
                ]
              },
              "RequiredQueryParameters": [
                "Market"
              ],
              "RequiredPackageMatchFields": [
                "Market"
              ],
              "UnsupportedQueryParameters": [
                "Moniker"
              ],
              "UnsupportedPackageMatchFields": [
                "Moniker"
              ],
              "Authentication": {
                "AuthenticationType": "microsoftEntraId",
                "MicrosoftEntraIdAuthenticationInfo" : {
                  "Resource": "GUID",
                  "Scope" : "test"
                }
              }
        }})delimiter");

    Authentication::AuthenticationArguments authArgs;
    authArgs.Mode = Authentication::AuthenticationMode::Silent;
    HttpClientHelper helper{ GetTestRestRequestHandler(web::http::status_codes::OK, sample) };
    RestClient client = CreateRestClient(TestRestUri, {}, {}, helper, authArgs);
    REQUIRE(client.GetSourceIdentifier() == "Source123");
    auto information = client.GetSourceInformation();
    REQUIRE(information.SourceAgreementsIdentifier == "agreementV1");
    REQUIRE(information.SourceAgreements.size() == 1);
    REQUIRE(information.SourceAgreements.at(0).Label == "EULA");
    REQUIRE(information.SourceAgreements.at(0).Text == "this is store agreement");
    REQUIRE(information.SourceAgreements.at(0).Url == "https://store.agreement");
    REQUIRE(information.RequiredQueryParameters.size() == 1);
    REQUIRE(information.RequiredQueryParameters.at(0) == "Market");
    REQUIRE(information.RequiredPackageMatchFields.size() == 1);
    REQUIRE(information.RequiredPackageMatchFields.at(0) == "Market");
    REQUIRE(information.UnsupportedQueryParameters.size() == 1);
    REQUIRE(information.UnsupportedQueryParameters.at(0) == "Moniker");
    REQUIRE(information.UnsupportedPackageMatchFields.size() == 1);
    REQUIRE(information.UnsupportedPackageMatchFields.at(0) == "Moniker");
    REQUIRE(information.Authentication.Type == Authentication::AuthenticationType::MicrosoftEntraId);
    REQUIRE(information.Authentication.MicrosoftEntraIdInfo.has_value());
    REQUIRE(information.Authentication.MicrosoftEntraIdInfo->Resource == "GUID");
    REQUIRE(information.Authentication.MicrosoftEntraIdInfo->Scope == "test");
}

// Simulate the msstore cache round trip using real world data.
TEST_CASE("RestInformationCache_RoundTrip", "[RestInformationCache]")
{
    Settings::Stream{ Settings::Stream::RestInformationCache }.Remove();

    std::wstring endpoint = L"https://test-url-com/information";
    CacheControlPolicy cacheControl{ L"public, max-age=77287" };
    auto response = web::json::value::parse(
R"delimiter({
    "$type": "Microsoft.Marketplace.Storefront.StoreEdgeFD.BusinessLogic.Response.PackageMetadata.PackageMetadataResponse, StoreEdgeFD",
    "Data": {
        "$type": "Microsoft.Marketplace.Storefront.StoreEdgeFD.BusinessLogic.Response.PackageMetadata.PackageMetadataData, StoreEdgeFD",
        "SourceIdentifier": "StoreEdgeFD",
        "SourceAgreements": {
            "$type": "Microsoft.Marketplace.Storefront.StoreEdgeFD.BusinessLogic.Response.PackageMetadata.SourceAgreements, StoreEdgeFD",
            "AgreementsIdentifier": "StoreEdgeFD",
            "Agreements": [
                {
                    "$type": "Microsoft.Marketplace.Storefront.StoreEdgeFD.BusinessLogic.Response.PackageManifest.AgreementDetail, StoreEdgeFD",
                    "AgreementLabel": "Terms of Transaction",
                    "AgreementUrl": "https://aka.ms/microsoft-store-terms-of-transaction"
                }
            ]
        },
        "ServerSupportedVersions": [ "1.0.0", "1.1.0", "1.6.0" ],
        "RequiredQueryParameters": [ "market" ],
        "RequiredPackageMatchFields": [ "market" ]
    }
})delimiter");

    RestInformationCache cache;
    cache.Cache(endpoint, {}, {}, cacheControl, response);
    auto cachedValue = cache.Get(endpoint, {}, {});

    REQUIRE(cachedValue.has_value());

    InformationResponseDeserializer deserializer;
    const auto expected = deserializer.Deserialize(response);
    const auto& actual = cachedValue.value();

    REQUIRE(expected.SourceIdentifier == actual.SourceIdentifier);
    REQUIRE(expected.SourceAgreementsIdentifier == actual.SourceAgreementsIdentifier);
    REQUIRE(expected.SourceAgreements.size() == actual.SourceAgreements.size());
    REQUIRE(1 == actual.SourceAgreements.size());
    REQUIRE(expected.SourceAgreements[0].Label == actual.SourceAgreements[0].Label);
    REQUIRE(expected.SourceAgreements[0].Text == actual.SourceAgreements[0].Text);
    REQUIRE(expected.SourceAgreements[0].Url == actual.SourceAgreements[0].Url);

    REQUIRE(expected.ServerSupportedVersions.size() == actual.ServerSupportedVersions.size());
    for (const auto& expectedVersion : expected.ServerSupportedVersions)
    {
        REQUIRE(std::find(actual.ServerSupportedVersions.begin(), actual.ServerSupportedVersions.end(), expectedVersion) != actual.ServerSupportedVersions.end());
    }

    REQUIRE(expected.RequiredQueryParameters.size() == actual.RequiredQueryParameters.size());
    REQUIRE(1 == actual.RequiredQueryParameters.size());
    REQUIRE(expected.RequiredQueryParameters[0] == actual.RequiredQueryParameters[0]);

    REQUIRE(expected.RequiredPackageMatchFields.size() == actual.RequiredPackageMatchFields.size());
    REQUIRE(1 == actual.RequiredPackageMatchFields.size());
    REQUIRE(expected.RequiredPackageMatchFields[0] == actual.RequiredPackageMatchFields[0]);
}

web::json::value CreateInformationResponse(std::string_view identifier)
{
    std::ostringstream stream;
    stream << R"({ "Data": { "SourceIdentifier": ")" << identifier << R"(", "ServerSupportedVersions": [ "1.0.0" ] } })";

    return web::json::value::parse(stream.str());
}

TEST_CASE("RestInformationCache_Get", "[RestInformationCache]")
{
    Settings::Stream{ Settings::Stream::RestInformationCache }.Remove();

    std::wstring endpoint1 = L"https://test-url1-com/information";
    std::wstring endpoint2 = L"https://test-url2-com/information";
    std::wstring endpointNotPresent = L"https://test-url-not-present-com/information";
    std::string header = "Header";
    std::string caller = "Caller";
    std::string publicEndpoint1Identifier = "Identifier1";
    std::string privateEndpoint1Identifier = "Identifier2";
    std::string privateEndpoint2Identifier = "Identifier3";
    auto publicEndpoint1Response = CreateInformationResponse(publicEndpoint1Identifier);
    auto privateEndpoint1Response = CreateInformationResponse(privateEndpoint1Identifier);
    auto privateEndpoint2Response = CreateInformationResponse(privateEndpoint2Identifier);

    RestInformationCache cache;

    // Cache:
    //  1. public and private for same endpoint
    cache.Cache(endpoint1, header, caller, { L"public" }, publicEndpoint1Response);
    cache.Cache(endpoint1, header, caller, {}, privateEndpoint1Response);
    //  2. another endpoint with private data (same headers)
    cache.Cache(endpoint2, header, caller, {}, privateEndpoint2Response);

    SECTION("Same headers prefers private")
    {
        auto cachedValue = cache.Get(endpoint1, header, caller);
        REQUIRE(cachedValue.has_value());
        REQUIRE(privateEndpoint1Identifier == cachedValue->SourceIdentifier);
    }
    SECTION("Different headers falls back to public")
    {
        auto cachedValue = cache.Get(endpoint1, "Different", "Different");
        REQUIRE(cachedValue.has_value());
        REQUIRE(publicEndpoint1Identifier == cachedValue->SourceIdentifier);
    }
    SECTION("Second endpoint")
    {
        auto cachedValue = cache.Get(endpoint2, header, caller);
        REQUIRE(cachedValue.has_value());
        REQUIRE(privateEndpoint2Identifier == cachedValue->SourceIdentifier);
    }
    SECTION("Second endpoint different headers")
    {
        auto cachedValue = cache.Get(endpoint2, {}, {});
        REQUIRE(!cachedValue.has_value());
    }
    SECTION("Missing endpoint")
    {
        auto cachedValue = cache.Get(endpointNotPresent, header, caller);
        REQUIRE(!cachedValue.has_value());
    }
}

TEST_CASE("RestInformationCache_Cache_NoStore", "[RestInformationCache]")
{
    Settings::Stream{ Settings::Stream::RestInformationCache }.Remove();

    std::wstring endpoint = L"https://test-url-com/information";

    RestInformationCache cache;
    cache.Cache(endpoint, {}, {}, { L"no-store" }, CreateInformationResponse("Identifier"));

    auto cachedValue = cache.Get(endpoint, {}, {});
    REQUIRE(!cachedValue.has_value());
}

TEST_CASE("RestInformationCache_Cache_Expiration", "[RestInformationCache]")
{
    Settings::Stream{ Settings::Stream::RestInformationCache }.Remove();

    std::wstring endpoint = L"https://test-url-com/information";

    RestInformationCache cache;
    cache.Cache(endpoint, {}, {}, { L"max-age=2" }, CreateInformationResponse("Identifier"));

    auto cachedValue = cache.Get(endpoint, {}, {});
    REQUIRE(cachedValue.has_value());

    std::this_thread::sleep_for(5s);

    cachedValue = cache.Get(endpoint, {}, {});
    REQUIRE(!cachedValue.has_value());
}

TEST_CASE("RestInformationCache_Cache_Overwrite", "[RestInformationCache]")
{
    Settings::Stream{ Settings::Stream::RestInformationCache }.Remove();

    std::wstring endpoint = L"https://test-url-com/information";
    std::string identifier1 = "Identifier1";
    std::string identifier2 = "Identifier2";

    RestInformationCache cache;
    cache.Cache(endpoint, {}, {}, {}, CreateInformationResponse(identifier1));

    auto cachedValue = cache.Get(endpoint, {}, {});
    REQUIRE(cachedValue.has_value());
    REQUIRE(identifier1 == cachedValue->SourceIdentifier);

    cache.Cache(endpoint, {}, {}, {}, CreateInformationResponse(identifier2));

    cachedValue = cache.Get(endpoint, {}, {});
    REQUIRE(cachedValue.has_value());
    REQUIRE(identifier2 == cachedValue->SourceIdentifier);
}

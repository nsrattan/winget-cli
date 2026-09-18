// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "Rest/Schema/1_28/Interface.h"

namespace AppInstaller::Repository::Rest::Schema::V1_29
{
struct Interface : public V1_28::Interface
{
    Interface(
        const std::string &restApi, const Http::HttpClientHelper &helper,
        IRestClient::Information information,
        const Http::HttpClientHelper::HttpRequestHeaders &additionalHeaders = {},
        Authentication::AuthenticationArguments authArgs = {});

    Interface(const Interface &) = delete;
    Interface &operator=(const Interface &) = delete;

    Interface(Interface &&) = default;
    Interface &operator=(Interface &&) = default;

    Utility::Version GetVersion() const override;
    bool IsOperationSupported(AppInUseOperation operation) const override;
    AppInUseResult
    GetAppInUseMetadata(const AppInUseBinding &binding) const override;

    // Shared by the HTTP transport and the test-hook-only local validation provider.
    static AppInUseResult DeserializeAppInUseResponse(
        const web::json::value &response, const AppInUseBinding &binding);

  private:
    utility::string_t m_appInUseEndpoint;
    Http::HttpClientHelper m_httpClientHelper;
};
} // namespace AppInstaller::Repository::Rest::Schema::V1_29

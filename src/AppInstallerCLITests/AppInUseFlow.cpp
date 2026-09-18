// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "TestCommon.h"
#include "TestSource.h"
#include "WorkflowCommon.h"

#include <AppInUse.h>
#include <AppInstallerSHA256.h>
#include <Workflows\AppInUseFlow.h>
#include <winget/ManifestYamlParser.h>

using namespace AppInstaller;
using namespace AppInstaller::CLI;
using namespace AppInstaller::CLI::Execution;
using namespace AppInstaller::CLI::Workflow;
using namespace AppInstaller::Manifest;
using namespace AppInstaller::Repository;
using namespace TestCommon;

namespace
{
    struct AppInUseTestContext
    {
        AppInUseTestContext()
        {
            Manifest = YamlParser::CreateFromPath(TestDataFile("InstallFlowTest_Exe.yaml"));
            Manifest.Installers.at(0).Sha256.assign(Utility::SHA256::HashBufferSizeInBytes, 0x42);
            Manifest.Installers.at(0).Url = "https://example.test/installer.exe";
            Source = std::make_shared<TestSource>();
            PackageVersion = TestPackageVersion::Make(Manifest, Source);

            Context.Add<Data::Manifest>(Manifest);
            Context.Add<Data::Installer>(Manifest.Installers.at(0));
            Context.Add<Data::PackageVersion>(PackageVersion);
        }

        std::ostringstream Output;
        TestContext Context{ Output, std::cin };
        AppInstaller::Manifest::Manifest Manifest;
        std::shared_ptr<TestSource> Source;
        std::shared_ptr<TestPackageVersion> PackageVersion;
    };

    AppInUseCapability AvailableCapability()
    {
        AppInUseCapability result;
        result.SourceEligible = true;
        result.ContractSupported = true;
        result.OfferingEnabled = true;
        result.SchemaSupported = true;
        result.OperationSupported = true;
        return result;
    }
}

TEST_CASE("AppInUseFlow_DisabledDoesNotContactSource", "[AppInUseFlow][workflow]")
{
    AppInUseTestContext test;
    bool capabilityCalled = false;
    bool metadataCalled = false;
    test.Source->GetAppInUseCapabilityFunction = [&](AppInUseOperation)
    {
        capabilityCalled = true;
        return AvailableCapability();
    };
    test.Source->GetAppInUseMetadataFunction = [&](const AppInUseBinding& binding)
    {
        metadataCalled = true;
        AppInstaller::Repository::AppInUseResult result;
        result.Binding = binding;
        return result;
    };

    test.Context << AppInUseGate(AppInUseOperation::Install, InstallerTypeEnum::Exe);

    REQUIRE_FALSE(test.Context.IsTerminated());
    REQUIRE_FALSE(test.Context.Contains(Data::AppInUseResult));
    REQUIRE_FALSE(capabilityCalled);
    REQUIRE_FALSE(metadataCalled);
}

TEST_CASE("AppInUseFlow_UnavailableCapabilityPreservesInstallerFlow", "[AppInUseFlow][workflow]")
{
    AppInUseTestContext test;
    size_t metadataCalls = 0;
    test.Source->GetAppInUseCapabilityFunction = [](AppInUseOperation)
    {
        return AppInUseCapability{};
    };
    test.Source->GetAppInUseMetadataFunction = [&](const AppInUseBinding& binding)
    {
        ++metadataCalls;
        AppInstaller::Repository::AppInUseResult result;
        result.Binding = binding;
        return result;
    };
    test.Context.Args.AddArg(Args::Type::AppInUseBehavior, std::string{ "probe" });

    test.Context << AppInUseGate(AppInUseOperation::Install, InstallerTypeEnum::Exe);

    REQUIRE_FALSE(test.Context.IsTerminated());
    REQUIRE(test.Context.Contains(Data::AppInUseResult));
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Status == AppInUseResultStatus::MetadataUnavailable);
    REQUIRE(metadataCalls == 0);
}

TEST_CASE("AppInUseFlow_ProbeEmptyResources", "[AppInUseFlow][workflow]")
{
    AppInUseTestContext test;
    size_t metadataCalls = 0;
    test.Source->GetAppInUseCapabilityFunction = [](AppInUseOperation)
    {
        return AvailableCapability();
    };
    test.Source->GetAppInUseMetadataFunction = [&](const AppInUseBinding& binding)
    {
        ++metadataCalls;
        REQUIRE(binding.Operation == AppInUseOperation::Install);
        REQUIRE(binding.PackageIdentifier == test.Manifest.Id);
        REQUIRE(binding.TargetVersion == test.Manifest.Version);
        REQUIRE(binding.SelectedInstaller.has_value());

        AppInstaller::Repository::AppInUseResult result;
        result.Status = AppInUseStatus::Available;
        result.Binding = binding;
        result.GeneratedAt = "2026-09-10T00:00:00Z";
        result.ValidUntil = "2026-09-10T00:30:00Z";
        result.Generation = 1;
        return result;
    };
    test.Context.Args.AddArg(Args::Type::AppInUseBehavior, std::string{ "probe" });

    test.Context << AppInUseGate(AppInUseOperation::Install, InstallerTypeEnum::Exe);

    REQUIRE_FALSE(test.Context.IsTerminated());
    INFO("Output: " << test.Output.str());
    INFO("Status: " << static_cast<int>(test.Context.Get<Data::AppInUseResult>().Status));
    INFO("Reason: " << static_cast<int>(test.Context.Get<Data::AppInUseResult>().Reason));
    REQUIRE(metadataCalls == 1);
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Status == AppInUseResultStatus::NotInUse);
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Probe.has_value());
    REQUIRE_FALSE(test.Context.Get<Data::AppInUseResult>().Probe->IsInUse());
}

TEST_CASE("AppInUseFlow_MsixProbeIsNotApplicable", "[AppInUseFlow][workflow]")
{
    AppInUseTestContext test;
    test.Context.Args.AddArg(Args::Type::AppInUseBehavior, std::string{ "probe" });

    test.Context << AppInUseGate(AppInUseOperation::Install, InstallerTypeEnum::Msix);

    REQUIRE(test.Context.IsTerminated());
    REQUIRE(test.Context.GetTerminationHR() == APPINSTALLER_CLI_ERROR_APP_IN_USE_POLICY_NOT_SUPPORTED);
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Status == AppInUseResultStatus::NotApplicable);
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Reason == AppInUseResultReason::SideEffectFreeProbeUnavailable);
}

TEST_CASE("AppInUseFlow_MsixForceUsesPlatformDeployment", "[AppInUseFlow][workflow]")
{
    AppInUseTestContext test;
    test.Context.Args.AddArg(Args::Type::AppInUseBehavior, std::string{ "force-shutdown" });

    test.Context << AppInUseGate(AppInUseOperation::Install, InstallerTypeEnum::Msix);

    REQUIRE_FALSE(test.Context.IsTerminated());
    REQUIRE(test.Context.Contains(Data::AppInUseResult));
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Status == AppInUseResultStatus::DelegatedToInstaller);
    REQUIRE(test.Context.Get<Data::AppInUseResult>().Reason == AppInUseResultReason::InstallerTypeUsesPlatformDeployment);
}

TEST_CASE("AppInUseFlow_UninstallUsesAvailableSourcePackageIdentifier", "[AppInUseFlow][workflow]")
{
    std::ostringstream output;
    TestContext context{ output, std::cin };
    auto source = std::make_shared<TestSource>();
    auto availableManifest = YamlParser::CreateFromPath(TestDataFile("InstallFlowTest_Exe.yaml"));
    auto installedManifest = availableManifest;
    installedManifest.Id = "ARP\\Machine\\X64\\SyntheticProductCode";

    auto package = TestCompositePackage::Make(
        installedManifest,
        TestCompositePackage::MetadataMap
        {
            { PackageVersionMetadata::InstalledType, "Exe" },
            { PackageVersionMetadata::InstalledScope, "Machine" },
        },
        std::vector<AppInstaller::Manifest::Manifest>{ availableManifest },
        source);

    context.Add<Data::Package>(package);
    context.Add<Data::InstalledPackageVersion>(package->Installed->GetLatestVersion());
    context.Args.AddArg(Args::Type::AppInUseBehavior, std::string{ "probe" });

    source->GetAppInUseCapabilityFunction = [](AppInUseOperation)
    {
        return AvailableCapability();
    };
    source->GetAppInUseMetadataFunction = [&](const AppInUseBinding& binding)
    {
        REQUIRE(binding.PackageIdentifier == availableManifest.Id);
        REQUIRE(binding.CurrentVersion == installedManifest.Version);

        AppInstaller::Repository::AppInUseResult result;
        result.Status = AppInUseStatus::Available;
        result.Binding = binding;
        result.GeneratedAt = "2026-09-10T00:00:00Z";
        result.ValidUntil = "2026-09-10T00:30:00Z";
        result.Generation = 1;
        return result;
    };

    context << AppInUseGate(AppInUseOperation::Uninstall, InstallerTypeEnum::Exe);

    REQUIRE_FALSE(context.IsTerminated());
    REQUIRE(context.Get<Data::AppInUseResult>().Status == AppInUseResultStatus::NotInUse);
}

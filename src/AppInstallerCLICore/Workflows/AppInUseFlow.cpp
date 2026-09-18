// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "AppInUseFlow.h"
#include "AppInUse.h"
#include "Resources.h"
#include "TableOutput.h"

#include <AppInstallerErrors.h>
#include <AppInstallerLogging.h>
#include <AppInstallerSHA256.h>
#include <AppInstallerStrings.h>
#include <winget/RestartManager.h>

namespace AppInstaller::CLI::Workflow
{
    namespace
    {
        AppInUsePolicy GetPolicy(const Execution::Context& context)
        {
            std::string_view value = context.Args.GetArg(Execution::Args::Type::AppInUseBehavior);
            if (value == "probe")
            {
                return AppInUsePolicy::Probe;
            }
            else if (value == "shutdown")
            {
                return AppInUsePolicy::Shutdown;
            }
            else if (value == "force-shutdown")
            {
                return AppInUsePolicy::ForceShutdown;
            }

            return AppInUsePolicy::Disabled;
        }

        void SetResult(
            Execution::Context& context,
            AppInUseResultStatus status,
            AppInUseResultReason reason,
            HRESULT error = S_OK)
        {
            AppInUseResult result;
            result.Status = status;
            result.Reason = reason;
            result.ExtendedErrorCode = error;
            context.Add<Execution::Data::AppInUseResult>(std::move(result));
        }

        void SetNotApplicableAndTerminate(Execution::Context& context, AppInUseResultReason reason)
        {
            SetResult(
                context,
                AppInUseResultStatus::NotApplicable,
                reason,
                APPINSTALLER_CLI_ERROR_APP_IN_USE_POLICY_NOT_SUPPORTED);
            AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_APP_IN_USE_POLICY_NOT_SUPPORTED);
        }

        Resource::LocString GetApplicationType(RestartManager::ApplicationType type)
        {
            switch (type)
            {
            case RestartManager::ApplicationType::MainWindow:
                return Resource::LocString{ Resource::String::AppInUseTypeMainWindow };
            case RestartManager::ApplicationType::OtherWindow:
                return Resource::LocString{ Resource::String::AppInUseTypeOtherWindow };
            case RestartManager::ApplicationType::Service:
                return Resource::LocString{ Resource::String::AppInUseTypeService };
            case RestartManager::ApplicationType::Explorer:
                return Resource::LocString{ Resource::String::AppInUseTypeExplorer };
            case RestartManager::ApplicationType::Console:
                return Resource::LocString{ Resource::String::AppInUseTypeConsole };
            case RestartManager::ApplicationType::Critical:
                return Resource::LocString{ Resource::String::AppInUseTypeCritical };
            default:
                return Resource::LocString{ Resource::String::AppInUseTypeUnknown };
            }
        }

        void ReportBlockers(Execution::Context& context, const std::vector<RestartManager::Blocker>& blockers)
        {
            if (blockers.empty())
            {
                return;
            }

            context.Reporter.Error() << Resource::String::AppInUseBlockersFound << std::endl;
            Execution::TableOutput<6> table(
                context.Reporter,
                {
                    Resource::String::AppInUseColumnName,
                    Resource::String::AppInUseColumnPid,
                    Resource::String::AppInUseColumnSession,
                    Resource::String::AppInUseColumnType,
                    Resource::String::AppInUseColumnRestartable,
                    Resource::String::AppInUseColumnWindows,
                });

            for (const auto& blocker : blockers)
            {
                std::vector<std::string> titles;
                titles.reserve(blocker.WindowTitles.size());
                for (const auto& title : blocker.WindowTitles)
                {
                    titles.emplace_back(Utility::ConvertToUTF8(title));
                }

                table.OutputLine(
                    {
                        Utility::ConvertToUTF8(blocker.ServiceName.empty() ? blocker.ApplicationName : blocker.ServiceName),
                        blocker.ProcessId ? std::to_string(blocker.ProcessId) : std::string{},
                        std::to_string(blocker.SessionId),
                        GetApplicationType(blocker.Type).get(),
                        Resource::LocString{
                            blocker.IsRestartable ?
                                Resource::String::PromptOptionYes :
                                Resource::String::PromptOptionNo }.get(),
                        Utility::Join(" | ", titles),
                    });
            }

            table.Complete();
        }

        bool HandlePlatformInstaller(
            Execution::Context& context,
            AppInUsePolicy policy,
            Repository::AppInUseOperation operation,
            Manifest::InstallerTypeEnum installerType)
        {
            switch (installerType)
            {
            case Manifest::InstallerTypeEnum::Msix:
            case Manifest::InstallerTypeEnum::MSStore:
                if (policy == AppInUsePolicy::ForceShutdown && operation != Repository::AppInUseOperation::Uninstall)
                {
                    SetResult(
                        context,
                        AppInUseResultStatus::DelegatedToInstaller,
                        AppInUseResultReason::InstallerTypeUsesPlatformDeployment);
                    return true;
                }

                SetNotApplicableAndTerminate(
                    context,
                    policy == AppInUsePolicy::Probe ?
                        AppInUseResultReason::SideEffectFreeProbeUnavailable :
                        (policy == AppInUsePolicy::Shutdown ?
                            AppInUseResultReason::GracefulShutdownUnavailable :
                            AppInUseResultReason::InstallerTypeUsesPlatformDeployment));
                return true;
            case Manifest::InstallerTypeEnum::Font:
                SetNotApplicableAndTerminate(context, AppInUseResultReason::InstallerTypeHasNoAppInUseSemantics);
                return true;
            case Manifest::InstallerTypeEnum::Unknown:
                SetNotApplicableAndTerminate(context, AppInUseResultReason::UnsupportedInstallerType);
                return true;
            case Manifest::InstallerTypeEnum::Zip:
                // The v1 binding does not yet carry both outer archive and nested installer identities.
                SetNotApplicableAndTerminate(context, AppInUseResultReason::UnsupportedInstallerType);
                return true;
            default:
                return false;
            }
        }

        std::optional<std::string> GetMetadataValue(
            const Repository::IPackageVersion::Metadata& metadata,
            Repository::PackageVersionMetadata property)
        {
            auto value = metadata.find(property);
            if (value == metadata.end() || value->second.empty())
            {
                return std::nullopt;
            }

            return value->second;
        }

        struct MetadataSource
        {
            Repository::Source Source;
            std::string PackageIdentifier;
        };

        std::optional<MetadataSource> GetSource(
            Execution::Context& context,
            Repository::AppInUseOperation operation)
        {
            if (context.Contains(Execution::Data::PackageVersion))
            {
                const auto& packageVersion = context.Get<Execution::Data::PackageVersion>();
                if (packageVersion)
                {
                    return MetadataSource
                    {
                        packageVersion->GetSource(),
                        packageVersion->GetProperty(Repository::PackageVersionProperty::Id),
                    };
                }
            }

            if (!context.Contains(Execution::Data::Package))
            {
                return std::nullopt;
            }

            std::optional<MetadataSource> result;
            for (const auto& availablePackage : context.Get<Execution::Data::Package>()->GetAvailable())
            {
                auto version = availablePackage->GetLatestVersion();
                if (!version)
                {
                    continue;
                }

                Repository::Source candidate = version->GetSource();
                if (!candidate.GetAppInUseCapability(operation).IsAvailable())
                {
                    continue;
                }

                std::string packageIdentifier =
                    version->GetProperty(Repository::PackageVersionProperty::Id);
                if (packageIdentifier.empty())
                {
                    continue;
                }
                if (result &&
                    (result->Source != candidate || result->PackageIdentifier != packageIdentifier))
                {
                    return std::nullopt;
                }

                result = MetadataSource
                {
                    std::move(candidate),
                    std::move(packageIdentifier),
                };
            }

            return result;
        }

        std::optional<Repository::AppInUseSelectedInstaller> GetSelectedInstaller(Execution::Context& context)
        {
            if (!context.Contains(Execution::Data::Installer))
            {
                return std::nullopt;
            }

            const auto& installerValue = context.Get<Execution::Data::Installer>();
            if (!installerValue || installerValue->Sha256.empty())
            {
                return std::nullopt;
            }

            const auto& installer = installerValue.value();
            Repository::AppInUseSelectedInstaller result;
            result.Sha256 = Utility::SHA256::ConvertToString(installer.Sha256);
            result.FileName = Utility::GetFileNameFromURI(installer.Url).u8string();
            result.Architecture = std::string{ Utility::ToString(installer.Arch) };
            result.EffectiveInstallerType = std::string{ Manifest::InstallerTypeToString(installer.EffectiveInstallerType()) };
            if (installer.Scope != Manifest::ScopeEnum::Unknown)
            {
                result.Scope = Manifest::ScopeToString(installer.Scope);
            }
            if (!installer.Locale.empty())
            {
                result.Locale = installer.Locale;
            }

            if (result.FileName.empty())
            {
                return std::nullopt;
            }

            return result;
        }

        std::optional<Repository::AppInUseBinding> GetBinding(
            Execution::Context& context,
            Repository::AppInUseOperation operation,
            std::string_view sourcePackageIdentifier)
        {
            std::shared_ptr<Repository::IPackageVersion> current;
            if (context.Contains(Execution::Data::InstalledPackageVersion))
            {
                current = context.Get<Execution::Data::InstalledPackageVersion>();
            }

            std::shared_ptr<Repository::IPackageVersion> target;
            if (context.Contains(Execution::Data::PackageVersion))
            {
                target = context.Get<Execution::Data::PackageVersion>();
            }

            Repository::AppInUseBinding result;
            result.Operation = operation;

            if (!sourcePackageIdentifier.empty())
            {
                result.PackageIdentifier = sourcePackageIdentifier;
            }
            else if (target)
            {
                result.PackageIdentifier = target->GetProperty(Repository::PackageVersionProperty::Id);
            }
            else if (current)
            {
                result.PackageIdentifier = current->GetProperty(Repository::PackageVersionProperty::Id);
            }

            if (result.PackageIdentifier.empty())
            {
                return std::nullopt;
            }

            if (current)
            {
                result.CurrentVersion = current->GetProperty(Repository::PackageVersionProperty::Version);
                std::string channel = current->GetProperty(Repository::PackageVersionProperty::Channel);
                if (!channel.empty())
                {
                    result.CurrentChannel = std::move(channel);
                }

                result.CurrentScope = GetMetadataValue(
                    current->GetMetadata(), Repository::PackageVersionMetadata::InstalledScope);
            }

            if (operation == Repository::AppInUseOperation::Install ||
                operation == Repository::AppInUseOperation::Upgrade)
            {
                if (!target)
                {
                    return std::nullopt;
                }

                result.TargetVersion = target->GetProperty(Repository::PackageVersionProperty::Version);
                std::string channel = target->GetProperty(Repository::PackageVersionProperty::Channel);
                if (!channel.empty())
                {
                    result.TargetChannel = std::move(channel);
                }
            }

            if (operation != Repository::AppInUseOperation::Uninstall)
            {
                result.SelectedInstaller = GetSelectedInstaller(context);
                if (!result.SelectedInstaller)
                {
                    return std::nullopt;
                }
            }

            if ((operation == Repository::AppInUseOperation::Upgrade ||
                 operation == Repository::AppInUseOperation::Repair ||
                 operation == Repository::AppInUseOperation::Uninstall) &&
                !result.CurrentVersion)
            {
                return std::nullopt;
            }

            return result;
        }

        RestartManager::ResourceSet GetRestartManagerResources(const Repository::AppInUseResult& metadata)
        {
            RestartManager::ResourceSet result;
            result.Files.reserve(metadata.Files.size());
            result.ServiceNames.reserve(metadata.Services.size());

            for (const auto& file : metadata.Files)
            {
                result.Files.emplace_back(file.Path);
            }
            for (const auto& service : metadata.Services)
            {
                result.ServiceNames.emplace_back(Utility::ConvertToUTF16(service.Name));
            }

            return result;
        }

        void RunRestartManager(
            Execution::Context& context,
            AppInUsePolicy policy,
            const RestartManager::ResourceSet& resources)
        {
            if (resources.Files.empty() && resources.ServiceNames.empty())
            {
                AppInUseResult result;
                result.Status = AppInUseResultStatus::NotInUse;
                if (policy == AppInUsePolicy::Probe)
                {
                    result.Probe.emplace();
                }
                else
                {
                    result.Shutdown.emplace();
                }
                context.Add<Execution::Data::AppInUseResult>(std::move(result));
                return;
            }

            try
            {
                RestartManager::Coordinator coordinator;
                if (policy == AppInUsePolicy::Probe)
                {
                    RestartManager::ProbeResult probe = coordinator.Probe(resources);
                    AppInUseResult result;
                    result.Status = probe.IsInUse() ? AppInUseResultStatus::InUse : AppInUseResultStatus::NotInUse;
                    result.Probe = std::move(probe);
                    context.Add<Execution::Data::AppInUseResult>(std::move(result));

                    if (context.Get<Execution::Data::AppInUseResult>().Probe->IsInUse())
                    {
                        ReportBlockers(
                            context,
                            context.Get<Execution::Data::AppInUseResult>().Probe->Blockers);
                        AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_APP_IN_USE);
                    }
                }
                else
                {
                    RestartManager::ShutdownResult shutdown = coordinator.Shutdown(
                        resources,
                        policy == AppInUsePolicy::ForceShutdown ?
                            RestartManager::ShutdownType::Force :
                            RestartManager::ShutdownType::Graceful);

                    AppInUseResult result;
                    result.Shutdown = std::move(shutdown);
                    const auto& shutdownResult = result.Shutdown.value();
                    if (shutdownResult.Status == RestartManager::ShutdownStatus::NotNeeded)
                    {
                        result.Status = AppInUseResultStatus::NotInUse;
                    }
                    else if (shutdownResult.Status == RestartManager::ShutdownStatus::Completed)
                    {
                        result.Status = AppInUseResultStatus::ShutdownCompleted;
                    }
                    else
                    {
                        result.Status = AppInUseResultStatus::ShutdownIncomplete;
                        result.ExtendedErrorCode =
                            shutdownResult.Status == RestartManager::ShutdownStatus::Failed ?
                                APPINSTALLER_CLI_ERROR_RESTART_MANAGER_FAILED :
                                APPINSTALLER_CLI_ERROR_APP_SHUTDOWN_INCOMPLETE;
                    }

                    context.Add<Execution::Data::AppInUseResult>(std::move(result));
                    HRESULT error = context.Get<Execution::Data::AppInUseResult>().ExtendedErrorCode;
                    if (FAILED(error))
                    {
                        ReportBlockers(
                            context,
                            context.Get<Execution::Data::AppInUseResult>().Shutdown->Final.Blockers);
                        AICLI_TERMINATE_CONTEXT(error);
                    }
                }
            }
            catch (const wil::ResultException&)
            {
                SetResult(
                    context,
                    AppInUseResultStatus::ProbeError,
                    AppInUseResultReason::None,
                    APPINSTALLER_CLI_ERROR_RESTART_MANAGER_FAILED);
                AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_RESTART_MANAGER_FAILED);
            }
        }
    }

    void AppInUseGate::operator()(Execution::Context& context) const
    {
        AppInUsePolicy policy = GetPolicy(context);
        if (policy == AppInUsePolicy::Disabled)
        {
            return;
        }

        if (HandlePlatformInstaller(context, policy, m_operation, m_installerType))
        {
            return;
        }

        if (context.Contains(Execution::Data::Installer) &&
            context.Get<Execution::Data::Installer>() &&
            Manifest::IsArchiveType(context.Get<Execution::Data::Installer>()->BaseInstallerType))
        {
            SetNotApplicableAndTerminate(context, AppInUseResultReason::UnsupportedInstallerType);
            return;
        }

        auto source = GetSource(context, m_operation);
        if (!source)
        {
            SetResult(context, AppInUseResultStatus::MetadataUnavailable, AppInUseResultReason::SourceIneligible);
            context.Reporter.Warn() << Resource::String::AppInUseMetadataUnavailableWarning << std::endl;
            return;
        }

        Repository::AppInUseCapability capability = source->Source.GetAppInUseCapability(m_operation);
        if (!capability.IsAvailable())
        {
            SetResult(
                context,
                AppInUseResultStatus::MetadataUnavailable,
                capability.SourceEligible ?
                    AppInUseResultReason::OfferingUnavailable :
                    AppInUseResultReason::SourceIneligible);
            context.Reporter.Warn() << Resource::String::AppInUseMetadataUnavailableWarning << std::endl;
            return;
        }

        auto binding = GetBinding(context, m_operation, source->PackageIdentifier);
        if (!binding)
        {
            SetResult(context, AppInUseResultStatus::MetadataUnavailable, AppInUseResultReason::TransitionUnavailable);
            context.Reporter.Warn() << Resource::String::AppInUseMetadataUnavailableWarning << std::endl;
            return;
        }

        Repository::AppInUseResult metadata;
        try
        {
            metadata = source->Source.GetAppInUseMetadata(*binding);
        }
        catch (const wil::ResultException& e)
        {
            HRESULT error = e.GetErrorCode();
            bool invalid = error == APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID;
            SetResult(
                context,
                invalid ? AppInUseResultStatus::InvalidMetadata : AppInUseResultStatus::SourceError,
                AppInUseResultReason::None,
                invalid ? error : APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED);
            AICLI_TERMINATE_CONTEXT(
                invalid ? APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID :
                    APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED);
        }

        if (!(metadata.Binding == *binding))
        {
            SetResult(
                context,
                AppInUseResultStatus::InvalidMetadata,
                AppInUseResultReason::None,
                APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
            AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
        }

        switch (metadata.Status)
        {
        case Repository::AppInUseStatus::Available:
            RunRestartManager(context, policy, GetRestartManagerResources(metadata));
            break;
        case Repository::AppInUseStatus::Unavailable:
            SetResult(context, AppInUseResultStatus::MetadataUnavailable, AppInUseResultReason::TransitionUnavailable);
            context.Reporter.Warn() << Resource::String::AppInUseMetadataUnavailableWarning << std::endl;
            break;
        case Repository::AppInUseStatus::Error:
            SetResult(
                context,
                AppInUseResultStatus::SourceError,
                AppInUseResultReason::None,
                APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED);
            AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_FETCH_FAILED);
        default:
            SetResult(
                context,
                AppInUseResultStatus::InvalidMetadata,
                AppInUseResultReason::None,
                APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
            AICLI_TERMINATE_CONTEXT(APPINSTALLER_CLI_ERROR_EXTENDED_METADATA_INVALID);
        }
    }
}

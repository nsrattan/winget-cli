// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "ExecutionContext.h"
#include <winget/Manifest.h>
#include <winget/RepositorySource.h>

namespace AppInstaller::CLI::Workflow
{
    struct AppInUseGate : public WorkflowTask
    {
        AppInUseGate(Repository::AppInUseOperation operation, Manifest::InstallerTypeEnum installerType) :
            WorkflowTask("AppInUseGate"), m_operation(operation), m_installerType(installerType) {}

        void operator()(Execution::Context& context) const override;

    private:
        Repository::AppInUseOperation m_operation;
        Manifest::InstallerTypeEnum m_installerType;
    };
}

#pragma once

#include "framework/application/application.h"

CommonConfiguration BuildDefaultCommonConfiguration(const IApplication& application);
bool ValidateCommonConfiguration(const CommonConfiguration& common_configuration, std::string& error);
bool LoadConfiguration(CommonConfiguration& common_configuration,
                       const std::string& main_path,
                       const std::string& override_path,
                       std::unique_ptr<IApplicationConfiguration>& application_configuration,
                       IApplication& application);

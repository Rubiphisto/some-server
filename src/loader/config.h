#pragma once

#include "framework/application/application.h"

class IApplication;
struct StartupOptions;

ApplicationContext BuildDefaultContext(const StartupOptions& options, const IApplication& application);
bool LoadYamlIntoContext(ApplicationContext& context, bool config_path_explicit, bool verbose);
void ApplyCliOverrides(ApplicationContext& context, const StartupOptions& options);
bool ValidateContext(const ApplicationContext& context);
bool ResolveConfiguration(ApplicationContext& context, const StartupOptions& options, const IApplication& application);

#pragma once

#include "framework/application/application.h"

class IApplication;
struct StartupOptions;

bool ResolveConfiguration(ApplicationContext& context, const StartupOptions& options, const IApplication& application);

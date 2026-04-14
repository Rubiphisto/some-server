#include "pch.h"
#include "application.h"

std::unique_ptr<IApplication> ApplicationFactory::Create()
{
    return std::make_unique<Application>();
}


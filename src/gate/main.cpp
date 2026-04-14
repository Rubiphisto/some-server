#include "pch.h"

#include "application.h"
#include "loader/application/loader.h"

int32_t main(int32_t argc, char* argv[])
{
    ApplicationFactory factory;
    Loader loader(factory);
    return loader.Run(argc, argv);
}

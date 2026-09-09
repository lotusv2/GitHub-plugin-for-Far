#pragma once

#include <plugin.hpp>

extern PluginStartupInfo GPluginInfo;
extern FarStandardFunctions GFarFunctions;
extern const GUID MainGuid;
extern const GUID MenuGuid;

const wchar_t* GetPluginMessage(int id);

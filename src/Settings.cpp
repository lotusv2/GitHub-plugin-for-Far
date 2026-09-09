#include "Settings.hpp"
#include "Plugin.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace
{
const wchar_t* TokenName = L"Token";
}

bool GitHubSettings::Protect(const std::wstring& value, std::string& data) const
{
    DATA_BLOB input = {};
    DATA_BLOB output = {};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(value.data()));
    input.cbData = static_cast<DWORD>(value.size() * sizeof(wchar_t));

    if (!CryptProtectData(&input, L"FarGitHub token", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;

    data.assign(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return true;
}

bool GitHubSettings::Unprotect(const std::string& data, std::wstring& value) const
{
    if (data.empty())
        return false;

    DATA_BLOB input = {};
    DATA_BLOB output = {};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(data.data()));
    input.cbData = static_cast<DWORD>(data.size());

    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return false;

    if (output.cbData % sizeof(wchar_t) != 0)
    {
        LocalFree(output.pbData);
        return false;
    }

    value.assign(reinterpret_cast<const wchar_t*>(output.pbData), output.cbData / sizeof(wchar_t));
    LocalFree(output.pbData);
    return true;
}

bool GitHubSettings::LoadToken(std::wstring& token) const
{
    token.clear();
    if (!GPluginInfo.SettingsControl)
        return false;

    FarSettingsCreate create = { sizeof(create), MainGuid, INVALID_HANDLE_VALUE };
    if (!GPluginInfo.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &create))
        return false;

    FarSettingsItem item = { sizeof(item), 0, TokenName, FST_DATA, {} };
    const bool ok = GPluginInfo.SettingsControl(create.Handle, SCTL_GET, 0, &item) != FALSE;
    std::string encrypted;
    if (ok && item.Type == FST_DATA && item.Data.Data && item.Data.Size)
        encrypted.assign(reinterpret_cast<const char*>(item.Data.Data), item.Data.Size);

    GPluginInfo.SettingsControl(create.Handle, SCTL_FREE, 0, nullptr);
    return ok && Unprotect(encrypted, token);
}

bool GitHubSettings::SaveToken(const std::wstring& token) const
{
    if (!GPluginInfo.SettingsControl)
        return false;

    std::string encrypted;
    if (!Protect(token, encrypted))
        return false;

    FarSettingsCreate create = { sizeof(create), MainGuid, INVALID_HANDLE_VALUE };
    if (!GPluginInfo.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &create))
        return false;

    FarSettingsItem item = { sizeof(item), 0, TokenName, FST_DATA, {} };
    item.Data.Size = encrypted.size();
    item.Data.Data = encrypted.data();
    const bool ok = GPluginInfo.SettingsControl(create.Handle, SCTL_SET, 0, &item) != FALSE;
    GPluginInfo.SettingsControl(create.Handle, SCTL_FREE, 0, nullptr);
    return ok;
}

bool GitHubSettings::ClearToken() const
{
    if (!GPluginInfo.SettingsControl)
        return false;

    FarSettingsCreate create = { sizeof(create), MainGuid, INVALID_HANDLE_VALUE };
    if (!GPluginInfo.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &create))
        return false;

    FarSettingsValue value = { sizeof(value), 0, TokenName };
    const bool ok = GPluginInfo.SettingsControl(create.Handle, SCTL_DELETE, 0, &value) != FALSE;
    GPluginInfo.SettingsControl(create.Handle, SCTL_FREE, 0, nullptr);
    return ok;
}

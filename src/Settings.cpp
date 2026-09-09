#include "Settings.hpp"
#include "Plugin.hpp"

#include <windows.h>
#include <wincrypt.h>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace
{
const wchar_t* TokenName = L"Token";
const wchar_t* FavoritesName = L"Favorites";
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

bool GitHubSettings::LoadData(const wchar_t* name, std::string& data) const
{
    data.clear();
    if (!GPluginInfo.SettingsControl)
        return false;

    FarSettingsCreate create = { sizeof(create), MainGuid, INVALID_HANDLE_VALUE };
    if (!GPluginInfo.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &create))
        return false;

    FarSettingsItem item = { sizeof(item), 0, name, FST_DATA, {} };
    const bool ok = GPluginInfo.SettingsControl(create.Handle, SCTL_GET, 0, &item) != FALSE;
    if (ok && item.Type == FST_DATA && item.Data.Data && item.Data.Size)
        data.assign(reinterpret_cast<const char*>(item.Data.Data), item.Data.Size);

    GPluginInfo.SettingsControl(create.Handle, SCTL_FREE, 0, nullptr);
    return ok;
}

bool GitHubSettings::SaveData(const wchar_t* name, const std::string& data) const
{
    if (!GPluginInfo.SettingsControl)
        return false;

    FarSettingsCreate create = { sizeof(create), MainGuid, INVALID_HANDLE_VALUE };
    if (!GPluginInfo.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &create))
        return false;

    FarSettingsItem item = { sizeof(item), 0, name, FST_DATA, {} };
    item.Data.Size = data.size();
    item.Data.Data = const_cast<char*>(data.data());
    const bool ok = GPluginInfo.SettingsControl(create.Handle, SCTL_SET, 0, &item) != FALSE;
    GPluginInfo.SettingsControl(create.Handle, SCTL_FREE, 0, nullptr);
    return ok;
}

bool GitHubSettings::DeleteData(const wchar_t* name) const
{
    if (!GPluginInfo.SettingsControl)
        return false;

    FarSettingsCreate create = { sizeof(create), MainGuid, INVALID_HANDLE_VALUE };
    if (!GPluginInfo.SettingsControl(INVALID_HANDLE_VALUE, SCTL_CREATE, 0, &create))
        return false;

    FarSettingsValue value = { sizeof(value), 0, name };
    const bool ok = GPluginInfo.SettingsControl(create.Handle, SCTL_DELETE, 0, &value) != FALSE;
    GPluginInfo.SettingsControl(create.Handle, SCTL_FREE, 0, nullptr);
    return ok;
}

bool GitHubSettings::LoadToken(std::wstring& token) const
{
    token.clear();
    std::string encrypted;
    if (!LoadData(TokenName, encrypted))
        return false;
    return Unprotect(encrypted, token);
}

bool GitHubSettings::SaveToken(const std::wstring& token) const
{
    std::string encrypted;
    if (!Protect(token, encrypted))
        return false;
    return SaveData(TokenName, encrypted);
}

bool GitHubSettings::ClearToken() const
{
    return DeleteData(TokenName);
}

bool GitHubSettings::LoadFavorites(std::vector<std::wstring>& favorites) const
{
    favorites.clear();
    std::string data;
    if (!LoadData(FavoritesName, data) || data.empty())
        return true;

    if (data.size() % sizeof(wchar_t) != 0)
        return false;

    const auto* values = reinterpret_cast<const wchar_t*>(data.data());
    const size_t count = data.size() / sizeof(wchar_t);
    size_t start = 0;
    for (size_t i = 0; i <= count; ++i)
    {
        if (i != count && values[i] != L'\n')
            continue;

        if (i > start)
            favorites.emplace_back(values + start, i - start);
        start = i + 1;
    }
    return true;
}

bool GitHubSettings::SaveFavorites(const std::vector<std::wstring>& favorites) const
{
    std::wstring value;
    for (const auto& favorite : favorites)
    {
        if (!value.empty())
            value += L'\n';
        value += favorite;
    }

    if (value.empty())
        return DeleteData(FavoritesName);

    const auto* bytes = reinterpret_cast<const char*>(value.data());
    const std::string data(bytes, value.size() * sizeof(wchar_t));
    return SaveData(FavoritesName, data);
}

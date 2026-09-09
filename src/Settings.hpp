#pragma once

#include <string>
#include <vector>

class GitHubSettings
{
public:
    bool LoadToken(std::wstring& token) const;
    bool SaveToken(const std::wstring& token) const;
    bool ClearToken() const;
    bool LoadFavorites(std::vector<std::wstring>& favorites) const;
    bool SaveFavorites(const std::vector<std::wstring>& favorites) const;

private:
    bool Protect(const std::wstring& value, std::string& data) const;
    bool Unprotect(const std::string& data, std::wstring& value) const;
    bool LoadData(const wchar_t* name, std::string& data) const;
    bool SaveData(const wchar_t* name, const std::string& data) const;
    bool DeleteData(const wchar_t* name) const;
};

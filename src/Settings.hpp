#pragma once

#include <string>

class GitHubSettings
{
public:
    bool LoadToken(std::wstring& token) const;
    bool SaveToken(const std::wstring& token) const;
    bool ClearToken() const;

private:
    bool Protect(const std::wstring& value, std::string& data) const;
    bool Unprotect(const std::string& data, std::wstring& value) const;
};

#pragma once

#include <string>
#include <vector>

struct GitHubEntry
{
    std::wstring Name;
    std::wstring Type;
    std::wstring Sha;
    unsigned long long Size = 0;
};

struct GitHubRepository
{
    std::wstring Name;
    std::wstring FullName;
    std::wstring DefaultBranch;
};

class GitHubClient
{
public:
    explicit GitHubClient(const std::wstring& token);
    GitHubClient(const std::wstring& token, const std::wstring& repository);

    bool TestConnection(std::wstring& login, std::wstring& error);
    bool GetRepositories(std::vector<GitHubRepository>& repositories, std::wstring& error);
    bool GetEntries(const std::wstring& path, std::vector<GitHubEntry>& entries, std::wstring& error);
    bool GetFile(const std::wstring& path, std::string& content, std::wstring& sha, std::wstring& error);
    bool PutFile(const std::wstring& path, const std::string& content, const std::wstring& sha, const std::wstring& message, std::wstring& error);
    bool CreateDirectoryEntry(const std::wstring& path, const std::wstring& message, std::wstring& error);

private:
    std::wstring Token;
    std::wstring Repository;

    bool Request(const std::wstring& method, const std::wstring& path, const std::string& body, std::string& response, std::wstring& error);
    static std::string Base64Encode(const std::string& data);
    static bool Base64Decode(const std::string& data, std::string& result);
    static std::string Utf8(const std::wstring& value);
    static std::wstring Wide(const std::string& value);
    static std::wstring JsonString(const std::string& json, const std::string& key);
    static unsigned long long JsonNumber(const std::string& json, const std::string& key);
    static std::string JsonEscape(const std::string& value);
    static std::wstring UrlPath(const std::wstring& value);
    static std::vector<std::string> JsonObjects(const std::string& json);
};

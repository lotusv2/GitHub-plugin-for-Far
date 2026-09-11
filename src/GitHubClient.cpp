#include "GitHubClient.hpp"
#include "SaveProgress.hpp"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")

GitHubClient::GitHubClient(const std::wstring& token) : Token(token) {}
GitHubClient::GitHubClient(const std::wstring& token, const std::wstring& repository) : Token(token), Repository(repository) {}
GitHubClient::GitHubClient(const std::wstring& token, const std::wstring& repository, const std::wstring& branch) : Token(token), Repository(repository), Branch(branch) {}

std::string GitHubClient::Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring GitHubClient::Wide(const std::string& value)
{
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), size, result.data(), size);
    return result;
}

std::wstring GitHubClient::UrlPath(const std::wstring& value)
{
    const std::string utf8 = Utf8(value);
    const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : utf8)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/' || c == '~')
            result.push_back(static_cast<char>(c));
        else
        {
            result.push_back('%');
            result.push_back(hex[c >> 4]);
            result.push_back(hex[c & 0x0F]);
        }
    }
    return Wide(result);
}

bool GitHubClient::Request(const std::wstring& method, const std::wstring& path, const std::string& body, std::string& response, std::wstring& error)
{
    HINTERNET session = WinHttpOpen(L"GitHub-plugin-for-Far/0.5.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) { error = L"WinHttpOpen failed"; return false; }
    // Ограничиваем время сетевых операций, чтобы зависший GitHub не блокировал Far Manager.
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
    HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { error = L"WinHttpConnect failed"; WinHttpCloseHandle(session); return false; }
    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
    {
        error = L"WinHttpOpenRequest failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    std::wstring headers = L"Accept: application/vnd.github+json\r\nUser-Agent: GitHub-plugin-for-Far\r\nContent-Type: application/json\r\n";
    if (!Token.empty()) headers += L"Authorization: Bearer " + Token + L"\r\n";
    BOOL ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (!ok)
    {
        error = L"GitHub HTTP request failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
    response.clear();
    char buffer[8192];
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available)
    {
        DWORD read = 0;
        const DWORD chunk = std::min<DWORD>(available, sizeof(buffer));
        if (!WinHttpReadData(request, buffer, chunk, &read) || !read) break;
        response.append(buffer, read);
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (status < 200 || status >= 300)
    {
        const std::wstring message = JsonString(response, "message");
        error = L"GitHub HTTP " + std::to_wstring(status);
        if (!message.empty()) error += L": " + message;
        return false;
    }
    return true;
}

std::wstring GitHubClient::JsonString(const std::string& json, const std::string& key)
{
    const std::string marker = "\"" + key + "\"";
    size_t p = json.find(marker);
    if (p == std::string::npos) return {};
    p += marker.size();
    while (p < json.size() && (std::isspace(static_cast<unsigned char>(json[p])) || json[p] == ':')) ++p;
    if (p >= json.size() || json[p] != '"') return {};
    ++p;
    std::string value;
    bool escape = false;
    for (; p < json.size(); ++p)
    {
        const char c = json[p];
        if (escape)
        {
            switch (c)
            {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                default: value += c; break;
            }
            escape = false;
        }
        else if (c == '\\') escape = true;
        else if (c == '"') break;
        else value += c;
    }
    return Wide(value);
}

unsigned long long GitHubClient::JsonNumber(const std::string& json, const std::string& key)
{
    const std::string marker = "\"" + key + "\"";
    size_t p = json.find(marker);
    if (p == std::string::npos) return 0;
    p += marker.size();
    while (p < json.size() && (std::isspace(static_cast<unsigned char>(json[p])) || json[p] == ':')) ++p;
    return _strtoui64(json.c_str() + p, nullptr, 10);
}

bool GitHubClient::JsonBool(const std::string& json, const std::string& key)
{
    const std::string marker = "\"" + key + "\"";
    size_t p = json.find(marker);
    if (p == std::string::npos) return false;
    p += marker.size();
    while (p < json.size() && (std::isspace(static_cast<unsigned char>(json[p])) || json[p] == ':')) ++p;
    return json.compare(p, 4, "true") == 0;
}

std::string GitHubClient::JsonEscape(const std::string& value)
{
    std::string result;
    for (unsigned char c : value)
    {
        switch (c)
        {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(static_cast<char>(c)); break;
        }
    }
    return result;
}

std::vector<std::string> GitHubClient::JsonObjects(const std::string& json)
{
    std::vector<std::string> result;
    bool inString = false, escape = false;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if (inString)
        {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{') { if (depth == 0) start = i; ++depth; }
        else if (c == '}' && depth > 0) { --depth; if (depth == 0) result.push_back(json.substr(start, i - start + 1)); }
    }
    return result;
}

bool GitHubClient::TestConnection(std::wstring& login, std::wstring& error)
{
    std::string response;
    if (!Request(L"GET", L"/user", {}, response, error)) return false;
    login = JsonString(response, "login");
    if (login.empty()) { error = L"GitHub did not return the authenticated user name"; return false; }
    return true;
}

bool GitHubClient::GetRepositories(std::vector<GitHubRepository>& repositories, std::wstring& error)
{
    repositories.clear();
    for (unsigned int page = 1; ; ++page)
    {
        std::string response;
        if (!Request(L"GET", L"/user/repos?per_page=100&sort=full_name&page=" + std::to_wstring(page), {}, response, error)) return false;
        const auto objects = JsonObjects(response);
        if (objects.empty()) break;
        for (const auto& object : objects)
        {
            GitHubRepository repository;
            repository.Name = JsonString(object, "name");
            repository.FullName = JsonString(object, "full_name");
            repository.DefaultBranch = JsonString(object, "default_branch");
            if (!repository.Name.empty() && !repository.FullName.empty()) repositories.push_back(repository);
        }
        if (objects.size() < 100) break;
    }
    return true;
}

bool GitHubClient::GetBranches(std::vector<GitHubBranch>& branches, std::wstring& error)
{
    branches.clear();
    if (Repository.empty()) { error = L"Repository is not selected"; return false; }
    for (unsigned int page = 1; ; ++page)
    {
        std::string response;
        if (!Request(L"GET", L"/repos/" + Repository + L"/branches?per_page=100&page=" + std::to_wstring(page), {}, response, error)) return false;
        const auto objects = JsonObjects(response);
        if (objects.empty()) break;
        for (const auto& object : objects)
        {
            GitHubBranch branch;
            branch.Name = JsonString(object, "name");
            branch.Protected = JsonBool(object, "protected");
            const size_t commitPos = object.find("\"commit\"");
            if (commitPos != std::string::npos) branch.Sha = JsonString(object.substr(commitPos), "sha");
            if (!branch.Name.empty()) branches.push_back(branch);
        }
        if (objects.size() < 100) break;
    }
    std::sort(branches.begin(), branches.end(), [](const auto& left, const auto& right) { return _wcsicmp(left.Name.c_str(), right.Name.c_str()) < 0; });
    return true;
}

bool GitHubClient::GetEntries(const std::wstring& path, std::vector<GitHubEntry>& entries, std::wstring& error)
{
    entries.clear();
    if (Repository.empty()) { error = L"Repository is not selected"; return false; }
    std::wstring api = L"/repos/" + Repository + L"/contents/" + UrlPath(path);
    if (!Branch.empty()) api += L"?ref=" + UrlPath(Branch);
    std::string response;
    if (!Request(L"GET", api, {}, response, error)) return false;
    const auto objects = JsonObjects(response);
    if (!objects.empty())
    {
        for (const auto& object : objects)
        {
            GitHubEntry entry;
            entry.Name = JsonString(object, "name");
            entry.Type = JsonString(object, "type");
            entry.Sha = JsonString(object, "sha");
            entry.Size = JsonNumber(object, "size");
            if (!entry.Name.empty()) entries.push_back(entry);
        }
        return true;
    }
    for (const auto& object : JsonObjects(response))
    {
        GitHubEntry entry;
        entry.Name = JsonString(object, "name");
        entry.Type = JsonString(object, "type");
        entry.Sha = JsonString(object, "sha");
        entry.Size = JsonNumber(object, "size");
        if (!entry.Name.empty()) entries.push_back(entry);
    }
    return true;
}

bool GitHubClient::Base64Decode(const std::string& data, std::string& result)
{
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int val = 0, valb = -8;
    result.clear();
    for (unsigned char c : data)
    {
        if (std::isspace(c) || c == '=') continue;
        const size_t pos = chars.find(c);
        if (pos == std::string::npos) return false;
        val = (val << 6) + static_cast<int>(pos);
        valb += 6;
        if (valb >= 0) { result.push_back(static_cast<char>((val >> valb) & 0xFF)); valb -= 8; }
    }
    return true;
}

std::string GitHubClient::Base64Encode(const std::string& data)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, valb = -6;
    for (unsigned char c : data)
    {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) { result.push_back(table[(val >> valb) & 0x3F]); valb -= 6; }
    }
    if (valb > -6) result.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

bool GitHubClient::GetFile(const std::wstring& path, std::string& content, std::wstring& sha, std::wstring& error)
{
    std::wstring api = L"/repos/" + Repository + L"/contents/" + UrlPath(path);
    if (!Branch.empty()) api += L"?ref=" + UrlPath(Branch);
    std::string response;
    if (!Request(L"GET", api, {}, response, error)) return false;
    sha = JsonString(response, "sha");
    const std::wstring encoded = JsonString(response, "content");
    if (!Base64Decode(Utf8(encoded), content)) { error = L"Invalid Base64 content returned by GitHub"; return false; }
    return true;
}

bool GitHubClient::PutFile(const std::wstring& path, const std::string& content, const std::wstring& sha, const std::wstring& message, std::wstring& error)
{
    std::string body = "{\"message\":\"" + JsonEscape(Utf8(message)) + "\",\"content\":\"" + Base64Encode(content) + "\"";
    if (!sha.empty()) body += ",\"sha\":\"" + JsonEscape(Utf8(sha)) + "\"";
    if (!Branch.empty()) body += ",\"branch\":\"" + JsonEscape(Utf8(Branch)) + "\"";
    body += "}";

    return RunGitHubProgress(L"Отправка изменений на GitHub...", [&]()
    {
        std::string response;
        return Request(L"PUT", L"/repos/" + Repository + L"/contents/" + UrlPath(path), body, response, error);
    });
}

bool GitHubClient::DeleteFile(const std::wstring& path, const std::wstring& sha, const std::wstring& message, std::wstring& error)
{
    if (sha.empty()) { error = L"File SHA is empty"; return false; }
    std::string body = "{\"message\":\"" + JsonEscape(Utf8(message)) + "\",\"sha\":\"" + JsonEscape(Utf8(sha)) + "\"";
    if (!Branch.empty()) body += ",\"branch\":\"" + JsonEscape(Utf8(Branch)) + "\"";
    body += "}";
    std::string response;
    return Request(L"DELETE", L"/repos/" + Repository + L"/contents/" + UrlPath(path), body, response, error);
}

bool GitHubClient::RenameFile(const std::wstring& oldPath, const std::wstring& newPath, const std::wstring& message, std::wstring& error)
{
    std::string content;
    std::wstring sha;
    if (!GetFile(oldPath, content, sha, error)) return false;
    if (!PutFile(newPath, content, {}, message, error)) return false;
    if (!DeleteFile(oldPath, sha, message, error))
    {
        std::wstring rollbackError;
        std::wstring newSha;
        std::string ignored;
        if (GetFile(newPath, ignored, newSha, rollbackError)) DeleteFile(newPath, newSha, L"Rollback rename " + oldPath, rollbackError);
        return false;
    }
    return true;
}

bool GitHubClient::CreateDirectoryEntry(const std::wstring& path, const std::wstring& message, std::wstring& error)
{
    return PutFile(path + L"/.gitkeep", {}, {}, message, error);
}

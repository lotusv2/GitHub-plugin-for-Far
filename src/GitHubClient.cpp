#include "GitHubClient.hpp"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")

GitHubClient::GitHubClient(const std::wstring& token)
    : Token(token) {}

GitHubClient::GitHubClient(const std::wstring& token, const std::wstring& repository)
    : Token(token), Repository(repository) {}

std::string GitHubClient::Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring GitHubClient::Wide(const std::string& value)
{
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
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
    HINTERNET session = WinHttpOpen(L"GitHub-plugin-for-Far/0.2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) { error = L"WinHttpOpen failed"; return false; }
    HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { error = L"WinHttpConnect failed"; WinHttpCloseHandle(session); return false; }
    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) { error = L"WinHttpOpenRequest failed"; WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }

    std::wstring headers = L"Accept: application/vnd.github+json\r\nUser-Agent: GitHub-plugin-for-Far\r\nContent-Type: application/json\r\n";
    if (!Token.empty()) headers += L"Authorization: Bearer " + Token + L"\r\n";
    BOOL ok = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (ok) ok = WinHttpReceiveResponse(request, nullptr);
    if (!ok) { error = L"GitHub HTTP request failed"; WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &statusSize, nullptr);
    response.clear();
    char buffer[8192];
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available)
    {
        DWORD read = 0;
        DWORD chunk = std::min<DWORD>(available, sizeof(buffer));
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
                case 'u':
                    // GitHub API normally returns UTF-8 directly; preserve unknown escapes safely.
                    value += 'u';
                    break;
                default: value += c; break;
            }
            escape = false;
            continue;
        }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') break;
        value += c;
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
    bool inString = false;
    bool escape = false;
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
        if (c == '{')
        {
            if (depth == 0) start = i;
            ++depth;
        }
        else if (c == '}' && depth > 0)
        {
            --depth;
            if (depth == 0) result.push_back(json.substr(start, i - start + 1));
        }
    }
    return result;
}

bool GitHubClient::TestConnection(std::wstring& login, std::wstring& error)
{
    std::string response;
    if (!Request(L"GET", L"/user", {}, response, error)) return false;
    login = JsonString(response, "login");
    if (login.empty())
    {
        error = L"GitHub did not return the authenticated user name";
        return false;
    }
    return true;
}

bool GitHubClient::GetRepositories(std::vector<GitHubRepository>& repositories, std::wstring& error)
{
    repositories.clear();
    std::string response;
    if (!Request(L"GET", L"/user/repos?per_page=100&sort=full_name", {}, response, error)) return false;

    for (const std::string& object : JsonObjects(response))
    {
        GitHubRepository repository;
        repository.Name = JsonString(object, "name");
        repository.FullName = JsonString(object, "full_name");
        repository.DefaultBranch = JsonString(object, "default_branch");
        if (!repository.Name.empty() && !repository.FullName.empty())
            repositories.push_back(repository);
    }
    return true;
}

bool GitHubClient::GetEntries(const std::wstring& path, std::vector<GitHubEntry>& entries, std::wstring& error)
{
    entries.clear();
    if (Repository.empty()) { error = L"Repository is not selected"; return false; }
    std::wstring api = L"/repos/" + Repository + L"/contents";
    if (!path.empty()) api += L"/" + UrlPath(path);
    std::string response;
    if (!Request(L"GET", api, {}, response, error)) return false;

    for (const std::string& object : JsonObjects(response))
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
        if (valb >= 0)
        {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
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
        while (valb >= 0)
        {
            result.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) result.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (result.size() % 4) result.push_back('=');
    return result;
}

bool GitHubClient::GetFile(const std::wstring& path, std::string& content, std::wstring& sha, std::wstring& error)
{
    std::string response;
    if (!Request(L"GET", L"/repos/" + Repository + L"/contents/" + UrlPath(path), {}, response, error)) return false;
    sha = JsonString(response, "sha");
    return Base64Decode(Utf8(JsonString(response, "content")), content);
}

bool GitHubClient::PutFile(const std::wstring& path, const std::string& content, const std::wstring& sha, const std::wstring& message, std::wstring& error)
{
    std::string body = "{\"message\":\"" + JsonEscape(Utf8(message)) + "\",\"content\":\"" + Base64Encode(content) + "\"";
    if (!sha.empty()) body += ",\"sha\":\"" + JsonEscape(Utf8(sha)) + "\"";
    body += "}";
    std::string response;
    return Request(L"PUT", L"/repos/" + Repository + L"/contents/" + UrlPath(path), body, response, error);
}

bool GitHubClient::CreateDirectory(const std::wstring& path, const std::wstring& message, std::wstring& error)
{
    return PutFile(path + L"/.gitkeep", {}, {}, message, error);
}

#include "GitHubClient.hpp"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cctype>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

GitHubClient::GitHubClient(const std::wstring& token) : Token(token) {}

std::string GitHubClient::Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring GitHubClient::Wide(const std::string& value)
{
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), size);
    return result;
}

bool GitHubClient::Request(const std::wstring& method, const std::wstring& path, const std::string& body, std::string& response, std::wstring& error)
{
    HINTERNET session = WinHttpOpen(L"GitHub-plugin-for-Far/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!session) { error = L"WinHttpOpen failed"; return false; }

    HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { error = L"WinHttpConnect failed"; WinHttpCloseHandle(session); return false; }

    HINTERNET request = WinHttpOpenRequest(connect, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) { error = L"WinHttpOpenRequest failed"; WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return false; }

    std::wstring headers = L"Accept: application/vnd.github+json\r\nUser-Agent: GitHub-plugin-for-Far\r\n";
    if (!Token.empty()) headers += L"Authorization: Bearer " + Token + L"\r\n";

    BOOL ok = WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L,
        body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
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
        error = L"GitHub HTTP " + std::to_wstring(status) + L": " + Wide(JsonString(response, "message"));
        return false;
    }
    return true;
}

std::wstring GitHubClient::JsonString(const std::string& json, const std::string& key)
{
    std::string marker = "\"" + key + "\":\"";
    size_t p = json.find(marker);
    if (p == std::string::npos) return {};
    p += marker.size();
    std::string value;
    bool escape = false;
    for (; p < json.size(); ++p)
    {
        char c = json[p];
        if (escape) { value += c; escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') break;
        value += c;
    }
    return Wide(value);
}

unsigned long long GitHubClient::JsonNumber(const std::string& json, const std::string& key)
{
    std::string marker = "\"" + key + "\":";
    size_t p = json.find(marker);
    if (p == std::string::npos) return 0;
    p += marker.size();
    while (p < json.size() && std::isspace((unsigned char)json[p])) ++p;
    return std::strtoull(json.c_str() + p, nullptr, 10);
}

bool GitHubClient::GetEntries(const std::wstring& path, std::vector<GitHubEntry>& entries, std::wstring& error)
{
    entries.clear();
    std::wstring api = L"/repos/" + Repository + L"/contents";
    if (!path.empty()) api += L"/" + path;
    std::string response;
    if (!Request(L"GET", api, {}, response, error)) return false;

    size_t p = 0;
    while ((p = response.find("{\"name\":", p)) != std::string::npos)
    {
        size_t end = response.find('}', p);
        if (end == std::string::npos) break;
        std::string object = response.substr(p, end - p + 1);
        GitHubEntry entry;
        entry.Name = JsonString(object, "name");
        entry.Type = JsonString(object, "type");
        entry.Sha = JsonString(object, "sha");
        entry.Size = JsonNumber(object, "size");
        if (!entry.Name.empty()) entries.push_back(entry);
        p = end + 1;
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
        if (std::isspace(c)) continue;
        size_t pos = chars.find(c);
        if (pos == std::string::npos) continue;
        val = (val << 6) + (int)pos;
        valb += 6;
        if (valb >= 0) { result.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
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
    std::string response;
    if (!Request(L"GET", L"/repos/" + Repository + L"/contents/" + path, {}, response, error)) return false;
    sha = JsonString(response, "sha");
    std::wstring encoded = JsonString(response, "content");
    return Base64Decode(Utf8(encoded), content);
}

bool GitHubClient::PutFile(const std::wstring& path, const std::string& content, const std::wstring& sha, const std::wstring& message, std::wstring& error)
{
    std::string body = "{\"message\":\"" + Utf8(message) + "\",\"content\":\"" + Base64Encode(content) + "\"";
    if (!sha.empty()) body += ",\"sha\":\"" + Utf8(sha) + "\"";
    body += "}";
    std::string response;
    return Request(L"PUT", L"/repos/" + Repository + L"/contents/" + path, body, response, error);
}

bool GitHubClient::CreateDirectory(const std::wstring& path, const std::wstring& message, std::wstring& error)
{
    return PutFile(path + L"/.gitkeep", {}, {}, message, error);
}

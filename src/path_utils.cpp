#include "path_utils.h"

std::wstring NormalizeRelativePath(const wchar_t *input)
{
    if (!input)
        return std::wstring();

    std::wstring normalized = input;
    for (auto &ch : normalized)
    {
        if (ch == L'/')
            ch = L'\\';
    }

    for (size_t i = 1; i < normalized.size();)
    {
        if (normalized[i] == L'\\' && normalized[i - 1] == L'\\')
            normalized.erase(i, 1);
        else
            ++i;
    }

    return normalized;
}

bool IsAbsoluteSnapshotPath(const wchar_t *path)
{
    if (!path)
        return false;
    return (wcsncmp(path, L"\\\\", 2) == 0) ||
           (wcsncmp(path, L"\\??\\", 4) == 0);
}

std::wstring EnsureTrailingBackslash(std::wstring path)
{
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    return path;
}

namespace
{
std::wstring TrimLeadingSeparators(std::wstring value)
{
    while (!value.empty() && (value.front() == L'\\' || value.front() == L'/'))
        value.erase(value.begin());
    return value;
}

std::string WideToUtf8(const std::wstring &value)
{
    if (value.empty())
        return std::string();

    int length = WideCharToMultiByte(CP_UTF8,
                                     0,
                                     value.c_str(),
                                     static_cast<int>(value.size()),
                                     NULL,
                                     0,
                                     NULL,
                                     NULL);
    if (length <= 0)
        return std::string();

    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8,
                        0,
                        value.c_str(),
                        static_cast<int>(value.size()),
                        utf8.empty() ? nullptr : &utf8[0],
                        length,
                        NULL,
                        NULL);
    return utf8;
}
} // namespace

std::wstring BuildSnapshotPath(const std::wstring &rootWithSlash, const std::wstring &relativeNormalized)
{
    std::wstring trimmed = TrimLeadingSeparators(relativeNormalized);
    if (trimmed.empty())
        return rootWithSlash;

    std::wstring full = rootWithSlash;
    if (!full.empty() && full.back() != L'\\')
        full.push_back(L'\\');
    full.append(trimmed);
    return full;
}

std::string MakeZipEntryName(const std::wstring &relativeNormalized)
{
    std::wstring trimmed = TrimLeadingSeparators(relativeNormalized);
    if (trimmed.empty())
        return std::string();
    std::string entry = WideToUtf8(trimmed);
    for (auto &ch : entry)
    {
        if (ch == '\\')
            ch = '/';
    }
    return entry;
}

#pragma once

#include "common.h"

std::wstring NormalizeRelativePath(const wchar_t *input);
bool IsAbsoluteSnapshotPath(const wchar_t *path);
std::wstring EnsureTrailingBackslash(std::wstring path);
std::wstring BuildSnapshotPath(const std::wstring &rootWithSlash, const std::wstring &relativeNormalized);
std::string MakeZipEntryName(const std::wstring &relativeNormalized);


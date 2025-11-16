#include "file_utils.h"

void DeletePartialArchive(const std::wstring &path)
{
    if (path.empty())
        return;

    if (DeleteFileW(path.c_str()))
    {
        wprintf(L"[i] Removed incomplete archive %ls\n", path.c_str());
    }
    else
    {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND)
        {
            wprintf(L"[!] Failed to remove incomplete archive %ls: %lu\n",
                    path.c_str(),
                    err);
        }
    }
}

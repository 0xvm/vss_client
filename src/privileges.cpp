#include "privileges.h"

namespace
{
BOOL EnablePrivilege(LPCWSTR name)
{
    HANDLE token;
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &token))
        return FALSE;

    if (!LookupPrivilegeValueW(NULL, name, &luid))
    {
        CloseHandle(token);
        return FALSE;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL))
    {
        CloseHandle(token);
        return FALSE;
    }

    if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
    {
        CloseHandle(token);
        return FALSE;
    }

    CloseHandle(token);
    return TRUE;
}
} // namespace

bool EnableRequiredPrivileges()
{
    struct Priv
    {
        LPCWSTR name;
        LPCWSTR label;
    } privs[] = {
        {SE_BACKUP_NAME, L"SE_BACKUP_NAME"},
        {SE_RESTORE_NAME, L"SE_RESTORE_NAME"},
        {SE_MANAGE_VOLUME_NAME, L"SE_MANAGE_VOLUME_NAME"},
    };

    for (size_t i = 0; i < ARRAYSIZE(privs); ++i)
    {
        wprintf(L"[+] Enabling privilege %ls...\n", privs[i].label);
        if (!EnablePrivilege(privs[i].name))
        {
            wprintf(L"[!] Failed to enable %ls\n", privs[i].label);
            return false;
        }
    }
    return true;
}


// vss_client_accessible.cpp
#define UNICODE
#define _UNICODE
#define _WIN32_DCOM
#include <windows.h>
#include <stdio.h>
#include <string>
#include <cwchar>
#include <combaseapi.h>
#include <vss.h>
#include <vswriter.h>
#include <vsbackup.h>

#pragma comment(lib, "vssapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")

static BOOL EnablePrivilege(LPCWSTR name)
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

static BOOL EnableRequiredPrivileges(void)
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
            return FALSE;
        }
    }
    return TRUE;
}

static std::wstring NormalizeRelativePath(const wchar_t *input)
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

static void DeleteSnapshotIfNeeded(IVssBackupComponents *vss,
                                   VSS_ID snapshotId,
                                   bool snapshotReady,
                                   bool keepSnapshot)
{
    if (!vss || snapshotId == GUID_NULL || !snapshotReady)
        return;

    if (keepSnapshot)
    {
        wprintf(L"[i] Leaving snapshot %08lX... (--keep)\n", snapshotId.Data1);
        return;
    }

    LONG deletedSnapshots = 0;
    VSS_ID nonDeleted = GUID_NULL;
    HRESULT hr = vss->DeleteSnapshots(snapshotId,
                                      VSS_OBJECT_SNAPSHOT,
                                      TRUE,
                                      &deletedSnapshots,
                                      &nonDeleted);
    if (FAILED(hr))
    {
        wprintf(L"[!] DeleteSnapshots failed: 0x%08lx\n", hr);
    }
    else
    {
        wprintf(L"[+] Snapshot deleted (%ld object(s))\n", deletedSnapshots);
    }
}

static void PrintUsage(const wchar_t *exe)
{
    fwprintf(stderr,
             L"Usage:\n"
             L"  %ls [<snapshot-relative-path> <destination>] [--keep]\n"
             L"Examples:\n"
             L"  %ls\n"
             L"  %ls Users\\\\user\\\\Desktop\\\\test.txt C:\\\\test.txt\n"
             L"  %ls Users\\\\user\\\\Desktop\\\\test.txt C:\\\\test.txt --keep\n",
             exe, exe, exe, exe);
}

int wmain(int argc, wchar_t *argv[])
{
    const wchar_t *snapshotRelative = NULL;
    const wchar_t *copyDestination = NULL;
    bool keepSnapshot = true;
    bool keepExplicit = false;

    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"--keep") == 0)
        {
            keepSnapshot = true;
            keepExplicit = true;
            continue;
        }

        if (!snapshotRelative)
        {
            snapshotRelative = argv[i];
        }
        else if (!copyDestination)
        {
            copyDestination = argv[i];
        }
        else
        {
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if ((snapshotRelative && !copyDestination) ||
        (!snapshotRelative && copyDestination))
    {
        PrintUsage(argv[0]);
        return 1;
    }

    if (snapshotRelative && !keepExplicit)
        keepSnapshot = false;

    if (snapshotRelative)
        wprintf(L"[i] Will copy '%ls' from snapshot to '%ls'\n", snapshotRelative, copyDestination);

    if (!EnableRequiredPrivileges())
    {
        wprintf(L"[!] Could not enable required privileges\n");
        return 1;
    }

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        wprintf(L"[!] CoInitializeEx failed: 0x%08lx\n", hr);
        return 1;
    }
    wprintf(L"[+] COM initialized\n");

    hr = CoInitializeSecurity(NULL,
                              -1,
                              NULL,
                              NULL,
                              RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                              RPC_C_IMP_LEVEL_IMPERSONATE,
                              NULL,
                              EOAC_NONE | EOAC_DYNAMIC_CLOAKING,
                              NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE)
    {
        wprintf(L"[!] CoInitializeSecurity failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] COM security %ls\n", hr == RPC_E_TOO_LATE ? L"already initialized" : L"initialized");

    IVssBackupComponents *vss = NULL;
    hr = CreateVssBackupComponents(&vss);
    if (FAILED(hr))
    {
        wprintf(L"[!] CreateVssBackupComponents failed: 0x%08lx\n", hr);
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] IVssBackupComponents created\n");

    hr = vss->InitializeForBackup();
    if (FAILED(hr))
    {
        wprintf(L"[!] InitializeForBackup failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Backup components initialized\n");

    hr = vss->SetContext(VSS_CTX_CLIENT_ACCESSIBLE);
    if (FAILED(hr))
    {
        wprintf(L"[!] SetContext failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] VSS context set to client-accessible\n");

    hr = vss->SetBackupState(false, false, VSS_BT_FULL, false);
    if (FAILED(hr))
    {
        wprintf(L"[!] SetBackupState failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Backup state configured (full, no writers)\n");

    VSS_ID setId = GUID_NULL, snapId = GUID_NULL;
    bool snapshotReady = false;
    hr = vss->StartSnapshotSet(&setId);
    if (FAILED(hr))
    {
        wprintf(L"[!] StartSnapshotSet failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Snapshot set created: %08lX-%04X-%04X-????\n",
            setId.Data1, setId.Data2, setId.Data3);

    hr = vss->AddToSnapshotSet((VSS_PWSZ)L"C:\\", GUID_NULL, &snapId);
    if (FAILED(hr))
    {
        wprintf(L"[!] AddToSnapshotSet failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Drive C:\\ added to snapshot set\n");

    IVssAsync *async = NULL;
    hr = vss->DoSnapshotSet(&async);
    if (FAILED(hr) || !async)
    {
        wprintf(L"[!] DoSnapshotSet failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Snapshot set creation started\n");

    hr = async->Wait();
    if (FAILED(hr))
    {
        wprintf(L"[!] Async wait failed: 0x%08lx\n", hr);
        async->Release();
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Snapshot creation completed\n");

    HRESULT hrRes = S_OK;
    async->QueryStatus(&hrRes, NULL);
    snapshotReady = true;
    async->Release();
    if (FAILED(hrRes))
    {
        DeleteSnapshotIfNeeded(vss, snapId, snapshotReady, keepSnapshot);
        wprintf(L"[!] Snapshot status failed: 0x%08lx\n", hrRes);
        vss->Release();
        CoUninitialize();
        return 1;
    }
    wprintf(L"[+] Snapshot status: 0x%08lx\n", hrRes);

    VSS_SNAPSHOT_PROP prop;
    ZeroMemory(&prop, sizeof(prop));
    hr = vss->GetSnapshotProperties(snapId, &prop);
    if (FAILED(hr))
    {
        DeleteSnapshotIfNeeded(vss, snapId, snapshotReady, keepSnapshot);
        wprintf(L"[!] GetSnapshotProperties failed: 0x%08lx\n", hr);
        vss->Release();
        CoUninitialize();
        return 1;
    }

    // Snapshot device path, e.g. \\?\GLOBALROOT\Device\HarddiskVolumeShadowCopyX
    wprintf(L"[+] Snapshot device: %ls\n", prop.m_pwszSnapshotDeviceObject);

    if (snapshotRelative && copyDestination)
    {
        std::wstring snapshotRoot = prop.m_pwszSnapshotDeviceObject;
        if (!snapshotRoot.empty() && snapshotRoot.back() != L'\\')
            snapshotRoot.push_back(L'\\');

        std::wstring relativeNormalized = NormalizeRelativePath(snapshotRelative);
        std::wstring fullSource;
        if ((wcsncmp(snapshotRelative, L"\\\\", 2) == 0) ||
            (wcsncmp(snapshotRelative, L"\\??\\", 4) == 0))
        {
            fullSource = snapshotRelative;
        }
        else
        {
            fullSource = snapshotRoot;
            if (relativeNormalized.empty())
            {
                // nothing to append
            }
            else if (relativeNormalized[0] == L'\\')
            {
                fullSource.append(relativeNormalized.c_str() + 1);
            }
            else
            {
                fullSource.append(relativeNormalized);
            }
        }

        wprintf(L"[+] Copying %ls -> %ls\n", fullSource.c_str(), copyDestination);
        if (!CopyFileW(fullSource.c_str(), copyDestination, FALSE))
        {
            DWORD err = GetLastError();
            wprintf(L"[!] CopyFile failed: %lu\n", err);
            DeleteSnapshotIfNeeded(vss, snapId, snapshotReady, keepSnapshot);
            VssFreeSnapshotProperties(&prop);
            vss->Release();
            CoUninitialize();
            return 1;
        }
        wprintf(L"[+] Copy completed\n");
    }

    DeleteSnapshotIfNeeded(vss, snapId, snapshotReady, keepSnapshot);
    VssFreeSnapshotProperties(&prop);
    vss->Release();
    wprintf(L"[+] Completed successfully\n");
    CoUninitialize();
    return 0;
}

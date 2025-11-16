#pragma once

#include "common.h"

void DeleteSnapshotIfNeeded(IVssBackupComponents *vss, VSS_ID snapshotId, bool snapshotReady, bool keepSnapshot);


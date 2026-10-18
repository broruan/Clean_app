#pragma once

#include "types.h"

// Local, read-only package inventory. Run on a worker thread; no network access
// or privilege escalation is used. Package sizes exclude personal application
// data and are not a prediction of the space an uninstall will reclaim.
InventoryResult readInstalledSoftware();

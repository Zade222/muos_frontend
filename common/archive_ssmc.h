#pragma once

#include "archive.h"

// Returns a pointer to the VTable for the SSMC archive handler.
ArchiveVTable* get_ssmc_archive_handler(void);

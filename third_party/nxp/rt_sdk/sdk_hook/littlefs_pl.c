
/*
 * Copyright 2021 - 2022 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "lfs.h"
#include "littlefs_pl.h"
#include "fwk_filesystem.h"



lfs_t * lfs_pl_init(void)
{
    return (lfs_t*)FS_InitGetHandle();
}

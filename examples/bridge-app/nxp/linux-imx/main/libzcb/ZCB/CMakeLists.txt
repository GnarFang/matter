# * ********************************************************************************************************* *
# *
# * Copyright 2019-2023 NXP
# *
# * SPDX-License-Identifier: BSD-3-Clause
# * The BSD-3-Clause license for this file can be found in the LICENSE.txt file included with this distribution
# * or at https://spdx.org/licenses/BSD-3-Clause.html#licenseText
# *
# * ********************************************************************************************************* *
set(MODULE_ZCB  zcb)

include_directories(../IotCommon)
# include_directories(../IotCommon/cJSON)
include_directories(../../)

aux_source_directory(. ZCB_SOURCE)
aux_source_directory(../IotCommon/. IOT_SOURCE)
# aux_source_directory(../IotCommon/cJSON/. JSON_SOURCE)
add_library(${MODULE_ZCB} STATIC ${ZCB_SOURCE} ${IOT_SOURCE} ${JSON_SOURCE})

target_link_libraries(${MODULE_ZCB} PUBLIC pthread m)
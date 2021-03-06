/*
 *  NASA Docket No. GSC-18,370-1, and identified as "Operating System Abstraction Layer"
 *
 *  Copyright (c) 2019 United States Government as represented by
 *  the Administrator of the National Aeronautics and Space Administration.
 *  All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/**
 * \file     osapi-binsem.c
 * \ingroup  shared
 * \author   joseph.p.hickey@nasa.gov
 *
 *         This file  contains some of the OS APIs abstraction layer code
 *         that is shared/common across all OS-specific implementations.
 */

/****************************************************************************************
                                    INCLUDE FILES
 ***************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * User defined include files
 */
#include "os-shared-binsem.h"
#include "os-shared-idmap.h"

/*
 * Sanity checks on the user-supplied configuration
 * The relevent OS_MAX limit should be defined and greater than zero
 */
#if !defined(OS_MAX_BIN_SEMAPHORES) || (OS_MAX_BIN_SEMAPHORES <= 0)
#error "osconfig.h must define OS_MAX_BIN_SEMAPHORES to a valid value"
#endif

/*
 * Global data for the API
 */
enum
{
    LOCAL_NUM_OBJECTS = OS_MAX_BIN_SEMAPHORES,
    LOCAL_OBJID_TYPE  = OS_OBJECT_TYPE_OS_BINSEM
};

OS_bin_sem_internal_record_t OS_bin_sem_table[LOCAL_NUM_OBJECTS];

/****************************************************************************************
                                  SEMAPHORE API
 ***************************************************************************************/

/*---------------------------------------------------------------------------------------
   Name: OS_BinSemAPI_Init

   Purpose: Init function for OS-independent layer

   Returns: OS_SUCCESS

---------------------------------------------------------------------------------------*/

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemAPI_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemAPI_Init(void)
{
    memset(OS_bin_sem_table, 0, sizeof(OS_bin_sem_table));
    return OS_SUCCESS;
} /* end OS_BinSemAPI_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemCreate
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemCreate(osal_id_t *sem_id, const char *sem_name, uint32 sem_initial_value, uint32 options)
{
    OS_common_record_t *record;
    int32               return_code;
    uint32              local_id;

    /* Check for NULL pointers */
    if (sem_id == NULL || sem_name == NULL)
    {
        return OS_INVALID_POINTER;
    }

    if (strlen(sem_name) >= OS_MAX_API_NAME)
    {
        return OS_ERR_NAME_TOO_LONG;
    }

    /* Note - the common ObjectIdAllocate routine will lock the object type and leave it locked. */
    return_code = OS_ObjectIdAllocateNew(LOCAL_OBJID_TYPE, sem_name, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        /* Save all the data to our own internal table */
        strcpy(OS_bin_sem_table[local_id].obj_name, sem_name);
        record->name_entry = OS_bin_sem_table[local_id].obj_name;

        /* Now call the OS-specific implementation.  This reads info from the table. */
        return_code = OS_BinSemCreate_Impl(local_id, sem_initial_value, options);

        /* Check result, finalize record, and unlock global table. */
        return_code = OS_ObjectIdFinalizeNew(return_code, record, sem_id);
    }

    return return_code;

} /* end OS_BinSemCreate */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemDelete
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemDelete(osal_id_t sem_id)
{
    OS_common_record_t *record;
    uint32              local_id;
    int32               return_code;

    return_code = OS_ObjectIdGetById(OS_LOCK_MODE_EXCLUSIVE, LOCAL_OBJID_TYPE, sem_id, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        return_code = OS_BinSemDelete_Impl(local_id);

        /* Complete the operation via the common routine */
        return_code = OS_ObjectIdFinalizeDelete(return_code, record);
    }

    return return_code;

} /* end OS_BinSemDelete */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemGive
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemGive(osal_id_t sem_id)
{
    OS_common_record_t *record;
    uint32              local_id;
    int32               return_code;

    /* Check Parameters */
    return_code = OS_ObjectIdGetById(OS_LOCK_MODE_NONE, LOCAL_OBJID_TYPE, sem_id, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        return_code = OS_BinSemGive_Impl(local_id);
    }

    return return_code;

} /* end OS_BinSemGive */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemFlush
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemFlush(osal_id_t sem_id)
{
    OS_common_record_t *record;
    uint32              local_id;
    int32               return_code;

    /* Check Parameters */
    return_code = OS_ObjectIdGetById(OS_LOCK_MODE_NONE, LOCAL_OBJID_TYPE, sem_id, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        return_code = OS_BinSemFlush_Impl(local_id);
    }

    return return_code;
} /* end OS_BinSemFlush */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemTake
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemTake(osal_id_t sem_id)
{
    OS_common_record_t *record;
    uint32              local_id;
    int32               return_code;

    /* Check Parameters */
    return_code = OS_ObjectIdGetById(OS_LOCK_MODE_NONE, LOCAL_OBJID_TYPE, sem_id, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        return_code = OS_BinSemTake_Impl(local_id);
    }

    return return_code;
} /* end OS_BinSemTake */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemTimedWait
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemTimedWait(osal_id_t sem_id, uint32 msecs)
{
    OS_common_record_t *record;
    uint32              local_id;
    int32               return_code;

    /* Check Parameters */
    return_code = OS_ObjectIdGetById(OS_LOCK_MODE_NONE, LOCAL_OBJID_TYPE, sem_id, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        return_code = OS_BinSemTimedWait_Impl(local_id, msecs);
    }

    return return_code;
} /* end OS_BinSemTimedWait */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemGetIdByName
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemGetIdByName(osal_id_t *sem_id, const char *sem_name)
{
    int32 return_code;

    if (sem_id == NULL || sem_name == NULL)
    {
        return OS_INVALID_POINTER;
    }

    return_code = OS_ObjectIdFindByName(LOCAL_OBJID_TYPE, sem_name, sem_id);

    return return_code;
} /* end OS_BinSemGetIdByName */

/*----------------------------------------------------------------
 *
 * Function: OS_BinSemGetInfo
 *
 *  Purpose: Implemented per public OSAL API
 *           See description in API and header file for detail
 *
 *-----------------------------------------------------------------*/
int32 OS_BinSemGetInfo(osal_id_t sem_id, OS_bin_sem_prop_t *bin_prop)
{
    OS_common_record_t *record;
    uint32              local_id;
    int32               return_code;

    /* Check parameters */
    if (bin_prop == NULL)
    {
        return OS_INVALID_POINTER;
    }

    memset(bin_prop, 0, sizeof(OS_bin_sem_prop_t));

    /* Check Parameters */
    return_code = OS_ObjectIdGetById(OS_LOCK_MODE_GLOBAL, LOCAL_OBJID_TYPE, sem_id, &local_id, &record);
    if (return_code == OS_SUCCESS)
    {
        strncpy(bin_prop->name, record->name_entry, OS_MAX_API_NAME - 1);
        bin_prop->creator = record->creator;
        return_code       = OS_BinSemGetInfo_Impl(local_id, bin_prop);
        OS_Unlock_Global(LOCAL_OBJID_TYPE);
    }

    return return_code;
} /* end OS_BinSemGetInfo */

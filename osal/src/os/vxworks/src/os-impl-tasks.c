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
 * \file     os-impl-tasks.c
 * \ingroup  vxworks
 * \author   joseph.p.hickey@nasa.gov
 *
 */
/****************************************************************************************
                                    INCLUDE FILES
****************************************************************************************/

#include "os-vxworks.h"
#include "os-impl-tasks.h"

#include "os-shared-task.h"
#include "os-shared-idmap.h"
#include "os-shared-timebase.h"

#include <errnoLib.h>
#include <taskLib.h>
#include <sysLib.h>

/****************************************************************************************
                                     DEFINES
****************************************************************************************/

/*
 * macros for stack size manipulation.
 * These are normally provided by vxWorks.h if relevant for the platform.
 * If they are not defined, use a reasonable default/substitute.
 */
#if defined(_STACK_ALIGN_SIZE)
#define VX_IMPL_STACK_ALIGN_SIZE _STACK_ALIGN_SIZE
#else
#define VX_IMPL_STACK_ALIGN_SIZE 16
#endif

#if defined(STACK_ROUND_DOWN)
#define VX_IMPL_STACK_ROUND_DOWN(x) STACK_ROUND_DOWN(x)
#else
#define VX_IMPL_STACK_ROUND_DOWN(x) ((x) & ~(VX_IMPL_STACK_ALIGN_SIZE - 1))
#endif

#if defined(STACK_ROUND_UP)
#define VX_IMPL_STACK_ROUND_UP(x) STACK_ROUND_UP(x)
#else
#define VX_IMPL_STACK_ROUND_UP(x) (((x) + (VX_IMPL_STACK_ALIGN_SIZE - 1)) & ~(VX_IMPL_STACK_ALIGN_SIZE - 1))
#endif

/****************************************************************************************
                                   GLOBAL DATA
****************************************************************************************/

/* Tables where the OS object information is stored */
OS_impl_task_internal_record_t OS_impl_task_table[OS_MAX_TASKS];

/*---------------------------------------------------------------------------------------
   Name: OS_VxWorksEntry

   Purpose: A Simple VxWorks-compatible entry point that calls the common task entry function

   NOTES: This wrapper function is only used locally by OS_TaskCreate below

---------------------------------------------------------------------------------------*/
int OS_VxWorks_TaskEntry(int arg)
{
    VxWorks_ID_Buffer_t id;

    id.arg = arg;

    OS_TaskEntryPoint(id.id);

    return 0;
} /* end OS_VxWorksEntry */

/****************************************************************************************
                                    TASK API
****************************************************************************************/

/*----------------------------------------------------------------
 *
 * Function: OS_VxWorks_TaskAPI_Impl_Init
 *
 *  Purpose: Local helper routine, not part of OSAL API.
 *
 *-----------------------------------------------------------------*/
int32 OS_VxWorks_TaskAPI_Impl_Init(void)
{
    memset(OS_impl_task_table, 0, sizeof(OS_impl_task_table));
    return (OS_SUCCESS);
} /* end OS_VxWorks_TaskAPI_Impl_Init */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskCreate_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskCreate_Impl(uint32 task_id, uint32 flags)
{
    STATUS                          status;
    int                             vxflags;
    int                             vxpri;
    long                            actualsz;
    long                            userstackbase;
    long                            actualstackbase;
    OS_impl_task_internal_record_t *lrec;
    VxWorks_ID_Buffer_t             id;

    lrec = &OS_impl_task_table[task_id];

    /* Create VxWorks Task */

    /* see if the user wants floating point enabled. If
     * so, then se the correct option.
     */
    vxflags = 0;
    if (flags & OS_FP_ENABLED)
    {
        vxflags |= VX_FP_TASK;
    }

    /*
     * Get priority/stack specs from main struct
     * priority should be a direct passthru
     */
    vxpri         = OS_task_table[task_id].priority;
    actualsz      = OS_task_table[task_id].stack_size;
    userstackbase = (long)OS_task_table[task_id].stack_pointer;

    /*
     * NOTE: Using taskInit() here rather than taskSpawn() allows us
     * to specify a specific statically-allocated WIND_TCB instance.
     *
     * This is very important as it allows for efficient reverse-lookup;
     * a call to taskTcb() will get the WIND_TCB pointer back, which
     * in turn provides an index into OSAL local data structures.  With
     * this we can have the equivalent of a taskVar that works on both
     * UMP and SMP deployments.
     *
     * The difficulty with taskInit() is that we must also manually
     * allocate the stack as well (there is no API that allows
     * a specific WIND_TCB but automatically allocates the stack).
     * Furthermore, VxWorks uses this pointer directly as the CPU
     * stack pointer register, so we need to manually adjust it for
     * downward-growing stacks.
     *
     * NOTE: Allocation of the stack requires a malloc() of some form.
     * This is what taskSpawn() effectively does internally to create
     * stack.  If the system malloc() is unacceptable here then this
     * could be replaced with a statically-allocated OSAL stack buffer.
     *
     * ALSO NOTE: The stack-rounding macros are normally supplied from
     * vxWorks.h on relevant platforms.  If not provided then it is
     * assumed that no specific alignment is needed on this platform.
     */

    if (userstackbase == 0)
    {
        /* add a little extra in case the base address needs alignment too.
         * this helps ensure that the final aligned stack is not less
         * than what was originally requested (but might be a bit more)  */
        actualsz += VX_IMPL_STACK_ALIGN_SIZE;
        actualsz = VX_IMPL_STACK_ROUND_UP(actualsz);

        /*
         * VxWorks does not provide a way to deallocate
         * a taskInit-provided stack when a task exits.
         *
         * So in this case we will find the leftover heap
         * buffer when OSAL reuses this local record block.
         *
         * If that leftover heap buffer is big enough it
         * can be used directly.  Otherwise it needs to be
         * re-created.
         */
        if (lrec->heap_block_size < actualsz)
        {
            if (lrec->heap_block != NULL)
            {
                /* release the old block */
                free(lrec->heap_block);
                lrec->heap_block_size = 0;
            }

            /* allocate a new heap block to use for a stack */
            lrec->heap_block = malloc(actualsz);

            if (lrec->heap_block != NULL)
            {
                lrec->heap_block_size = actualsz;
            }
        }

        userstackbase = (long)lrec->heap_block;
    }

    if (userstackbase == 0)
    {
        /* no stack - cannot create task */
        return OS_ERROR;
    }

    actualstackbase = userstackbase;

    /* also round the base address */
    actualstackbase = VX_IMPL_STACK_ROUND_UP(actualstackbase);
    actualsz -= (actualstackbase - userstackbase);
    actualsz = VX_IMPL_STACK_ROUND_DOWN(actualsz);

    /*
     * On most CPUs the stack grows downward, so assume that to be
     * the case in the event that _STACK_DIR is not defined/known
     */
#if !defined(_STACK_DIR) || (_STACK_DIR != _STACK_GROWS_UP)
    actualstackbase += actualsz; /* move to last byte of stack block */
#endif

    id.id  = OS_global_task_table[task_id].active_id;
    status = taskInit(&lrec->tcb,                                              /* address of new task's TCB */
                      (char *)OS_global_task_table[task_id].name_entry, vxpri, /* priority of new task */
                      vxflags,                                                 /* task option word */
                      (char *)actualstackbase,                                 /* base of new task's stack */
                      actualsz,                                                /* size (bytes) of stack needed */
                      (FUNCPTR)OS_VxWorks_TaskEntry,                           /* entry point of new task */
                      id.arg,                                                  /* 1st arg is ID */
                      0, 0, 0, 0, 0, 0, 0, 0, 0);

    if (status != OK)
    {
        return OS_ERROR;
    }

    lrec->vxid = (TASK_ID)&lrec->tcb;

    taskActivate(lrec->vxid);

    return OS_SUCCESS;

} /* end OS_TaskCreate_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskDelete_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskDelete_Impl(uint32 task_id)
{
    /*
    ** Try to delete the task
    ** If this fails, not much recourse - the only potential cause of failure
    ** to cancel here is that the thread ID is invalid because it already exited itself,
    ** and if that is true there is nothing wrong - everything is OK to continue normally.
    */
    if (taskDelete(OS_impl_task_table[task_id].vxid) != OK)
    {
        OS_DEBUG("taskDelete() - vxWorks errno %d\n", errno);
        return OS_ERROR;
    }

    OS_impl_task_table[task_id].vxid = 0;
    return OS_SUCCESS;

} /* end OS_TaskDelete_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskExit_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
void OS_TaskExit_Impl()
{
    taskExit(0);
} /* end OS_TaskExit_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskDelay_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskDelay_Impl(uint32 milli_second)
{
    /* msecs rounded to the closest system tick count */
    int sys_ticks;

    /* Convert to ticks if possible */
    if (OS_Milli2Ticks(milli_second, &sys_ticks) != OS_SUCCESS)
    {
        return OS_ERROR;
    }

    /* if successful, the execution of task will pend here until delay finishes */
    if (taskDelay(sys_ticks) != OK)
    {
        return OS_ERROR;
    }
    return OS_SUCCESS;

} /* end OS_TaskDelay_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskSetPriority_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskSetPriority_Impl(uint32 task_id, uint32 new_priority)
{
    /* Set VxWorks Task Priority */
    if (taskPrioritySet(OS_impl_task_table[task_id].vxid, new_priority) != OK)
    {
        return OS_ERROR;
    }

    return OS_SUCCESS;

} /* end OS_TaskSetPriority_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskMatch_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskMatch_Impl(uint32 task_id)
{
    /*
    ** Get VxWorks Task Id
    */
    if (taskIdSelf() != OS_impl_task_table[task_id].vxid)
    {
        return (OS_ERROR);
    }

    return OS_SUCCESS;
} /* end OS_TaskMatch_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskRegister_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskRegister_Impl(osal_id_t global_task_id)
{
    return OS_SUCCESS;
} /* end OS_TaskRegister_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskGetId_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
osal_id_t OS_TaskGetId_Impl(void)
{
    OS_impl_task_internal_record_t *lrec;
    size_t                          index;
    osal_id_t                       id;

    id   = OS_OBJECT_ID_UNDEFINED;
    lrec = (OS_impl_task_internal_record_t *)taskTcb(taskIdSelf());

    if (lrec != NULL)
    {
        index = lrec - &OS_impl_task_table[0];
        if (index < OS_MAX_TASKS)
        {
            id = OS_global_task_table[index].active_id;
        }
    }

    return id;

} /* end OS_TaskGetId_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskGetInfo_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskGetInfo_Impl(uint32 task_id, OS_task_prop_t *task_prop)
{
    return OS_SUCCESS;

} /* end OS_TaskGetInfo_Impl */

/*----------------------------------------------------------------
 *
 * Function: OS_TaskValidateSystemData_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
int32 OS_TaskValidateSystemData_Impl(const void *sysdata, uint32 sysdata_size)
{
    if (sysdata == NULL || sysdata_size != sizeof(TASK_ID))
    {
        return OS_INVALID_POINTER;
    }
    return OS_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Function: OS_TaskIdMatchSystemData_Impl
 *
 *  Purpose: Implemented per internal OSAL API
 *           See prototype for argument/return detail
 *
 *-----------------------------------------------------------------*/
bool OS_TaskIdMatchSystemData_Impl(void *ref, uint32 local_id, const OS_common_record_t *obj)
{
    const TASK_ID *target = (const TASK_ID *)ref;

    return (*target == OS_impl_task_table[local_id].vxid);
}

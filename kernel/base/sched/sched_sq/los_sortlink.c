/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_sortlink_pri.h"
#include "los_mp.h"

VOID OsSortLinkInit(SortLinkAttribute *sortLinkHeader)
{
    LOS_ListInit(&sortLinkHeader->sortLink);
    LOS_SpinInit(&sortLinkHeader->spinLock);
    sortLinkHeader->nodeNum = 0;
}

STATIC INLINE VOID AddNode2SortLink(SortLinkAttribute *sortLinkHeader, SortLinkList *sortList)
{
    LOS_DL_LIST *head = (LOS_DL_LIST *)&sortLinkHeader->sortLink;

    if (LOS_ListEmpty(head)) {
        LOS_ListHeadInsert(head, &sortList->sortLinkNode);
        sortLinkHeader->nodeNum++;
        return;
    }

    SortLinkList *listSorted = LOS_DL_LIST_ENTRY(head->pstNext, SortLinkList, sortLinkNode);
    if (listSorted->responseTime > sortList->responseTime) {
        LOS_ListAdd(head, &sortList->sortLinkNode);
        sortLinkHeader->nodeNum++;
        return;
    } else if (listSorted->responseTime == sortList->responseTime) {
        LOS_ListAdd(head->pstNext, &sortList->sortLinkNode);
        sortLinkHeader->nodeNum++;
        return;
    }

    LOS_DL_LIST *prevNode = head->pstPrev;
    do {
        listSorted = LOS_DL_LIST_ENTRY(prevNode, SortLinkList, sortLinkNode);
        if (listSorted->responseTime <= sortList->responseTime) {
            LOS_ListAdd(prevNode, &sortList->sortLinkNode);
            sortLinkHeader->nodeNum++;
            break;
        }

        prevNode = prevNode->pstPrev;
    } while (1);
}

VOID OsAdd2SortLink(SortLinkAttribute *head, SortLinkList *node, UINT64 responseTime, UINT16 idleCpu)
{
    LOS_SpinLock(&head->spinLock);
    SET_SORTLIST_VALUE(node, responseTime);
    AddNode2SortLink(head, node);
#ifdef LOSCFG_KERNEL_SMP
    node->cpuid = idleCpu;
#endif
    LOS_SpinUnlock(&head->spinLock);

#ifdef LOSCFG_KERNEL_SMP
    if (idleCpu != ArchCurrCpuid()) {
        LOS_MpSchedule(CPUID_TO_AFFI_MASK(idleCpu));
    }
#endif
}

VOID OsDeleteFromSortLink(SortLinkAttribute *head, SortLinkList *node)
{
    LOS_SpinLock(&head->spinLock);
    if (node->responseTime != OS_SORT_LINK_INVALID_TIME) {
        OsDeleteNodeSortLink(head, node);
    }
    LOS_SpinUnlock(&head->spinLock);
}

UINT32 OsSortLinkGetTargetExpireTime(UINT64 currTime, const SortLinkList *targetSortList)
{
    if (currTime >= targetSortList->responseTime) {
        return 0;
    }

    return (UINT32)(targetSortList->responseTime - currTime);
}

UINT32 OsSortLinkGetNextExpireTime(UINT64 currTime, const SortLinkAttribute *sortLinkHeader)
{
    LOS_DL_LIST *head = (LOS_DL_LIST *)&sortLinkHeader->sortLink;

    if (LOS_ListEmpty(head)) {
        return 0;
    }

    SortLinkList *listSorted = LOS_DL_LIST_ENTRY(head->pstNext, SortLinkList, sortLinkNode);
    return OsSortLinkGetTargetExpireTime(currTime, listSorted);
}


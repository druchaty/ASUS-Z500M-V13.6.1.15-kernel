/*************************************************************************/ /*!
@File
@Title          Devicemem history functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Devicemem history functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "allocmem.h"
#include "pmr.h"
#include "pvrsrv.h"
#include "pvrsrv_device.h"
#include "pvr_debug.h"
#include "dllist.h"
#include "syscommon.h"
#include "devicemem_server.h"
#include "lock.h"
#include "devicemem_history_server.h"

/* a device memory allocation */
typedef struct _DEVICEMEM_HISTORY_ALLOCATION_
{
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_CHAR szString[DEVICEMEM_HISTORY_TEXT_BUFSZ];
	IMG_UINT64 ui64Time;
	/* FALSE if this allocation has been freed */
	IMG_BOOL bAllocated;
	IMG_PID uiPID;
} DEVICEMEM_HISTORY_ALLOCATION;

/* this number of entries makes the history buffer allocation just under 2MB */
#define DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN 29127

#define DECREMENT_WITH_WRAP(value, sz) ((value) ? ((value) - 1) : ((sz) - 1))

typedef struct _DEVICEMEM_HISTORY_DATA_
{
	IMG_UINT32 ui32Head;
	DEVICEMEM_HISTORY_ALLOCATION *psAllocations;
	POS_LOCK hLock;
	void *pvStatsEntry;
} DEVICEMEM_HISTORY_DATA;

static DEVICEMEM_HISTORY_DATA gsDevicememHistoryData = { 0 };

static void DevicememHistoryLock(void)
{
	OSLockAcquire(gsDevicememHistoryData.hLock);
}

static void DevicememHistoryUnlock(void)
{
	OSLockRelease(gsDevicememHistoryData.hLock);
}

/* given a time stamp, calculate the age in nanoseconds (relative to now) */
static IMG_UINT64 _CalculateAge(IMG_UINT64 ui64Then)
{
	IMG_UINT64 ui64Now;

	ui64Now = OSClockns64();

	if(ui64Now >= ui64Then)
	{
		/* no clock wrap */
		return ui64Now - ui64Then;
	}
	else
	{
		/* clock has wrapped */
		return ((~(IMG_UINT64) 0) - ui64Then) + ui64Now + 1;
	}
}

static void DeviceMemHistoryFmt(IMG_UINT32 ui32Off, IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	DEVICEMEM_HISTORY_ALLOCATION *psAlloc;

	psAlloc = &gsDevicememHistoryData.psAllocations[ui32Off % DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN];

	szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN - 1] = '\0';
	OSSNPrintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
				/* PID NAME MAP/UNMAP MIN-MAX SIZE AbsUS AgeUS*/
				"%04u %-40s %-6s "
				IMG_DEV_VIRTADDR_FMTSPEC "-" IMG_DEV_VIRTADDR_FMTSPEC" "
				"0x%08llX "
				"%013llu", /* 13 digits is over 2 hours of ns */
				psAlloc->uiPID,
				psAlloc->szString,
				psAlloc->bAllocated ? "MAP" : "UNMAP",
				psAlloc->sDevVAddr.uiAddr,
				psAlloc->sDevVAddr.uiAddr + psAlloc->uiSize - 1,
				psAlloc->uiSize,
				psAlloc->ui64Time);
}

static void DeviceMemHistoryFmtHeader(IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
				"%-4s %-40s %-6s   %10s   %10s   %8s %13s",
				"PID",
				"NAME",
				"ACTION",
				"ADDR MIN",
				"ADDR MAX",
				"SIZE",
				"ABS NS");
}

static void DevicememHistoryPrintAll(void *pvFilePtr, OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN];
	IMG_UINT32 ui32Iter;

	DeviceMemHistoryFmtHeader(szBuffer);
	pfnOSStatsPrintf(pvFilePtr, "%s\n", szBuffer);

	for(ui32Iter = DECREMENT_WITH_WRAP(gsDevicememHistoryData.ui32Head, DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN);
			;
			ui32Iter = DECREMENT_WITH_WRAP(ui32Iter, DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN))
	{
		DEVICEMEM_HISTORY_ALLOCATION *psAlloc;

		psAlloc = &gsDevicememHistoryData.psAllocations[ui32Iter];

		/* no more written elements */
		if(psAlloc->sDevVAddr.uiAddr == 0)
		{
			break;
		}

		DeviceMemHistoryFmt(ui32Iter, szBuffer);
pfnOSStatsPrintf(pvFilePtr, "%s\n", szBuffer);

		if(ui32Iter == gsDevicememHistoryData.ui32Head)
		{
			break;
		}
	}
	pfnOSStatsPrintf(pvFilePtr, "\nTimestamp reference: %013llu\n", OSClockns64());
}

static void DevicememHistoryPrintAllWrapper(void *pvFilePtr, void *pvData, OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	PVR_UNREFERENCED_PARAMETER(pvData);
	DevicememHistoryLock();
	DevicememHistoryPrintAll(pvFilePtr, pfnOSStatsPrintf);
	DevicememHistoryUnlock();
}

PVRSRV_ERROR DevicememHistoryInitKM(void)
{
	PVRSRV_ERROR eError;

	eError = OSLockCreate(&gsDevicememHistoryData.hLock, LOCK_TYPE_PASSIVE);

	if(eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "DevicememHistoryInitKM: Failed to create lock"));
		goto err_lock;
	}

	gsDevicememHistoryData.psAllocations = OSAllocZMem(sizeof(DEVICEMEM_HISTORY_ALLOCATION) * DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN);

	if(gsDevicememHistoryData.psAllocations == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "DevicememHistoryInitKM: Failed to allocate space for allocations list"));
		goto err_allocations;
	}

	gsDevicememHistoryData.pvStatsEntry = OSCreateStatisticEntry("devicemem_history",
						NULL,
						DevicememHistoryPrintAllWrapper,
						NULL,
						NULL,
						NULL);

	return PVRSRV_OK;

err_allocations:
	OSLockDestroy(gsDevicememHistoryData.hLock);
err_lock:
	return eError;
}

void DevicememHistoryDeInitKM(void)
{
	if(gsDevicememHistoryData.pvStatsEntry != NULL)
	{
		OSRemoveStatisticEntry(gsDevicememHistoryData.pvStatsEntry);
	}
	OSFREEMEM(gsDevicememHistoryData.psAllocations);
	OSLockDestroy(gsDevicememHistoryData.hLock);
}

static PVRSRV_ERROR DevicememHistoryWrite(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize,
						const char szString[DEVICEMEM_HISTORY_TEXT_BUFSZ],
						IMG_BOOL bAlloc)
{
	DEVICEMEM_HISTORY_ALLOCATION *psAlloc;

	PVR_ASSERT(gsDevicememHistoryData.psAllocations != NULL);

	DevicememHistoryLock();

	psAlloc = &gsDevicememHistoryData.psAllocations[gsDevicememHistoryData.ui32Head];
	PVR_ASSERT(gsDevicememHistoryData.ui32Head < DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN);

	gsDevicememHistoryData.ui32Head = (gsDevicememHistoryData.ui32Head + 1) % DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN;

	psAlloc->sDevVAddr = sDevVAddr;
	psAlloc->uiSize = uiSize;
	psAlloc->uiPID = OSGetCurrentProcessID();
	OSStringNCopy(psAlloc->szString, szString, sizeof(psAlloc->szString));
	psAlloc->szString[sizeof(psAlloc->szString) - 1] = '\0';
	psAlloc->bAllocated = bAlloc;
	psAlloc->ui64Time = OSClockns64();

	DevicememHistoryUnlock();

	return PVRSRV_OK;
}

PVRSRV_ERROR DevicememHistoryMapKM(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize, const char szString[DEVICEMEM_HISTORY_TEXT_BUFSZ])
{
	return DevicememHistoryWrite(sDevVAddr, uiSize, szString, IMG_TRUE);
}

PVRSRV_ERROR DevicememHistoryUnmapKM(IMG_DEV_VIRTADDR sDevVAddr, size_t uiSize, const char szString[DEVICEMEM_HISTORY_TEXT_BUFSZ])
{
	return DevicememHistoryWrite(sDevVAddr, uiSize, szString, IMG_FALSE);
}

IMG_BOOL DevicememHistoryQuery(DEVICEMEM_HISTORY_QUERY_IN *psQueryIn, DEVICEMEM_HISTORY_QUERY_OUT *psQueryOut)
{
	IMG_UINT32 ui32Entry;

	/* initialise the results count for the caller */
	psQueryOut->ui32NumResults = 0;

	DevicememHistoryLock();

	/* search from newest to oldest */

	ui32Entry = gsDevicememHistoryData.ui32Head;

	do
	{
		DEVICEMEM_HISTORY_ALLOCATION *psAlloc;

		/* searching backwards (from newest to oldest)
		 * wrap around backwards when going past zero
		 */
		ui32Entry = (ui32Entry != 0) ? ui32Entry - 1 : DEVICEMEM_HISTORY_ALLOCATION_HISTORY_LEN - 1;
		psAlloc = &gsDevicememHistoryData.psAllocations[ui32Entry];

		if(((psAlloc->uiPID == psQueryIn->uiPID) || (psQueryIn->uiPID == DEVICEMEM_HISTORY_PID_ANY)) &&
			(psQueryIn->sDevVAddr.uiAddr >= psAlloc->sDevVAddr.uiAddr) &&
			(psQueryIn->sDevVAddr.uiAddr < psAlloc->sDevVAddr.uiAddr + psAlloc->uiSize))
		{
				DEVICEMEM_HISTORY_QUERY_OUT_RESULT *psResult = &psQueryOut->sResults[psQueryOut->ui32NumResults];

				OSStringNCopy(psResult->szString, psAlloc->szString, sizeof(psResult->szString));
				psResult->szString[DEVICEMEM_HISTORY_TEXT_BUFSZ - 1] = '\0';
				psResult->sBaseDevVAddr = psAlloc->sDevVAddr;
				psResult->uiSize = psAlloc->uiSize;
				psResult->bAllocated = psAlloc->bAllocated;
				psResult->ui64Age = _CalculateAge(psAlloc->ui64Time);
				psResult->ui64When = psAlloc->ui64Time;
				/* write the responsible PID in the placeholder */
				psResult->sProcessInfo.uiPID = psAlloc->uiPID;

				psQueryOut->ui32NumResults++;
		}
	} while((psQueryOut->ui32NumResults < DEVICEMEM_HISTORY_QUERY_OUT_MAX_RESULTS) &&
						(ui32Entry != gsDevicememHistoryData.ui32Head));

	DevicememHistoryUnlock();

	return psQueryOut->ui32NumResults > 0;
}

/*************************************************************************/ /*!
@File
@Title          Linux module setup
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include <linux/version.h>

#if defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
#define PVR_USE_PRE_REGISTERED_PLATFORM_DEV
#endif

#include <linux/module.h>
#include <linux/device.h>

#include <linux/fs.h>

#if defined(LDM_PLATFORM)
#include <linux/dma-mapping.h>
#endif

#include "pvr_debug.h"
#include "srvkm.h"
#include "pvrmodule.h"
#include "linkage.h"
#include "sysinfo.h"
#include "module_common.h"

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
#include "syscommon.h"
#endif

#if defined(SUPPORT_SHARED_SLC)
#include "rgxapi_km.h"
#endif

#include "mtk_mfgsys.h"
#include <linux/of.h>

/* MTK: fixme */
#ifndef MODULE
#define MODULE
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
#include "kerneldisplay.h"
#endif

/*
 * DRVNAME is the name we use to register our driver.
 * DEVNAME is the name we use to register actual device nodes.
 */
#define	DRVNAME		PVR_LDM_DRIVER_REGISTRATION_NAME
#define DEVNAME		PVRSRV_MODNAME

/*
 * This is all module configuration stuff required by the linux kernel.
 */
MODULE_SUPPORTED_DEVICE(DEVNAME);

#if defined(SUPPORT_DISPLAY_CLASS)
/* Display class interface */
#include "kerneldisplay.h"
EXPORT_SYMBOL(DCRegisterDevice);
EXPORT_SYMBOL(DCUnregisterDevice);
EXPORT_SYMBOL(DCDisplayConfigurationRetired);
EXPORT_SYMBOL(DCDisplayHasPendingCommand);
EXPORT_SYMBOL(DCImportBufferAcquire);
EXPORT_SYMBOL(DCImportBufferRelease);

/* Physmem interface (required by LMA DC drivers) */
#include "physheap.h"
EXPORT_SYMBOL(PhysHeapAcquire);
EXPORT_SYMBOL(PhysHeapRelease);
EXPORT_SYMBOL(PhysHeapGetType);
EXPORT_SYMBOL(PhysHeapGetAddress);
EXPORT_SYMBOL(PhysHeapGetSize);
EXPORT_SYMBOL(PhysHeapCpuPAddrToDevPAddr);
#endif

/* System interface (required by DC drivers) */
#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING)
EXPORT_SYMBOL(SysInstallDeviceLISR);
EXPORT_SYMBOL(SysUninstallDeviceLISR);
#endif

#if defined(SUPPORT_SHARED_SLC)
EXPORT_SYMBOL(RGXInitSLC);
#endif

struct device *psDev;

/*
 * Device class used for /sys entries (and udev device node creation)
 */
static struct class *psPvrClass;

/*
 * This is the major number we use for all nodes in /dev.
 */
static int AssignedMajorNumber;

/*
 * These are the operations that will be associated with the device node
 * we create.
 *
 * With gcc -W, specifying only the non-null members produces "missing
 * initializer" warnings.
*/
static int PVRSRVOpen(struct inode* pInode, struct file* pFile);
static int PVRSRVRelease(struct inode* pInode, struct file* pFile);

static struct file_operations pvrsrv_fops =
{
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= PVRSRV_BridgeDispatchKM,
#if defined(CONFIG_COMPAT)
	.compat_ioctl	= PVRSRV_BridgeCompatDispatchKM,
#endif
	.open		= PVRSRVOpen,
	.release	= PVRSRVRelease,
	.mmap		= MMapPMR,
};

#if defined(LDM_PLATFORM)
#define	LDM_DRV	struct platform_driver
#endif /*LDM_PLATFORM */

#if defined(LDM_PCI)
#define	LDM_DRV	struct pci_driver
#endif /* LDM_PCI */

#if defined(LDM_PLATFORM)
static int PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device);
#endif

#if defined(LDM_PCI)
static void PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device, const struct pci_device_id *id);
#endif

#if defined(LDM_PCI)
/* This structure is used by the Linux module code */
struct pci_device_id powervr_id_table[] __devinitdata = {
	{PCI_DEVICE(SYS_RGX_DEV_VENDOR_ID, SYS_RGX_DEV_DEVICE_ID)},
#if defined (SYS_RGX_DEV1_DEVICE_ID)
	{PCI_DEVICE(SYS_RGX_DEV_VENDOR_ID, SYS_RGX_DEV1_DEVICE_ID)},
#endif
	{0}
};
MODULE_DEVICE_TABLE(pci, powervr_id_table);
#endif /*defined(LDM_PCI) */

#if defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
static struct platform_device_id powervr_id_table[] __devinitdata = {
	{SYS_RGX_DEV_NAME, 0},
	{}
};
#endif

static struct dev_pm_ops powervr_dev_pm_ops = {
	.suspend	= PVRSRVDriverSuspend,
	.resume		= PVRSRVDriverResume,
};

static const struct of_device_id mtk_dt_ids[] = {
	{ .compatible = "mediatek,mt8173-han" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_dt_ids);

static LDM_DRV powervr_driver = {
#if defined(LDM_PLATFORM)
	.driver = {
		.name	= DRVNAME,
		.pm	= &powervr_dev_pm_ops,
		.of_match_table = of_match_ptr(mtk_dt_ids),
	},
#endif
#if defined(LDM_PCI)
	.name		= DRVNAME,
	.driver.pm	= &powervr_dev_pm_ops,
#endif
#if defined(LDM_PCI) || defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
	.id_table	= powervr_id_table,
#endif
	.probe		= PVRSRVDriverProbe,
#if defined(LDM_PLATFORM)
	.remove		= PVRSRVDriverRemove,
#endif
#if defined(LDM_PCI)
	.remove		= __devexit_p(PVRSRVDriverRemove),
#endif
	.shutdown	= PVRSRVDriverShutdown,
};

#if defined(LDM_PLATFORM)
#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
static void PVRSRVDeviceRelease(struct device unref__ *pDevice)
{
}

static struct platform_device powervr_device =
{
	.name			= DEVNAME,
	.id			= -1,
	.dev 			= {
		.release	= PVRSRVDeviceRelease
	}
};
#else
static struct platform_device_info powervr_device_info =
{
	.name			= DEVNAME,
	.id			= -1,
	.dma_mask		= DMA_BIT_MASK(32),
};
#endif	/* (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)) */
#endif	/* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
#endif	/* defined(LDM_PLATFORM) */

static IMG_BOOL bCalledSysInit = IMG_FALSE;
static IMG_BOOL	bDriverProbeSucceeded = IMG_FALSE;

/*!
******************************************************************************

 @Function		PVRSRVSystemInit

 @Description

 Wrapper for PVRSRVInit.

 @input pDevice - the device for which a probe is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
static int PVRSRVSystemInit(LDM_DEV *pDevice)
{
	PVR_TRACE(("PVRSRVSystemInit (pDevice=%p)", pDevice));

	gpsPVRLDMDev = pDevice;
	bCalledSysInit = IMG_TRUE;

	if (PVRSRVInit(pDevice) != PVRSRV_OK)
	{
		return -ENODEV;
	}

	return 0;
}

/*!
******************************************************************************

 @Function		PVRSRVSystemDeInit

 @Description

 Wrapper for PVRSRVDeInit.

 @input pDevice - the device for which a probe is requested
 @Return nothing.

*****************************************************************************/
static void PVRSRVSystemDeInit(LDM_DEV *pDevice)
{
	PVR_TRACE(("PVRSRVSystemDeInit"));

	PVRSRVDeInit(pDevice);

#if !defined(LDM_PLATFORM) || (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	gpsPVRLDMDev = IMG_NULL;
#endif
}

/*!
******************************************************************************

 @Function		PVRSRVDriverProbe

 @Description

 See whether a given device is really one we can drive.

 @input pDevice - the device for which a probe is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(LDM_PLATFORM)
static int PVRSRVDriverProbe(LDM_DEV *pDevice)
#endif
#if defined(LDM_PCI)
static int __devinit PVRSRVDriverProbe(LDM_DEV *pDevice, const struct pci_device_id *pID)
#endif
{
	int result = 0;

	PVR_DPF((PVR_DBG_ERROR,"PVRSRVDriverProbe (pDevice=%p),name %s", pDevice, pDevice->name));
	
	if (OSStringCompare(pDevice->name,DEVNAME) != 0)
	{
		result = MTKMFGBaseInit(pDevice);
		if (result != 0)
			return result;
	}

	result = PVRSRVSystemInit(pDevice);
	bDriverProbeSucceeded = (result == 0);
	return result;
}


/*!
******************************************************************************

 @Function		PVRSRVDriverRemove

 @Description

 This call is the opposite of the probe call; it is called when the device is
 being removed from the driver's control.

 @input pDevice - the device for which driver detachment is happening

 @Return 0, or no return value at all, depending on the device type.

*****************************************************************************/
#if defined (LDM_PLATFORM)
static int PVRSRVDriverRemove(LDM_DEV *pDevice)
#endif
#if defined(LDM_PCI)
static void __devexit PVRSRVDriverRemove(LDM_DEV *pDevice)
#endif
{
	PVR_TRACE(("PVRSRVDriverRemove (pDevice=%p)", pDevice));

	PVRSRVSystemDeInit(pDevice);

#if defined(LDM_PLATFORM)
	return 0;
#endif
}

/*!
******************************************************************************

 @Function		PVRSRVOpen

 @Description

 Open the PVR services node.

 @input pInode - the inode for the file being openeded.
 @input dev    - the DRM device corresponding to this driver.

 @input pFile - the file handle data for the actual file being opened

 @Return 0 for success or <0 for an error.

*****************************************************************************/
static int PVRSRVOpen(struct inode unref__ *pInode, struct file *pFile)
{
	int err;

	if (!try_module_get(THIS_MODULE))
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to get module"));
		return -ENOENT;
	}

	if ((err = PVRSRVCommonOpen(pFile)) != 0)
	{
		module_put(THIS_MODULE);
	}

	return err;
}

/*!
******************************************************************************

 @Function		PVRSRVRelease

 @Description

 Release access the PVR services node - called when a file is closed, whether
 at exit or using close(2) system call.

 @input pInode - the inode for the file being released
 @input pvPrivData - driver private data

 @input pFile - the file handle data for the actual file being released

 @Return 0 for success or <0 for an error.

*****************************************************************************/
static int PVRSRVRelease(struct inode unref__ *pInode, struct file *pFile)
{
	PVRSRVCommonRelease(pFile);

	module_put(THIS_MODULE);

	return 0;
}

/*!
******************************************************************************

 @Function		PVRCore_Init

 @Description

 Insert the driver into the kernel.

 Readable and/or writable debugfs entries under /sys/kernel/debug/pvr are
 created with PVRDebugFSCreateEntry().  These can be read at runtime to get
 information about the device (eg. 'cat /sys/kernel/debug/pvr/nodes')

 __init places the function in a special memory section that the kernel frees
 once the function has been run.  Refer also to module_init() macro call below.

 @input none

 @Return none

*****************************************************************************/
static int __init PVRCore_Init(void)
{
	int error = 0;

	PVR_TRACE(("PVRCore_Init"));

	if ((error = PVRSRVDriverInit()) != 0)
	{
		return error;
	}

	AssignedMajorNumber = register_chrdev(0, DEVNAME, &pvrsrv_fops);
	if (AssignedMajorNumber <= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to get major number"));
		return -EBUSY;
	}

	PVR_TRACE(("PVRCore_Init: major device %d", AssignedMajorNumber));

	/*
	 * This code facilitates automatic device node creation on platforms
	 * with udev (or similar).
	 */
	psPvrClass = class_create(THIS_MODULE, "pvr");
	if (IS_ERR(psPvrClass))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create class (%ld)", PTR_ERR(psPvrClass)));
		return -EBUSY;
	}

	psDev = device_create(psPvrClass, NULL, MKDEV(AssignedMajorNumber, 0),
				  NULL, DEVNAME);
	if (IS_ERR(psDev))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create device (%ld)", PTR_ERR(psDev)));
		return -EBUSY;
	}

#if defined(LDM_PLATFORM)
	error = platform_driver_register(&powervr_driver);
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register platform driver (%d)", error));
		return error;
	}

#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	error = platform_device_register(&powervr_device);
#else
	gpsPVRLDMDev = platform_device_register_full(&powervr_device_info);
	error = IS_ERR(gpsPVRLDMDev) ? PTR_ERR(gpsPVRLDMDev) : 0;
#endif
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register platform device (%d)", error));
		return error;
	}
#endif /* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
#endif /* defined(LDM_PLATFORM) */ 

#if defined(LDM_PCI)
	error = pci_register_driver(&powervr_driver);
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register PCI driver (%d)", error));
		return error;
	}
#endif /* defined(LDM_PCI) */

	/* Check that the driver probe function was called */
	if (!bDriverProbeSucceeded)
	{
		PVR_TRACE(("PVRCore_Init: PVRSRVDriverProbe has not been called or did not succeed - check that hardware is detected"));
		return error;
	}

	error = PVRSRVDeviceInit();
	if (error != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: PVRSRVDeviceInit() failed (%d)", error));
		return -ENODEV;
	}

	/* MTK MFG system entry */
	MTKMFGSystemInit();

	return 0;
}


/*!
*****************************************************************************

 @Function		PVRCore_Cleanup

 @Description	

 Remove the driver from the kernel.

 There's no way we can get out of being unloaded other than panicking; we
 just do everything and plough on regardless of error.

 __exit places the function in a special memory section that the kernel frees
 once the function has been run.  Refer also to module_exit() macro call below.

 @input none

 @Return none

*****************************************************************************/
static void __exit PVRCore_Cleanup(void)
{
	PVR_TRACE(("PVRCore_Cleanup"));

	/* MTK MFG sytem cleanup */
	MTKMFGSystemDeInit();
	PVRSRVDeviceDeinit();

	if (psDev)
	{
		device_destroy(psPvrClass, MKDEV(AssignedMajorNumber, 0));
	}

	if (psPvrClass)
	{
		class_destroy(psPvrClass);
	}

	if (AssignedMajorNumber > 0)
	{
		unregister_chrdev((IMG_UINT)AssignedMajorNumber, DEVNAME);
	}

#if defined(LDM_PCI)
	pci_unregister_driver(&powervr_driver);
#endif /* defined(LDM_PCI) */

#if defined (LDM_PLATFORM)
#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	platform_device_unregister(&powervr_device);
#else
	PVR_ASSERT(gpsPVRLDMDev != NULL);
	platform_device_unregister(gpsPVRLDMDev);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)) */
#endif /* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
	platform_driver_unregister(&powervr_driver);
#endif /* defined (LDM_PLATFORM) */

	PVRSRVDriverDeinit();

	PVR_TRACE(("PVRCore_Cleanup: unloading"));
}

/*
 * These macro calls define the initialisation and removal functions of the
 * driver.  Although they are prefixed `module_', they apply when compiling
 * statically as well; in both cases they define the function the kernel will
 * run to start/stop the driver.
*/
module_init(PVRCore_Init);
module_exit(PVRCore_Cleanup);

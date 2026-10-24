#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif
#include <asm/io.h>
#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"
#include <linux/dma-mapping.h>
#include <mach/mt_irq.h>
#include "arm_core_scaling.h"
#include "platform_pmm.h"
#include "mali_pm.h"
#include "mali_osk.h"
#include "mt_reg_base.h"

static void mali_platform_device_release(struct device *device);
static int mali_pm_suspend(struct device *device);
static int mali_pm_resume(struct device *device);


static struct mali_gpu_device_data mali_gpu_data =
{
    // System memory
    .shared_mem_size = 1024 * 1024 * 1024, /* 1GB */
    // Framebuffer physical address, only for validation usage
    .fb_start = 0x80000000,
    .fb_size  = 0x80000000,
    // DVFS
    .utilization_interval = 200, /* ms */
    .utilization_callback = mali_pmm_utilization_handler, /*<utilization function>,*/
};

static struct resource mali_gpu_resources[] =
{
    MALI_GPU_RESOURCES_MALI450_MP4(
                    IO_VIRT_TO_PHYS(MALI_BASE),
                    MT_MFG_IRQ0_ID,
                    MT_MFG_IRQ1_ID,
                    MT_MFG_IRQ2_ID,
                    MT_MFG_IRQ3_ID,
                    MT_MFG_IRQ4_ID,
                    MT_MFG_IRQ5_ID,
                    MT_MFG_IRQ6_ID,
                    MT_MFG_IRQ7_ID,
                    MT_MFG_IRQ8_ID,
                    MT_MFG_IRQ9_ID,
                    MT_MFG_IRQ10_ID
                )
};

static struct resource mali_gpu_resources_MP3[] =
{
    MALI_GPU_RESOURCES_MALI450_MP3(
                    IO_VIRT_TO_PHYS(MALI_BASE),
                    MT_MFG_IRQ0_ID,
                    MT_MFG_IRQ1_ID,
                    MT_MFG_IRQ2_ID,
                    MT_MFG_IRQ3_ID,
                    MT_MFG_IRQ4_ID,
                    MT_MFG_IRQ5_ID,
                    MT_MFG_IRQ6_ID,
                    MT_MFG_IRQ7_ID,                   
                    MT_MFG_IRQ10_ID
                )
};


static struct resource mali_gpu_resources_MP2[] =
{
    MALI_GPU_RESOURCES_MALI450_MP2(
                    IO_VIRT_TO_PHYS(MALI_BASE),
                    MT_MFG_IRQ0_ID,
                    MT_MFG_IRQ1_ID,
                    MT_MFG_IRQ2_ID,
                    MT_MFG_IRQ3_ID,
                    MT_MFG_IRQ4_ID,
                    MT_MFG_IRQ5_ID,
                    MT_MFG_IRQ10_ID
                )
};


static struct resource mali_gpu_resources_MP1[] =
{
    MALI_GPU_RESOURCES_MALI450_MP1(
                    IO_VIRT_TO_PHYS(MALI_BASE),
                    MT_MFG_IRQ0_ID,
                    MT_MFG_IRQ1_ID,
                    MT_MFG_IRQ2_ID,
                    MT_MFG_IRQ3_ID,
                    MT_MFG_IRQ10_ID
                )
};


static struct dev_pm_ops mali_gpu_device_type_pm_ops =
{
    .suspend = mali_pm_suspend,
    .resume  = mali_pm_resume, 
    .freeze  = mali_pm_suspend, 
    .thaw    = mali_pm_resume,   
	.restore = mali_pm_resume,
	
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = mali_runtime_suspend,
	.runtime_resume  = mali_runtime_resume,
	.runtime_idle   = mali_runtime_idle,
#endif
};

static struct device_type mali_gpu_device_device_type =
{
	.pm = &mali_gpu_device_type_pm_ops,
};


static struct platform_device mali_gpu_device =
{
   .name = MALI_GPU_NAME_UTGARD,
   .id = 0,
   .num_resources = ARRAY_SIZE(mali_gpu_resources),
   .resource = (struct resource *)&mali_gpu_resources,
   .dev.platform_data = &mali_gpu_data,
   .dev.release = mali_platform_device_release,
	.dev.coherent_dma_mask = DMA_BIT_MASK(32),    
	 /// Ideally .dev.pm_domain should be used instead, as this is the new framework designed
	 /// to control the power of devices.	 
	.dev.type = &mali_gpu_device_device_type /// We should probably use the pm_domain instead of type on newer kernels
};


extern u32 get_devinfo_with_index(u32 index);


#define MALI_REASSIGN_RESOURCE(device, X) \
do {\
	device->resource = (struct resource *)&(X);\
	device->num_resources = ARRAY_SIZE((X));\
}while(0)

/*
 * MT8590 fuses the PP count into devinfo word 3 bits [18:17], 3 meaning MP1, and
 * the stock kernel pins MP1 regardless (MTK_CONFIG_FF_PP). Registering cores the
 * die has fused off would point the driver at registers that are not there.
 */
static void update_dev_info(struct platform_device * device ) {
    u32 pp = (get_devinfo_with_index(3) & 0x60000) >> 17;
    MALI_PRINT(("devinfo PP fuse %u, using MP1\n", pp));
    MALI_REASSIGN_RESOURCE(device, mali_gpu_resources_MP1);
}

/*
 * The stock kernel's built-in r6p2 driver registers mali-utgard.0 on these same
 * registers and IRQs. Once it is unbound through sysfs, drop that device so the
 * resources can be claimed here; while it is still bound, refuse to load.
 * Getting the stock driver back takes a reboot.
 */
static int mali_take_over_stock_device(void)
{
    struct device *dev = bus_find_device_by_name(&platform_bus_type, NULL, "mali-utgard.0");

    if (NULL == dev)
        return 0;
    if (NULL != dev->driver) {
        put_device(dev);
        MALI_PRINT_ERROR(("mali-utgard.0 is still bound to the stock driver; unbind it first\n"));
        return -EBUSY;
    }
    platform_device_unregister(to_platform_device(dev));
    put_device(dev);
    MALI_PRINT(("took over the stock mali-utgard.0 resources\n"));
    return 0;
}

extern unsigned int get_max_DRAM_size (void);
int mali_platform_device_register(void)
{
    int err = mali_take_over_stock_device();

#if defined(__MALI_CORE_SCALING_ENABLE__)
    int num_pp_cores = 1;
#endif

    if (err)
        return err;
    MALI_DEBUG_PRINT(1, ("%s\n", __FUNCTION__));
    mali_gpu_data.shared_mem_size = get_max_DRAM_size();

    update_dev_info(&mali_gpu_device);
    
    err = platform_device_register(&mali_gpu_device);
	            
    if (0 == err) 
    {         
        mali_pmm_init();
        
#ifdef CONFIG_PM_RUNTIME
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
				pm_runtime_set_autosuspend_delay(&(mali_gpu_device.dev), 1000);
				pm_runtime_use_autosuspend(&(mali_gpu_device.dev));
#endif
				pm_runtime_enable(&(mali_gpu_device.dev));
#endif

#if defined(__MALI_CORE_SCALING_ENABLE__)
        mali_core_scaling_init(num_pp_cores);
#endif        
        return 0;
    }

    MALI_DEBUG_PRINT(1, ("%s err=%d\n",__FUNCTION__, err));

    platform_device_unregister(&mali_gpu_device);

    return err;
}

void mali_platform_device_unregister(void)
{
    MALI_DEBUG_PRINT(1, ("%s\n", __FUNCTION__));    
    
#if defined(__MALI_CORE_SCALING_ENABLE__)    
    mali_core_scaling_term();
#endif
    
    mali_pmm_deinit();
 
    platform_device_unregister(&mali_gpu_device);
}

static void mali_platform_device_release(struct device *device)
{
    MALI_DEBUG_PRINT(1, ("%s\n", __FUNCTION__));
}

static int mali_pm_suspend(struct device *device)
{
    int ret = 0;

    MALI_DEBUG_PRINT(1, ("Mali PM:%s\n", __FUNCTION__));

    if (NULL != device->driver &&
        NULL != device->driver->pm &&
        NULL != device->driver->pm->suspend)
    {
        /* Need to notify Mali driver about this event */
        ret = device->driver->pm->suspend(device);
    }
   
    _mali_osk_pm_delete_callback_timer();
    mali_platform_power_mode_change(MALI_POWER_MODE_DEEP_SLEEP);

    return ret;
}

static int mali_pm_resume(struct device *device)
{
    int ret = 0;

    MALI_DEBUG_PRINT(1, ("Mali PM: %s\n", __FUNCTION__));

    mali_platform_power_mode_change(MALI_POWER_MODE_ON);

    if (NULL != device->driver &&
        NULL != device->driver->pm &&
        NULL != device->driver->pm->resume)
    {
        /* Need to notify Mali driver about this event */
        ret = device->driver->pm->resume(device);
    }

    return ret;
}



#if 0//because not used
static int mali_pm_freeze(struct device *device)
{
    int ret = 0;
    
    MALI_DEBUG_PRINT(1, ("Mali PM: %s\n", __FUNCTION__));
    
    if (NULL != device->driver &&
        NULL != device->driver->pm &&
        NULL != device->driver->pm->freeze)
    {
        /* Need to notify Mali driver about this event */
        ret = device->driver->pm->freeze(device);
    }

    return ret;
}

static int mali_pm_thaw(struct device *device)
{
    int ret = 0;

    MALI_DEBUG_PRINT(1, ("Mali PM: %s\n", __FUNCTION__));

    if (NULL != device->driver &&
        NULL != device->driver->pm &&
        NULL != device->driver->pm->thaw)
    {
        /* Need to notify Mali driver about this event */
        ret = device->driver->pm->thaw(device);
    }

    return ret;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int mali_runtime_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_suspend() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_suspend)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_suspend(device);
	}

	mali_platform_power_mode_change(MALI_POWER_MODE_LIGHT_SLEEP);

	return ret;
}

static int mali_runtime_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_resume() called\n"));

	mali_platform_power_mode_change(MALI_POWER_MODE_ON);

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_resume)
	{
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_resume(device);
	}

	return ret;
}

static int mali_runtime_idle(struct device *device)
{
	MALI_DEBUG_PRINT(4, ("mali_runtime_idle() called\n"));

	if (NULL != device->driver &&
	    NULL != device->driver->pm &&
	    NULL != device->driver->pm->runtime_idle)
	{
		/* Need to notify Mali driver about this event */
		int ret = device->driver->pm->runtime_idle(device);
		if (0 != ret)
		{
			return ret;
		}
	}

	pm_runtime_suspend(device);

	return 0;
}
#endif /// CONFIG_PM_RUNTIME

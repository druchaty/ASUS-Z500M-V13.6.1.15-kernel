/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2010-2015 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

#ifndef __MALI_KERNEL_UTILIZATION_H__
#define __MALI_KERNEL_UTILIZATION_H__

#include <linux/mali/mali_utgard.h>
#include "mali_osk.h"

/**
 * Initialize/start the Mali GPU utilization metrics reporting.
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t mali_utilization_init(void);

/**
 * Terminate the Mali GPU utilization metrics reporting
 */
void mali_utilization_term(void);

/**
 * Check if Mali utilization is enabled
 */
mali_bool mali_utilization_enabled(void);

/**
 * Should be called when a job is about to execute a GP job
 */
void mali_utilization_gp_start(void);

/**
 * Should be called when a job has completed executing a GP job
 */
void mali_utilization_gp_end(void);

/**
 * Should be called when a job is about to execute a PP job
 */
void mali_utilization_pp_start(void);

/**
 * Should be called when a job has completed executing a PP job
 */
void mali_utilization_pp_end(void);

/**
 * Should be called to calcution the GPU utilization
 */
struct mali_gpu_utilization_data *mali_utilization_calculate(u64 *start_time, u64 *time_period, mali_bool *need_add_timer);

_mali_osk_spinlock_irq_t *mali_utilization_get_lock(void);

void mali_utilization_platform_realize(struct mali_gpu_utilization_data *util_data);

void mali_utilization_data_lock(void);

void mali_utilization_data_unlock(void);

void mali_utilization_data_assert_locked(void);

void mali_utilization_reset(void);


#endif /* __MALI_KERNEL_UTILIZATION_H__ */

/*
 * Copyright (c) 2013-2015, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * APU specific definition of processors in the subsystem as well as functions
 * for getting information about and changing state of the APU.
 */

#include "pm_client.h"

static const struct pm_ipi apu_ipi = {
	.mask = IPI_APU_MASK,
	.base = IPI_BASEADDR,
	.buffer_base = IPI_BUFFER_APU_BASE,
};

static const struct pm_proc pm_apu_0_proc = {
	.node_id = NODE_APU_0,
	.pwrdn_mask = APU_0_PWRCTL_CPUPWRDWNREQ_MASK,
	.ipi = &apu_ipi,
};

static const struct pm_proc pm_apu_1_proc = {
	.node_id = NODE_APU_1,
	.pwrdn_mask = APU_1_PWRCTL_CPUPWRDWNREQ_MASK,
	.ipi = &apu_ipi,
};

static const struct pm_proc pm_apu_2_proc = {
	.node_id = NODE_APU_2,
	.pwrdn_mask = APU_2_PWRCTL_CPUPWRDWNREQ_MASK,
	.ipi = &apu_ipi,
};

static const struct pm_proc pm_apu_3_proc = {
	.node_id = NODE_APU_3,
	.pwrdn_mask = APU_3_PWRCTL_CPUPWRDWNREQ_MASK,
	.ipi = &apu_ipi,
};

/* Order in pm_proc_all array must match cpu ids */
static const struct pm_proc *const pm_procs_all[] = {
	&pm_apu_0_proc,
	&pm_apu_1_proc,
	&pm_apu_2_proc,
	&pm_apu_3_proc,
};

/**
 * pm_get_proc() - returns pointer to the proc structure
 * @cpuid:	id of the cpu whose proc struct pointer should be returned
 *
 * Return: pointer to a proc structure if proc is found, otherwise NULL
 */
const struct pm_proc *pm_get_proc(const uint32_t cpuid)
{
	if (cpuid < PM_ARRAY_SIZE(pm_procs_all))
		return pm_procs_all[cpuid];

	return NULL;
}

/**
 * pm_get_proc_by_node() - returns pointer to the proc structure
 * @nid:	node id of the processor
 *
 * Return: pointer to a proc structure if proc is found, otherwise NULL
 */
const struct pm_proc *pm_get_proc_by_node(const enum pm_node_id nid)
{
	uint32_t i;

	for (i = 0; i < PM_ARRAY_SIZE(pm_procs_all); i++) {
		if (nid == pm_procs_all[i]->node_id) {
			return pm_procs_all[i];
		}
	}
	return NULL;
}

/**
 * pm_get_cpuid() - get the local cpu ID for a global node ID
 * @nid:	node id of the processor
 *
 * Return: the cpu ID (starting from 0) for the subsystem
 */
static uint32_t pm_get_cpuid(const enum pm_node_id nid)
{
	uint32_t i;

	for (i = 0; i < PM_ARRAY_SIZE(pm_procs_all); i++) {
		if (pm_procs_all[i]->node_id == nid) {
			return i;
		}
	}
	return UNDEFINED_CPUID;
}

const enum pm_node_id subsystem_node = NODE_APU;
const struct pm_proc *primary_proc = &pm_apu_0_proc;

/**
 * pm_client_suspend() - Client-specific suspend actions
 *
 * This function should contain any PU-specific actions
 * required prior to sending suspend request to PMU
 */
void pm_client_suspend(const struct pm_proc *const proc)
{
	/* Disable interrupts at processor level (for current cpu) */
	arm_gic_cpuif_deactivate();
	/* Set powerdown request */
	pm_write(APU_PWRCTL, pm_read(APU_PWRCTL) | proc->pwrdn_mask);
}


/**
 * pm_client_abort_suspend() - Client-specific abort-suspend actions
 *
 * This function should contain any PU-specific actions
 * required for aborting a prior suspend request
 */
void pm_client_abort_suspend(void)
{
	/* Enable interrupts at processor level (for current cpu) */
	arm_gic_cpuif_setup();
	/* Clear powerdown request */
	pm_write(APU_PWRCTL,
		 pm_read(APU_PWRCTL) & ~primary_proc->pwrdn_mask);
}

/**
 * pm_client_wakeup() - Client-specific wakeup actions
 *
 * This function should contain any PU-specific actions
 * required for waking up another APU core
 */
void pm_client_wakeup(const struct pm_proc *const proc)
{
	uint32_t cpuid = pm_get_cpuid(proc->node_id);

	if (UNDEFINED_CPUID != cpuid) {
		/* clear powerdown bit for affected cpu */
		uint32_t val = pm_read(APU_PWRCTL);
		val &= ~(proc->pwrdn_mask);
		pm_write(APU_PWRCTL, val);
	}
}

/**
 * pm_ipi_wait() - wait for pmu to handle request
 * @proc	proc which is waiting for PMU to handle request
 */
enum pm_ret_status pm_ipi_wait(const struct pm_proc *const proc)
{
	bakery_lock_get(&pm_secure_lock);
	uint32_t status = 1;

	/* Wait until previous interrupt is handled by PMU */
	while (status) {
		status = pm_read(proc->ipi->base + IPI_OBS_OFFSET)
			& IPI_PMU_PM_INT_MASK;
		/* TODO: 1) Use timer to add delay between read attempts */
		/* TODO: 2) Return PM_RET_ERR_TIMEOUT if this times out */
	}
	/* Message sent successfully to PMU */
	bakery_lock_release(&pm_secure_lock);

	return PM_RET_SUCCESS;
}

/**
 * pm_ipi_send() - Sends IPI request to the PMU
 * @proc	Pointer to the processor who is initiating request
 * @payload	API id and call arguments to be written in IPI buffer
 *
 * @return	Returns status, either success or error+reason
 */
enum pm_ret_status pm_ipi_send(const struct pm_proc *const proc,
				      uint32_t payload[PAYLOAD_ARG_CNT])
{
	uint32_t i;
	uint32_t offset = 0;
	uint32_t buffer_base = proc->ipi->buffer_base +
		IPI_BUFFER_TARGET_PMU_OFFSET +
		IPI_BUFFER_REQ_OFFSET;

	/* Wait until previous interrupt is handled by PMU */
	pm_ipi_wait(proc);

	/* Write payload into IPI buffer */
	for (i = 0; i < PAYLOAD_ARG_CNT; i++) {
		pm_write(buffer_base + offset, payload[i]);
		offset += PAYLOAD_ARG_SIZE;
	}
	/* Generate IPI to PMU */
	pm_write(proc->ipi->base + IPI_TRIG_OFFSET, IPI_PMU_PM_INT_MASK);

	return PM_RET_SUCCESS;
}

/**
 * pm_ipi_buff_read32() - Reads IPI response after PMU has handled interrupt
 * @proc	Pointer to the processor who is waiting and reading response
 * @value 	Used to return value from 2nd IPI buffer element (optional)
 *
 * @return	Returns status, either success or error+reason
 */
enum pm_ret_status pm_ipi_buff_read32(const struct pm_proc *const proc,
					     uint32_t *value)
{
	uint32_t buffer_base = proc->ipi->buffer_base +
		IPI_BUFFER_TARGET_PMU_OFFSET +
		IPI_BUFFER_RESP_OFFSET;

	pm_ipi_wait(proc);

	/*
	 * Read response from IPI buffer
	 * buf-0: success or error+reason
	 * buf-1: value
	 * buf-2: unused
	 * buf-3: unused
	 */
	if (NULL != value)
		*value = pm_read(buffer_base + PAYLOAD_ARG_SIZE);

	return pm_read(buffer_base);
}
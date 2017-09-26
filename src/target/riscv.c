/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2017  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file implements debugging functionality specific to RISC-V targets.
 * According to risv-debug-spec 0.11nov12 November 12, 2016
 */

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "target.h"

#define RISCV_IR_IDCODE     0x01
#define RISCV_IR_DTMCONTROL 0x10
#define RISCV_IR_DBUS       0x11
#define RISCV_IR_BYPASS     0x1f

#define RISCV_DTMCONTROL_DBUSRESET (1 << 16)

#define RISCV_DBUS_NOP   0
#define RISCV_DBUS_READ  1
#define RISCV_DBUS_WRITE 2

#define RISCV_DMCONTROL 0x10
#define RISCV_DMINFO    0x11

#define RISCV_DMCONTROL_INTERRUPT (1ull << 33)

struct riscv_dtm {
	jtag_dev_t *dev;
	uint8_t version; /* As read from dmtcontrol */
	uint8_t abits; /* Debug bus address bits (6 bits wide) */
	uint8_t idle; /* Number of cycles required in run-test/idle */
	bool error;
	uint64_t lastdbus;
};

static void riscv_dtm_reset(struct riscv_dtm *dtm)
{
	jtag_dev_write_ir(dtm->dev, RISCV_IR_DTMCONTROL);
	uint32_t dtmcontrol = RISCV_DTMCONTROL_DBUSRESET;
	jtag_dev_shift_dr(dtm->dev, (void*)&dtmcontrol, (void*)&dtmcontrol, 32);
	DEBUG("after dbusreset: dtmcontrol = 0x%08x\n", dtmcontrol);
}

static uint64_t riscv_dtm_low_access(struct riscv_dtm *dtm, uint64_t dbus)
{
	uint64_t ret = 0;
retry:
	jtag_dev_shift_dr(dtm->dev, (void*)&ret, (const void*)&dbus, 36 + dtm->abits);
	switch (ret & 3) {
	case 3:
		riscv_dtm_reset(dtm);
		jtag_dev_write_ir(dtm->dev, RISCV_IR_DBUS);
		DEBUG("retry out %"PRIx64"\n", dbus);
		jtag_dev_shift_dr(dtm->dev,
		                  (void*)&ret, (const void*)&dtm->lastdbus,
		                  dtm->abits + 36);
		DEBUG("in %"PRIx64"\n", ret);
		jtagtap_tms_seq(0, dtm->idle);
		goto retry;
	case 0:
		dtm->lastdbus = dbus;
		break;
	case 2:
	default:
		dtm->error = true;
		return 0;
	}
	jtagtap_tms_seq(0, dtm->idle);
	return (ret >> 2) & 0x3ffffffffull;
}

static void riscv_dtm_write(struct riscv_dtm *dtm, uint32_t addr, uint64_t data)
{
	uint64_t dbus = ((uint64_t)addr << 36) |
	                ((data & 0x3ffffffffull) << 2) | RISCV_DBUS_WRITE;
	riscv_dtm_low_access(dtm, dbus);
}

static uint64_t riscv_dtm_read(struct riscv_dtm *dtm, uint32_t addr)
{
	riscv_dtm_low_access(dtm, ((uint64_t)addr << 36) | RISCV_DBUS_READ);
	return riscv_dtm_low_access(dtm, RISCV_DBUS_NOP);
}
static uint32_t riscv_debug_ram_exec(struct riscv_dtm *dtm,
                                     const uint32_t code[], int count)
{
	int i;
	for (i = 0; i < count - 1; i++) {
		riscv_dtm_write(dtm, i, code[i]);
	}
	riscv_dtm_write(dtm, i, code[i] | RISCV_DMCONTROL_INTERRUPT);
	uint64_t ret;
	do {
		ret = riscv_dtm_read(dtm, count);
	} while (ret & RISCV_DMCONTROL_INTERRUPT);
	return ret;
}

static uint32_t riscv_mem_read32(struct riscv_dtm *dtm, uint32_t addr)
{
	/* Debug RAM stub
	 * 400:   41002403   lw   s0, 0x410(zero)
	 * 404:   00042483   lw   s1, 0(s0)
	 * 408:   40902a23   sw   s1, 0x414(zero)
	 * 40c:   3f80006f   j    0 <resume>
	 * 410:              dw   addr
	 * 414:              dw   data
	 */
	uint32_t ram[] = {0x41002403, 0x42483, 0x40902a23, 0x3f80006f, addr};
	return riscv_debug_ram_exec(dtm, ram, 5);
}

void riscv_jtag_handler(jtag_dev_t *dev)
{
	uint32_t dtmcontrol = 0;
	DEBUG("Scanning RISC-V target! %p\n", dev);
	jtag_dev_write_ir(dev, RISCV_IR_DTMCONTROL);
	jtag_dev_shift_dr(dev, (void*)&dtmcontrol, (void*)&dtmcontrol, 32);
	DEBUG("dtmcontrol = 0x%08x\n", dtmcontrol);
	uint8_t version = dtmcontrol & 0xf;

	if (version > 0)
		return; /* We'll come back to this someday */

	struct riscv_dtm *dtm = alloca(sizeof(*dtm));
	memset(dtm, 0, sizeof(*dtm));
	dtm->dev = dev;
	dtm->abits = ((dtmcontrol >> 13) & 3) << 4 |
	              ((dtmcontrol >> 4) & 0xf);
	dtm->idle = (dtmcontrol >> 10) & 7;
	DEBUG("abits = %d\n", dtm->abits);
	DEBUG("idle = %d\n", dtm->idle);
	DEBUG("dbusstat = %d\n", (dtmcontrol >> 8) & 3);
	riscv_dtm_reset(dtm);

	jtag_dev_write_ir(dev, RISCV_IR_DBUS);

	uint64_t dmcontrol = riscv_dtm_read(dtm, RISCV_DMCONTROL);
	uint32_t dminfo = riscv_dtm_read(dtm, RISCV_DMINFO);
	DEBUG("dmcontrol = %"PRIx64"\n", dmcontrol);
	DEBUG("dminfo = %"PRIx32"\n", dminfo);
	uint8_t dmversion = ((dminfo >> 4) & 0xc) | (dminfo & 3);
	DEBUG("\tloversion = %d\n", dmversion);
	if (dmversion != 1)
		return;

	uint8_t authtype = (dminfo >> 2) & 3;
	uint8_t authbusy = (dminfo >> 4) & 1;
	uint8_t authenticated = (dminfo >> 5) & 1;
	DEBUG("\tauthtype = %d, authbusy = %d, authenticated = %d\n",
	      authtype, authbusy, authenticated);

	if (authenticated != 1)
		return;

	uint8_t dramsize = (dminfo >> 10) & 0x3f;
	DEBUG("\tdramsize = %d (%d bytes)\n", dramsize, (dramsize + 1) * 4);

	riscv_dtm_write(dtm, 0, 0xbeefcafe);
	riscv_dtm_write(dtm, 1, 0xdeadbeef);
	DEBUG("%"PRIx32"\n", (uint32_t)riscv_dtm_read(dtm, 0));
	DEBUG("%"PRIx32"\n", (uint32_t)riscv_dtm_read(dtm, 1));

	for (int i = 0; i < dramsize + 1; i++) {
		DEBUG("DebugRAM[%d] = %08"PRIx32"\n", i, riscv_mem_read32(dtm, 0x400 + i*4));
	}
}

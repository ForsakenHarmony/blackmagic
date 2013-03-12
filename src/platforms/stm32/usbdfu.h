/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include <libopencm3/usb/usbd.h>

#ifdef STM32F4
#	define APP_ADDRESS	0x08010000
#else
#	define APP_ADDRESS	0x08002000
#endif

/* dfucore.c - DFU core, common to libopencm3 platforms. */
void dfu_init(const usbd_driver *driver);
void dfu_main(void);

/* Device specific functions */
void dfu_check_and_do_sector_erase(uint32_t sector);
void dfu_flash_program_buffer(uint32_t baseaddr, void *buf, int len);
uint32_t dfu_poll_timeout(uint8_t cmd, uint32_t addr, uint16_t blocknum);
void dfu_protect_enable(void);
void dfu_jump_app_if_valid(void);

/* Platform specific function */
void dfu_detach(void);


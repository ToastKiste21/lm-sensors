/*
    i2c-via.c - Part of lm_sensors,  Linux kernel modules
                for hardware monitoring

    i2c Support for Via Technologies 82C586B South Bridge

    Copyright (c) 1998 Ky�sti M�lkki <kmalkki@cc.hut.fi>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/types.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < 0x020136 /* 2.1.54 */
#include <linux/bios32.h>
#endif

#include "i2c.h"
#include "algo-bit.h"

/* PCI device */
#define VENDOR		PCI_VENDOR_ID_VIA
#define DEVICE		PCI_DEVICE_ID_VIA_82C586_3

/* Power management registers */

#define PM_CFG_REVID    0x08  /* silicon revision code */
#define PM_CFG_IOBASE0  0x20
#define PM_CFG_IOBASE1  0x48

#define I2C_DIR		(pm_io_base+0x40)
#define I2C_OUT		(pm_io_base+0x42)
#define I2C_IN		(pm_io_base+0x44)
#define I2C_SCL		0x02   /* clock bit in DIR/OUT/IN register */
#define I2C_SDA		0x04

/* io-region reservation */
#define IOSPACE		0x06
#define IOTEXT		"VIA i2c"

/* ----- global defines -----------------------------------------------	*/
#define DEB(x) x	/* silicon revision, io addresses	 	*/
#define DEB2(x) x	/* line status					*/
#define DEBE(x) 	/* 						*/

/* ----- local functions ----------------------------------------------	*/

u16 pm_io_base;

static void bit_via_setscl(void *data, int state)
{
  outb(state ? inb(I2C_OUT)|I2C_SCL : inb(I2C_OUT)&~I2C_SCL, I2C_OUT);
}

static void bit_via_setsda(void *data, int state)
{
  outb(state ? inb(I2C_OUT)|I2C_SDA : inb(I2C_OUT)&~I2C_SDA, I2C_OUT);
}

static int bit_via_getscl(void *data)
{
  return (0 != (inb(I2C_IN) & I2C_SCL) );
}

static int bit_via_getsda(void *data)
{
  return (0 != (inb(I2C_IN) & I2C_SDA) );
}

static int bit_via_reg(struct i2c_client *client)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int bit_via_unreg(struct i2c_client *client)
{
	MOD_DEC_USE_COUNT;
	return 0;
}


/* ------------------------------------------------------------------------ */

struct bit_adapter bit_via_ops = {
	"VIA i2c",
	HW_B_VIA,
	NULL,
	bit_via_setsda,
	bit_via_setscl,
	bit_via_getsda,
	bit_via_getscl,
	bit_via_reg,
	bit_via_unreg,
	5, 5, 100,	/*waits, timeout */
};


/* When exactly was the new pci interface introduced? */
#if LINUX_VERSION_CODE >= 0x020136 /* 2.1.54 */
static int find_via(void)
{
	struct pci_dev *s_bridge;
	u16 base;
	u8 rev;
	
	if (! pci_present())
		return -ENODEV;
		
	s_bridge = pci_find_device(VENDOR, DEVICE, NULL);
		
	if (! s_bridge)	{
		printk("vt82c586b not found\n");
		return -ENODEV;
	}

	if ( PCIBIOS_SUCCESSFUL != 
		pci_read_config_byte(s_bridge, PM_CFG_REVID, &rev))
		return -ENODEV;

	switch(rev)
	{
		case 0x00:	base = PM_CFG_IOBASE0;
				break;
		case 0x01:
		case 0x10:	base = PM_CFG_IOBASE1;
				break;
				
		default	:	base = PM_CFG_IOBASE1;
				/* later revision */	
	}	

	if ( PCIBIOS_SUCCESSFUL !=
		pci_read_config_word(s_bridge, base, &pm_io_base))
		return -ENODEV;
		
	pm_io_base &= (0xff<<8);
	return 0;
}

#else

static int find_via(void)
{
	unsigned char VIA_bus, VIA_devfn;
	u16 base;
	u8 rev;
	
	if (! pcibios_present())
		return -ENODEV;
		
	if(pcibios_find_device(VENDOR, DEVICE, 0, &VIA_bus, &VIA_devfn))
	{
		printk("vt82c586b not found\n");
		return -ENODEV;
	}
	
	if ( PCIBIOS_SUCCESSFUL != 
		pcibios_read_config_byte(VIA_bus, VIA_devfn,
					PM_CFG_REVID, &rev))
		return -ENODEV;

	switch(rev)
	{
		case 0x00:	base = PM_CFG_IOBASE0;
				break;
		case 0x01:
		case 0x10:	base = PM_CFG_IOBASE1;
				break;
				
		default	:	base = PM_CFG_IOBASE1;
				/* later revision */	
	}	

	if ( PCIBIOS_SUCCESSFUL !=
                pcibios_read_config_word(VIA_bus, VIA_devfn, base, &pm_io_base))
		return -ENODEV;

	pm_io_base &= (0xff<<8);
	return 0;
}
#endif

int init_i2c_via()
{
	if (find_via() < 0) {
		printk("Error while reading PCI configuration\n");
		return -ENODEV;
	}

	if ( check_region(I2C_DIR, IOSPACE) < 0) {
		printk("IO 0x%x-0x%x already in use\n",
			I2C_DIR, I2C_DIR+IOSPACE);
		return -EBUSY;
	} else {
		request_region(I2C_DIR, IOSPACE, IOTEXT);
		outb(inb(I2C_DIR) | I2C_SDA | I2C_SCL, I2C_DIR);
		outb(inb(I2C_OUT) | I2C_SDA | I2C_SCL, I2C_OUT);		
	}
			
	if (i2c_bit_add_bus(&bit_via_ops) == 0) {
		printk("Via i2c: Module succesfully loaded\n");
		return 0;
	} else {
		outb(inb(I2C_DIR)&~(I2C_SDA|I2C_SCL), I2C_DIR);	
		release_region(I2C_DIR, IOSPACE);
		printk("Via i2c: Algo-bit error, couldn't register bus\n");
		return -ENODEV;
	}
}

#ifdef MODULE
MODULE_AUTHOR("Ky�sti M�lkki <kmalkki@cc.hut.fi>");
MODULE_DESCRIPTION("i2c for Via vt82c586b southbridge");

int init_module(void) 
{
	return init_i2c_via();
}

void cleanup_module(void) 
{
	i2c_bit_del_bus(&bit_via_ops);
	release_region(I2C_DIR, IOSPACE);
}
#endif

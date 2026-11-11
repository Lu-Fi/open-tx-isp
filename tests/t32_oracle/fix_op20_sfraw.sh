#!/bin/sh
# Build-time fix of the reference copy (ref/ itself stays untouched): lib_tisp_save_specified_frame_raw
# calls printk (strong in env.c), isp_printf, system_reg_write and tisp_vic_mdma_enable; the oracle records them (with the
# printk stack arguments, which the mechanical C passes as arguments 5..) under its own names.
sed -e 's/__asm__("printk")/__asm__("op20_sfr_printk")/' -e 's/__asm__("isp_printf")/__asm__("op20_sfr_isp_printf")/' \
	-e 's/__asm__("tisp_vic_mdma_enable")/__asm__("op20_sfr_mdma")/' -e 's/__asm__("system_reg_write")/__asm__("op20_sfr_wr")/'

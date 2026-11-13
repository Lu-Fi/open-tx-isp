/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_0000000000000c14_private_remove_proc_entry.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0xc14: private_remove_proc_entry; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
/*
 * Tail-call thunk wrapper for remove_proc_entry().
 * Original binary: lui/addiu t9 to remove_proc_entry, jr t9, nop.
 */
void private_remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
	remove_proc_entry(name, parent);
}

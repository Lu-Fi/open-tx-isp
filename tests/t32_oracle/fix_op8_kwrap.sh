#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for Matt Davis' reviewed
# kernel-wrapper C (init_module, cleanup_module, __private_spin_lock_irqsave): host
# definitions of the kernel names they use; spin_lock_irqsave goes to the oracle's recorder
# t32o8_irq_save (it returns the saved flags, as local_irq_save does).
perl -0pe 's/^(#include "..\/ref_compat.h"\n)/$1#define __init\n#define __exit\ntypedef unsigned int spinlock_t;\nunsigned long t32o8_irq_save(void *lock);\n#define spin_lock_irqsave(l, f) ((f) = t32o8_irq_save(l))\n/m'

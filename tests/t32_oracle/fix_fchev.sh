#!/bin/sh
# REFFIX of t32_fchev_oracle: env.c defines private_complete for other oracles and the
# host libc owns sprintf; the reference copy calls the oracle's recording versions
# t32o_fe_complete / t32o_fe_sprintf instead (symbol name of the call only).
sed 's/__asm__("private_complete")/__asm__("t32o_fe_complete")/; s/__asm__("sprintf")/__asm__("t32o_fe_sprintf")/'

#!/bin/sh
# REFFIX of t32_txgctrl_oracle: op3_ren.sh, and the thread_info the vendor's
# inlined access_ok reads (+24 address limit) becomes the oracle object
# op3_fake_ti instead of the host compat NULL.  Function body unchanged.
sh "$(dirname "$0")/op3_ren.sh" | sed 's/current_thread_info()/((void *)op3_fake_ti)/; s|^#include "../ref_compat.h"$|#include "../ref_compat.h"\nextern unsigned char op3_fake_ti[];|'

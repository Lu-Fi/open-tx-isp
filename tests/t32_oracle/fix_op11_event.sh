#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for t32_event_oracle:
# tisp_event_push is also an env.c stub (other oracles record events with it), so the reference
# copy is renamed ref_tisp_event_push.
sed -e 's/^u64 tisp_event_push(/u64 ref_tisp_event_push(/'

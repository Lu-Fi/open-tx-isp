/*
 * T21 /dev/tx-isp first-open activate walk with rollback.
 *
 * Stock (oem-t21.ko tx_isp_open @0x9528) calls activate_module of each of
 * the 16 top-level modules in order; -ENOIOCTLCMD (-515) means "no
 * activate", any other error stops the walk.  On such an error stock
 * returns it and leaves the modules activated before it active (clocks on,
 * core state 2, ...), although the open fails and no release will follow.
 *
 * Beyond vendor: on that error, the modules this walk activated are slaked
 * again, in reverse order, so a failed open leaves the device as it was
 * before.  The failing module itself is not slaked (its activate did not
 * succeed).  The success path is unchanged.
 *
 * Kernel- and host-includable (tests/tx_isp_t21_open_test.c).
 */
#ifndef TX_ISP_T21_OPEN_H
#define TX_ISP_T21_OPEN_H

#define T21_OPEN_NOIOCTLCMD (-515)
#define T21_OPEN_ACTIVATE 0u
#define T21_OPEN_SLAKE 1u

typedef int32_t (*t21_open_module_fn)(void *module);

/* lookup(module, T21_OPEN_ACTIVATE / T21_OPEN_SLAKE) -> fn or NULL. */
static inline int32_t
t21_open_activate_modules(void *const *modules, unsigned int count,
			  t21_open_module_fn (*lookup)(void *module,
						       unsigned int which))
{
	uint32_t activated = 0;
	int32_t result = 0;
	unsigned int i;

	if (count > 32)
		count = 32;

	for (i = 0; i < count; i++) {
		void *module = modules[i];
		t21_open_module_fn activate;

		if (!module)
			continue;
		activate = lookup(module, T21_OPEN_ACTIVATE);
		if (!activate) {
			result = T21_OPEN_NOIOCTLCMD;
			continue;
		}
		result = activate(module);
		if (result == 0) {
			activated |= 1u << i;
			continue;
		}
		if (result != T21_OPEN_NOIOCTLCMD)
			break;
		result = T21_OPEN_NOIOCTLCMD;
	}

	if (i == count && result == T21_OPEN_NOIOCTLCMD)
		result = 0;
	if (result == 0)
		return 0;

	/* Roll back: slake what this walk activated, newest first. */
	while (i-- > 0) {
		t21_open_module_fn slake;

		if (!(activated & (1u << i)))
			continue;
		slake = lookup(modules[i], T21_OPEN_SLAKE);
		if (slake)
			slake(modules[i]);
	}
	return result;
}

#endif /* TX_ISP_T21_OPEN_H */

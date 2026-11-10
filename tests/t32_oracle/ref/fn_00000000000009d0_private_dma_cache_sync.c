/* Reference copy for the host oracle, NOT driver code.
 * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,
 * all-C recovery of the Ingenic T32 tx-isp module).
 * Source: driver/t32/recovered/fn_00000000000009d0_private_dma_cache_sync.c@2e2e06bc, unchanged except the include line below. */
/* T32 OEM 0x9d0: private_dma_cache_sync; recovery status: normalized_exact_match. */
#include "../ref_compat.h"
void private_dma_cache_sync(struct device *dev, void *t32_data_e3f30, size_t size, enum dma_data_direction direction);
void private_dma_cache_sync(struct device *dev, void *t32_data_e3f30, size_t size, enum dma_data_direction direction)
{
    dma_cache_sync(dev, t32_data_e3f30, size, direction);
}

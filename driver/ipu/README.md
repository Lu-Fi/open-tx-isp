# driver/ipu: T31 IPU (OSD blend)

`t31_ipu.ko` provides `/dev/ipu` for OpenIMP's T31 OSD backend
(`src/t31/openimp_t31_services.c`), which blends the overlays into the NV12
frame with one ioctl, `IPU_START` (`_IO('I', 106)`, `struct ipu_param` in
`jz_ipu_v13.h`).

- Source: port of the vendor 3.10 `drivers/video/jz_ipu/jz_ipu_v13.c` (GPL)
  including the thingino wedge mitigation (100 ms timeout, stop/reset/retry,
  clock refcount fix). The vendor T31 kernel builds v13, not v12.
- Kernels: for mainline-based kernels (thingino kernel 7). It binds to the DT
  node `compatible = "ingenic,t31-ipu"` (reg `0x13080000`/`0x10000` as in the vendor map, INTC 6,
  clocks `ipu` + `ahb1`). The vendor 3.10 kernel keeps its built-in IPU
  driver; there the `jz-ipu` platform device only exists together with it.
- Not used by OpenIMP and kept as in the vendor driver: PBUFF (114/115),
  BUF_LOCK/UNLOCK (116/117). `BUF_FLUSH_CACHE` (118) is a no-op where
  `dma_cache_sync()` no longer exists.
- Build: `make -C <kernel> M=$(pwd)/driver/ipu modules`.

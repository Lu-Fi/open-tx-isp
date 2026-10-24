# Testing the kernel modules in QEMU

The `ingenic-txx` branch of [gtxaspec/qemu](https://github.com/gtxaspec/qemu) models the Ingenic T-series SoCs (machines `ingenic-t10`, `-t20`, `-t21`, `-t23`, `-t31`, `-t40`, `-t41`). A camera flash image boots unchanged, so the modules of this repository can be loaded, unloaded and exercised without a camera. It is a crash-test bench, not a picture-quality bench.

## Setup

- Build only the MIPS target: `configure --target-list=mipsel-softmmu --disable-docs --enable-slirp`, then `ninja qemu-system-mipsel` (about 9 minutes with `nice`).
- Boot a **copy** of the flash image (QEMU writes to the flash file). The flash starts through its SPL header, so no `-kernel` is needed: `-M ingenic-<soc> -m <RAM> -drive file=<copy>,format=raw,if=none,id=flash0`, the console is the second UART (`-serial null -serial <chardev>`), and the sensor is a device on the I2C bus the board uses: `-device image-sensor,model=<sensor>,bus=i2c<N>`.
- To get a shell without the root password, set `debug=1` in the copy's U-Boot environment (the thingino inittab then runs `/bin/sh`).
- A reload test is a loop of: stop the streamer, `rmmod` the sensor and ISP modules, `insmod` them again, start the streamer, look for oopses and for lines such as `irq N: nobody cared`.

## What works per SoC (agg43 images, 2026-10-24)

| SoC | Machine, RAM, sensor model | Boots to shell | ISP and sensor probe | Streamer starts | Reload cycles |
|---|---|---|---|---|---|
| T10 | `ingenic-t10`, jxh42 on i2c0 | yes | yes | yes | 10 clean |
| T20 | `ingenic-t20`, 128 MB, `soc-variant t20x`, jxf23 on i2c0 | yes | yes | yes | 10 clean |
| T21 | `ingenic-t21`, jxf23 on i2c0 | yes | yes | yes | first cycle after streaming locked up in `modprobe` (pending ISP IRQ); fixed by the IRQ quiesce, not re-run |
| T23 | `ingenic-t23`, sc2336 on i2c0 | yes | yes | yes | no oops; `irq 38: nobody cared` each cycle before the IRQ quiesce fix |
| T31 | `ingenic-t31`, 128 MB, sc2336 on i2c0 | yes | yes (1080p + 360p) | yes | no oops; same IRQ message before the fix |
| T40 | `ingenic-t40`, 256 MB (512 MB does not boot), sc830ai on i2c1 | yes | yes | crashed in the ISR before the slot-index fix | first `rmmod` oopsed (fixed, see the T40 reload entry in the changelog) |
| T41 | `ingenic-t41`, 64 MB, gc5603 on i2c0 | yes, `eth0` present | yes | yes | 10 clean |

## Limits of the model

- The ISP is a register file (registers read back as written) plus a modelled frame path: VIC start/reset, the multi-scaler with its buffer queue, done FIFO and frame numbers, frame start/done interrupts, the idle handshake, OSD draw and sensor frames (colour bars) at the sensor frame rate. There is no 3A statistics or tuning math, so AE/AWB registers are inert and the pictures are synthetic.
- The AVPU encoder (H.264, H.265, JPEG) runs on the host CPU and works with the unmodified libimp.
- MIPI receiver and PHY, USB PHY and the extended UART are stubs. T40 has no network in the model; Wi-Fi needs an extra device model.
- Timing, a real sensor, image quality, thermal behaviour and Wi-Fi cannot be tested.

## What it found

- T40: `rmmod tx_isp_t40` oopsed on the first cycle (NULL spinlock in the module exit) and the streamer start crashed the kernel in the ISR (frame slot index); both reproduced on the camera and fixed.
- T31, T23, T21: the ISP interrupt status was still pending when the handler was registered again on reload (`irq 38: nobody cared`, interrupts dead until reboot; T21 livelocked). The status is now masked and acknowledged before `request_irq` and on remove.

Crash tests (oops and panic capture, `panic=2` reboot loops, load/unload soaks) are cheap and scriptable in this setup; use it to catch regressions before a camera is flashed.

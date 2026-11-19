/*
 * Lu-Fi/openimp#695 regression (QEMU ingenic-t30, sc4236 model):
 * module loaded with the core crop parameters of the ABUS stock kernel
 * (insmod tx-isp-t30.ko ispw=1920 isph=1080 ispleft=12 isptop=20), then
 * N x (ISP init, FrameSource ch0 1920x1080 NV12 without scaler, EnableChn,
 * teardown).
 *
 * EnableChn sends the cropped core format; the core applies the crop through
 * apical_command(image_crop_xoffset / image_crop_yoffset).  A driver built
 * before open-tx-isp 9fe73901d (#463) stored the y offset at uint32_t
 * pointer + 0x14b8 (byte +0x52e0) instead of byte +0x14b8 (vendor sh
 * a1,5304(a0)) and oopsed with a store TLB miss behind the module end.
 * Pass: "cycles done", no Oops in dmesg, dmesg "core set fmt ch=0 1920x1080
 * ... crop=1".
 *
 * Second argument "check" (#695 stride): while ch0 streams, read the core FR
 * crop block (ISP core 0x13380000 + 0x4c0..0x4d0, written by _update_fr from
 * the crop FSM fields the image_resize_width / height / crop_x/yoffset
 * setters store) and require enable 1, x 12, y 20, w 1920, h 1080.  Before
 * the fix the width/height setters stored at void * index (byte +0x2964 ..),
 * the FSM kept 0xffff and _update_fr clamped to 1936x1089 (sensor 1948x1109
 * minus the offsets): the FR writer then produced 1936-pixel lines (line
 * pitch 1952 in the 1920-wide buffer on the ABUS T30N).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 3;
    int check = argc > 2 && !strcmp(argv[2], "check");
    volatile uint32_t *core = NULL;
    int bad = 0;
    if (check) {
        int fd = open("/dev/mem", O_RDONLY | O_SYNC);
        void *m = fd < 0 ? MAP_FAILED : mmap(NULL, 0x1000, PROT_READ, MAP_SHARED, fd, 0x13380000);
        if (fd >= 0)
            close(fd);
        if (m == MAP_FAILED) {
            printf("cannot map /dev/mem at 0x13380000\n");
            return 2;
        }
        core = m;
    }
    IMPSensorInfo si; memset(&si, 0, sizeof si);
    strcpy(si.name, "sc4236"); si.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strcpy(si.i2c.type, "sc4236"); si.i2c.addr = 0x30; si.i2c.i2c_adapter = 0;
    si.rst_gpio = -1; si.pwdn_gpio = -1; si.power_gpio = -1;
    IMPFSChnAttr fs0;
    memset(&fs0, 0, sizeof fs0);
    fs0.picWidth = 1920; fs0.picHeight = 1080; fs0.pixFmt = PIX_FMT_NV12;
    fs0.outFrmRateNum = 15; fs0.outFrmRateDen = 1; fs0.nrVBs = 2; fs0.type = FS_PHY_CHANNEL;
    for (int i = 0; i < n; i++) {
        int a = IMP_System_Init(), b = IMP_ISP_Open(), c = IMP_ISP_AddSensor(&si), d = IMP_ISP_EnableSensor();
        int e = IMP_FrameSource_CreateChn(0, &fs0);
        int g = IMP_FrameSource_EnableChn(0);
        printf("cycle %d init=%d open=%d add=%d en=%d c0=%d e0=%d\n", i, a, b, c, d, e, g); fflush(stdout);
        usleep(500000);
        if (core) {
            uint32_t en = core[0x4c0 / 4] & 1, x = core[0x4c4 / 4] & 0xffff, y = core[0x4c8 / 4] & 0xffff;
            uint32_t w = core[0x4cc / 4] & 0xffff, h = core[0x4d0 / 4] & 0xffff;
            printf("cycle %d fr crop en=%u x=%u y=%u w=%u h=%u\n", i, en, x, y, w, h);
            if (en != 1 || x != 12 || y != 20 || w != 1920 || h != 1080)
                bad++;
        }
        IMP_FrameSource_DisableChn(0); IMP_FrameSource_DestroyChn(0);
        IMP_ISP_DisableSensor(); IMP_ISP_DelSensor(&si); IMP_ISP_Close(); IMP_System_Exit();
        if (a || b || c || d || e || g)
            return 1;
    }
    if (bad) {
        printf("FR crop wrong in %d cycles\n", bad);
        return 1;
    }
    printf("cycles done\n");
    return 0;
}

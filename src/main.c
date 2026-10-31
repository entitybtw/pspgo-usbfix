/* ARK plugin for the PSP Go: outside the XMB usb.prx keeps the port off even
 * after sceUsbActivate, so the PC never sees the console (state 0x211). The
 * plugin makes the call that brings it up once a driver has activated USB, and
 * before that it applies the USB device picked in the CFW menu, like the XMB
 * does on ARK and PRO. Flash is always read only: writing it from a PC can brick
 * the console. It writes nothing itself, ef0: may belong to the PC. */
#include <pspkernel.h>
#include <pspinit.h>
#include <pspusbdevice.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <systemctrl_ark.h>

PSP_MODULE_INFO("usbfix", PSP_MODULE_KERNEL, 1, 0);
PSP_NO_CREATE_MAIN_THREAD();

#define NID_USB_GET_STATE 0xC21645A4
#define NID_USB_PORT_ENABLE 0x46EA407B
#define NID_USB_PORT_DISABLE 0x1582F0A2
#define NID_USB_DRV_STATE 0x112CC951
#define NID_GET_CONFIG 0x16C3B7EE
#define NID_DEV_SET 0xD4D90520
#define NID_DEV_FINISH 0xB3CF0329
#define NID_LOAD_MODULE 0x977DE386
#define NID_START_MODULE 0x50F0C1EC
#define USB_ACTIVATED 0x200
#define USB_CABLE_OFF 0x010
#define USB_LINK_UP 0x002
#define POLL_US 250000
#define STUCK_POLLS 4

static void *usb_fn(u32 nid)
{
    return (void *)sctrlHENFindFunction("sceUSB_Driver", "sceUsb_driver", nid);
}

/* 0 is the normal storage, otherwise ARK device number + 1, see pspusbdevice.h */
static int picked_device(void)
{
    static u32 cfg[64];
    int (*get)(void *) = (void *)sctrlHENFindFunction("SystemControl", "SystemCtrlForKernel", NID_GET_CONFIG);
    if (!get || get(cfg) < 0)
        return 0;
    int dev = 0;
    if (cfg[0] == ARK_CONFIG_MAGIC)
        dev = ((SEConfigARK *)cfg)->usbdevice;
    else if (cfg[0] == SECONFIG_MAGIC_PRO)
        dev = ((SEConfigPRO *)cfg)->usbdevice;
    return dev >= 0 && dev <= 5 ? dev : 0;
}

static void *dev_fn(u32 nid)
{
    return (void *)sctrlHENFindFunction("pspUsbDev_Driver", "pspUsbDevice_driver", nid);
}

static void apply_device(void)
{
    int (*drv_state)(const char *) = usb_fn(NID_USB_DRV_STATE);
    if (!drv_state || drv_state("USBStor_Driver") != 1)
        return;
    int dev = picked_device();
    if (dev == 0) {
        /* An application may have mapped flash, the normal storage undoes it. */
        int (*finish)(void) = dev_fn(NID_DEV_FINISH);
        if (finish)
            finish();
        return;
    }
    if (!dev_fn(NID_DEV_SET)) {
        /* The user library NIDs stay the same on every firmware. */
        SceUID (*load)(const char *, int, void *) = (void *)sctrlHENFindFunction("sceModuleManager", "ModuleMgrForUser", NID_LOAD_MODULE);
        int (*start)(SceUID, SceSize, void *, int *, void *) = (void *)sctrlHENFindFunction("sceModuleManager", "ModuleMgrForUser", NID_START_MODULE);
        SceUID mod = load && start ? load(USBDEV_PRX_FLASH, 0, NULL) : -1;
        if (mod >= 0)
            start(mod, 0, NULL, NULL, NULL);
    }
    int (*set)(u32, int, int) = dev_fn(NID_DEV_SET);
    if (set)
        set(dev - 1, 1, 0);
}

static int watch(SceSize args, void *argp)
{
    int (*get_state)(void) = NULL, (*enable)(void) = NULL, (*disable)(void) = NULL;
    int stuck = 0, done = 0;

    for (;;) {
        sceKernelDelayThread(POLL_US);
        if (!get_state) {
            get_state = usb_fn(NID_USB_GET_STATE);
            enable = usb_fn(NID_USB_PORT_ENABLE);
            disable = usb_fn(NID_USB_PORT_DISABLE);
            if (!get_state || !enable || !disable) {
                get_state = NULL;
                continue;
            }
        }
        int state = get_state();
        if (!(state & USB_ACTIVATED)) {
            stuck = done = 0;
            continue;
        }
        if (done || (state & USB_LINK_UP) || !(state & USB_CABLE_OFF)) {
            stuck = 0;
            continue;
        }
        if (++stuck < STUCK_POLLS)
            continue;
        apply_device();
        /* A failed enable means the flag is already set, as after a restart. */
        if (enable() < 0) {
            disable();
            enable();
        }
        done = 1;
    }
    return 0;
}

int module_start(SceSize args, void *argp)
{
    if (sceKernelInitKeyConfig() == PSP_INIT_KEYCONFIG_VSH)
        return 1;
    SceUID th = sceKernelCreateThread("usbfix", watch, 48, 0x2000, 0, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
    return 0;
}

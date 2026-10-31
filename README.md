# psp-usbfix

A small ARK plugin that makes USB work outside the XMB on my PSP Go.

This fixes a problem I had on my own console. I don't know how common it is, and
I have only tested this on one PSP. I'm publishing it in case it helps someone
with the same symptoms.

## The problem

USB worked fine in the XMB, but never in games or homebrew:

- PSPLink started, but the PC never saw the console (`usbhostfs_pc` stayed on
  `waiting for device...`).
- USB mode inside homebrew launchers did nothing.
- The PC kernel log showed nothing at all: no new device, no enumeration errors.
- `sceUsbGetState()` stayed at `0x211`: activated, but no cable and no connection.

## My setup

| | |
|---|---|
| Console | PSP Go (model index 4, baryon `0x00304000`) |
| Firmware | 6.61 |
| CFW | ARK-5 |
| ARK setting | `always, usbcharge, off` |
| PC | Linux |

## What I found

Comparing hardware registers between the XMB and a game shows the port is simply
never turned on in game mode, even after `sceUsbActivate`:

| Register | XMB, USB working | Game, after `sceUsbActivate` |
|---|---|---|
| `0xBC1000B8` port gate | `0` | stays `1` |
| `0xBC100074` USB control | `0x101` | stays `0x001` |
| GPIO port 1, bit 2 | on | off |

`usb.prx` has a kernel export in `sceUsb_driver`, NID `0x46EA407B`, with no
arguments, that reaches the port code. The name is unknown to me, and `0x1582F0A2`
looks like its counterpart. **The order matters:** called before the USB bus driver
has started, it returns 0 and changes nothing. Called after, the port comes up:

- the PC sees `054c:01c9`
- `sceUsbGetState()` reports `0x222`
- `usbhostfs_pc` prints `Connected to device`

## Is the firmware or ARK broken?

As far as I can tell, no:

- `usb.prx` and `syscon.prx` as loaded in the console's memory match the official
  6.61 updater. The only differences are relocations and import stubs.
- `lowio.prx` also matches, except for 16 words I did not fully explain.
- In my `flash0` dump, 101 files match byte for byte. The other 239 are encrypted
  per console, so those only match by size.
- I read the current ARK-5 source on GitHub (not the exact build on my console).
  It never calls these exports and only touches `usb.prx` for USB charging.

## Probably related: USB charging

ARK-5 enables `usbcharge` by default, and I had it turned off. This may be why most
people never see this problem, but **it is an untested guess**. I have not run the
test with charging on and the plugin off.

If you have this problem, try turning USB Charge on first. I can't leave it on
because my battery has problems right now, so this plugin is my solution: it works
with charging off.

## Install

1. Copy `release/usbfix.prx` to `SEPLUGINS` on your storage (`ef0:` on a PSP Go,
   `ms0:` otherwise).
2. Add this line to `SEPLUGINS/PLUGINS.TXT`, with the path for your device:

   ```
   always, ef0:/seplugins/usbfix.prx, on
   ```

3. Reboot the console.

## What the plugin does

It does nothing in the XMB. Everywhere else, a low priority thread checks
`sceUsbGetState()` four times a second. If USB has been activated but the state
shows no cable and no connection for about a second:

1. If USB storage is running, it applies the USB device selected in the CFW.
2. It calls `0x46EA407B`. If that fails, it calls `0x1582F0A2` and tries again.

| CFW USB device | Result |
|---|---|
| Normal storage | undoes any mapping left by an application (`pspUsbDeviceFinishDevice`) |
| `flash0`-`flash3`, UMD | maps that device, always read only |

The USB device setting is read from ARK and PRO config layouts. **Only ARK-5 on a
PSP Go has been tried, and PRO support is untested.** On consoles where the port
comes up by itself, the plugin should never act.

Flash is forced to read only, even if the CFW setting allows writes. Writing to
flash from a PC can brick the console.

## Warnings

- This uses an undocumented firmware export. If the NIDs don't exist on your
  firmware, the plugin does nothing.
- Testing so far is limited. The call itself worked on my console from a test
  program. The USB device selection has not been confirmed on hardware.
- **Do not call `sceSysconCtrlUsbPower`** (`0x3987FEA3`, `0x47378317` on 6.61) to
  try to fix this. On my console, calling it with `1` powered the console off.
- Stopping `scePadSvc` after `sceBSMan`, or unloading `scePadSvc`, rebooted or
  powered off the console. Stopping the Bluetooth and USB host modules did not
  fix USB either, so there is no reason to try it.
- Never let code on the PSP write to `ef0:` while a PC has that volume mounted. A
  logging plugin of mine did this and corrupted the FAT.

## Build

Needs the [pspdev](https://github.com/pspdev/pspdev) toolchain.

```
cd src
make
```

## Release

`release/usbfix.prx`, SHA-256:

```
028cc7cfddafbaaba554eb122e0496bee3fc3e9229c6011e0e0637ee9f9f0747
```

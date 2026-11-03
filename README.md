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
arguments, that reaches the port code. `0x1582F0A2` is its counterpart. Neither
has an official name (psplibdoc lists them as `sceUsb_driver_46EA407B` and
`sceUsb_driver_1582F0A2`). **The order matters:** called before the USB bus driver
has started, it returns 0 and changes nothing. Called after, the port comes up:

- the PC sees `054c:01c9`
- `sceUsbGetState()` reports `0x222`
- `usbhostfs_pc` prints `Connected to device`

### What the 6.61 code does

From a disassembly of `usb.prx` in the official 6.61 PSP Go updater:

- `0x46EA407B` sets a "port enabled" flag. If the bus driver has been started, it
  then registers the USB interrupts and brings the port up. If the flag is
  already set it returns `0x80243001`.
- `0x1582F0A2` is the reverse. It returns `0x80243001` if the flag is not set.
- In the Go firmware, the only module that imports either one is
  `sceImpose_Driver` (`impose_05g`). So in a normal system `impose` turns the port
  on. I don't know why it does not do so in games on my console.

## Is the firmware or ARK broken?

As far as I can tell, no:

- `usb.prx` and `syscon.prx` as loaded in the console's memory match the official
  6.61 updater. The only differences are relocations and import stubs.
- `lowio.prx` also matches, except for 16 words I did not fully explain.
- In my `flash0` dump, 101 files match byte for byte. The other 239 are encrypted
  per console, so those only match by size.
- I read the current ARK-5 source on GitHub (not the exact build on my console).
  It never calls these exports and only touches `usb.prx` for USB charging.

## USB charging is not the cause

I first guessed that ARK's `usbcharge` option was related, because I had it off.
The ARK-5 source only hooks two `scePower_driver` imports of `usb.prx`
(`0x72D1B53A`, `0x7EAA4247`) when it is on, and in 6.61 `usb.prx` never calls
either of them. So it does not change how the port comes up. The PSP Go also
charges over the same port whether the option is on or off.

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

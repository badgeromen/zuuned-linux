# Zune Firmware Flash — USB Capture Guide

## Goal

Capture the USB traffic during a Zune firmware flash so we can reverse-engineer the protocol and build `zune_flash_firmware()` into libzune. This eliminates the need for Windows entirely.

## Background

- The Zune 30/80/120 has a **2MB NOR flash chip** (Intel PH28F160C3TD) soldered to the motherboard with the first-stage bootloader
- The bootloader is NOT on the HDD — a blank SSD will still boot into recovery mode
- The i.MX31 SoC has **no secure boot** (no HAB), so there's no cryptographic barrier
- The firmware is 4 binaries: `zboot.bin`, `nk.bin`, `recovery.bin`, `ext.bin` (Windows CE container format)
- Nobody has ever documented the USB protocol used during firmware flashing

## What You Need

### Hardware
- Linux PC (x86_64) with USB-A ports
- USB cable for Zune (USB-A to Zune 30-pin connector)
- Your Zune device (with old HDD or new blank SSD)

### Software (Linux)

```bash
# Wireshark + USB capture support
sudo apt install wireshark usbutils

# Enable USB capture without root
sudo usermod -aG wireshark $USER
sudo modprobe usbmon

# lessmsi alternative for Linux — use 7z to extract the MSI
sudo apt install p7zip-full

# .NET runtime for dotnet-serve (or use python3 http.server instead)
# Python is simpler on Linux:
sudo apt install python3

# Windows VM for Zune Software (QEMU/KVM or VirtualBox)
sudo apt install qemu-kvm virt-manager
# OR
sudo apt install virtualbox virtualbox-ext-pack
```

### Firmware Files

Download the firmware MSI from the Internet Archive:
```
https://archive.org/details/zune-firmware-x-86
```
File: `ZunePackage.msi` (~129 MB)

Extract the contents:
```bash
mkdir ~/zune-firmware
cd ~/zune-firmware
7z x ZunePackage.msi
```

The extracted CAB files contain firmware for each Zune model. Extract the CABs:
```bash
# Find the CAB files
find . -name "*.cab" -exec 7z x {} \;

# Look for the .bin files
find . -name "*.bin" | sort
# Expected: zboot.bin, nk.bin, recovery.bin, ext.bin (per model)
```

### Zune Software 4.8 (Windows)

Download from Microsoft (if still available) or Internet Archive:
```
https://archive.org/search?query=zune+software+4.8
```

This runs inside the Windows VM. Install it in the VM before starting.

## Setup: Windows VM with USB Passthrough

### Option A: QEMU/KVM (Recommended)

```bash
# Create a Windows VM (use an existing Windows ISO)
virt-manager
# During VM creation:
#   - Add USB Host Device: Microsoft Zune (VID 045e, PID 0710)
#   - Enable USB 2.0 (EHCI) controller

# OR pass through USB from command line:
qemu-system-x86_64 \
    -enable-kvm \
    -m 4096 \
    -hda windows.qcow2 \
    -usb \
    -device usb-host,vendorid=0x045e,productid=0x0710
```

### Option B: VirtualBox

```
Settings > USB > USB 2.0 (EHCI)
Add Filter: Microsoft Zune (045e:0710)
```

**Important:** The USB passthrough must forward the device to the Windows VM so the Zune software can talk to it. But we capture BEFORE it reaches the VM — on the Linux host.

## Setup: Firmware Hosting (Inside the Windows VM)

The Zune software needs to find firmware at `resources.zune.net`. Since Microsoft's servers are dead, we redirect locally.

### 1. Edit Windows hosts file (inside the VM)

Open Notepad **as Administrator**, edit `C:\Windows\System32\drivers\etc\hosts`:
```
127.0.0.1 resources.zune.net
```

### 2. Create the firmware directory structure (inside the VM)

```
C:\zune-firmware\firmware\v4_5\
```

Copy these files into that folder:
- `zuneprod.xml` — rename from `FirmwareUpdate.xml` (found in the extracted MSI)
- All `.cab` firmware files

### 3. Start a local web server (inside the VM)

Using Python (install Python for Windows in the VM):
```cmd
cd C:\zune-firmware
python -m http.server 80
```

Or using `dotnet serve`:
```cmd
cd C:\zune-firmware
dotnet serve -p 80
```

### 4. Verify it works

Open a browser in the VM and go to:
```
http://resources.zune.net/firmware/v4_5/zuneprod.xml
```

You should see the XML firmware manifest. If not, check:
- Is the web server running on port 80?
- Is the hosts file saved correctly?
- Is the folder structure right? (`firmware/v4_5/zuneprod.xml`)

## Capture Process

### Step 1: Find the USB bus

Before plugging in the Zune, list USB buses:
```bash
ls /sys/bus/usb/devices/
# or
lsusb
```

Plug in the Zune and find it:
```bash
lsusb | grep -i 045e
# Expected: Bus 00X Device 00Y: ID 045e:0710 Microsoft Corp. Zune
# Note the bus number (e.g., Bus 001)
```

The usbmon interface is `usbmonX` where X = bus number.

### Step 2: Start Wireshark capture on the USB bus

```bash
# Start Wireshark with USB capture
sudo wireshark &

# Select the correct usbmon interface:
#   usbmon0 = all buses (captures everything, noisier)
#   usbmon1 = bus 1 only (cleaner, if Zune is on bus 1)

# Start capture BEFORE doing the firmware flash
```

Alternatively, capture from command line with `tcpdump`:
```bash
# Capture all USB traffic on bus 1
sudo tcpdump -i usbmon1 -w ~/zune_firmware_capture.pcapng

# Or capture with tshark (Wireshark CLI)
sudo tshark -i usbmon1 -w ~/zune_firmware_capture.pcapng
```

### Step 3: Pass the Zune USB to the Windows VM

If using QEMU with virt-manager:
- VM menu > Redirect USB device > Microsoft Zune

If using VirtualBox:
- Devices menu > USB > Microsoft Zune

The Zune should now appear in the Windows VM.

### Step 4: Perform the firmware flash

Inside the Windows VM:
1. Open Zune Software 4.8
2. The Zune should be detected (either in normal mode or recovery mode)
3. Go to **Settings > Device > Update**
4. Let it check for updates (it will find the locally hosted firmware)
5. **Let it flash completely** — do not interrupt
6. Wait for the Zune to reboot

The entire firmware push is being captured by Wireshark/tcpdump on the Linux host.

### Step 5: Stop capture and save

```bash
# If using Wireshark GUI: File > Save As > zune_firmware_flash.pcapng
# If using tcpdump: Ctrl+C to stop

# Verify the capture file
ls -lh ~/zune_firmware_capture.pcapng
# Should be several MB to tens of MB depending on firmware size
```

## Ideal Captures (Do Two If Possible)

### Capture 1: Firmware Update (Old HDD still installed)
- Safer — device already works
- Shows the "update" protocol path
- Good baseline

### Capture 2: Firmware Restore (Blank SSD installed)
- The real scenario we want to support
- Zune boots into recovery mode ("Connect Zune to your PC")
- Shows the "cold flash" protocol path
- May have different USB descriptors and handshake

The protocol might differ between update and cold flash. Having both tells us everything.

## Post-Capture Analysis

### Quick Wireshark Filters

```
# All USB bulk transfers (firmware data lives here)
usb.transfer_type == 3

# Host → device only (the firmware push direction)
usb.src == "host"

# Filter by Zune vendor ID
usb.idVendor == 0x045e

# Filter by specific device address (replace X.Y with actual)
usb.device_address == Y

# Large transfers (firmware chunks)
usb.data_len > 512

# First 100 packets after device connection (handshake)
frame.number < 100
```

### What To Look For

1. **Device descriptors in recovery mode**
   - What VID:PID does the Zune expose? (might be different from 045e:0710)
   - What USB class/subclass? (MTP = 0x06/0x01, or vendor-specific?)
   - What endpoints are used?

2. **Protocol identification**
   - Is it standard PTP/MTP with vendor operations?
   - Is it a proprietary bulk transfer protocol?
   - Are there USB control transfers for handshaking?

3. **Firmware transfer structure**
   - What order are the .bin files sent? (zboot → nk → recovery → ext?)
   - Are there headers before each file?
   - What chunk size? (512B? 64KB?)
   - Is there a checksum/CRC per chunk?

4. **Partition commands**
   - Does the host tell the device which partition to write?
   - Is there a "format disk" command first?
   - Is there a "reboot" command at the end?

5. **Authentication**
   - Is MTPZ auth used during firmware flash? (probably NOT — bootloader doesn't have the crypto stack)
   - Is there any handshake/challenge-response?

### Export for Analysis

```bash
# Export just the bulk transfer data (no USB framing)
tshark -r ~/zune_firmware_capture.pcapng \
    -Y "usb.transfer_type == 3 && usb.src == host" \
    -T fields -e usb.capdata \
    > ~/zune_fw_bulk_data.hex

# Export as JSON for programmatic analysis
tshark -r ~/zune_firmware_capture.pcapng \
    -Y "usb.transfer_type == 3" \
    -T json \
    > ~/zune_fw_transfers.json
```

## What We Build After

Once we have the capture data, the plan is:

1. Decode the USB protocol (control transfers, bulk transfer format, handshake)
2. Parse the .bin container format (Windows CE nb0/bin — documented)
3. Build `zune_flash_firmware()` in libzune:
   ```c
   int zune_flash_firmware(const char *firmware_dir);
   // firmware_dir contains: zboot.bin, nk.bin, recovery.bin, ext.bin
   // Handles: USB enumeration, protocol handshake, partition writing, reboot
   ```
4. Wire into Zuuned app — "Flash Firmware" button in settings
5. No Windows needed. Ever. Again.

## Zune Models and Hardware IDs

| Model | USB VID:PID | SoC | Boot Flash | HDD Interface |
|-------|------------|-----|------------|---------------|
| Zune 30 | 045e:0710 | Freescale i.MX31L | 2MB NOR (Intel PH28F160C3TD) | 1.8" ZIF |
| Zune 80 | 045e:0710 | Freescale i.MX31L | 2MB NOR | 1.8" ZIF |
| Zune 120 | 045e:0710 | Freescale i.MX31L | 2MB NOR | 1.8" ZIF |
| Zune 4/8/16 | 045e:063E | Freescale i.MX31L | NOR flash | Internal NAND |
| Zune HD | 045e:063E | NVIDIA Tegra APX | Different boot chain | Internal NAND |

## References

- [bunnie's blog — Zune 30 hardware teardown](https://www.bunniestudios.com/blog/2006/zune-guts/)
- [Scott Hanselman — Zune firmware update without Microsoft](https://www.hanselman.com/blog/how-to-update-the-firmware-on-your-zune-without-microsoft-dammit)
- [Internet Archive — Zune firmware MSI](https://archive.org/details/zune-firmware-x-86)
- [XDA Forums — Zune HD ROM dump](https://xdaforums.com/t/zune-hd-rom-dump.564591/)
- [XDA Forums — Zune bootloader extraction](https://xdaforums.com/t/pls-help-extract-the-ms-zune-bootloader.3981943/)
- [Hackaday — 32GB solid state Zune upgrade](https://hackaday.com/2009/10/10/32gb-solid-state-zune-upgrade/)
- [zuneupdate.com — community firmware hosting](https://www.zuneupdate.com/)
- [tipok.org.ua — Zune reboot/reformat/restore](https://tipok.org.ua/node/30)

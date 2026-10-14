# Installing LakoOS

Tested in a virtual machine: UEFI (OVMF), q35, 4 GB of memory, an NVMe disk, `LakoOS-amd64-setup.iso`.
The same steps apply to arm64 and to BIOS machines.

## 1. Start from the setup image

Attach `LakoOS-amd64-setup.iso` and boot. Pick the language and keyboard layout.

![Language](img/install-s01_language.png)

## 2. Run the installer

Choose **Install LakoOS** (or **Run LakoOS Live CD** to try it without installing).

![Run or install](img/install-s02_start.png)

The wizard opens. Read the notice and continue.

![Welcome](img/install-s03_welcome.png)

## 3. Installation type

![Installation type](img/install-s04_type.png)

## 4. Choose the disk

Select the disk or free space and tick the confirmation that the disk will be erased.

![Disk](img/install-s05_disk_ok.png)

## 5. Name your PC and create an account

If you leave the password empty, LakoOS signs in automatically.

![Account](img/install-s06_account.png)

## 6. Time zone

![Time zone](img/install-s07_timezone.png)

## 7. Review and install

![Summary](img/install-s08_summary.png)

## 8. Wait for the copy to finish

The list shows which stage the installer is in. The VM restarts when it is done.

![Progress](img/install-s09_progress1.png)

## 9. First start

Remove the setup image so the machine boots from the disk. You arrive at the desktop.

![Desktop](img/desktop.png)

| Start | File Explorer |
|---|---|
| ![Start](img/start.png) | ![Explorer](img/explorer.png) |

| Control Panel | System properties |
|---|---|
| ![Control Panel](img/control.png) | ![System](img/system.png) |

# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
echo "LakoOS: looking for FreeLoader on USB storage"
pci enum
usb start
for lakoos_dev in 0 1 2 3 4 5 6 7; do
	if test -e usb ${lakoos_dev}:1 efi/boot/bootriscv64.efi; then
		echo "LakoOS: starting FreeLoader from usb ${lakoos_dev}:1"
		setenv lakoos_fdt ${fdtcontroladdr}
		if load usb ${lakoos_dev}:1 ${fdt_addr_r} dtb/starfive/jh7110-orangepi-rv.dtb; then
			setenv lakoos_fdt ${fdt_addr_r}
		fi
		load usb ${lakoos_dev}:1 ${kernel_addr_r} efi/boot/bootriscv64.efi
		bootefi ${kernel_addr_r} ${lakoos_fdt}
	fi
done
echo "LakoOS: no USB disk holds efi/boot/bootriscv64.efi"

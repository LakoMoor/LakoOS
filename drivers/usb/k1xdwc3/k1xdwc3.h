/*
 * PROJECT:     LakoOS SpacemiT K1 USB3 Glue Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SpacemiT K1 USB3 block definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <wdmguid.h>

#define K1XDWC3_TAG 'cwDK'

#define K1X_APMU_BASE               0xD4282800ULL
#define K1X_APMU_SIZE               0x400
#define K1X_APMU_USB                0x05C
#define K1X_APMU_USB_USB30_CLOCK    (1UL << 8)
#define K1X_APMU_USB_USB30_RESET    ((1UL << 9) | (1UL << 10) | (1UL << 11))
#define K1X_APMU_PCIE0              0x3CC
#define K1X_APMU_PCIE0_COMBO_RESET  (1UL << 8)
#define K1X_APMU_PHY_SELECT         0x110
#define K1X_APMU_PHY_SELECT_USB3    (1UL << 3)

#define K1X_COMBPHY_BASE            0xC0B10000ULL
#define K1X_COMBPHY_SIZE            0x800
#define K1X_COMBPHY_STATUS          0x008
#define K1X_COMBPHY_STATUS_PLL      (1UL << 0)
#define K1X_COMBPHY_RX_CONTROL      0x018
#define K1X_COMBPHY_PLL_CONTROL     0x048
#define K1X_COMBPHY_LFPS            0x058
#define K1X_COMBPHY_LFPS_MASK       (7UL << 8)
#define K1X_COMBPHY_LFPS_DEFAULT    (3UL << 8)
#define K1X_COMBPHY_MODE            0x068

#define K1X_USB2PHY_BASE            0xC0A30000ULL
#define K1X_USB2PHY_SIZE            0x200
#define K1X_USB2PHY_CONTROL         0x004
#define K1X_USB2PHY_CONTROL_PLL     (1UL << 0)
#define K1X_USB2PHY_HOST            0x010
#define K1X_USB2PHY_HOST_NO_CLEAR   (1UL << 2)
#define K1X_USB2PHY_PATH            0x018
#define K1X_USB2PHY_PATH_HS_SOURCE  (1UL << 0)
#define K1X_USB2PHY_GATING          0x034
#define K1X_USB2PHY_ANALOG          0x0A4
#define K1X_USB2PHY_ANALOG_ISEL     0xFUL
#define K1X_USB2PHY_ANALOG_ISEL_15  0xCUL
#define K1X_USB2PHY_ANALOG_IREG     (1UL << 4)

#define K1X_GPIO_BASE               0xD4019000ULL
#define K1X_GPIO_SIZE               0x800
#define K1X_GPIO_SET                0x018
#define K1X_GPIO_CLEAR              0x024
#define K1X_GPIO_SET_OUTPUT         0x054
#define K1X_GPIO_COUNT              128
#define K1X_HUB_MAX_GPIOS           4
#define K1X_HUB_POWER_OFF_MS        250
#define K1X_HUB_VBUS_DELAY_MS       10

#define DWC3_GCTL                   0xC110
#define DWC3_GCTL_PRTCAP_MASK       (3UL << 12)
#define DWC3_GCTL_PRTCAP_HOST       (1UL << 12)
#define DWC3_GUCTL1                 0xC11C
#define DWC3_GUCTL1_PARKMODE_SS     (1UL << 17)
#define DWC3_GUCTL1_IPGAP_CHECK     (1UL << 28)
#define DWC3_GSNPSID                0xC120
#define DWC3_GUSB2PHYCFG            0xC200
#define DWC3_GUSB2PHYCFG_SUSPHY     (1UL << 6)
#define DWC3_GUSB2PHYCFG_ENBLSLPM   (1UL << 8)
#define DWC3_GUSB3PIPECTL           0xC2C0
#define DWC3_GUSB3PIPECTL_SUSPHY    (1UL << 17)
#define DWC3_GUSB3PIPECTL_DEPOCHG   (1UL << 18)
#define DWC3_REGISTER_SPAN          0xC300

typedef struct _K1XDWC3_GPIO_LIST
{
    ULONG Count;
    ULONG InterDelay;
    ULONG Number[K1X_HUB_MAX_GPIOS];
    BOOLEAN ActiveLow[K1X_HUB_MAX_GPIOS];
} K1XDWC3_GPIO_LIST, *PK1XDWC3_GPIO_LIST;

typedef struct _K1XDWC3_HUB_POWER
{
    K1XDWC3_GPIO_LIST Hub;
    K1XDWC3_GPIO_LIST Vbus;
    ULONG VbusDelay;
} K1XDWC3_HUB_POWER, *PK1XDWC3_HUB_POWER;

#define K1XDWC3_FDO 0x4F444631UL
#define K1XDWC3_PDO 0x4F445031UL

typedef struct _K1XDWC3_EXTENSION
{
    ULONG Type;
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT Child;
    struct _K1XDWC3_EXTENSION *Parent;
    BUS_INTERFACE_STANDARD BusInterface;
    BOOLEAN BusInterfaceValid;
    BOOLEAN ChildReported;
    ULONG64 CoreBase;
    ULONG CoreSize;
    ULONG CoreInterrupt;
} K1XDWC3_EXTENSION, *PK1XDWC3_EXTENSION;

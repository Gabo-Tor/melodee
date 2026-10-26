/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* RF-only clock/reset preparation. Register facts checked against FM1 Open
 * 4c7496d bt_rf_init, offsets 0x26..0x168, and Optimist HW sections 5/16.
 * Executes wholly from RAM. No Bluetooth baseband or packet transmitter starts. */
#pragma once
#include <stdint.h>
#define RFCAL_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
static void fmcal_hw_prepare(void)
{
    RFCAL_REG(0x10010)&=~(3u<<14);
    for(unsigned a=0x14040;a<=0x14050;a+=4)RFCAL_REG(a)=0;
    RFCAL_REG(0x14040)=0x141;
    fm1_delay_us(2);
    RFCAL_REG(0x14040)|=4;
    fm1_delay_us(2);
    RFCAL_REG(0x14040)|=8;
    fm1_delay_us(2);
    RFCAL_REG(0x14040)|=2;
    fm1_delay_us(100);
    RFCAL_REG(0x1405c)=(RFCAL_REG(0x1405c)&0xff000000u)|5u;
    RFCAL_REG(0x1405c)|=0x47000000u;
    RFCAL_REG(0x1405c)&=~0x80000000u;
    RFCAL_REG(0x1404c)|=1u<<24;
    RFCAL_REG(0x30f04)=(RFCAL_REG(0x30f04)|0xe37u)&~8u;
    RFCAL_REG(0x30f00)=(RFCAL_REG(0x30f00)&~0x3f0u)|0x46fu;
}
static void fmcal_hw_stop(void)
{
    RFCAL_REG(0x28028)=0;
    RFCAL_REG(0x28000)=0;
    RFCAL_REG(0x14000)=0;
}

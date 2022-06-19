#ifndef _BISCUITOS_RTC_H
#define _BISCUITOS_RTC_H

#include "broiler/device.h"

/* PORT 0070-007F - CMOS RAM/RTC (REAL TIME CLOCK) */
#define RTC_BUS_TYPE		DEVICE_BUS_IOPORT
#define RTC_BASE_ADDRESS	0x0070

/*
 * MC146818 RTC registers
 */
#define RTC_SECONDS			0x00
#define RTC_SECONDS_ALARM		0x01
#define RTC_MINUTES			0x02
#define RTC_MINUTES_ALARM		0x03
#define RTC_HOURS			0x04
#define RTC_HOURS_ALARM			0x05
#define RTC_DAY_OF_WEEK			0x06
#define RTC_DAY_OF_MONTH		0x07
#define RTC_MONTH			0x08
#define RTC_YEAR			0x09
#define RTC_CENTURY			0x32

#define RTC_REG_A			0x0A
#define RTC_REG_B			0x0B
#define RTC_REG_C			0x0C
#define RTC_REG_D			0x0D

/*
 * Register D Bits
 */
#define RTC_REG_D_VRT			(1 << 7)

struct rtc_device {
	u8 cmos_idx;
	u8 cmos_data[128];
};

static inline unsigned char bin2bcd(unsigned val)
{
	return ((val / 10) << 4) + val % 10;
}

#endif

#include "broiler/broiler.h"
#include "broiler/device.h"
#include "broiler/ioport.h"
#include "broiler/rtc.h"

static struct rtc_device broiler_rtc;

static void cmos_ram_io(struct broiler_cpu *vcpu, u64 addr, u8 *data,
                        u32 len, u8 is_write, void *ptr)
{
	struct tm *tm;
	time_t ti;

	if (is_write) {
		if (addr == RTC_BASE_ADDRESS) { /* index register */
			u8 value = ioport_read8(data);

			vcpu->broiler->nmi_disabled = value & (1UL << 7);
			broiler_rtc.cmos_idx = value & ~(1UL << 7);
			return;
		}

		switch (broiler_rtc.cmos_idx) {
		case RTC_REG_C:
		case RTC_REG_D:
			/* Read-only */
			break;
		default:
			broiler_rtc.cmos_data[broiler_rtc.cmos_idx] =
							ioport_read8(data);
			break;
		}
		return;
	}

	if (addr == RTC_BASE_ADDRESS)   /* index register is write-only */
		return;

	time(&ti);

	tm = gmtime(&ti);
	switch (broiler_rtc.cmos_idx) {
	case RTC_SECONDS:
		ioport_write8(data, bin2bcd(tm->tm_sec));
		break;
	case RTC_MINUTES:
		ioport_write8(data, bin2bcd(tm->tm_min));
		break;
	case RTC_HOURS:
		ioport_write8(data, bin2bcd(tm->tm_hour));
		break;
	case RTC_DAY_OF_WEEK:
		ioport_write8(data, bin2bcd(tm->tm_wday + 1));
		break;
	case RTC_DAY_OF_MONTH:
		ioport_write8(data, bin2bcd(tm->tm_mday));
		break;
	case RTC_MONTH:
		ioport_write8(data, bin2bcd(tm->tm_mon + 1));
		break;
	case RTC_YEAR: {
		int year;

		year = tm->tm_year + 1900;
		ioport_write8(data, bin2bcd(year % 100));
		break;
	}
	case RTC_CENTURY: {
		int year;

		year = tm->tm_year + 1900;
		ioport_write8(data, bin2bcd(year / 100));
		break;
	}
	default:
		ioport_write8(data, 
				broiler_rtc.cmos_data[broiler_rtc.cmos_idx]);
		break;
	}
}

static struct device rtc_dev = {
	.bus_type = RTC_BUS_TYPE,
	.data	  = NULL,
};

int broiler_rtc_init(struct broiler *broiler)
{
	int r;

	r = device_register(&rtc_dev);
	if (r < 0)
		return r;

	r = broiler_ioport_register(broiler, RTC_BASE_ADDRESS, 2,
				cmos_ram_io, NULL, RTC_BUS_TYPE);
	if (r < 0)
		goto err_ioport;

	/* Set the VRT bit in Register D to indicate valid RAM and time */
	broiler_rtc.cmos_data[RTC_REG_D] = RTC_REG_D_VRT;

	return r;

err_ioport:
	device_unregister(&rtc_dev);

	return r;
}

int broiler_rtc_exit(struct broiler *broiler)
{
	broiler_ioport_deregister(broiler, RTC_BASE_ADDRESS, RTC_BUS_TYPE);

	return 0;
}

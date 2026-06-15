#if !defined(__DRIVERS_DRIVERS_H__)
#define __DRIVERS_DRIVERS_H__
#include <drivers/msc.h>
#include <common/dynarray.h>

dynarray(MSCDriver*) getBlockDevices();

#endif // __DRIVERS_DRIVERS_H__

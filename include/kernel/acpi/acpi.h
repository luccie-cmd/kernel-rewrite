#if !defined(__KERNEL_ACPI_ACPI_H__)
#define __KERNEL_ACPI_ACPI_H__
#include <stdbool.h>

void acpiInitialize();
bool acpiIsInitialized();
void* acpiGetTable(const char* table);

#endif // __KERNEL_ACPI_ACPI_H__

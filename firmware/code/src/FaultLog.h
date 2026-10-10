/** @file FaultLog.h
 *  @brief A fault (MemManage, BusFault, UsageFault) resets the CHOMPI into the launcher instead
 *  of hanging it, and leaves what it was in the backup SRAM, which a reset keeps: the next boot
 *  writes it to /FRIZZ/fault.txt (WriteFaultLog). So a crash shows, and the device stays
 *  reachable over USB without a power cycle (#51).
 *
 *  The three are switched on at the start of main (EnableFaultLog); without that they'd go
 *  to libDaisy's HardFault_Handler, which waits for a debugger forever. A fault in the audio
 *  interrupt still becomes a hard fault (a fault handler can't preempt an interrupt of its
 *  own priority), so EnableFaultLog puts a handler of ours in the hard fault's vector too.
 *  Only a lockup (a fault in a fault handler) still hangs. Nothing on the host.
 */
#pragma once
#include <stdint.h>

#if defined(__arm__)
#include "daisy_seed.h"
#include "fatfs.h"

namespace chompi
{

struct FaultRecord
{
    uint32_t magic; // kFaultMagic: a fault since the last boot wrote it out
    uint32_t kind;  // 1 MemManage, 2 BusFault, 3 UsageFault, 4 HardFault
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t pc, lr; // stacked by the fault
};
static const uint32_t kFaultMagic = 0xF4017EC0;

// in the backup SRAM (chompi_sram.lds's BACKUP_SRAM), after libDaisy's boot_info: its own
// section, so boot_info keeps the address the bootloader writes it at (the linker checks)
__attribute__((section(".backup_sram.frizz"))) volatile FaultRecord fault_record;

/** From the handlers: sp is where the fault stacked r0-r3, r12, lr, pc, xpsr */
extern "C" __attribute__((used, noreturn)) void FaultEntry(uint32_t* sp, uint32_t kind)
{
    fault_record.kind = kind;
    fault_record.cfsr = SCB->CFSR;
    fault_record.hfsr = SCB->HFSR;
    fault_record.mmfar = SCB->MMFAR;
    fault_record.bfar = SCB->BFAR;
    fault_record.lr = sp[5];
    fault_record.pc = sp[6];
    fault_record.magic = kFaultMagic;
    __DSB();
    NVIC_SystemReset();
    while (true) {}
}

#define FRIZZ_FAULT_HANDLER(name, kind)                                                     \
    extern "C" __attribute__((naked)) void name()                                           \
    {                                                                                       \
        __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n movs r1, #" #kind \
                       "\n b FaultEntry\n");                                                \
    }
FRIZZ_FAULT_HANDLER(MemManage_Handler, 1)
FRIZZ_FAULT_HANDLER(BusFault_Handler, 2)
FRIZZ_FAULT_HANDLER(UsageFault_Handler, 3)
FRIZZ_FAULT_HANDLER(FrizzHardFault_Handler, 4)

// the vector table (libDaisy's startup_stm32h750xx.c), in SRAM: VTOR points at it
extern "C" void* g_pfnVectors[];

/** At the start of main, before the caches are on: the three faults to their handlers
 *  above, not to a hard fault, and a hard fault to ours rather than libDaisy's */
inline void EnableFaultLog()
{
    g_pfnVectors[3] = reinterpret_cast<void*>(&FrizzHardFault_Handler);
    // in memory, should the bootloader have left the D-cache on
    SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(g_pfnVectors), 32);
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    __DSB();
    __ISB();
}

/** Once the card is up, at boot: a fault left from before into /FRIZZ/fault.txt (appended),
 *  and forgotten */
inline void WriteFaultLog()
{
    if (fault_record.magic != kFaultMagic)
        return;
    alignas(32) static char text[160];
    const int n = snprintf(text, sizeof(text),
                           "fault %lu cfsr %08lx hfsr %08lx mmfar %08lx bfar %08lx pc %08lx lr %08lx\n",
                           (unsigned long)fault_record.kind, (unsigned long)fault_record.cfsr,
                           (unsigned long)fault_record.hfsr, (unsigned long)fault_record.mmfar,
                           (unsigned long)fault_record.bfar, (unsigned long)fault_record.pc,
                           (unsigned long)fault_record.lr);
    fault_record.magic = 0;
    static FIL file;
    if (n > 0 && f_open(&file, "/FRIZZ/fault.txt", FA_OPEN_APPEND | FA_WRITE) == FR_OK)
    {
        UINT written = 0;
        f_write(&file, text, static_cast<UINT>(n), &written);
        f_close(&file);
    }
}

} // namespace chompi

#else
namespace chompi
{
inline void EnableFaultLog() {}
inline void WriteFaultLog() {}
} // namespace chompi
#endif

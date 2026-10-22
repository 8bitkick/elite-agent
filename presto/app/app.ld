/* The agent as an app (rp2350-bbc-micro's src/common/app_api.h): linked at the app slot, where
   it runs from the flash (execute in place), its header first; its .data and .bss in the app RAM
   at the top of the SRAM, as much as they take (the loader copying .data in from after the code and
   zeroing .bss): build.sh links twice, the second time with APP_RAM_BASE at app_api.h's APP_RAM_TOP
   less what the first took. It sets APP_SLOT_XIP, APP_RAM_BASE and APP_RAM_SIZE with --defsym. */

MEMORY
{
    SLOT(rx) : ORIGIN = APP_SLOT_XIP, LENGTH = 1M
    RAM(rwx) : ORIGIN = APP_RAM_BASE, LENGTH = APP_RAM_SIZE
}

SECTIONS
{
    .text : {
        KEEP(*(.app_header))
        *(EXCLUDE_FILE(*libgcc.a: *libm.a: *libc.a:) .text*)
        *libc.a:libc_a-strstr.o(.text*)     /* (no frame calls it: from the flash) */
        *(.rodata*)
        . = ALIGN(4);
    } > SLOT
    .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > SLOT
    /* libgcc, libm and the string functions run from RAM, as the firmware has them (the SDK's
       linker script): the agent calls them all through its frame */
    .data : {
        *libgcc.a:(.text*)
        *libm.a:(.text*)
        *libc.a:(.text*)
        *(.time_critical*)
        *(.data*)
        . = ALIGN(4);
    } > RAM AT > SLOT
    __app_data_load = LOADADDR(.data);
    __app_data_size = SIZEOF(.data);
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
    } > RAM
    __app_bss_size = SIZEOF(.bss);
    __app_end = LOADADDR(.data) + SIZEOF(.data);
    __app_ram_base = ORIGIN(RAM);
    /DISCARD/ : { *(.init) *(.fini) *(.init_array*) *(.fini_array*) *(.preinit_array*) *(.comment) }
}

/* The agent as an app (rp2350-bbc-micro's src/common/app_api.h): linked at the app slot, where
   it runs from the flash (execute in place), its header first; its .data and .bss in the app RAM
   area, the loader copying .data in from after the code and zeroing .bss. build.sh sets
   APP_SLOT_XIP, APP_RAM_BASE and APP_RAM_SIZE (from app_api.h) with --defsym. */

MEMORY
{
    SLOT(rx) : ORIGIN = 0x10e00000, LENGTH = 1M
    RAM(rwx) : ORIGIN = 0x2006e000, LENGTH = 72K
}

SECTIONS
{
    .text : {
        KEEP(*(.app_header))
        *(EXCLUDE_FILE(*libgcc.a: *libm.a: *libc.a:) .text*)
        *libc.a:libc_a-strstr.o(.text*)     /* (no frame calls it: from the flash, the 72 KB is full) */
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
    /DISCARD/ : { *(.init) *(.fini) *(.init_array*) *(.fini_array*) *(.preinit_array*) *(.comment) }
}
ASSERT(ORIGIN(SLOT) == APP_SLOT_XIP && ORIGIN(RAM) == APP_RAM_BASE && LENGTH(RAM) == APP_RAM_SIZE,
       "app.ld's slot or RAM isn't app_api.h's")

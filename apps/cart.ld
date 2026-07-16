OUTPUT_ARCH(riscv)
ENTRY(_cart_entry)

SECTIONS
{
  . = 0x88000000;

  .cart_header : {
    KEEP(*(.cart_header))
  }

  .text : {
    KEEP(*(.text.entry))
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
  }

  .data : {
    *(.data .data.*)
    *(.sdata .sdata.*)
  }

  /* _edata is the exact objcopy output size; the loader's bss memset
     starts here, so it also clears any alignment gap before .bss. */
  _edata = .;

  .bss : {
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
  }

  . = ALIGN(16);
  _end = .;

  __cart_load_size = _edata - 0x88000000;
  __cart_bss_size = _end - _edata;
}

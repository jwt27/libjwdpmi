/* Default linker script, for normal executables */
/* Copyright (C) 2014-2020 Free Software Foundation, Inc.
   Copying and distribution of this script, with or without modification,
   are permitted in any medium without royalty provided the copyright
   notice and this notice are preserved.  */
OUTPUT_FORMAT("coff-go32-exe")
ENTRY (start)
SECTIONS
{
  .text  ALIGN(0x1000+SIZEOF_HEADERS, 0x200) : SUBALIGN(0x10) {
    *(.text)
    *(.text.*)
    *(.gnu.linkonce.t*)
    *(.const*)
    *(.ro*)
    *(.gnu.linkonce.r*)
    /* HACK:  These should be in .data but require 16-byte alignment.  */
    *(.data)
    *(.data.*)
    *(.gnu.linkonce.d*)
    etext  =  . ; PROVIDE(_etext = .) ;
    . = ALIGN(0x200);
  }
  .data : {
    djgpp_first_ctor = . ;
    KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.ctors))
    KEEP(*(.ctor))
    djgpp_last_ctor = . ;
    djgpp_first_dtor = . ;
    KEEP(*(.dtor))
    KEEP(*(.dtors))
    KEEP(*(SORT_BY_INIT_PRIORITY(.dtors.*)))
    djgpp_last_dtor = . ;
    __environ = . ;
    PROVIDE(_environ = .) ;
    LONG(0) ;
    KEEP(*(.gcc_exc*))
    ___EH_FRAME_BEGIN__ = . ;
    KEEP(*(.eh_fram*))
    ___EH_FRAME_END__ = . ;
    LONG(0);
    edata  =  . ; PROVIDE(_edata = .) ;
    . = ALIGN(0x200);
  }
  .bss : SUBALIGN(0x10)
  {
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
     end = . ; PROVIDE(_end = .) ;
     . = ALIGN(0x200);
  }
  /* Discard LTO sections.  */
  /DISCARD/ : { *(.gnu.lto_*) }
  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line .debug_line.* .debug_line_end) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* DWARF 3 */
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  .debug_ranges   0 : { *(.debug_ranges) }
  /* DWARF Extension.  */
  .debug_macro    0 : { *(.debug_macro) }
  .debug_addr     0 : { *(.debug_addr) }
}

ENTRY(_start)
PHDRS {
 main PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6);
 mcount PT_LOAD FLAGS(5); public PT_LOAD FLAGS(5);
 inner PT_LOAD FLAGS(5); zram PT_LOAD FLAGS(5);
 down PT_LOAD FLAGS(5); save PT_LOAD FLAGS(5); restore PT_LOAD FLAGS(5);
}
SECTIONS {
 .text 0x00010000 : { *(.text) } :main
 .data 0x00020000 : { *(.data) } :data
 .mcount 0x0010ee90 : { *(.mcount) } :mcount
 .rw_public 0x0017bc4c : { *(.rw_public) } :public
 .rw_inner 0x003c55ec : { *(.rw_inner) } :inner
 .zram_code 0x004480a8 : { *(.zram_code) } :zram
 .blocking_down 0x008c7e60 : { *(.blocking_down) } :down
 .lock_save 0x008c9c3c : { *(.lock_save) } :save
 .lock_restore 0x008c9f04 : { *(.lock_restore) } :restore
}

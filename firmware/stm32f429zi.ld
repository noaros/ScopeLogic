MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text       : { *(.text*) *(.rodata*) } > FLASH
}

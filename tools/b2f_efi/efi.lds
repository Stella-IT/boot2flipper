/* ELF layout that objcopy turns into an EFI application (as gnu-efi does) */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)
SECTIONS
{
    . = 0;
    ImageBase = .;
    .hash : { *(.hash) }
    .gnu.hash : { *(.gnu.hash) }
    . = ALIGN(4096);
    .text : { *(.text) *(.text.*) }
    . = ALIGN(4096);
    .reloc : { *(.reloc) }
    . = ALIGN(4096);
    .data : { *(.rodata*) *(.data) *(.data.*) *(COMMON) *(.bss) *(.bss.*) }
    . = ALIGN(4096);
    .dynamic : { *(.dynamic) }
    .rela : { *(.rela*) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

/* A PE loader needs a relocation section to load the image at any address.
 * The code is position independent, so it only has a no-op entry. */
    .data
dummy:
    .long 0

    .section .reloc, "a"
label:
    .long dummy - label /* Page RVA */
    .long 12            /* Block size */
    .word 0             /* IMAGE_REL_BASED_ABSOLUTE, no-op */
    .word 0

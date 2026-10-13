/*
 * Boot2Flipper legacy BIOS boot loader
 *
 * Sector 0 of this image is the MBR boot code (only bytes 0-439 are used;
 * the partition table is filled in by virtual_fat.c). It loads stage 2 from
 * STAGE2_LBA, which sits in the unpartitioned gap before the FAT32 partition.
 *
 * Stage 2 loads IPXE.LKR (a bzImage, Linux boot protocol >= 2.02) and passes
 * BOOT.CFG as its initrd. iPXE registers the initrd as an image and executes
 * it as a script, which is how the generated script reaches BIOS iPXE (it
 * cannot read AUTOEXEC.IPXE from the USB disk the way the EFI build does).
 *
 * The disk is generated on the fly, so instead of parsing FAT the file
 * locations are patched into the parameter block at the start of stage 2.
 *
 * Rebuild src/disk/bios_boot_image.h with tools/bios_boot/build.sh.
 */

	.code16
	.text
	.globl	_start

	.set	STAGE2_LBA, 64
	.set	STAGE2_SECTORS, 4
	.set	STAGE2_ADDR, 0x7e00

	.set	PARAM_MAGIC, 0x4c463242	/* "B2FL" */

	.set	KERNEL_SEG, 0x1000	/* real-mode part at 0x10000 */
	.set	KERNEL_HIGH, 0x100000	/* protected-mode part */
	.set	INITRD_ADDR, 0x2000000	/* iPXE relocates itself below this */
	.set	BOUNCE_SEG, 0x2000
	.set	BOUNCE_ADDR, 0x20000
	.set	CHUNK_SECTORS, 32	/* 16KB per disk read */
	.set	HEAP_END, 0xe000	/* offsets within KERNEL_SEG */
	.set	CMDLINE_OFF, 0xe000

/*****************************************************************************
 * Stage 1 (MBR)
 */
_start:
	/* Some BIOSes enter at 07c0:0000 */
	ljmp	$0, $1f
1:	cli
	xorw	%ax, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	movw	$0x7c00, %sp
	sti
	cld
	movb	%dl, boot_drive

	/* Require INT 13h extensions (LBA reads) */
	movb	$0x41, %ah
	movw	$0x55aa, %bx
	int	$0x13
	jc	s1_fail
	cmpw	$0xaa55, %bx
	jne	s1_fail
	testb	$1, %cl
	jz	s1_fail

	movw	$s1_dap, %si
	movb	$0x42, %ah
	movb	boot_drive, %dl
	int	$0x13
	jc	s1_fail

	cmpl	$PARAM_MAGIC, stage2_magic
	jne	s1_fail
	jmp	stage2_main

s1_fail:
	movw	$s1_msg, %si
1:	lodsb
	testb	%al, %al
	jz	2f
	movb	$0x0e, %ah
	movw	$0x0007, %bx
	int	$0x10
	jmp	1b
2:	int	$0x18
3:	hlt
	jmp	3b

s1_msg:
	.asciz	"Boot2Flipper: cannot load stage 2\r\n"

	.balign	4
s1_dap:
	.byte	0x10, 0
	.word	STAGE2_SECTORS
	.word	STAGE2_ADDR, 0
	.quad	STAGE2_LBA

boot_drive:
	.byte	0x80

	/* 440-509: disk signature and partition table (filled in by C) */
	.org	440
	.org	510
	.word	0xaa55

/*****************************************************************************
 * Stage 2
 */
stage2_start:
	/* Parameter block, patched by bios_boot.c */
stage2_magic:
	.long	PARAM_MAGIC
kernel_lba:
	.long	0
kernel_size:
	.long	0
initrd_lba:
	.long	0
initrd_size:
	.long	0

stage2_main:
	movw	$msg_banner, %si
	call	print
	movw	$msg_kernel, %si
	call	print

	/* Read the first two kernel sectors and check the setup header */
	movl	kernel_size, %ecx
	cmpl	$1024, %ecx
	jb	bad_kernel
	movl	kernel_lba, %eax
	movl	$1024, %ecx
	movl	$(KERNEL_SEG << 4), %edi
	call	load
	jc	disk_error

	movw	$KERNEL_SEG, %ax
	movw	%ax, %es
	cmpl	$0x53726448, %es:0x202	/* "HdrS" */
	jne	bad_kernel
	cmpw	$0x0202, %es:0x206
	jb	bad_kernel
	testb	$0x01, %es:0x211	/* LOADED_HIGH */
	jz	bad_kernel
	movzbl	%es:0x1f1, %ebx		/* setup_sects, 0 means 4 */
	testl	%ebx, %ebx
	jnz	1f
	movl	$4, %ebx
1:	incl	%ebx			/* plus the boot sector */
	cmpl	$64, %ebx		/* must stay below HEAP_END */
	ja	bad_kernel
	shll	$9, %ebx		/* %ebx = real-mode part in bytes */
	cmpl	kernel_size, %ebx
	jae	bad_kernel

	/* Real-mode part */
	movl	kernel_lba, %eax
	movl	%ebx, %ecx
	movl	$(KERNEL_SEG << 4), %edi
	call	load
	jc	disk_error

	/* Protected-mode part */
	movl	%ebx, %eax
	shrl	$9, %eax
	addl	kernel_lba, %eax
	movl	kernel_size, %ecx
	subl	%ebx, %ecx
	movl	$KERNEL_HIGH, %edi
	call	load
	jc	disk_error
	movw	$msg_ok, %si
	call	print

	/* initrd (the iPXE script) */
	movl	initrd_size, %ecx
	testl	%ecx, %ecx
	jz	1f
	movw	$msg_initrd, %si
	call	print
	movl	initrd_lba, %eax
	movl	$INITRD_ADDR, %edi
	call	load
	jc	disk_error
	movw	$msg_ok, %si
	call	print
1:
	/* Fill in the setup header */
	movw	$KERNEL_SEG, %ax
	movw	%ax, %es
	movb	$0xff, %es:0x210	/* type_of_loader: undefined */
	orb	$0x80, %es:0x211	/* CAN_USE_HEAP */
	movw	$(HEAP_END - 0x200), %es:0x224
	movb	$0, %es:CMDLINE_OFF
	movl	$((KERNEL_SEG << 4) + CMDLINE_OFF), %es:0x228
	movl	initrd_size, %eax
	movl	%eax, %es:0x21c
	xorl	%edx, %edx
	testl	%eax, %eax
	jz	1f
	movl	$INITRD_ADDR, %edx
1:	movl	%edx, %es:0x218

	movw	$msg_boot, %si
	call	print

	cli
	movw	$KERNEL_SEG, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss
	movw	$HEAP_END, %sp
	ljmp	$(KERNEL_SEG + 0x20), $0

disk_error:
	movw	$msg_disk_error, %si
	jmp	fatal
bad_kernel:
	movw	$msg_bad_kernel, %si
fatal:
	call	print
	movw	$msg_any_key, %si
	call	print
	xorb	%ah, %ah
	int	$0x16
	int	$0x18
1:	hlt
	jmp	1b

/*
 * Load %ecx bytes starting at disk sector %eax to physical address %edi.
 * Reads go through a bounce buffer below 1MB and are copied into place with
 * INT 15h AH=87h, so any destination below 4GB works. CF set on error.
 */
load:
	pushal
1:	movl	%ecx, %ebx		/* %ebx = sectors this round */
	addl	$511, %ebx
	shrl	$9, %ebx
	cmpl	$CHUNK_SECTORS, %ebx
	jbe	2f
	movl	$CHUNK_SECTORS, %ebx
2:	call	read_sectors
	jc	9f
	call	copy_bounce
	jc	9f
	movw	$msg_dot, %si
	call	print
	addl	%ebx, %eax
	shll	$9, %ebx
	addl	%ebx, %edi
	cmpl	%ebx, %ecx
	jbe	8f
	subl	%ebx, %ecx
	jmp	1b
8:	popal
	clc
	ret
9:	popal
	stc
	ret

/* Read %bx sectors from disk sector %eax into the bounce buffer */
read_sectors:
	pushal
	movw	$4, rd_tries
1:	movw	$0x0010, dap
	movw	%bx, dap_count
	movw	$0, dap_offset
	movw	$BOUNCE_SEG, dap_segment
	movl	%eax, dap_lba
	movl	$0, dap_lba + 4
	pushl	%eax
	movw	$dap, %si
	movb	boot_drive, %dl
	movb	$0x42, %ah
	int	$0x13
	popl	%eax
	jnc	2f
	decw	rd_tries
	jz	3f
	xorb	%ah, %ah		/* reset and retry */
	movb	boot_drive, %dl
	int	$0x13
	jmp	1b
2:	popal
	clc
	ret
3:	popal
	stc
	ret

/* Copy %bx sectors from the bounce buffer to physical address %edi */
copy_bounce:
	pushal
	movl	%edi, %eax
	movw	%ax, gdt_dst_base_low
	shrl	$16, %eax
	movb	%al, gdt_dst_base_mid
	movb	%ah, gdt_dst_base_high
	movw	%bx, %cx
	shlw	$8, %cx			/* words */
	pushw	%ds
	popw	%es
	movw	$gdt, %si
	movb	$0x87, %ah
	int	$0x15
	sti
	popal
	ret

print:
	pushal
1:	lodsb
	testb	%al, %al
	jz	2f
	call	putc
	jmp	1b
2:	popal
	ret

putc:
	pushal
	movb	$0x0e, %ah
	movw	$0x0007, %bx
	int	$0x10
	popal
	ret

msg_banner:
	.asciz	"\r\nBoot2Flipper BIOS loader\r\n"
msg_kernel:
	.asciz	"Loading IPXE.LKR "
msg_initrd:
	.asciz	"Loading BOOT.CFG "
msg_dot:
	.asciz	"."
msg_ok:
	.asciz	" ok\r\n"
msg_boot:
	.asciz	"Starting iPXE...\r\n"
msg_disk_error:
	.asciz	"\r\nDisk read error"
msg_bad_kernel:
	.asciz	"\r\nIPXE.LKR is missing or not a valid ipxe.lkrn"
msg_any_key:
	.asciz	"\r\nPress any key to boot the next device\r\n"

	.balign	8
/* INT 15h AH=87h descriptor table */
gdt:
	.quad	0, 0
	/* source: bounce buffer */
	.word	0xffff
	.word	BOUNCE_ADDR & 0xffff
	.byte	(BOUNCE_ADDR >> 16) & 0xff
	.byte	0x93
	.byte	0
	.byte	(BOUNCE_ADDR >> 24) & 0xff
	/* destination */
	.word	0xffff
gdt_dst_base_low:
	.word	0
gdt_dst_base_mid:
	.byte	0
	.byte	0x93
	.byte	0
gdt_dst_base_high:
	.byte	0
	.quad	0, 0

	.balign	4
dap:
	.word	0
dap_count:
	.word	0
dap_offset:
	.word	0
dap_segment:
	.word	0
dap_lba:
	.quad	0
rd_tries:
	.word	0

	.org	512 + STAGE2_SECTORS * 512

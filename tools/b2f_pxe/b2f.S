/*
 * b2f.pxe - write the Boot2Flipper .B2F files from BIOS iPXE
 *
 * A PXE NBP that returns to iPXE. The Boot2Flipper BIOS loader passes it to
 * ipxe.lkrn as the initrd, which iPXE registers as the image "<INITRD>":
 *
 *   imgexec <INITRD> status Booting kernel...\nPlease wait
 *   imgexec <INITRD> frame http://server/logo.bin
 *   imgexec <INITRD> clear
 *
 * The arguments come from iPXE's PXENV_FILE_CMDLINE, the frame is read with
 * PXENV_FILE_OPEN/READ (any URI iPXE can open). The Flipper is found by the
 * "B2F INFO" header at B2F_INFO_LBA, read with INT 13h extensions, and the
 * file LBAs are read from it. Returns AX = 0 on success.
 *
 * Rebuild src/b2f/b2f_pxe_image.h with tools/b2f_pxe/build.sh.
 */

	.code16
	.text
	.globl	_start

	.set	B2F_INFO_LBA, 0x2002
	.set	FRAME_SIZE, 1024

	.set	PXENV_FILE_OPEN, 0x00e0
	.set	PXENV_FILE_CLOSE, 0x00e1
	.set	PXENV_FILE_READ, 0x00e3
	.set	PXENV_FILE_CMDLINE, 0x00e8

/* iPXE calls the NBP at 0000:7c00 with ES:BX pointing to PXENV+ */
_start:
	pushal
	pushw	%ds
	pushw	%es
	movw	%es:10(%bx), %ax	/* PXENV+ RMEntry offset */
	movw	%es:12(%bx), %dx	/* and segment */
	xorw	%cx, %cx
	movw	%cx, %ds
	movw	%cx, %es
	cld
	movw	%ax, pxe_entry

	/* .bss is not part of the image, clear it */
	pushw	%ax
	movw	$__bss_start, %di
	movw	$_end, %cx
	subw	%di, %cx
	xorb	%al, %al
	rep	stosb
	popw	%ax
	movw	%dx, pxe_entry + 2

	call	main
	movw	%ax, exit_code

	popw	%es
	popw	%ds
	popal
	movw	%cs:exit_code, %ax
	lret

main:
	/* Arguments, NUL terminated */
	movw	$cmdline_params, %di
	movw	$PXENV_FILE_CMDLINE, %bx
	call	pxe_call
	movw	$cmdline, %si
	call	skip_spaces

	/* iPXE passes only the arguments, but skip a leading name too */
	call	find_command
	jnc	1f
	call	skip_word
	call	find_command
	jc	usage
1:	pushw	%ax			/* command handler */
	call	skip_word		/* %si = its argument */

	call	find_disk
	jc	no_disk
	ret				/* to the handler */

usage:
	movw	$msg_usage, %si
	jmp	fail
no_disk:
	popw	%ax
	movw	$msg_no_disk, %si
fail:
	call	print
	movw	$1, %ax
	ret

/* Handlers: %si = argument, return %ax = exit code */
cmd_status:
	movw	$sector, %di
	call	zero_sector
	movw	$sector, %di
	movw	$511, %cx		/* keep a NUL at the end */
1:	lodsb
	testb	%al, %al
	jz	3f
	cmpb	$'\\', %al		/* "\n" starts a new line */
	jne	2f
	cmpb	$'n', (%si)
	jne	2f
	incw	%si
	movb	$'\n', %al
2:	stosb
	loop	1b
3:	movl	status_lba, %eax
	movw	$1, %cx
	movw	$sector, %bx
	jmp	write_sectors

cmd_clear:
	movw	$frame, %di
	call	zero_sector
	movw	$frame + 512, %di
	call	zero_sector
	movl	frame_lba, %eax
	movw	$2, %cx
	movw	$frame, %bx
	call	write_sectors
	testw	%ax, %ax
	jnz	1f
	movw	$cmdline + 255, %si	/* empty string */
	jmp	cmd_status
1:	ret

cmd_frame:
	/* NUL terminate the URI */
	movw	%si, file_name
	movw	%si, %di
1:	cmpb	$' ', (%di)
	jbe	2f
	incw	%di
	jmp	1b
2:	movb	$0, (%di)

	movw	$0, open_status
	movw	$open_params, %di
	movw	$PXENV_FILE_OPEN, %bx
	call	pxe_call
	testw	%ax, %ax
	jnz	frame_open_error
	movw	open_handle, %ax
	movw	%ax, read_handle
	movw	%ax, close_handle

	/* Read until EOF or a full frame */
	movw	$frame, %dx
3:	movw	$FRAME_SIZE + frame, %cx
	subw	%dx, %cx
	jz	4f
	movw	%cx, read_size
	movw	%dx, read_buffer
	movw	$read_params, %di
	movw	$PXENV_FILE_READ, %bx
	call	pxe_call
	testw	%ax, %ax
	jnz	4f
	movw	read_size, %cx
	jcxz	4f			/* EOF */
	addw	%cx, %dx
	jmp	3b
4:	pushw	%dx
	movw	$close_params, %di
	movw	$PXENV_FILE_CLOSE, %bx
	call	pxe_call
	popw	%dx
	cmpw	$FRAME_SIZE + frame, %dx
	jne	frame_size_error

	movl	frame_lba, %eax
	movw	$2, %cx
	movw	$frame, %bx
	jmp	write_sectors

frame_open_error:
	movw	$msg_open, %si
	jmp	fail
frame_size_error:
	movw	$msg_size, %si
	jmp	fail

/* Find the command word at %si: CF clear and %ax = handler if found */
find_command:
	movw	$commands, %bx
1:	movw	(%bx), %di		/* name */
	testw	%di, %di
	jz	4f
	pushw	%si
2:	movb	(%di), %al
	testb	%al, %al
	jz	3f
	cmpb	(%si), %al
	jne	5f
	incw	%si
	incw	%di
	jmp	2b
3:	cmpb	$' ', (%si)		/* word must end here */
	ja	5f
	popw	%si
	movw	2(%bx), %ax
	clc
	ret
5:	popw	%si
	addw	$4, %bx
	jmp	1b
4:	stc
	ret

skip_word:
1:	cmpb	$' ', (%si)
	jbe	skip_spaces
	incw	%si
	jmp	1b
skip_spaces:
1:	cmpb	$' ', (%si)
	jne	2f
	incw	%si
	jmp	1b
2:	ret

/* Find the Flipper: CF clear with drive, status_lba and frame_lba set */
find_disk:
	pushw	%si
	movb	$0x80, %dl
1:	movb	%dl, drive
	movb	$0x41, %ah
	movw	$0x55aa, %bx
	int	$0x13
	jc	3f
	cmpw	$0xaa55, %bx
	jne	3f
	testb	$1, %cl			/* extended disk access */
	jz	3f
	movl	$B2F_INFO_LBA, %eax
	movw	$1, %cx
	movw	$sector, %bx
	movb	$0x42, %dh
	call	disk_io
	jc	3f
	movw	$sector, %si
	movw	$magic, %di
	movw	$8, %cx
	repe	cmpsb
	jne	3f
	call	parse_info
	jnc	4f
3:	movb	drive, %dl
	incb	%dl
	cmpb	$0x90, %dl
	jb	1b
	stc
4:	popw	%si
	ret

/* Read status_lba and frame_lba from the info header in sector */
parse_info:
	movl	$0, status_lba
	movl	$0, frame_lba
	movw	sector + 10, %cx	/* entry count */
	movw	$sector + 16, %bx
1:	jcxz	3f
	cmpw	$sector + 512, %bx
	jae	3f
	movw	%bx, %si
	movw	$name_status, %di
	call	name_equal
	jne	2f
	movl	16(%bx), %eax
	movl	%eax, status_lba
2:	movw	%bx, %si
	movw	$name_frame, %di
	call	name_equal
	jne	2f
	movl	16(%bx), %eax
	movl	%eax, frame_lba
2:	addw	$32, %bx
	decw	%cx
	jmp	1b
3:	cmpl	$0, status_lba
	je	4f
	cmpl	$0, frame_lba
	je	4f
	clc
	ret
4:	stc
	ret

/* ZF set if the NUL terminated names at %si and %di match */
name_equal:
	pushw	%cx
1:	movb	(%di), %al
	cmpb	(%si), %al
	jne	2f
	incw	%si
	incw	%di
	testb	%al, %al
	jnz	1b
2:	popw	%cx
	ret

/* Write %cx sectors from %bx to %eax, %ax = exit code */
write_sectors:
	movb	$0x43, %dh
	call	disk_io
	jc	1f
	movw	$0, %ax
	ret
1:	movw	$msg_write, %si
	jmp	fail

/* INT 13h extended read (%dh = 0x42) or write (0x43) of %cx sectors at
 * %eax to/from %bx, on drive. CF set on error. */
disk_io:
	pushal
	movw	%cx, dap_count
	movw	%bx, dap_offset
	movl	%eax, dap_lba
	movb	%dh, %bl
	movb	$4, %bh			/* tries */
1:	movb	%bl, %ah
	xorb	%al, %al		/* no verify */
	movb	drive, %dl
	movw	$dap, %si
	int	$0x13
	jnc	2f
	decb	%bh
	jz	3f
	xorb	%ah, %ah		/* reset and retry */
	movb	drive, %dl
	int	$0x13
	jmp	1b
2:	popal
	clc
	ret
3:	popal
	stc
	ret

/* Call the PXE API: %bx = opcode, %ds:%di = parameters. %ax = status */
pxe_call:
	pushw	%ds
	pushw	%es
	pushw	%ds
	popw	%es
	lcall	*pxe_entry
	popw	%es
	popw	%ds
	ret

zero_sector:
	pushw	%cx
	xorw	%ax, %ax
	movw	$256, %cx
	rep	stosw
	popw	%cx
	ret

print:
	pushal
1:	lodsb
	testb	%al, %al
	jz	3f
	cmpb	$'\n', %al
	jne	2f
	movb	$'\r', %al
	call	putc
	movb	$'\n', %al
2:	call	putc
	jmp	1b
3:	popal
	ret

putc:
	pushal
	movb	$0x0e, %ah
	movw	$0x0007, %bx
	int	$0x10
	popal
	ret

commands:
	.word	name_status, cmd_status
	.word	name_frame, cmd_frame
	.word	name_clear, cmd_clear
	.word	0

magic:
	.ascii	"B2F INFO"
name_status:
	.asciz	"status"
name_frame:
	.asciz	"frame"
name_clear:
	.asciz	"clear"
msg_usage:
	.asciz	"usage: b2f.pxe status <text> | frame <uri> | clear\n"
msg_no_disk:
	.asciz	"b2f: Flipper disk not found\n"
msg_open:
	.asciz	"b2f: cannot open the frame\n"
msg_size:
	.asciz	"b2f: frame must be 1024 bytes\n"
msg_write:
	.asciz	"b2f: write failed\n"

	.balign	4
pxe_entry:
	.word	0, 0
exit_code:
	.word	0
drive:
	.byte	0
	.balign	4
status_lba:
	.long	0
frame_lba:
	.long	0

dap:
	.byte	0x10, 0
dap_count:
	.word	0
dap_offset:
	.word	0
dap_segment:
	.word	0
dap_lba:
	.long	0, 0

cmdline_params:
	.word	0			/* Status */
	.word	255			/* BufferSize */
	.word	cmdline, 0		/* Buffer */

open_params:
open_status:
	.word	0
open_handle:
	.word	0
file_name:
	.word	0, 0
	.long	0

read_params:
	.word	0
read_handle:
	.word	0
read_size:
	.word	0
read_buffer:
	.word	0, 0

close_params:
	.word	0
close_handle:
	.word	0

	.bss
	.balign	16
cmdline:
	.space	256
sector:
	.space	512
frame:
	.space	FRAME_SIZE

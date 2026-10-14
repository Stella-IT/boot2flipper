/*
 * b2f.pxe - write the Boot2Flipper .B2F files from BIOS iPXE
 *
 * A PXE NBP that returns to iPXE. The Boot2Flipper BIOS loader passes it to
 * ipxe.lkrn as the initrd, which iPXE registers as the image "<INITRD>":
 *
 *   imgexec <INITRD> status Booting kernel...\nPlease wait
 *   imgexec <INITRD> frame http://server/logo.bin
 *   imgexec <INITRD> mode framebuffer
 *   imgexec <INITRD> clear
 *
 * and to ask the Flipper's user (see .B2F/prompt/ in src/b2f/b2f_dir.h):
 *
 *   imgexec <INITRD> question Which OS?
 *   imgexec <INITRD> options Ubuntu|Debian|Windows
 *   imgexec <INITRD> kbdtype ip
 *   imgexec <INITRD> timeout 30
 *   imgexec <INITRD> prompt choice
 *
 * prompt waits for the answer and sets the iPXE setting b2f_result to it with
 * PXENV_FILE_EXEC. If the user cancelled or the time ran out, b2f_result is
 * "cancel" or "timeout" and prompt fails.
 *
 *   imgexec <INITRD> sysinfo platform=${platform}\narch=${buildarch}
 *   imgexec <INITRD> log Some line for the Flipper's log file
 *   imgexec <INITRD> exec
 *
 * exec runs the answer of the last prompt as an iPXE command, with
 * PXENV_FILE_EXEC, and fails if the command fails or there is no answer.
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
	.set	PXENV_FILE_EXEC, 0x00e5
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
	movl	status_lba, %eax
	jmp	write_line_file

cmd_mode:
	movl	mode_lba, %eax
	jmp	write_line_file

cmd_sysinfo:
	movl	sysinfo_lba, %eax
	jmp	write_optional_line
cmd_log:
	movl	log_lba, %eax
write_optional_line:
	testl	%eax, %eax
	jnz	write_line_file
	movw	$msg_no_file, %si
	jmp	fail

/* Write the text at %si to the 1 sector file at %eax */
write_line_file:
	movw	$1, %dx
	movb	$0, pipe_newline
	jmp	write_text

/* Write the text at %si to the file at %eax of %dx sectors (1 or 2). "\n"
 * starts a new line, and so does '|' if pipe_newline is set. */
write_text:
	pushl	%eax
	movw	$frame, %di
	call	zero_sector
	movw	$frame + 512, %di
	call	zero_sector
	movw	%dx, %cx
	shlw	$9, %cx
	decw	%cx			/* keep a NUL at the end */
	movw	$frame, %di
1:	lodsb
	testb	%al, %al
	jz	3f
	cmpb	$'|', %al
	jne	4f
	cmpb	$0, pipe_newline
	je	2f
	movb	$'\n', %al
	jmp	2f
4:	cmpb	$'\\', %al		/* "\n" starts a new line */
	jne	2f
	cmpb	$'n', (%si)
	jne	2f
	incw	%si
	movb	$'\n', %al
2:	stosb
	loop	1b
3:	popl	%eax
	movw	%dx, %cx
	movw	$frame, %bx
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
	movw	$empty, %si
	movl	status_lba, %eax
	call	write_line_file
	testw	%ax, %ax
	jnz	1f
	movw	$name_status, %si	/* back to status mode */
	movl	mode_lba, %eax
	call	write_line_file
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

/* Prompt files, on a Flipper that has them */
cmd_question:
	movl	question_lba, %eax
	jmp	write_prompt_line
cmd_kbdtype:
	movl	kbdtype_lba, %eax
	jmp	write_prompt_line
cmd_timeout:
	movl	timeout_lba, %eax
	jmp	write_prompt_line
cmd_options:
	call	check_prompt
	jc	no_prompt
	movl	options_lba, %eax
	movw	$2, %dx
	movb	$1, pipe_newline
	jmp	write_text
write_prompt_line:
	call	check_prompt
	jc	no_prompt
	jmp	write_line_file
no_prompt:
	movw	$msg_no_prompt, %si
	jmp	fail

/* CF set if the Flipper has no prompt files */
check_prompt:
	pushw	%si
	movw	$type_lba, %si
1:	cmpl	$0, (%si)
	je	2f
	addw	$4, %si
	cmpw	$info_lbas_end, %si
	jb	1b
	popw	%si
	clc
	ret
2:	popw	%si
	stc
	ret

/* Ask the prompt of type %si and wait for the answer */
cmd_prompt:
	call	check_prompt
	jc	no_prompt
	movl	type_lba, %eax
	call	write_line_file
	testw	%ax, %ax
	jnz	9f
	movw	$name_prompt, %si
	movl	mode_lba, %eax
	call	write_line_file
	testw	%ax, %ax
	jnz	9f
	movw	$msg_waiting, %si
	call	print

1:	movl	answer_lba, %eax
	movw	$1, %cx
	movw	$sector, %bx
	movb	$0x42, %dh
	call	disk_io
	jc	prompt_read_error
	movw	$sector, %si
	movw	$answer_ok, %di
	call	starts_with
	je	2f
	movw	$sector, %si
	movw	$answer_cancel, %di
	call	starts_with
	je	prompt_cancelled
	movw	$sector, %si
	movw	$answer_timeout, %di
	call	starts_with
	je	prompt_timed_out
	movb	$0x86, %ah		/* wait 100ms */
	movw	$0x0001, %cx
	movw	$0x86a0, %dx
	int	$0x15
	jmp	1b

	/* The answer is the line after "ok" */
2:	movw	$sector + 3, %si
	call	set_result
	jc	prompt_exec_error
	xorw	%ax, %ax
9:	ret

prompt_read_error:
	movw	$msg_read, %si
	jmp	fail
prompt_cancelled:
	movw	$name_cancel, %si
	call	set_result
	movw	$msg_cancelled, %si
	jmp	fail
prompt_timed_out:
	movw	$name_timeout, %si
	call	set_result
	movw	$msg_timed_out, %si
	jmp	fail
prompt_exec_error:
	movw	$msg_exec, %si
	jmp	fail

/* Run the answer of the last prompt as an iPXE command */
cmd_exec:
	call	check_prompt
	jc	no_prompt
	movl	answer_lba, %eax
	movw	$1, %cx
	movw	$sector, %bx
	movb	$0x42, %dh
	call	disk_io
	jc	prompt_read_error
	movw	$sector, %si
	movw	$answer_ok, %di
	call	starts_with
	jne	1f
	movw	$sector + 3, %si
	movw	$empty, %bx
	call	exec_line
	jc	2f
	xorw	%ax, %ax
	ret
1:	movw	$msg_no_answer, %si
	jmp	fail
2:	movw	$1, %ax			/* the command printed why */
	ret

/* Run "set b2f_result <the line at %si>" in iPXE, CF set on error */
set_result:
	movw	$set_result_cmd, %bx
/* Run the NUL terminated text at %bx followed by the line at %si in iPXE,
 * CF set on error */
exec_line:
	pushw	%si
	movw	%bx, %si
	movw	$cmdline, %di
1:	lodsb
	testb	%al, %al
	jz	2f
	stosb
	jmp	1b
2:	popw	%si
	movw	$cmdline + 255, %cx
	subw	%di, %cx
3:	lodsb
	cmpb	$'\n', %al
	je	4f
	testb	%al, %al
	jz	4f
	stosb
	loop	3b
4:	movb	$0, (%di)
	movw	$exec_params, %di
	movw	$PXENV_FILE_EXEC, %bx
	call	pxe_call
	testw	%ax, %ax
	jnz	5f
	clc
	ret
5:	stc
	ret

/* ZF set if the text at %si starts with the NUL terminated text at %di */
starts_with:
1:	movb	(%di), %al
	testb	%al, %al
	jz	2f
	cmpb	(%si), %al
	jne	2f
	incw	%si
	incw	%di
	jmp	1b
2:	ret

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

/* Read the file LBAs from the info header in sector. CF set if status,
 * frame or mode is missing; the prompt LBAs are 0 if missing. */
parse_info:
	movw	$status_lba, %di
	movw	$(info_lbas_end - status_lba) / 2, %cx
	xorw	%ax, %ax
	rep	stosw
	movw	sector + 10, %cx	/* entry count */
	movw	$sector + 16, %bx
1:	jcxz	3f
	cmpw	$sector + 512, %bx
	jae	3f
	movw	$info_fields, field
2:	movw	field, %si
	movw	(%si), %di		/* field name */
	testw	%di, %di
	jz	4f
	movw	2(%si), %dx		/* its LBA variable */
	movw	%bx, %si
	call	name_equal
	jne	5f
	movl	16(%bx), %eax
	movw	%dx, %si
	movl	%eax, (%si)
5:	addw	$4, field
	jmp	2b
4:	addw	$32, %bx
	decw	%cx
	jmp	1b
3:	cmpl	$0, status_lba
	je	6f
	cmpl	$0, frame_lba
	je	6f
	cmpl	$0, mode_lba
	je	6f
	clc
	ret
6:	stc
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
	.word	name_mode, cmd_mode
	.word	name_clear, cmd_clear
	.word	name_question, cmd_question
	.word	name_options, cmd_options
	.word	name_kbdtype, cmd_kbdtype
	.word	name_timeout, cmd_timeout
	.word	name_prompt, cmd_prompt
	.word	name_sysinfo, cmd_sysinfo
	.word	name_log, cmd_log
	.word	name_exec, cmd_exec
	.word	0

/* Info header entries and where their LBA goes */
info_fields:
	.word	name_status, status_lba
	.word	name_frame, frame_lba
	.word	name_mode, mode_lba
	.word	name_sysinfo, sysinfo_lba
	.word	name_log, log_lba
	.word	name_info_type, type_lba
	.word	name_info_question, question_lba
	.word	name_info_options, options_lba
	.word	name_info_kbdtype, kbdtype_lba
	.word	name_info_answer, answer_lba
	.word	name_info_timeout, timeout_lba
	.word	0

magic:
	.ascii	"B2F INFO"
name_status:
	.asciz	"status"
name_frame:
	.asciz	"frame"
name_mode:
	.asciz	"mode"
name_clear:
	.asciz	"clear"
name_question:
	.asciz	"question"
name_options:
	.asciz	"options"
name_kbdtype:
	.asciz	"kbdtype"
name_prompt:
	.asciz	"prompt"
name_timeout:
	.asciz	"timeout"
name_sysinfo:
	.asciz	"sysinfo"
name_log:
	.asciz	"log"
name_exec:
	.asciz	"exec"
name_cancel:
	.asciz	"cancel"
name_info_type:
	.asciz	"prompt/type"
name_info_question:
	.asciz	"prompt/question"
name_info_options:
	.asciz	"prompt/options"
name_info_kbdtype:
	.asciz	"prompt/kbdtype"
name_info_answer:
	.asciz	"prompt/answer"
name_info_timeout:
	.asciz	"prompt/timeout"
answer_ok:
	.asciz	"ok\n"
answer_cancel:
	.asciz	"cancel\n"
answer_timeout:
	.asciz	"timeout\n"
set_result_cmd:
	.asciz	"set b2f_result "
empty:
	.byte	0
msg_usage:
	.ascii	"usage: b2f.pxe status <text> | frame <uri> | mode <mode> | clear |\n"
	.ascii	"       question <text> | options <a|b|c> | kbdtype <type> |\n"
	.ascii	"       timeout <seconds> | prompt <type> | exec |\n"
	.asciz	"       sysinfo <text> | log <text>\n"
msg_no_prompt:
	.asciz	"b2f: this Flipper has no prompt\n"
msg_no_file:
	.asciz	"b2f: this Flipper has no sysinfo or log\n"
msg_no_answer:
	.asciz	"b2f: no answer to run\n"
msg_waiting:
	.asciz	"b2f: answer on the Flipper\n"
msg_cancelled:
	.asciz	"b2f: cancelled\n"
msg_timed_out:
	.asciz	"b2f: timed out\n"
msg_read:
	.asciz	"b2f: read failed\n"
msg_exec:
	.asciz	"b2f: cannot set b2f_result\n"
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
mode_lba:
	.long	0
sysinfo_lba:
	.long	0
log_lba:
	.long	0
type_lba:
	.long	0
question_lba:
	.long	0
options_lba:
	.long	0
kbdtype_lba:
	.long	0
answer_lba:
	.long	0
timeout_lba:
	.long	0
info_lbas_end:
field:
	.word	0
pipe_newline:
	.byte	0
	.balign	2

exec_params:
	.word	0			/* Status */
	.word	cmdline, 0		/* Command */

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

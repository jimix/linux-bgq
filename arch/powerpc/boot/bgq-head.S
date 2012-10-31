#include "ppc_asm.h"
#define STACK_FRAME_64 128

	.machine "ppc64"

	.text
/*
 * Blue Gene firmware jumps to 0x10, see .bgq.tramp below, so we arange
 * for that trampoline to end up here.  We enter here in 64 bit mode so
 * we need to switch to 32 bit mode.
 */
	. = 0x0
	.globl __bgq_start_entry
__bgq_start_entry:
	/*
	 * R3 has the Firmware info block, which is likely beyond the
	 * reach of 32bit programs, so we grab the version and the
	 * CRC now (in r5 and r6 respectively) then pass the 64bit
	 * address in r3 and r4
	 */
	lwz	r5,4(r3)
	lwz	r6,0(r3)

	/* spread original R3 over R3 and R4 */
	rldicl	r4,r3,0,32	# r4 = r3 & 0xffff
	srdi	r3,r3,32	# r3 >>= 32

	/* switch to 32-bit mode */
	mfmsr	r9
	rlwinm	r9,r9,0,1,31
	mtmsr	r9
	isync
	b	_zimage_start

	/* fake a function descriptor for some loaders */
	.globl __bgq_start
__bgq_start:
	.long 0
	.long __bgq_start_entry
	.long 0
	.long 0

	/*
	 * int bgq_fw_call(ulong r3, ulong r4, ulong r5, ulong r6,
	 * 		   unsigned long offset,
	 *		   u32 fw_hi, u32 fw_lo);
	 * Calls a 64bit ABI function descriptor in 64bit mode
	 */
	.globl bgq_fw_call
bgq_fw_call:
	/* store LR into LRSAVE */
	mflr	r0
	stw	r0,4(r1)
	/* store R2 into stack */
	stw	r2,-4(r1)
	/* buy a new 64 bit stack frame */
	stdu	r1,-STACK_FRAME_64(r1)
	/* switch to 64bit mode */
	mfmsr	r10
	oris	r10,r10,0x8000
	mtmsr	r10
	isync

	/* convert R8 and R9 into a 64bit value */
	rldicl	r9,r9,0,32     # r9 &= 0xffffffff
	sldi	r8,r8,32       # r8 <<= 32
	or	r8,r8,r9       # r8 |= r9
	/* add offset in R7 to the 64bit address in R8 */
	add	r7,r7,r8
	/* load the file descriptor address */
	ld	r7,0(r7)
	/* load the TOC from the function descriptor in R7 */
	ld	r2,8(r7)
	/* load the function entry point into LR */
	ld	r8,0(r7)
	mtlr	r8
	/* make sure the arguments have clean upper bits */
	rldicl	r3,r3,0,32
	rldicl	r4,r4,0,32
	rldicl	r5,r5,0,32
	rldicl	r6,r6,0,32
	/* call C function */
	blrl
	nop
	addi	r1,r1,STACK_FRAME_64
	/* restore R2 */
	lwz	r2,-4(r1)
	/* restore LR */
	lwz	r0,4(r1)
	mtlr	r0
	/* switch back to 32-bit mode */
	mfmsr	r10
	rlwinm	r10,r10,0,1,31
	mtmsr	r10
	isync

	blr


	.globl bgq_kexec_primary_thread
bgq_kexec_primary_thread:
	li	r4,0
1:	li	r0,0	/* keep a 0 around, also where kexec lives */

	/*
	 * Normally we would simply leap at 0x60, unfortunately
	 * our cores are multi-threaded and the primary thread will
	 * remove the 0-maps-0 translation we are standing on when
	 * its cleaning up.  Since we do not have the magic of real
	 * mode that book3s has, we arrange for all the threads to
	 * run and spin on the ultimate virtual address.
	 */
#define SPRN_MAS0	0x270
#define SPRN_MAS1	0x271
#define SPRN_MAS0_MAS1	0x175
#define SPRN_MAS2	0x272
#define SPRN_MAS5_MAS6	0x15c
#define SPRN_MAS8	0x155

#define MAS0_HES		0x00004000
#define MAS0_ESEL_SHIFT		16
#define MAS1_IPROT		0x40000000

/* YES, my compiler does not seem to recognize these instructions */
#define TLBSX(r)	.long 0x7c000724 | ((r)<<10)
#define TLBWE		.long 0x7c0007a4

#define KERNEL_START CONFIG_KERNEL_START

	/* probably don't have to do this, but I'm paranoid */
	mtspr	SPRN_MAS5_MAS6,r0
	mtspr	SPRN_MAS8,r0

	/* get entry of the kexec stub, which happens to be at 0 */
	mtspr	SPRN_MAS0_MAS1,r0
	TLBSX(r0)

	/* write it to 3 because that is where A2 will replace it */
	lis	r5,(3 << MAS0_ESEL_SHIFT)@h
	mtspr	SPRN_MAS0,r5

	/* grab the upper bits of KERNEL_START and insert into the EPN */
	lis	r7,(KERNEL_START >> 48)
	rldicr  r7,r7,32,31
	mfspr	r6,SPRN_MAS2
	or	r6,r6,r7
	mtspr	SPRN_MAS2,r6
	TLBWE
	isync

	/* now launch at the kexec vector using r7 as a base */
2:	ori	r7,r7,0x60
	mtctr	r7
	bctr

	/* arrange for r4 to non-zero and load up r7 */
	.globl bgq_kexec_secondary_thread
bgq_kexec_secondary_thread:
	li	r4,1
	lis	r7,(KERNEL_START >> 48)
	rldicr  r7,r7,32,31
	b	2b

	/*
	 * The second thread of the first core is special since we
	 * need it to install the alias mapping above.  If we did
	 * this with the boting thread then we would have to switch
	 * back and forth to 64bit mode, I think this is way easier.
	 */
	.globl bgq_kexec_thread_0_1
bgq_kexec_thread_0_1:
	li	r4,1
	b	1b


/*
 * The wrapper will arrange for this to be located at location 0
 * when we objcopy from 32bit to 64bit image.
 * The HW loader (only takes 64bit elf) will leap at offset 0x10.
 * NOTE: the 32bit image will not have this tramp relocated
 */
	.section .text.bgq_tramp, "ax"
	. = 0x10
	lis	r9,__bgq_start_entry@h
	ori	r9,r9,__bgq_start_entry@l
	mtlr	r9
	blr

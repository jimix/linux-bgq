#include <asm/reg.h>
#include <asm/exception-64e.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

/* we can use EX_R14 to save CTR */
#define EX_CTR EX_R14

/* This can only be 8 instruction so have to split up the handeling */
#define EXCEPTION_BGQ(loc, type)			\
	. = loc;					\
	mtspr	SPRN_SPRG_##type##_SCRATCH,r13;		\
	mfspr	r13,SPRN_SPRG_PACA;	/* get PACA */	\
	/* save r10 and r11 */				\
	std	r10,PACA_EX##type+EX_R10(r13);		\
	std	r11,PACA_EX##type+EX_R11(r13);		\
	/* load the low half of the of the handler */	\
	lis	r10,bgq_interrupt_##loc@h;		\
	ori	r10,r10,bgq_interrupt_##loc@l;		\
	b	bgq_exception_cont_##type

/* we continue the above macro with this code, it is 8 instructions */
#define EXCEPTION_BGQ_CONT(type)			\
bgq_exception_cont_##type:;				\
	/* r10 holds the low half of address */		\
	/* r11 is saved */				\
	/* get the high half of the address */		\
	lis     r11,bgq_exception_cont@highest;		\
	ori     r11,r11,bgq_exception_cont@higher;	\
	rldicr  r11,r11,32,31;				\
	/* combine hi and lo and free up r11 */		\
	or	r10,r11,r10;				\
	/* save CTR */					\
	mfctr	r11;					\
	std	r11,PACA_EX##type+EX_CTR(r13);		\
	/* leap at r10 using the CTR */			\
	mtctr	r10;					\
	bctr

#define EXCEPTION_STUB(loc, type, label)		\
bgq_interrupt_##loc:;					\
	/* restore ctr */				\
	ld	r10,PACA_EX##type+EX_CTR(r13);		\
	mtctr	r10;					\
	/* restore everything else */			\
	ld	r11,PACA_EX##type+EX_R11(r13);		\
	ld	r10,PACA_EX##type+EX_R10(r13);		\
	mfspr	r13, SPRN_SPRG_##type##_SCRATCH;	\
	b	exc_##label##_book3e

	.balign 0x1000
	. = 0
	.globl bgq_interrupt_base_book3e
bgq_interrupt_base_book3e:
	EXCEPTION_BGQ(0x000, MC)
	EXCEPTION_BGQ(0x020, CRIT)
	EXCEPTION_BGQ(0x040, DBG)
	EXCEPTION_BGQ(0x060, GEN)
	EXCEPTION_BGQ(0x080, GEN)
	EXCEPTION_BGQ(0x0a0, GEN)
	EXCEPTION_BGQ(0x0c0, GEN)
	EXCEPTION_BGQ(0x0e0, GEN)
	EXCEPTION_BGQ(0x100, GEN)
	EXCEPTION_BGQ(0x120, GEN)
	EXCEPTION_BGQ(0x140, GEN)
	EXCEPTION_BGQ(0x160, GEN)
	EXCEPTION_BGQ(0x180, GEN)
	EXCEPTION_BGQ(0x1a0, GEN)
	EXCEPTION_BGQ(0x1c0, GEN)
	EXCEPTION_BGQ(0x1e0, GEN)
	EXCEPTION_BGQ(0x260, GEN)
	EXCEPTION_BGQ(0x280, GEN)
	EXCEPTION_BGQ(0x2a0, GEN)
	EXCEPTION_BGQ(0x2c0, GEN)
	EXCEPTION_BGQ(0x2e0, GEN)
	EXCEPTION_BGQ(0x300, GEN)
	EXCEPTION_BGQ(0x320, GEN)

	/*
	 * We need this at the end of 4K
	 * 4 continuations * 8 instructions * 4 bytes per insn
	 */
	. = 0x1000 - (4 * 8 * 4)
bgq_exception_cont:
	EXCEPTION_BGQ_CONT(MC)
	EXCEPTION_BGQ_CONT(CRIT)
	EXCEPTION_BGQ_CONT(DBG)
	EXCEPTION_BGQ_CONT(GEN)

	. = 0x1000
	.globl bgq_interrupt_end_book3e
bgq_interrupt_end_book3e:
	EXCEPTION_STUB(0x000, MC, machine_check)
	EXCEPTION_STUB(0x020, CRIT, critical_input)
	EXCEPTION_STUB(0x040, DBG, debug_crit)
	EXCEPTION_STUB(0x060, GEN, data_storage)
	EXCEPTION_STUB(0x080, GEN, instruction_storage)
	EXCEPTION_STUB(0x0a0, GEN, external_input)
	EXCEPTION_STUB(0x0c0, GEN, alignment)
	EXCEPTION_STUB(0x0e0, GEN, program)
	EXCEPTION_STUB(0x100, GEN, fp_unavailable)
	EXCEPTION_STUB(0x120, GEN, system_call)
	EXCEPTION_STUB(0x140, GEN, ap_unavailable)
	EXCEPTION_STUB(0x160, GEN, decrementer)
	EXCEPTION_STUB(0x180, GEN, fixed_interval)
	EXCEPTION_STUB(0x1a0, GEN, watchdog)
	EXCEPTION_STUB(0x1c0, GEN, data_tlb_miss)
	EXCEPTION_STUB(0x1e0, GEN, instruction_tlb_miss)
	EXCEPTION_STUB(0x260, GEN, perfmon)
	EXCEPTION_STUB(0x280, GEN, doorbell)
	EXCEPTION_STUB(0x2a0, GEN, doorbell_crit)
	EXCEPTION_STUB(0x2c0, GEN, guest_doorbell)
	EXCEPTION_STUB(0x2e0, GEN, guest_doorbell_crit)
	EXCEPTION_STUB(0x300, GEN, hypercall)
	EXCEPTION_STUB(0x320, GEN, ehpriv)

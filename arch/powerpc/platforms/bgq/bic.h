
#define BGQ_DCR_GEA_INTERRUPT_MAP0 0
#define BGQ_DCR_GEA_INTERRUPT_MAP(x) (BGQ_DCR_GEA_INTERRUPT_MAP0 + (x))

#define BGQ_DCR_DEVBUS_CONTROL				0x0
#define BGQ_DCR_DEVBUS_INTERRUPT_STATE			0x10
#define BGQ_DCR_DEVBUS_INTERRUPT_STATE_CONTROL_LOW	0x11
#define BGQ_DCR_DEVBUS_INTERRUPT_STATE_CONTROL_HI	0x12

#define BGQ_IRQ_SPURIOUS	-1
#define BGQ_IRQ_IPI		0
#define BGQ_IRQ_INTA		1
#define BGQ_IRQ_INTB		2
#define BGQ_IRQ_INTC		3
#define BGQ_IRQ_INTD		4
#define BGQ_IRQ_MU1		8
#define BGQ_IRQ_MU2		9
#define BGQ_IRQ_MU3		10
#define BGQ_IRQ_MU4		11
#define BGQ_IRQ_TESTINT		12
#define BGQ_IRQ_COUNT		13

struct bgq_bic_puea {
	u64 _reserved[0x400];
	u64 clear_external_reg_0[4];
	u64 clear_critical_reg_0[4];
	u64 clear_wakeup_reg_0[4];
	u64 _hole_0[4];
	u64 clear_external_reg_1[4];
	u64 clear_critical_reg_1[4];
	u64 clear_wakeup_reg_1[4];
	u64 _hole_1[4];
	u64 set_external_reg_0[4];
	u64 set_critical_reg_0[4];
	u64 set_wakeup_reg_0[4];
	u64 _hole_2[4];
	u64 set_external_reg_1[4];
	u64 set_critical_reg_1[4];
	u64 set_wakeup_reg_1[4];
	u64 _hole_3[4];
	u64 interrupt_send;
	u64 _hole_4[7];
	u64 wakeup_send;
	u64 _hole_5[7];
	u64 map_interrupt[4];
	u64 ext_int_summary[4];
	u64 crit_int_summary[4];
	u64 mach_int_summary[4];
	u64 input_status;
};

/* C2C Packet Format - int_type decoding */
#define BGQ_BIC_C2C_INTTYPE_EXTERNAL	0
#define BGQ_BIC_C2C_INTTYPE_CRITICAL	1
#define BGQ_BIC_C2C_INTTYPE_WAKE	2

#define BGQ_PUEA_INT_SUMMARY_WU_MASK		0x0000004000000000ULL
#define BGQ_PUEA_INT_SUMMARY_MU_MASK		0x0000002fff800000ULL
#define BGQ_PUEA_INT_SUMMARY_GEA_MASK		0x00000000007ffffcULL
#define BGQ_IRQ_MSI_MASK_BIT0			0x0000000000040000ULL
#define BGQ_IRQ_MSI_MASK_BIT(x)		(BGQ_IRQ_MSI_MASK_BIT0 >> ((x) & 3))
#define BGQ_IRQ_TESTINT_MASK_BIT		0x0000000000002000ULL
#define BGQ_IRQ_DEVBUS_MASK_BIT			0x0000000000004000ULL
#define BQG_IRQ_C2C_INT_MASK_BIT0		0x0000000000000002ULL
#define BQG_IRQ_C2C_INT_MASK_BIT1		0x0000000000000001ULL
#define BGQ_IPI_MASK (BQG_IRQ_C2C_INT_MASK_BIT0 | BQG_IRQ_C2C_INT_MASK_BIT1)

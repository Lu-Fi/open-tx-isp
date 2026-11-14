#ifndef TX_ISP_T20_FSM_H
#define TX_ISP_T20_FSM_H

/* Common header of every T20 Apical firmware FSM object. */
struct t20_fsm_hdr {
	void *isp;		/* 0x00: owning apical ISP context */
	int32_t state;		/* 0x04: current state */
	int32_t pending_irq;	/* 0x08: pending interrupt mask */
};

static inline struct t20_fsm_hdr *t20_hdr(void *p)
{
	return (struct t20_fsm_hdr *)p;
}

#endif

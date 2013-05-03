/** @file libmme1536.h Header file for libmme1536.c
 * Contains hardware related definitions and function prototypes
 * 
 * @author Geoffrey Ottoy - DraMCo research group
 * @date 2012/08/07 (last modified)
 * 
 * 
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Who  Date       Changes
 * ---- ---------- -----------------------------------------------------
 * GO   2012/08/07 Created file based on test app mont_mult1536_v3
 * </pre>
 * 
 */

#ifndef _LIBMME1536_H_
#define _LIBMME1536_H_

#include "libmme1536_types.h"

// default UIO device
#define DEFAULT_UIO_DEV	"/dev/uio6"

// select pipeline parts
#define LOW_PART	1
#define HIGH_PART	2
#define TOT_PIPELINE	3

// operand identifiers
#define OPERAND_0	0
#define OPERAND_1	1
#define OPERAND_2	2
#define OPERAND_3	3
#define MODULUS		4

// bit length of the pipeline parts
#define BITS_LOW	512
#define BITS_HIGH	1024
#define BITS_TOT	(BITS_LOW + BITS_HIGH)

// nr. of 32-bit words in each pipeline part
#define WORDS_LOW	(BITS_LOW/32)
#define WORDS_HIGH	(BITS_HIGH/32)
#define WORDS_TOT	(BITS_TOT/32)

// address related stuff
#define ADDR_STEP 	0x4
#define HIGH_OFFSET	(WORDS_LOW * ADDR_STEP)
#define PAGE_SIZE	0x1000 // 4K

#define DATA_BASE_ADDR	0xA0000000
#define OP0_OFFSET	0x00001000
#define OP1_OFFSET	0x00002000
#define OP2_OFFSET	0x00003000
#define OP3_OFFSET	0x00004000
#define M_OFFSET	0x00000000
#define FIFO_OFFSET	0x00005000

// bit fields in the control register
#define P_SEL_BITS	30
#define DEST_BITS	28
#define X_OP_BITS	26
#define Y_OP_BITS	24

// timeouts
#define TIMEOUT_S	0
#define TIMEOUT_US	140000

/**
 * Software Reset Masks
 * -- SOFT_RESET : software reset
 */
#define SOFT_RESET (0x0000000A)

/**
 * Interrupt Controller Space Offsets
 * -- INTR_DGIER : device (peripheral) global interrupt enable register
 * -- INTR_ISR   : ip (user logic) interrupt status register
 * -- INTR_IER   : ip (user logic) interrupt enable register
 */
#define MME1536_INTR_CNTRL_SPACE_OFFSET (0x00000200)
#define MME1536_INTR_DGIER_OFFSET (MME1536_INTR_CNTRL_SPACE_OFFSET + 0x0000001C)
#define MME1536_INTR_IPISR_OFFSET (MME1536_INTR_CNTRL_SPACE_OFFSET + 0x00000020)
#define MME1536_INTR_IPIER_OFFSET (MME1536_INTR_CNTRL_SPACE_OFFSET + 0x00000028)

/**
 * Interrupt Controller Masks
 * -- INTR_TERR_MASK : transaction error
 * -- INTR_DPTO_MASK : data phase time-out
 * -- INTR_IPIR_MASK : ip interrupt requeset
 * -- INTR_RFDL_MASK : read packet fifo deadlock interrupt request
 * -- INTR_WFDL_MASK : write packet fifo deadlock interrupt request
 * -- INTR_IID_MASK  : interrupt id
 * -- INTR_GIE_MASK  : global interrupt enable
 * -- INTR_NOPEND    : the DIPR has no pending interrupts
 */
#define INTR_TERR_MASK (0x00000001UL)
#define INTR_DPTO_MASK (0x00000002UL)
#define INTR_IPIR_MASK (0x00000004UL)
#define INTR_RFDL_MASK (0x00000020UL)
#define INTR_WFDL_MASK (0x00000040UL)
#define INTR_IID_MASK (0x000000FFUL)
#define INTR_GIE_MASK (0x80000000UL)
#define INTR_NOPEND (0x80)

/** Function prototypes
 */

int MME1536_Initialize(MME1536 * device_instance, char * uio_dev);
void MME1536_Clean(MME1536 * device_instance);

void MME1536_MME(MME1536 * device_instance, int * result, int * g0, int * g1, int * m, int * e0, int * e1, int n, int t);
void MME1536_UpdateModulus(MME1536 * device_instance, int * m, int n);
void MME1536_Multiply_m(MME1536 * device_instance, int * result, int * x, int * y);
void MME1536_Exp_m(MME1536 * device_instance, int * result, int * g, int * e, int t);
void MME1536_MME_m(MME1536 * device_instance, int * result, int * g0, int * g1, int * e0, int * e1, int t);

void MME1536_StartSingle(MME1536 * device_instance, int p_sel, int destination, int x_op, int y_op);
void MME1536_StartAuto(MME1536 * device_instance, int p_sel);
void MME1536_WaitUntilReady(MME1536 * device_instance);
void MME1536_StartSingle_m(MME1536 * device_instance, int destination, int x_op, int y_op);
void MME1536_StartAuto_m(MME1536 * device_instance);

void MME1536_PrintInfo(MME1536 * device_instance);
void MME1536_PrintOperands(MME1536 * device_instance);

void MME1536_SetExponent(MME1536 * device_instance, int * e0, int * e1, int t);
int MME1536_SetOperand(MME1536 * device_instance, int * operand_data, int operand, int length);
int MME1536_GetOperand(MME1536 * device_instance, int * operand_data, int operand, int length);
int MME1536_SetOperand_m(MME1536 * device_instance, int * operand_data, int operand);
int MME1536_GetOperand_m(MME1536 * device_instance, int * operand_data, int operand);
/** @todo
 *     1) implement MME1536_ResetHardware() ?
 *     2) max fifo size -> error checking
 */

#endif /*_LIBMME1536_*/


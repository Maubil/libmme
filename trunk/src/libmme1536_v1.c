/** @file libmme1536_v1.c This file contains the source code for
 * interfacing the mont_mult1536 hardware core of the board/platform.
 * It uses the UIO kernel driver to interface with the hardware as well
 * as direct mapped memory.
 * 
 * @author Geoffrey Ottoy - DraMCo research group
 * @date 2012/08/17 (last modified)
 * 
 * 
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Who  Date       Changes
 * ---- ---------- -----------------------------------------------------
 * GO   2012/08/17 Changed MME1536_Initialize()
 *                  -> other fault handling
 *                  -> added "char * uid_dev" to MME1536 struct
 * GO   2012/08/17 Created file based on test app mont_mult1536_v3
 * </pre>
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <errno.h>

#include <gmp.h>

#include "libmme1536_types.h"
#include "libmme1536_v1.h"


static int one[WORDS_TOT]={
	1,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0};

/******************************************************************************
 * Low-level Function Prototypes (not to be used outside this file)           *
 ******************************************************************************/
void MME1536_GetData(MME1536 * device_instance, int * buffer, int offset_start, int operand, int words);
void MME1536_ComputeR2(int * R2, int * m, int n);
void MME1536_EnableInterrupt(MME1536 * device_instance);
void MME1536_SetData(MME1536 * device_instance, int * data, int start_offset, int words);

/******************************************************************************
 * API Function Source                                                        *
 ******************************************************************************/

/** Initialise the hardware core and necessary variables.
 * 
 * @param device_instance is a pointer to a MME1536 variable associated with the
 *        hardware.
 * @param uio_dev is a string containing the path or the uio device
 * 
 * @return 0 upon success
 *         1 upon failure
 */
int MME1536_Initialize(MME1536 * device_instance, char * uio_dev){
	/* Direct mmap of core's data memory*/
	device_instance->data_fd=open("/dev/mem",O_RDWR|O_NONBLOCK);
	if(device_instance->data_fd<1){
		perror("[ERROR] MME1536: Initialize() -> could not open /dev/mem\n");
		return -1;
	}
	device_instance->data_ptr=mmap(NULL,PAGE_SIZE*6,
				       PROT_READ|PROT_WRITE,
				       MAP_SHARED,
				       device_instance->data_fd,
				       DATA_BASE_ADDR);
	if((int)device_instance->data_ptr==-1){
		perror("[ERROR] MME1536: Initialize() -> could not map data_ptr\n");
		return -1;
	}
	
	/* UIO setup*/
	if(uio_dev == NULL){
		device_instance->uio_dev = DEFAULT_UIO_DEV;
		printf("[INFO] MME1536: Initialize() -> taking default UIO device %s\n", DEFAULT_UIO_DEV);
	}
	else{
		device_instance->uio_dev = uio_dev;
	}
	
	device_instance->ctrl_fd = open(device_instance->uio_dev, O_RDWR|O_NONBLOCK);
	if(device_instance->ctrl_fd < 1) {
		perror("[ERROR] MME1536: Initialize() -> failed to open UIO device \n");
		return -1;
	}
	device_instance->ctrl_ptr = mmap(NULL, PAGE_SIZE,
					 PROT_READ|PROT_WRITE,
					 MAP_SHARED,
					 device_instance->ctrl_fd,
					 0);
	if(device_instance->ctrl_ptr == 0) {
		printf("[ERROR] MME1536: Initialize() -> failed to mmap the UIO device.\n");
		goto failed1;
	}
	
	/* Interrupt */
	// set timeouts
	device_instance->tv.tv_sec = TIMEOUT_S;
	device_instance->tv.tv_usec = TIMEOUT_US;
	// initialise fd_set variable
	FD_ZERO(&(device_instance->select_fd));
	FD_SET(device_instance->ctrl_fd, &(device_instance->select_fd));
	// enable interrupt in the hardware
	MME1536_EnableInterrupt(device_instance);
	// Enable uio interrupt
	int enable = 1;
	if(write(device_instance->ctrl_fd, &enable, sizeof(int)) == ENOSYS){
		printf("[ERROR] MME1536: Initialize() -> no interrupt for this device!\n");
		goto failed2;
	};
	// Read nr of ints until now
	int ints = 0;
	read(device_instance->ctrl_fd, &ints, sizeof(int));
	device_instance->prev_tot_ints = ints;
	
	// Reserve memory for R2
	device_instance->R2 = (int *)malloc(WORDS_TOT * sizeof(int));
	if(device_instance->R2 == NULL){
		printf("[ERROR] MME1536: Initialize() -> could not allocate memory for R2.\n");
		goto failed2;
	}
	
	return 0;
	
failed2:
	munmap(device_instance->ctrl_ptr, PAGE_SIZE);
failed1:
	close(device_instance->ctrl_fd);
	
	return -1;
}

/** Free memory resources and unmap memory
 * 
 * @param device_instance is a pointer to a MME1536 variable associated with the
 *        hardware.
 * 
 * @return nothing
 */
void MME1536_Clean(MME1536 * device_instance){
	free(device_instance->R2);
	
	munmap(device_instance->ctrl_ptr, PAGE_SIZE);
	close(device_instance->ctrl_fd);
	munmap(device_instance->data_ptr, PAGE_SIZE*6);
	close(device_instance->data_fd);
}

/** Start a single montgomery multiplication
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param p_sel sets the part of the pipeline being used (LOW_PART,
 *        HIGH_PART or TOT_PIPELINE).
 * @param destination sets the operand where the result will be stored
 * @param x_op, y_op select the multiplier and multiplicand
 * 
 * @return nothing
 */
void MME1536_StartSingle(MME1536 * device_instance, int p_sel, int destination, int x_op, int y_op){
	// get control register
	int control = *((unsigned *)(device_instance->ctrl_ptr));
	
	// set al control bits to the correct value
	control &= 0x003fffff; 
	control |= (p_sel << P_SEL_BITS) | (destination << DEST_BITS) 
		 | (x_op << X_OP_BITS) | (y_op << Y_OP_BITS) | 0x00800000;
	
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
	// clear start bit
	control &= 0xff7fffff;
	usleep(1); // necessary because otherwise the core won't start (we might want to look into this later)
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
}

/** Start a single montgomery multiplication with m set by UpdateModulus()
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param destination sets the operand where the result will be stored
 * @param x_op, y_op select the multiplier and multiplicand
 * 
 * @return nothing
 */
void MME1536_StartSingle_m(MME1536 * device_instance, int destination, int x_op, int y_op){
	// get control register
	int control = *((unsigned *)(device_instance->ctrl_ptr));
	
	// set al control bits to the correct value
	control &= 0x003fffff; 
	control |= (device_instance->part << P_SEL_BITS) | (destination << DEST_BITS) 
		 | (x_op << X_OP_BITS) | (y_op << Y_OP_BITS) | 0x00800000;
	
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
	// clear start bit
	control &= 0xff7fffff;
	usleep(1); // necessary because otherwise the core won't start (we might want to look into this later)
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
}

/** Start the main computation loop
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param p_sel sets the part of the pipeline being used (LOW_PART,
 *        HIGH_PART or TOT_PIPELINE).
 * 
 * @return nothing
 */
void MME1536_StartAuto(MME1536 * device_instance, int p_sel){
	// set bits start, auto-run and p_sel
	int control = 0x00c00000 | (p_sel << P_SEL_BITS);
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
	// clear start bit
	control &= 0xff7fffff;
	usleep(1); // necessary because otherwise the core won't start (we might want to look into this later)
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
}
/** Start the main computation loop with m set by UpdateModulus()
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * 
 * @return nothing
 */
void MME1536_StartAuto_m(MME1536 * device_instance){
	// set bits start, auto-run and p_sel
	int control = 0x00c00000 | (device_instance->part << P_SEL_BITS);
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
	// clear start bit
	control &= 0xff7fffff;
	usleep(1); // necessary because otherwise the core won't start (we might want to look into this later)
	// write control register
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
}

/** Do a single multiplication with m set
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param result is pointer to a buffer where the result will be stored
 * @param x,y the multiplicands
 * 
 * @warning only works when MME1536_UpdateModulus() has been called previously
 */
void MME1536_Multiply_m(MME1536 * device_instance, int * result, int * x, int * y){
	// write x and y to hardware
	MME1536_SetOperand_m(device_instance, x, OPERAND_0);	
	MME1536_SetOperand_m(device_instance, y, OPERAND_1);
	MME1536_SetOperand_m(device_instance, device_instance->R2, OPERAND_2);
	
	// do the multiplication
	MME1536_StartSingle_m(device_instance, OPERAND_3, OPERAND_0, OPERAND_1); //(x.y).R^(-1)
	MME1536_WaitUntilReady(device_instance);
	MME1536_StartSingle_m(device_instance, OPERAND_3, OPERAND_2, OPERAND_3); //(x.y.R^(-1).R^2).R^(-1)
	MME1536_WaitUntilReady(device_instance);
	
	// get the result
	MME1536_GetOperand_m(device_instance, result, OPERAND_3);
}

/** Do a modular exponentiation with m set
 * 
 * 
 * @warning not tested!
 */
void MME1536_Exp_m(MME1536 * device_instance, int * result, int * g, int * e, int t){
	// write operands to hardware
	MME1536_SetOperand_m(device_instance, g, OPERAND_0);	
	MME1536_SetOperand_m(device_instance, device_instance->R2, OPERAND_1);
	MME1536_SetOperand_m(device_instance, one, OPERAND_2);
	
	// compute gt0
	MME1536_StartSingle_m(device_instance,OPERAND_0,OPERAND_0,OPERAND_1);	
	MME1536_WaitUntilReady(device_instance);
	// compute R
	MME1536_StartSingle_m(device_instance,OPERAND_3,OPERAND_2,OPERAND_1);	
	MME1536_WaitUntilReady(device_instance);
	
	/* Main computation */
	// set exponent bits
	MME1536_SetExponent(device_instance, e , NULL, t);
	// start core in auto mode
	MME1536_StartAuto_m(device_instance);
	MME1536_WaitUntilReady(device_instance);
	
	/* Postcomputation */
	MME1536_StartSingle_m(device_instance,OPERAND_3,OPERAND_2,OPERAND_3);
	MME1536_WaitUntilReady(device_instance);

	MME1536_GetOperand_m(device_instance,result,OPERAND_3);
}

/** Set a new modulus to be used by the hardware
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param m is an array containing the modulus
 * @param n is the length of the modulus in bits
 * 
 * @return nothing
 */
void MME1536_UpdateModulus(MME1536 * device_instance, int * m, int n){
	switch(n){
		case BITS_LOW:{
			device_instance->part = LOW_PART;
		} break;
		case BITS_HIGH:{
			device_instance->part = HIGH_PART;
		} break;
		case BITS_TOT:{
			device_instance->part = TOT_PIPELINE;
		} break;
		default:{
			printf("[ERROR] MME1536: UpdataModulus() -> wrong modulus length: %d\n", n);
		} break;
	}
	
	device_instance->n = n;
	device_instance->words = n / 32;
	
	int i;
	for(i=0; i<WORDS_TOT; i++){
		device_instance->R2[i] = 0x00000000;
	}
	
	MME1536_ComputeR2(device_instance->R2, m, n);
	
	MME1536_SetOperand(device_instance, m, MODULUS, n);
}

/** Compute g0^e0 * g1^e1 mod m (with m set by UpdateModulus)
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param result is a pointer to the buffer where the result will be stored
 * @param g0, g1, e0, e1 are arrays containing the bases and exponents
 * @param t is the length of the exponents (#bits)
 * 
 * @return nothing
 */
void MME1536_MME_m(MME1536 * device_instance, int * result, int * g0, int * g1, int * e0, int * e1, int t){
	// write operands to hardware
	MME1536_SetOperand_m(device_instance, g0, OPERAND_0);	
	MME1536_SetOperand_m(device_instance, g1, OPERAND_1);
	MME1536_SetOperand_m(device_instance, one, OPERAND_2);
	MME1536_SetOperand_m(device_instance, device_instance->R2, OPERAND_3);

	// compute gt0
	MME1536_StartSingle_m(device_instance,OPERAND_0,OPERAND_0,OPERAND_3);	
	MME1536_WaitUntilReady(device_instance);
	// compute gt1
	MME1536_StartSingle_m(device_instance,OPERAND_1,OPERAND_1,OPERAND_3);	
	MME1536_WaitUntilReady(device_instance);
	// compute R
	MME1536_StartSingle_m(device_instance,OPERAND_3,OPERAND_2,OPERAND_3);	
	MME1536_WaitUntilReady(device_instance);
	// compute gt01
	MME1536_StartSingle_m(device_instance,OPERAND_2,OPERAND_0,OPERAND_1);	
	MME1536_WaitUntilReady(device_instance);
	
	/* Main computation */
	// set exponent bits
	MME1536_SetExponent(device_instance, e0 , e1, t);
	// start core in auto mode
	MME1536_StartAuto_m(device_instance);
	MME1536_WaitUntilReady(device_instance);
	
	/* Postcomputation */
	// write '1'
	MME1536_SetOperand_m(device_instance, one, OPERAND_2);
	MME1536_StartSingle_m(device_instance,OPERAND_3,OPERAND_2,OPERAND_3);
	MME1536_WaitUntilReady(device_instance);

	MME1536_GetOperand_m(device_instance,result,OPERAND_3);
}

/** Compute g0^e0 * g1^e1 mod m
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param result is a pointer to the buffer where the result will be stored
 * @param g0, g1, e0, e1, m are arrays containing the bases and exponents
 * @param n is the lenght of g0, g1 and m (#bits)
 * @param t is the length of the exponents (#bits)
 * 
 * @return nothing
 */
void MME1536_MME(MME1536 * device_instance, int * result, int * g0, int * g1, int * m, int * e0, int * e1, int n, int t){
	int part = 0;
	switch(n){
		case BITS_LOW:{
			part = LOW_PART;
		} break;
		case BITS_HIGH:{
			part = HIGH_PART;
		} break;
		case BITS_TOT:{
			part = TOT_PIPELINE;
		} break;
		default:{
			printf("[ERROR] MME1536: MME() -> wrong operand length: %d\n", n);
		} break;
	}
	
	// allocate space for R2
	int * R2;
	R2 = (int *) malloc(n/8);
	
	/* Precomputation */
	// compute R2
	MME1536_ComputeR2(R2, m, n); 
	
	// write modulus to hardware
	MME1536_SetOperand(device_instance, m, MODULUS, n);	
	
	// write operands to hardware
	MME1536_SetOperand(device_instance, g0, OPERAND_0, n);	
	MME1536_SetOperand(device_instance, g1, OPERAND_1, n);
	MME1536_SetOperand(device_instance, one, OPERAND_2, n);
	MME1536_SetOperand(device_instance, R2, OPERAND_3, n);

	// compute gt0
	MME1536_StartSingle(device_instance,part,OPERAND_0,OPERAND_0,OPERAND_3);	
	MME1536_WaitUntilReady(device_instance);
	// compute gt1
	MME1536_StartSingle(device_instance,part,OPERAND_1,OPERAND_1,OPERAND_3);	
	MME1536_WaitUntilReady(device_instance);
	// compute R
	MME1536_StartSingle(device_instance,part,OPERAND_3,OPERAND_2,OPERAND_3);	
	MME1536_WaitUntilReady(device_instance);
	// compute gt01
	MME1536_StartSingle(device_instance,part,OPERAND_2,OPERAND_0,OPERAND_1);	
	MME1536_WaitUntilReady(device_instance);
	
	/* Main computation */
	// set exponent bits
	MME1536_SetExponent(device_instance, e0 , e1, t);
	// start core in auto mode
	MME1536_StartAuto(device_instance, part);
	MME1536_WaitUntilReady(device_instance);
	
	/* Postcomputation */
	// write '1'
	MME1536_SetOperand(device_instance, one, OPERAND_2, n);
	MME1536_StartSingle(device_instance,part,OPERAND_3,OPERAND_2,OPERAND_3);
	MME1536_WaitUntilReady(device_instance);

	MME1536_GetOperand(device_instance,result,OPERAND_3,n);

	/* Cleanup */
	free(R2);
}

/** Wait until the core has completed it's operation (interrupt)
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * 
 * @return nothing
 */
void MME1536_WaitUntilReady(MME1536 * device_instance){
	int ints_passed = -1;
	struct timeval time;
	int timeout, current_time;
	gettimeofday(&time,NULL);
	timeout = (int)time.tv_usec + TIMEOUT_US;
	current_time = (int)time.tv_usec;
	// wait for an interrupt (poll flag)
	while(ints_passed <= device_instance->prev_tot_ints){
		read(device_instance->ctrl_fd, &ints_passed, sizeof(int));
		gettimeofday(&time,NULL);
		current_time = (int)time.tv_usec;
		if(current_time >= timeout){
			printf("[WARNING] MME1536: WaitUntilReady() -> Timeout!\n");
			break;
		}
		
	}
	// update the nr of interrupts
	device_instance->prev_tot_ints = ints_passed;
	
	// enable interrupts again
	int enable = 1;
	write(device_instance->ctrl_fd, &enable, sizeof(int));
}

/** Write exponents to the exponent fifo.
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param e0, e1 are arrays containing the exponents
 * @param t is the nr of bits in the exponent
 * @todo check when fifo is full
 * 
 * @return nothing
 */
void MME1536_SetExponent(MME1536 * device_instance, int * e0, int * e1, int t){
	int temp = 0;
	int i, words;
	
	if((t%32)!=0){
		printf("[ERROR] MME1536: SetExponent() -> exponent length %d, is no multiple of 32.\n", t);
		return;
	}
	words = t/32;
	
	// create fifo entries and write to fifo
	for(i=(words-1); i>=0; i--){
		if(e1==NULL) temp = ((e0[i] & 0xffff0000) >> 16);
		else temp = (e1[i] & 0xffff0000) | ((e0[i] & 0xffff0000) >> 16);
		*((unsigned *)(device_instance->data_ptr + FIFO_OFFSET)) = temp;
		//usleep(1);
		//printf("e: %08x\n", temp);
		
		if(e1==NULL) temp = (e0[i] & 0x0000ffff);
		else temp = ((e1[i] & 0x0000ffff) << 16) | (e0[i] & 0x0000ffff);
		*((unsigned *)(device_instance->data_ptr + FIFO_OFFSET)) = temp;
		
		//printf("e: %08x\n", temp);
	}
	
}

/** Write an operand to the core's memory
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param operand_data is an array containing the operand
 * @param operand sets the memory location where the operand data will be
 *        stored (OPERAND_0 to OPERAND_3).
 * @param length is the nr of elements in operand_data
 * 
 * @return nothing
 */
int MME1536_SetOperand(MME1536 * device_instance, int * operand_data, int operand, int length){
	int address_offset;
	int word;
	int zeros[WORDS_TOT]={
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};
	int * data;
	
	switch(operand){
		case OPERAND_0:{
			address_offset = OP0_OFFSET;
		} break;
		case OPERAND_1:{
			address_offset = OP1_OFFSET;
		} break;
		case OPERAND_2:{
			address_offset = OP2_OFFSET;
		} break;
		case OPERAND_3:{
			address_offset = OP3_OFFSET;
		} break;
		case MODULUS:{
			address_offset = M_OFFSET;
		} break;
		default:{
			printf("[ERROR] MME1536: SetOperand() -> wrong operand (%d)\n", operand);
			return -1;
		} break;
	}
	
	switch(length){
		case BITS_LOW: {
			// store data in lower part (rest is zero)
			for(word=0;word<WORDS_LOW;word++){
				zeros[word] = operand_data[word];
				data = zeros;
			}
		} break;
		case BITS_HIGH: {
			// store data in higher part (rest is zero)
			for(word=0;word<WORDS_HIGH;word++){
				zeros[word+WORDS_LOW] = operand_data[word];
				data = zeros;
			}
		} break;
		case BITS_TOT: {
			data = operand_data;
		} break;
		default: {
			printf("[ERROR] MME1536: SetOperand() -> wrong operand length (%d)\n", length);
			return -1;
		} break;
	}
	// write data
	MME1536_SetData(device_instance, data, address_offset, WORDS_TOT);
	
	return 0;
}

/** Write an operand to the core's memory (length defined by m which was
 * previously set in UpdateModulus()
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param operand sets the memory location where the operand data will be
 *        stored (OPERAND_0 to OPERAND_3).
 * @param operand_data is an array containing the operand
 * 
 * @return nothing
 */
int MME1536_SetOperand_m(MME1536 * device_instance, int * operand_data, int operand){
	int address_offset;
	int word;
	int zeros[WORDS_TOT]={
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0};
	int * data;
	
	switch(operand){
		case OPERAND_0:{
			address_offset = OP0_OFFSET;
		} break;
		case OPERAND_1:{
			address_offset = OP1_OFFSET;
		} break;
		case OPERAND_2:{
			address_offset = OP2_OFFSET;
		} break;
		case OPERAND_3:{
			address_offset = OP3_OFFSET;
		} break;
		case MODULUS:{
			address_offset = M_OFFSET;
		} break;
		default:{
			printf("[ERROR] MME1536: SetOperand_m() -> wrong operand (%d)\n", operand);
			return -1;
		} break;
	}
	
	switch(device_instance->n){
		case BITS_LOW: {
			// store data in lower part (rest is zero)
			for(word=0;word<WORDS_LOW;word++){
				zeros[word] = operand_data[word];
				data = zeros;
			}
		} break;
		case BITS_HIGH: {
			// store data in higher part (rest is zero)
			for(word=0;word<WORDS_HIGH;word++){
				zeros[word+WORDS_LOW] = operand_data[word];
				data = zeros;
			}
		} break;
		case BITS_TOT: {
			data = operand_data;
		} break;
		default: {
			printf("[ERROR] MME1536: SetOperand_m() -> wrong operand length (%d)\n", device_instance->n);
			return -1;
		} break;
	}
	// write data
	MME1536_SetData(device_instance, data, address_offset, WORDS_TOT);
	
	return 0;
}

/** Print some info about the hardware.
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * 
 * @return nothing
 * 
 */
void MME1536_PrintInfo(MME1536 * device_instance){
	printf("[INFO] MME1536: Hardware info\n");
	
	printf(" Addres map:\n");
	
	printf("  op0: 0x%08x\n", device_instance->data_ptr + OP0_OFFSET);
	printf("  op1: 0x%08x\n", device_instance->data_ptr + OP1_OFFSET);
	printf("  op2: 0x%08x\n", device_instance->data_ptr + OP2_OFFSET);
	printf("  op3: 0x%08x\n", device_instance->data_ptr + OP3_OFFSET);
	printf("  m:   0x%08x\n", device_instance->data_ptr + M_OFFSET);
	printf("  exp: 0x%08x\n", device_instance->data_ptr + FIFO_OFFSET);
	
	printf(" Pipeline info:\n");
	printf("  total nr. words:     %d\n", WORDS_TOT);
	printf("  low part nr. words:  %d\n", WORDS_LOW);
	printf("  high part nr. words: %d\n", WORDS_HIGH);
	printf("  upper part offset: 0x%08x\n", HIGH_OFFSET);
}

/** Read the core's operand memory and send to standard out.
 *  (Debugging purposes only).
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * 
 * @return nothing
 * 
 */
void MME1536_PrintOperands(MME1536 * device_instance){
	int data[WORDS_TOT];
	int word;
	
	MME1536_GetData(device_instance, data, OP0_OFFSET, OPERAND_0, WORDS_TOT);
	printf("OP0: ");
	for(word=0;word<WORDS_TOT;word++){
		printf("%08x", data[WORDS_TOT-1-word]);
	}
	printf("\n");
	
	MME1536_GetData(device_instance, data, OP1_OFFSET, OPERAND_1, WORDS_TOT);
	printf("OP1: ");
	for(word=0;word<WORDS_TOT;word++){
		printf("%08x", data[WORDS_TOT-1-word]);
	}
	printf("\n");
	
	MME1536_GetData(device_instance, data, OP2_OFFSET, OPERAND_2, WORDS_TOT);
	printf("OP2: ");
	for(word=0;word<WORDS_TOT;word++){
		printf("%08x", data[WORDS_TOT-1-word]);
	}
	printf("\n");
	
	MME1536_GetData(device_instance, data, OP3_OFFSET, OPERAND_3, WORDS_TOT);
	printf("OP3: ");
	for(word=0;word<WORDS_TOT;word++){
		printf("%08x", data[WORDS_TOT-1-word]);
	}
	printf("\n");
}

/** Read an operand from the core's memory.
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param operand_data is a pointer to a buffer where the operand will
 *        be stored.
 * @param operand specifies the operand to read from the memory
 *        (OPERAND_0 to OPERAND_3)
 * @param length is the length op the operand (#bits)
 * 
 * @return nothing
 */
int MME1536_GetOperand(MME1536 * device_instance, int * operand_data, int operand, int length){
	int address_offset;
	int words;
	
	switch(operand){
		case OPERAND_0:{
			address_offset = OP0_OFFSET;
		} break;
		
		case OPERAND_1:{
			address_offset = OP1_OFFSET;
		} break;
		
		case OPERAND_2:{
			address_offset = OP2_OFFSET;
		} break;
		
		case OPERAND_3:{
			address_offset = OP3_OFFSET;
		} break;
		
		default:{
			printf("[ERROR] MME1536: GetOperand() -> wrong operand (%d)\n", operand);
			return -1;
		} break;
	}
	
	switch(length){
		case BITS_LOW: {
			words = WORDS_LOW;
		} break;
		case BITS_HIGH: {
			address_offset += HIGH_OFFSET;
			words = WORDS_HIGH;
		} break;
		case BITS_TOT: {
			words = WORDS_TOT;
		} break;
		default: {
			printf("[ERROR] MME1536: GetOperand() -> wrong operand length (%d)\n", length);
			return -1;
		} break;
	}
	
	MME1536_GetData(device_instance, operand_data, address_offset, operand, words);
	
	return 0;
}

/** Read an operand from the core's memory.
 * 
 * @param device_instance is a pointer to a MME1536 variable
 *        associated with the hardware.
 * @param operand_data is a pointer to a buffer where the operand will
 *        be stored.
 * @param operand specifies the operand to read from the memory
 *        (OPERAND_0 to OPERAND_3)
 * 
 * @return nothing
 */
int MME1536_GetOperand_m(MME1536 * device_instance, int * operand_data, int operand){
	int address_offset;
	
	switch(operand){
		case OPERAND_0:{
			address_offset = OP0_OFFSET;
		} break;
		
		case OPERAND_1:{
			address_offset = OP1_OFFSET;
		} break;
		
		case OPERAND_2:{
			address_offset = OP2_OFFSET;
		} break;
		
		case OPERAND_3:{
			address_offset = OP3_OFFSET;
		} break;
		
		default:{
			printf("[ERROR] MME1536: GetOperand_m() -> wrong operand (%d)\n", operand);
			return -1;
		} break;
	}
	
	if(device_instance->n == BITS_HIGH){
		address_offset += HIGH_OFFSET;
	}
	
	MME1536_GetData(device_instance, operand_data, address_offset, operand, device_instance->words);
	
	return 0;
}

/******************************************************************************
 * Low-level Function Source                                                  *
 ******************************************************************************/

void MME1536_GetData(MME1536 * device_instance, int * buffer, int offset_start, int operand, int words){
	int control = 0x00000000;
	int word;
	int address_offset = offset_start;
	// set destination bits in the control register
	// (necessary for reading from the correct location)
	control |= (operand << DEST_BITS); 
	*((unsigned *)(device_instance->ctrl_ptr)) = control;
	// read all words
	for(word=0;word<words;word++){
		buffer[word] = *((unsigned *)(device_instance->data_ptr + address_offset));
		address_offset += ADDR_STEP;
	}
}

void MME1536_SetData(MME1536 * device_instance, int * data, int start_offset, int words){
	int word;
	int address_offset = start_offset;
	// write all words
	for(word=0;word<words;word++){
		*((unsigned *)(device_instance->data_ptr + address_offset)) = data[word];
		//printf("0x%08x\n",data[word]);
		address_offset += ADDR_STEP;
	}
}

void MME1536_ComputeR2(int * R2, int * m, int n){
	mpz_t R2_mpz, m_mpz, b2;

	mpz_init2(R2_mpz, n);
	mpz_init2(m_mpz, n);
	mpz_init_set_ui(b2, 2);
	// convert integer array to mpz
	mpz_import(m_mpz, n/(sizeof(int)*8), -1, sizeof(int), 0, 0, m);
	// compute R2
	mpz_powm_ui(R2_mpz, b2, (unsigned long int)(2 * n), m_mpz);
	// convert result back to integer array
	mpz_export((void*)R2,NULL,-1,sizeof(int),0,0,R2_mpz); 
}

void MME1536_EnableInterrupt(MME1536 * device_instance){
	// Enable all interrupt sources from user logic.
	*((unsigned *)(device_instance->ctrl_ptr + MME1536_INTR_IPIER_OFFSET)) = 0x00000001;

	// Set global interrupt enable.
	*((unsigned *)(device_instance->ctrl_ptr + MME1536_INTR_DGIER_OFFSET)) = INTR_GIE_MASK;
}

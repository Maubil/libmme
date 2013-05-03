#ifndef _LIBMME1536_TYPES_H_
#define _LIBMME1536_TYPES_H_


/// definition of the MME1536 structure
typedef struct mont_mult1536_st{
	/* memory */
	int data_fd;
	int ctrl_fd;
	void * ctrl_ptr;
	void * data_ptr;
	char * uio_dev;
	
	/* interrupt */
	struct timeval tv;
	fd_set select_fd;
	int prev_tot_ints;
	
	/* data */
	int * R2;
	int n, words, part;
} MME1536;

#endif /*_LIBMME1536_TYPES_H_*/


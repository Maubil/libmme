/** Application for testing the mod_sim_exp hardware core with the use
 *  of the mme1536 library (containing driver code and arithmetic API).
 * 
 *  It will test if the hardware accelerator correctly computes
 * 		g0^e0 * g1^e1 mod m
 * 
 *  This application will start with generating the required variables.
 *  It will perform the computation in both hardware and software and
 *  compare both values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "gmp.h"

#include "libmme1536_v1.h"

/***********************************************************************
 * Required subroutines                                                *
 **********************************************************************/

long getElapsedMilliSeconds(struct timeval time1, struct timeval time2){
	long elapsed_seconds  = time2.tv_sec  - time1.tv_sec;
	long elapsed_useconds = time2.tv_usec - time1.tv_usec;

	long elapsed_mtime = ((elapsed_seconds) * 1000 + elapsed_useconds/1000.0) + 0.5; 

	return elapsed_mtime;
}

long getElapsedMicroSeconds(struct timeval time1, struct timeval time2){
	long elapsed_seconds  = time2.tv_sec  - time1.tv_sec;
	long elapsed_useconds = time2.tv_usec - time1.tv_usec;

	long elapsed_utime = (elapsed_seconds) * 1000000 + elapsed_useconds;
	if(elapsed_seconds < 1){
		if(elapsed_utime < 1){
			elapsed_utime*=-1;
		}
	}
	return elapsed_utime;
}

void multi_exp_n(mpz_t * rop, mpz_t g0, mpz_t g1, mpz_t e0, mpz_t e1, mpz_t m){
	mpz_t h0, h1, h2;
	mpz_init(h0);
	mpz_init(h1);
	mpz_init(h2);

	mpz_powm(h0, g0, e0, m);
	mpz_powm(h1, g1, e1, m);

	mpz_mul(h2, h0, h1);
	mpz_mod(rop[0], h2, m);

	mpz_clear(h0);
	mpz_clear(h1);
	mpz_clear(h2);
}

void generate_rand(mpz_t rand, int length){
	mpz_t temp;
	mpz_init(temp);
	gmp_randstate_t state;
	struct timeval timeseed;

	gettimeofday(&timeseed, NULL);
	gmp_randinit_default(state);
	gmp_randseed_ui(state, timeseed.tv_usec);
	
	do{
		mpz_urandomb(rand, state, length);
	} while(mpz_even_p(rand)!=0); // because the modulus needs to be odd
	
	gmp_randclear(state);
}

void printUsage(){
	printf("\nUsage: mont_test N W\n N:\tthe lenth of the modulus and base operands [# bits]\n\tsupported: 512, 1024, 1536\n W:\tthe lenth of the exponents [# bits]\n");
}

int main(int argc, char *argv[]){
	int n,t;
	int i;
	
	struct timeval starttime, endtime;
	long elapsed_ms_gmp;   
	long elapsed_us_gmp;  

	printf("Test program for simultaneous modular exponentiation.\n");
	
	/* Check arguments. */
	if(argc!=3){
		printUsage();
		return 1;
	}
	
	/* Read lengths for the variables */
	n = atoi(argv[1]);
	t = atoi(argv[2]);
	
	if( (n!=512)&&(n!=1024)&&(n!=1536)){
		printf("Unsupported value for N!\n");
		printUsage();
		return 1;
	}
	if((t%32)!=0){
		printf("V is not a multiple of 32!\n");
		return 1;
	}
	
	printf("Generating test variables\r\n");
	
	/* integer arrays */
	int * m_bin;
	int * g0_bin;
	int * g1_bin;
	int * e0_bin;
	int * e1_bin;
	int * R2_bin;
	int * result_bin;
	
	/* Create bignum vars */
	mpz_t e0_mpz, e1_mpz, g0_mpz, g1_mpz, m_mpz, result_mpz;
	mpz_init(m_mpz);
	mpz_init(e0_mpz);
	mpz_init(e1_mpz);
	mpz_init(g0_mpz);
	mpz_init(g1_mpz);
	mpz_init(result_mpz);
	
	/* Generate random variables */
	generate_rand(m_mpz, n);
	generate_rand(g0_mpz, n);
	generate_rand(g1_mpz, n);
	generate_rand(e0_mpz, t);
	generate_rand(e1_mpz, t);

	/* Fill integer arrays from mpz variables */
	m_bin = (int *) malloc(n/8);
	g0_bin = (int *) malloc(n/8);
	g1_bin = (int *) malloc(n/8);
	R2_bin = (int *) malloc(n/8);
	e0_bin = (int *) malloc(t/8);
	e1_bin = (int *) malloc(t/8);
	result_bin = (int *) malloc(n/8);
	mpz_export((void*)g0_bin,NULL,-1,sizeof(int),0,0,g0_mpz); 
	mpz_export((void*)g1_bin,NULL,-1,sizeof(int),0,0,g1_mpz); 
	mpz_export((void*)e0_bin,NULL,-1,sizeof(int),0,0,e0_mpz); 
	mpz_export((void*)e1_bin,NULL,-1,sizeof(int),0,0,e1_mpz); 
	mpz_export((void*)m_bin,NULL,-1,sizeof(int),0,0,m_mpz);
	
	/* Print generated values on screen */
	printf("m_bin: 0x");
	for(i=0; i<(n/32); i++) printf("%08x", m_bin[i]);
	printf("\n");
	printf("g0_bin: 0x");
	for(i=0; i<(n/32); i++) printf("%08x", g0_bin[i]);
	printf("\n");
	printf("g1_bin: 0x");
	for(i=0; i<(n/32); i++) printf("%08x", g1_bin[i]);
	printf("\n");
	printf("e0_bin: 0x");
	for(i=0; i<(t/32); i++) printf("%08x", e0_bin[i]);
	printf("\n");
	printf("e1_bin: 0x");
	for(i=0; i<(t/32); i++) printf("%08x", e1_bin[i]);
	printf("\n");
	
	printf("\n\033[1;34mPress key to configure hardware.\033[0m\n");
	getchar();
	
	/******************************************************************/

	/* Hardware config */
	MME1536 mme_hw;
	MME1536_Initialize(&mme_hw, NULL);
	MME1536_PrintInfo(&mme_hw);
	
	printf("Done.\n");
	printf("\n\033[1;34mPress key to compute result using \"mont_mult1536\" hardware.\033[0m\n\n");
	getchar();
	
	/******************************************************************/
	
	/* Compute g0^(e0) g1^(e1) mod m (hardware) and write to file */

	gettimeofday(&starttime, NULL);	// start time exp
	MME1536_MME(&mme_hw, result_bin, g0_bin, g1_bin, m_bin, e0_bin, e1_bin, n, t);
	gettimeofday(&endtime, NULL);	// end time exp
	elapsed_ms_gmp = getElapsedMilliSeconds(starttime, endtime);
	elapsed_us_gmp = getElapsedMicroSeconds(starttime, endtime);
	
	printf("Done: %d ms, %d µs\n", elapsed_ms_gmp, elapsed_us_gmp);

	/******************************************************************/
	/* Compute g0^(e0) g1^(e1) mod m (software) and write to file */

	printf("\n\033[1;34mPress key to compute result using the \"GMP\" software library.\033[0m\n\n");
	getchar();

	/* Do the calculation*/
	printf("Starting...\n");

	gettimeofday(&starttime, NULL);	// start time exp
	multi_exp_n(&result_mpz, g0_mpz, g1_mpz, e0_mpz, e1_mpz, m_mpz); // exp
	gettimeofday(&endtime, NULL);	// end time exp
	elapsed_ms_gmp = getElapsedMilliSeconds(starttime, endtime);
	elapsed_us_gmp = getElapsedMicroSeconds(starttime, endtime);
	
	printf("Done: %d ms, %d µs\n", elapsed_ms_gmp, elapsed_us_gmp);

	/******************************************************************/
	printf("\nComparing results...\n");

	int * result_gmp;
	result_gmp = (int *) malloc(n/8);
	mpz_export((void*)result_gmp,NULL,-1,sizeof(int),0,0,result_mpz);
	
	if(memcmp(result_gmp, result_bin, n/32)==0){
		printf("\033[1;32mResults match!\033[0m\n");
	}
	else{
		printf("\033[1;31mDifferent results, dumping...\033[0m\n");
		int i;
		for(i=0;i<n/32;i++){
			printf("%08x - %08x\n",(int)result_gmp[i], (int)result_bin[i]);
		}
		printf("\n");
	}
	
	/******************************************************************/
	
	/* Cleanup */
	free(R2_bin);
	free(e1_bin);
	free(e0_bin);
	free(g1_bin);
	free(g0_bin);
	free(m_bin);
	free(result_gmp);
	free(result_bin);
	
	MME1536_Clean(&mme_hw);

	mpz_clear(result_mpz);
	mpz_clear(e1_mpz);
	mpz_clear(e0_mpz);
	mpz_clear(g1_mpz);
	mpz_clear(g0_mpz);
	mpz_clear(m_mpz);

	return 0;
}

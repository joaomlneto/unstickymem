/*
 * machine.h
 *
 *  Created on: Dec 4, 2017
 *      Author: GUREYA
 */

#ifndef MACHINE_H_
#define MACHINE_H_

#ifdef __x86_64__
#define rdtscll(val) { \
	    unsigned int __a,__d;                                        \
	    asm volatile("rdtsc" : "=a" (__a), "=d" (__d));              \
	    (val) = ((unsigned long)__a) | (((unsigned long)__d)<<32);   \
}
#else
   #define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))
#endif



#endif /* MACHINE_H_ */

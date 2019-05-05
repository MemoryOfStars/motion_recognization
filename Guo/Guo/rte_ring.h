/* SPDX-License-Identifier: BSD-3-Clause
*
* Copyright (c) 2010-2017 Intel Corporation
* Copyright (c) 2007-2009 Kip Macy kmacy@freebsd.org
* All rights reserved.
* Derived from FreeBSD's bufring.h
* Used as BSD-3 Licensed with permission from Kip Macy.
*/

#ifndef _RTE_RING_H_
#define _RTE_RING_H_

/**
* @file
* RTE Ring
*
* The Ring Manager is a fixed-size queue, implemented as a table of
* pointers. Head and tail pointers are modified atomically, allowing
* concurrent access to it. It has the following features:
*
* - FIFO (First In First Out)
* - Maximum size is fixed; the pointers are stored in a table.
* - Lockless implementation.
* - Multi- or single-consumer dequeue.
* - Multi- or single-producer enqueue.
* - Bulk dequeue.
* - Bulk enqueue.
*
* Note: the ring implementation is not preemptible. Refer to Programmer's
* guide/Environment Abstraction Layer/Multiple pthread/Known Issues/rte_ring
* for more information.
*
*/

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
//#include <emmintrin.h>
//#include <intrins.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/cdefs.h>

#define RTE_CACHE_LINE_SIZE 64
#define __rte_cache_aligned __attribute__((__aligned__(RTE_CACHE_LINE_SIZE)))
#define __rte_always_inline inline __attribute__((always_inline))
#define __rte_noinline  __attribute__((noinline))
#define offsetof(t, m) ((size_t) &((t *)0)->m)
#define likely(cond)  __glibc_likely(cond)
#define unlikely(cond)  __glibc_unlikely(cond)

	enum rte_ring_queue_behavior {
		RTE_RING_QUEUE_FIXED = 0, /* Enq/Deq a fixed number of items from a ring */
		RTE_RING_QUEUE_VARIABLE   /* Enq/Deq as many items as possible from ring */
	};

	/**
	 * An RTE ring structure.
	 *
	 * The producer and the consumer have a head and a tail index. The particularity
	 * of these index is that they are not between 0 and size(ring). These indexes
	 * are between 0 and 2^32, and we mask their value when we access the ring[]
	 * field. Thanks to this assumption, we can do subtractions between 2 index
	 * values in a modulo-32bit base: that's why the overflow of the indexes is not
	 * a problem.
	 */
	struct rte_ring {
		/*
		 * Note: this field kept the RTE_MEMZONE_NAMESIZE size due to ABI
		 * compatibility requirements, it could be changed to RTE_RING_NAMESIZE
		 * next time the ABI changes
		 */
		uint32_t size;           /**< Size of ring. */
		uint32_t mask;           /**< Mask (size-1) of ring. */
		uint32_t capacity;       /**< Usable size of ring */
		uint32_t elemlen;

		pthread_mutex_t mutex_data;     /*锁数据*/
		/** Ring producer status. */
		volatile uint32_t* head;
		volatile uint32_t* tail;

		void* data;
	}__rte_cache_aligned;

	/**
	 * @internal Enqueue several objects on the ring
	 *
	  * @param r
	 *   A pointer to the ring structure.
	 * @param obj_table
	 *   A pointer to a table of void * pointers (objects).
	 * @param n
	 *   The number of objects to add in the ring from the obj_table.
	 * @param behavior
	 *   RTE_RING_QUEUE_FIXED:    Enqueue a fixed number of items from a ring
	 *   RTE_RING_QUEUE_VARIABLE: Enqueue as many items as possible from ring
	 * @param is_sp
	 *   Indicates whether to use single producer or multi-producer head update
	 * @param free_space
	 *   returns the amount of space after the enqueue operation has finished
	 * @return
	 *   Actual number of objects enqueued.
	 *   If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
	 */
	static __rte_always_inline unsigned int
		__rte_ring_do_enqueue(struct rte_ring *r, void * obj_table, unsigned int n)
	{
		int i = 0;
		int free_entries = 0;
		if(pthread_mutex_lock(&r->mutex_data) != 0)
		{
			printf("申请锁遇到错误\n");
			return -1;
		}
		free_entries = (r->capacity + *r->tail - *r->head) % r->size;
		if(n > free_entries)
		{
			pthread_mutex_unlock(&r->mutex_data);
			return 0;
		}
		
		for (i = 0; i < n; i++)
		{
			memcpy(r->data + ((*r->head + i) % r->size) * r->elemlen, obj_table + i*r->elemlen, r->elemlen);
		}
		//ENQUEUE_PTRS(r, &r[1], prod_head, obj_table, n, void *);//memcpy data to prod_head

		*r->head = (*r->head + n)%r->size;
		pthread_mutex_unlock(&r->mutex_data);
		return n;
	}

	/**
	 * @internal Dequeue several objects from the ring
	 *
	 * @param r
	 *   A pointer to the ring structure.
	 * @param obj_table+
	 *   A pointer to a table of void * pointers (objects).
	 * @param n
	 *   The number of objects to pull from the ring.
	 * @param behavior
	 *   RTE_RING_QUEUE_FIXED:    Dequeue a fixed number of items from a ring
	 *   RTE_RING_QUEUE_VARIABLE: Dequeue as many items as possible from ring
	 * @param is_sc
	 *   Indicates whether to use single consumer or multi-consumer head update
	 * @param available
	 *   returns the number of remaining ring entries after the dequeue has finished
	 * @return
	 *   - Actual number of objects dequeued.
	 *     If behavior == RTE_RING_QUEUE_FIXED, this will be 0 or n only.
	 */
	static __rte_always_inline unsigned int
		__rte_ring_do_dequeue(struct rte_ring *r, void** obj_table, unsigned int n)
	{
		int i = 0;


		if(pthread_mutex_lock(&r->mutex_data) != 0)
		{
			printf("申请锁遇到错误\n");
			return -1;
		}
		
		if(*(r->head) == *(r->tail))
		{
			//printf("队列中已经没有数据\n");
			pthread_mutex_unlock(&r->mutex_data);
			return -1;
		}
		// gettimeofday(&ts, NULL);
		// printf("start clock_gettime : tv_sec=%ld, tv_nsec=%ld\n", ts.tv_sec, ts.tv_nsec);
		if(*obj_table == NULL)
		{
			printf("obj_table,不能进行dequeue\n");
			pthread_mutex_unlock(&r->mutex_data);
			return -1;
		}
		for (i = 0; i < n; i++)
		{
			memcpy(*obj_table + i*r->elemlen, r->data + ((*r->tail + i) % r->size)* r->elemlen, r->elemlen);
		}
		*r->tail = (*r->tail + n)%r->size;
		pthread_mutex_unlock(&r->mutex_data);
		return n;
	}

	/**
	 * Enqueue several objects on the ring (multi-producers safe).
	 *
	 * This function uses a "compare and set" instruction to move the
	 * producer index atomically.
	 *
	 * @param r
	 *   A pointer to the ring structure.
	 * @param obj_table
	 *   A pointer to a table of void * pointers (objects).
	 * @param n
	 *   The number of objects to add in the ring from the obj_table.
	 * @param free_space
	 *   if non-NULL, returns the amount of space in the ring after the
	 *   enqueue operation has finished.
	 * @return
	 *   The number of objects enqueued, either 0 or n
	 */
	static __rte_always_inline unsigned int
		rte_ring_enqueue_bulk(struct rte_ring *r, void * obj_table,
			unsigned int n)
	{
		return __rte_ring_do_enqueue(r, obj_table, n);
	}

	/**
	 * Enqueue one object on a ring (multi-producers safe).
	 *
	 * This function uses a "compare and set" instruction to move the
	 * producer index atomically.
	 *
	 * @param r
	 *   A pointer to the ring structure.
	 * @param obj
	 *   A pointer to the object to be added.
	 * @return
	 *   - 0: Success; objects enqueued.
	 _*   - -ENOBUFS: Not enough room in the ring to enqueue; no object is enqueued.
	 */
	static __rte_always_inline int
		rte_ring_enqueue(struct rte_ring *r, void *obj)
	{
		return rte_ring_enqueue_bulk(r, obj, 1);
	}

	/**
	 * Dequeue several objects from a ring (multi-consumers safe).
	 *
	 * This function uses a "compare and set" instruction to move the
	 * consumer index atomically.
	 *
	 * @param r
	 *   A pointer to the ring structure.
	 * @param obj_table
	 *   A pointer to a table of void * pointers (objects) that will be filled.
	 * @param n
	 *   The number of objects to dequeue from the ring to the obj_table.
	 * @param available
	 *   If non-NULL, returns the number of remaining ring entries after the
	 *   dequeue has finished.
	 * @return
	 *   The number of objects dequeued, either 0 or n
	 */
	static __rte_always_inline unsigned int
		rte_ring_dequeue_bulk(struct rte_ring *r, void *obj_table,
			unsigned int n)
	{
		return __rte_ring_do_dequeue(r, obj_table, n);
	}

	/**
	 * Dequeue one object from a ring (multi-consumers safe).
	 *
	 * This function uses a "compare and set" instruction to move the
	 * consumer index atomically.
	 *
	 * @param r
	 *   A pointer to the ring structure.
	 * @param obj_p
	 *   A pointer to a void * pointer (object) that will be filled.
	 * @return
	 *   - 0: Success; objects dequeued.
	 *   - -ENOENT: Not enough entries in the ring to dequeue; no object is
	 *     dequeued.
	 */
	static __rte_always_inline int
		rte_ring_dequeue(struct rte_ring *r, void **obj_p)
	{
		return rte_ring_dequeue_bulk(r, obj_p, 1);
	}

	/**
	* @param r
	*  A pointer to the ring structure.
	* @param p
	*  A pointer to a shared memory
	* @param totalen
	*  size of the shared memory
	* @param elemlen
	*  size of the element
	*/
	static __rte_always_inline void rte_ring_create(struct rte_ring* r, void* p, int totallen, int elemlen)
	{
		r->size = (totallen - 32) / elemlen;//64*4 global cons_head,cons_tail,prod_head,prod_tail;
		r->mask = r->size - 1;
		r->capacity = r->size - 1;
		r->elemlen = elemlen;
		r->head = p;
		r->tail = p + 16;
		r->data = p + 32;

		if (pthread_mutex_init(&r->mutex_data, NULL) != 0)
		{
			printf("互斥锁初始化失败\n");
		}
	}

	/**
	* De-allocate all memory used by the ring.
	*
	* @param r
	*   Ring to free
	*/
	// void rte_ring_free(struct rte_ring *r)
	// {
	// 	free(r);
	// 	return;
	// }

	// void ring_info(struct rte_ring *r)
	// {
	// 	printf("ring size:%d\n", r->size);
	// 	printf("ring usage:%d\n", (*r->prod_head - *r->cons_head + r->size) % r->size);
	// 	printf("prod_head:%d, prod_tail:%d, cons_head:%d, cons_tail:%d\n", *r->prod_head, *r->prod_tail, *r->cons_head, *r->cons_tail);
	// }

#ifdef __cplusplus
}
#endif

#endif /* _RTE_RING_H_ */

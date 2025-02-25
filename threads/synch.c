/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//condition variable 우선순위 비교
bool cmp_condvar_priority(const struct list_elem *a, const struct list_elem  *b, void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {

		//sema->waiters list에 들어갈 때 우선순위로 삽입
		list_insert_ordered(&sema->waiters, &thread_current()->elem, &cmp_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();

	if (!list_empty (&sema->waiters))
		//sema->waiters에 있는 맨 앞 쓰레드를 깨운다.
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
				struct thread, elem));	

	sema->value++;

	// thread_set_priority(thread_get_priority());
	intr_set_level (old_level);	
	
	//cpu 양보
	thread_yield();
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;	//처음에는 어떤 스레드도 소유하지 X
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	if(lock->holder != NULL)
	{
		//wait_on_lock에 현재 내가 필요로 하는 lock을 저장한다.
		thread_current()->wait_on_lock = lock;
		
		//반복문 while true
		//종료조건 나의 priority보다 클 때
		struct thread *front_td = thread_current()->wait_on_lock->holder;
		// printf("%d \n", front_td->priority);
		
	
		while(front_td != NULL)
		{
			if(front_td->priority < thread_current()->priority)
			{
				//현재 스레드를 donations 리스트에 삽입(우선순위순으로)
				list_insert_ordered(&lock->holder->donations, &thread_current()->d_elem, &cmp_donor_priority, NULL);
				// printf("before %d \n", front_td->priority);
				front_td->priority = thread_current()->priority;
				// printf("after %d \n", front_td->priority);
				front_td = front_td->wait_on_lock->holder;
			}
			else
				
				break;
		}

		// // //우선순위 기부
		// if(lock->holder->priority < thread_current()->priority)
		// {	
		// 	//현재 스레드를 donations 리스트에 삽입(우선순위순으로)
		// 	list_insert_ordered(&lock->holder->donations, &thread_current()->d_elem, &cmp_donor_priority, NULL);
		// 	lock->holder->priority = thread_current()->priority;
		// }
	}

	sema_down (&lock->semaphore);
	//락 획득 -> 현재 쓰레드의 wait_on_lock NULL로 설정
	thread_current()->wait_on_lock = NULL;
	lock->holder = thread_current ();	//현재 스레드에 대한 잠금 획득
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	if(!list_empty(&lock->holder->donations))
	{	
		//탐색을 위한 현재 donations 리스트에서 맨 앞 요소 확인
		struct list_elem *front_elem = list_begin(&lock->holder->donations);

		//리스트 탐색
		while(front_elem != list_end(&lock->holder->donations))
		{
			struct thread *front_thread = list_entry(front_elem, struct thread, d_elem);
			
			//해당 락 소유 쓰레드 찾기
			if(front_thread->wait_on_lock == lock)
			{
				list_remove(&front_thread->d_elem);
				front_thread->wait_on_lock = NULL;
			}			
			front_elem = list_next(front_elem);
		}

		//donations list가 비어있지 않다면 list에 있는 가장 큰 우선순위로 락 소유 스레드에게 기부 한다.
		if(!list_empty(&lock->holder->donations))
		{
			struct thread *next_thread = list_entry(list_front(&lock->holder->donations), struct thread, d_elem);
			lock->holder->priority = next_thread->priority;
		}	
		//비어있으면 원래의 우선순위로 복귀
		else
			lock->holder->priority = lock->holder->original_priority;
	}

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);

	//우선순위로 cond->waiters list에 넣어라
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_condvar_priority, NULL);

	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. 
   cond(lock으로 보호됨)에서 대기 중인 스레드가 있는 경우 이 함수는
   그 중 하나를 대기에서 깨우도록 신호를 보냅니다.
   이 함수를 호출하기 전에 LOCK을 유지해야 합니다.   
   */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
	{	
		list_sort(&cond->waiters, cmp_condvar_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
				struct semaphore_elem, elem)->semaphore);
	}
}


/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

//condition variable 우선순위 비교
bool
cmp_condvar_priority(const struct list_elem *a, const struct list_elem  *b, void *aux UNUSED) {
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	struct thread *t_a = list_entry(list_begin(&sema_a->semaphore.waiters), struct thread, elem);
	struct thread *t_b = list_entry(list_begin(&sema_b->semaphore.waiters), struct thread, elem);
	
	return t_a->priority > t_b->priority;
}

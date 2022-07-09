/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;
        //heap allocation memory for the structure
        sem = kmalloc(sizeof(*sem));
        if (sem == NULL) {
                return NULL;
        }
        //assign name through kstrdup saved in heap
        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }
        //assign new wait channel
	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        //assign the initial count passed to the sem_create()
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{//Wait
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
        while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0);
        sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{       //Signal
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
        struct lock *lock;

        lock = kmalloc(sizeof(*lock));
        if (lock == NULL) {
                return NULL;
        }
        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }
        #if USE_SEMAPHORE_FOR_LOCK
                lock->lk_sem = sem_create(lock->lk_name,1);
                if(lock->lk_sem ==NULL){
                     kfree(lock->lk_name);
                     kfree(lock);
                     return NULL;   
                }
        #else 
        lock->lk_wchan=wchan_create(lock->lk_name);
        if (lock->lk_wchan == NULL){
                kfree (lock->lk_name);
                kfree (lock);
                return NULL;
        }
        #endif
                lock->lk_owner = NULL;
                spinlock_init(&lock->lk_lock);
        
	//HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);
        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);
        spinlock_cleanup(&lock->lk_lock);
        #if USE_SEMAPHORE_FOR_LOCK
        sem_destroy(lock->lk_sem);
        #else 
        wchan_destroy(lock->lk_wchan);

        #endif

        kfree(lock->lk_name);
        kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
        KASSERT(lock!=NULL);
        
        if(lock_do_i_hold(lock)){
                kprintf("OH MY OWNER!");
        }
        KASSERT(!(lock_do_i_hold(lock)));
        KASSERT(curthread->t_in_interrupt ==false);
        #if USE_SEMAPHORE_FOR_LOCK
        P(lock->lk_sem);
        spinlock_acquire(&lock->lk_lock);
        //the semaphore of the lock is implemented initially with count = 1
        //is EXTREMELY IMPORTANT to call first P and after the acquire of the spinlock because 
        // OS161 forbids sleeping/realeasing the CPU while owning a spinlocks: this could be a cause of deadlock. 
        #else 
        spinlock_acquire(&lock->lk_lock);
        while (lock->lk_owner!=NULL){// in busy waiting
                wchan_sleep(lock->lk_wchan,&lock->lk_lock);
        }
        #endif 
        KASSERT(lock->lk_owner == NULL);
        lock->lk_owner = curthread;
        spinlock_release(&lock->lk_lock);
        

        (void)lock;  // suppress warning until code gets written

	/* Call this (atomically) once the lock is acquired */
	//HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
}

void
lock_release(struct lock *lock)
{
	KASSERT(lock != NULL);
        KASSERT(lock_do_i_hold(lock));
        spinlock_acquire(&lock->lk_lock);
        lock->lk_owner = NULL;
        #if USE_SEMAPHORE_FOR_LOCK
                V(lock->lk_sem);//call the signal 
        #else 
                wchan_wakeone(lock->lk_wchan,&lock->lk_lock);
        #endif
        spinlock_release(&lock->lk_lock);

}

bool
lock_do_i_hold(struct lock *lock)
{      //basically the function assures that who calls the acquire for the lock 
        //and who calls the release is in the both cases the same thread.
        //The check can be done also without a spinlock (on this case spinlok struct is not needed in the lock struct)
        //, but is a good practice protect the access
       bool res;
       spinlock_acquire(&lock->lk_lock);
       res = lock->lk_owner == curthread;
       spinlock_release(&lock->lk_lock);
       return res;
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
        struct cv *cv;

        cv = kmalloc(sizeof(*cv));
        if (cv == NULL) {
                return NULL;
        }

        cv->cv_name = kstrdup(name);
        if (cv->cv_name==NULL) {
                kfree(cv);
                return NULL;
        }

        cv->cv_wchan = wchan_create(cv->cv_name);
        if (cv->cv_wchan == NULL){
                kfree(cv);
                return NULL;
        }
        spinlock_init(&cv->cv_lock);
        return cv;
}

void
cv_destroy(struct cv *cv)
{
        KASSERT(cv != NULL);
        spinlock_cleanup(&cv->cv_lock);
        wchan_destroy(cv->cv_wchan);
        kfree(cv->cv_name);
        kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{     //remember that the cv works always with an upstream lock
      KASSERT(lock != NULL);
      KASSERT(cv != NULL);
      KASSERT(lock_do_i_hold(lock));//must be owned
      //under a spinlock protection I release the lock and get to sleep 
      spinlock_acquire(&cv->cv_lock);
      lock_release(lock);
      wchan_sleep(cv->cv_wchan,&cv->cv_lock);
      spinlock_release(&cv->cv_lock);   
      lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
       KASSERT(lock!=NULL);
       KASSERT(cv!=NULL);
       KASSERT(lock_do_i_hold(lock)); // must be the owner the reason why I pass the lock
       /*here the spinlock is NOT required, as no atomic operation 
	has to be done. The spinlock is just acquired because needed by wakeone */
        spinlock_acquire(&cv->cv_lock);
        wchan_wakeone(cv->cv_wchan,&cv->cv_lock);
        spinlock_release(&cv->cv_lock);

}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
KASSERT(lock != NULL);
KASSERT(cv != NULL);
KASSERT(lock_do_i_hold(lock));//must be the owner the reason why i pass the lock
spinlock_acquire(&cv->cv_lock);
wchan_wakeall(cv->cv_wchan,&cv->cv_lock);
spinlock_release(&cv->cv_lock);
}

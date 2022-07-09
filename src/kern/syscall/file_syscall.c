#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <uio.h>
#include <syscall.h>
#include <vnode.h>
#include <vfs.h>
#include <current.h>
#include <addrspace.h>
#include <proc.h>

int sys_read(userptr_t buf, int nbytes)
{ // the function accept 2 parameters the buffer and n of bytes to read (a char is 1 byte)
    char localBuff[1];

    for (int i = 0; i < nbytes; i++){
        kgets(localBuff, 1);
        if (localBuff[0] == '\0'){
            return 1; // if receive the terminator of the the string i can return 1
        }
        else{
            ((char *)buf)[i] = localBuff[0];
        }
    }
    return 0;
}

int sys_write(userptr_t buf, int nbytes, int *retval){
    
    char *p=(char*) buf;
    for(int i = 0; i < nbytes; i++){
        putch(p[i]);
    }
    (*retval) = 1;

    return 0;
}

void sys_exit(int code){
    struct proc *p =curproc;
    p->p_status = code & 0xff; //takes the lower 8 bits
    //proc_remthread(curthread); //this function remove the thred from the parent process.
    // This is useful when the wait() calls the as_destroy that require no active threads.
    V(p->p_sem); //here I call the V for the semaphore. The P is called on proc_wait() function
    //struct addrspace *as = proc_getas();
    /*struct thread *cur;
    cur = curthread;
	cur->t_state = code;*/
    //as_destroy(as); // now the wait() function call the as_destroy()
    thread_exit();
    panic("!!!");
}

int 
sys_waitpid(pid_t pid, userptr_t statusp, int options){
    struct proc *p = proc_search_pid(pid);  ;
    int s;
    (void) options ;//NOT USED TO REMOVE THE WARNING
    if(p==NULL) return -1;
    s = proc_wait(p);
    if (statusp!=NULL)
        *(int*)statusp = s;
    return pid;
}
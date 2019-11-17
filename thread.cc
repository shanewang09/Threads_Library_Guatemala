#include "interrupt.h"
#include "thread.h"
#include <ucontext.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <map>
#include <queue>
using namespace std;

static ucontext_t* current_thread;
static ucontext_t* initial_thread;
static queue<ucontext_t*> q_ready;
static queue<ucontext_t*> q_delete;
static map< unsigned int, queue<ucontext_t*> > q_lock;
static map< unsigned int, ucontext_t*> lock_user;
static map< unsigned int, map<unsigned int, queue<ucontext_t*> > > q_block;
static map< ucontext_t*, bool> m_delete;
static bool libinit_called = false;
static bool init_create = true;

void run_ready_thread();
void run_thread(thread_startfunc_t func, void *arg);

int thread_libinit(thread_startfunc_t func, void *arg)
{
  interrupt_disable();
  // Error testing:
  if (libinit_called == true)
  {
    interrupt_enable();
    return -1;
  }
  libinit_called = true;
  try {
    current_thread = (ucontext_t *) new ucontext_t;
    getcontext(current_thread);
    char *stack = new char[STACK_SIZE];
    current_thread->uc_stack.ss_sp = stack;
    current_thread->uc_stack.ss_size = STACK_SIZE;
    current_thread->uc_stack.ss_flags = 0;
    current_thread->uc_link = NULL;
    initial_thread = (ucontext_t*) new ucontext_t;
    getcontext(initial_thread);
    makecontext(current_thread, (void (*)())run_thread, 2, func, arg);
    swapcontext(initial_thread, current_thread);
  } catch(std::bad_alloc&) {
    interrupt_enable();
    return -1;
  }
  interrupt_enable();
  cout << "Thread library exiting.\n";
  exit(0);
}

int thread_create(thread_startfunc_t func, void *arg)
{
  interrupt_disable();
  // Error testing:
  if (libinit_called == false)
  {
    interrupt_enable();
    return -1;
  }
  try {
    ucontext_t *ucontext_ptr = (ucontext_t *)new ucontext_t;
    getcontext(ucontext_ptr);
    ucontext_ptr->uc_stack.ss_sp = new char[STACK_SIZE];
    ucontext_ptr->uc_stack.ss_size = STACK_SIZE;
    ucontext_ptr->uc_stack.ss_flags = 0;
    ucontext_ptr->uc_link = NULL;
    makecontext(ucontext_ptr, (void (*)())run_thread, 2, func, arg);
    q_ready.push(ucontext_ptr);
  } catch(std::bad_alloc&) {
    interrupt_enable();
    return -1;
  }
  interrupt_enable();
  return 0;
}
int thread_yield(void) {
    interrupt_disable();
    if (libinit_called == false) {
      interrupt_enable();
      return -1;
    }
    //getcontext(current_thread);
    q_ready.push(/*&thread_curr -> ucontext*/ current_thread);
    interrupt_enable();
    run_ready_thread();
    return 0;
}
int thread_lock(unsigned int lock)
{
  interrupt_disable();
  // Error testing:
  if (libinit_called == false)
  {
    interrupt_enable();
    return -1;
  }
  // Calling lock on thread that already has lock:
  if (lock_user[lock] == current_thread)
  {
    interrupt_enable();
    return -1;
  }
  if (lock_user[lock] == 0 || lock_user[lock] == NULL)
  {
    lock_user[lock] = current_thread;
    interrupt_enable();
  }
  else
  {
    q_lock[lock].push(current_thread);
    // THIS INTERRUPT MAY BE PROBLEMATIC:
    interrupt_enable();
    run_ready_thread();
    lock_user[lock] = current_thread;
  }

  return 0;
}

int thread_unlock(unsigned int lock)
{
  interrupt_disable();
  // Error testing:
  //cout << "Unlocking\n";
  if (libinit_called == false)
  {
    interrupt_enable();
    return -1;
  }
  // Calling unlock on thread that does not have lock:
  if (!lock_user.count(lock) || (lock_user.count(lock) && lock_user[lock] != current_thread))
  {
    interrupt_enable();
    return -1;
  }
  lock_user[lock] = 0;
  if (!q_lock[lock].empty())
  {
    if (q_lock[lock].front() == 0) {
      interrupt_enable();
      return -1;
    }
    q_ready.push(q_lock[lock].front());
    q_lock[lock].pop();
  }
  interrupt_enable();
  return 0;
}

int thread_wait(unsigned int lock, unsigned int cond)
{
  interrupt_disable();
  // Error testing:
  //cout << "Waiting\n";
  if (libinit_called == false)
  {
    interrupt_enable();
    return -1;
  }
  if (!q_lock[lock].empty()) {
    ucontext_t* front = q_lock[lock].front();
    q_lock[lock].pop();
    lock_user[lock] = front;
    q_ready.push(front);
  } else {
    lock_user[lock] = 0;
  }

    //add the running thread onto the WAITING queue
    q_block[lock][cond].push(current_thread);

    //take the first thread from the ready queue and run that
    interrupt_enable();
    run_ready_thread();

    //when you get here, it means that the waiting thread has been taken off from the ready queue
    //via a context switch and now the waiting thread needs to re-aquire the lock again
    thread_lock(lock);
    return 0;


}
int thread_signal(unsigned int lock, unsigned int cond)
{
  interrupt_disable();
  // Error testing:
  if (libinit_called == false)
  {
    interrupt_enable();
    return -1;
  }
  if (!q_block[lock][cond].empty())
  {
    if (q_block[lock][cond].front() == 0)
    {
      interrupt_enable();
      return -1;
    }
    q_ready.push(q_block[lock][cond].front());
    q_block[lock][cond].pop();
  }
  interrupt_enable();
  return 0;
}
int thread_broadcast(unsigned int lock, unsigned int cond)
{
  // Error testing:
  interrupt_disable();
  if (libinit_called == false)
  {
    interrupt_enable();
    return -1;
  }

  while (!q_block[lock][cond].empty())
  {
    if (q_block[lock][cond].front() == 0)
    {
      interrupt_enable();
      return -1;
    }
    q_ready.push(q_block[lock][cond].front());
    q_block[lock][cond].pop();
  }
  interrupt_enable();
  return 0;
}
void run_ready_thread() {
  interrupt_disable();
  if(!q_ready.empty()) {

    ucontext_t* new_thread = q_ready.front();
    ucontext_t* swap_thread = current_thread;
    q_ready.pop();
    //getcontext(current_thread);
    current_thread = new_thread;

    swapcontext(swap_thread, new_thread);
  } else {
    setcontext(initial_thread);
  }

  interrupt_enable();
}
void run_thread(thread_startfunc_t func, void *arg) {
  interrupt_enable();
  if(init_create) {
    init_create = false;
    //start_preemptions(1,1,0);
  }

  // interrupt_disable();
  func(arg);
  interrupt_disable();
  while(!q_delete.empty()) {
    //cout << "Deleting\n";
    ucontext_t* del_thread = q_delete.front();
    q_delete.pop();
    delete del_thread->uc_stack.ss_sp;
    //cout << "Checkpoint\n";
    del_thread->uc_stack.ss_sp = NULL;
    del_thread->uc_stack.ss_size = 0;
    del_thread->uc_stack.ss_flags = 0;
    del_thread->uc_link = NULL;
    //cout << "Checkpoint\n";
    delete del_thread;
    //cout << "Checkpoint\n";
  }
  q_delete.push(current_thread);
  interrupt_enable();
  run_ready_thread();
}

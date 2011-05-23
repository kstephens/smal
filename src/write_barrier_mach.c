/*
Modified from http://www.brianweb.net/misc/mach_exceptions_demo.c

Copyright (c) 2003, Brian Alliet. All rights reserved. 

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

3. Neither the name of the author nor the names of contributors may be used
to endorse or promote products derived from this software without specific
prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS `AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/thread_status.h>
#include <mach/exception.h>
#include <mach/task.h>
#include <pthread.h>
#include <sys/mman.h>
     

#define DIE(x) do { fprintf(stderr,"%s failed at %d\n",x,__LINE__); exit(1); } while(0)
#define ABORT(x) do { fprintf(stderr,"%s at %d\n",x,__LINE__); } while(0)

#ifndef my_handle_exn
/* this is not specific to mach exception handling, its just here to separate required mach code from YOUR code */
static int my_handle_exn(char *addr, integer_t code);
#endif

/* These are not defined in any header, although they are documented */
extern boolean_t exc_server(mach_msg_header_t *,mach_msg_header_t *);
extern kern_return_t exception_raise(
    mach_port_t,mach_port_t,mach_port_t,
    exception_type_t,exception_data_t,mach_msg_type_number_t);
extern kern_return_t exception_raise_state(
    mach_port_t,mach_port_t,mach_port_t,
    exception_type_t,exception_data_t,mach_msg_type_number_t,
    thread_state_flavor_t*,thread_state_t,mach_msg_type_number_t,
    thread_state_t,mach_msg_type_number_t*);
extern kern_return_t exception_raise_state_identity(
    mach_port_t,mach_port_t,mach_port_t,
    exception_type_t,exception_data_t,mach_msg_type_number_t,
    thread_state_flavor_t*,thread_state_t,mach_msg_type_number_t,
    thread_state_t,mach_msg_type_number_t*);

#define MAX_EXCEPTION_PORTS 16

static struct {
    mach_msg_type_number_t count;
    exception_mask_t      masks[MAX_EXCEPTION_PORTS];
    exception_handler_t   ports[MAX_EXCEPTION_PORTS];
    exception_behavior_t  behaviors[MAX_EXCEPTION_PORTS];
    thread_state_flavor_t flavors[MAX_EXCEPTION_PORTS];
} old_exc_ports;

static mach_port_t exception_port;

static void *exc_thread(void *junk) {
    mach_msg_return_t r;
    /* These two structures contain some private kernel data. We don't need to
       access any of it so we don't bother defining a proper struct. The
       correct definitions are in the xnu source code. */
    struct {
        mach_msg_header_t head;
        char data[256];
    } reply;
    struct {
        mach_msg_header_t head;
        mach_msg_body_t msgh_body;
        char data[1024];
    } msg;
    
    for(;;) {
        r = mach_msg(
            &msg.head,
            MACH_RCV_MSG|MACH_RCV_LARGE,
            0,
            sizeof(msg),
            exception_port,
            MACH_MSG_TIMEOUT_NONE,
            MACH_PORT_NULL);
        if(r != MACH_MSG_SUCCESS) DIE("mach_msg");
        
        /* Handle the message (calls catch_exception_raise) */
        if(!exc_server(&msg.head,&reply.head)) DIE("exc_server");
        
        /* Send the reply */
        r = mach_msg(
            &reply.head,
            MACH_SEND_MSG,
            reply.head.msgh_size,
            0,
            MACH_PORT_NULL,
            MACH_MSG_TIMEOUT_NONE,
            MACH_PORT_NULL);
        if(r != MACH_MSG_SUCCESS) DIE("mach_msg");
    }
    /* not reached */
}

static void exn_init() {
    kern_return_t r;
    mach_port_t me;
    pthread_t thread;
    pthread_attr_t attr;
    exception_mask_t mask;
    
    me = mach_task_self();
    r = mach_port_allocate(me,MACH_PORT_RIGHT_RECEIVE,&exception_port);
    if(r != MACH_MSG_SUCCESS) DIE("mach_port_allocate");
    
    r = mach_port_insert_right(me,exception_port,exception_port,
    	MACH_MSG_TYPE_MAKE_SEND);
    if(r != MACH_MSG_SUCCESS) DIE("mach_port_insert_right");
    
    /* for others see mach/exception_types.h */
    mask = EXC_MASK_BAD_ACCESS;
    
    /* get the old exception ports */
    r = task_get_exception_ports(
        me,
        mask,
        old_exc_ports.masks,
        &old_exc_ports.count,
        old_exc_ports.ports,
        old_exc_ports.behaviors,
        old_exc_ports.flavors
    );
    if(r != MACH_MSG_SUCCESS) DIE("task_get_exception_ports");
    
    /* set the new exception ports */
    r = task_set_exception_ports(
        me,
        mask,
        exception_port,
        EXCEPTION_DEFAULT,
        MACHINE_THREAD_STATE
    );
    if(r != MACH_MSG_SUCCESS) DIE("task_set_exception_ports");
    
    if(pthread_attr_init(&attr) != 0) DIE("pthread_attr_init");
    if(pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED) != 0) 
        DIE("pthread_attr_setdetachedstate");
    
    if(pthread_create(&thread,&attr,exc_thread,NULL) != 0)
        DIE("pthread_create");
    pthread_attr_destroy(&attr);
}

/* The source code for Apple's GDB was used as a reference for the exception
   forwarding code. This code is similar to be GDB code only because there is 
   only one way to do it. */
static kern_return_t forward_exception(
        mach_port_t thread,
        mach_port_t task,
        exception_type_t exception,
        exception_data_t data,
        mach_msg_type_number_t data_count
) {
    int i;
    kern_return_t r;
    mach_port_t port;
    exception_behavior_t behavior;
    thread_state_flavor_t flavor;
    
    thread_state_data_t thread_state;
    mach_msg_type_number_t thread_state_count = THREAD_STATE_MAX;
        
    for(i=0;i<old_exc_ports.count;i++)
        if(old_exc_ports.masks[i] & (1 << exception))
            break;
    if(i==old_exc_ports.count) ABORT("No handler for exception!");
    
    port = old_exc_ports.ports[i];
    behavior = old_exc_ports.behaviors[i];
    flavor = old_exc_ports.flavors[i];

    if(behavior != EXCEPTION_DEFAULT) {
        r = thread_get_state(thread,flavor,thread_state,&thread_state_count);
        if(r != KERN_SUCCESS)
            ABORT("thread_get_state failed in forward_exception");
    }
    
    switch(behavior) {
        case EXCEPTION_DEFAULT:
            r = exception_raise(port,thread,task,exception,data,data_count);
            break;
        case EXCEPTION_STATE:
            r = exception_raise_state(port,thread,task,exception,data,
                data_count,&flavor,thread_state,thread_state_count,
                thread_state,&thread_state_count);
            break;
        case EXCEPTION_STATE_IDENTITY:
            r = exception_raise_state_identity(port,thread,task,exception,data,
                data_count,&flavor,thread_state,thread_state_count,
                thread_state,&thread_state_count);
            break;
        default:
            r = KERN_FAILURE; /* make gcc happy */
            ABORT("forward_exception: unknown behavior");
            break;
    }
    
    if(behavior != EXCEPTION_DEFAULT) {
        r = thread_set_state(thread,flavor,thread_state,thread_state_count);
        if(r != KERN_SUCCESS)
            ABORT("thread_set_state failed in forward_exception");
    }
    
    return r;
}

#define FWD() forward_exception(thread,task,exception,code,code_count)

kern_return_t
catch_exception_raise(
   mach_port_t exception_port,mach_port_t thread,mach_port_t task,
   exception_type_t exception,exception_data_t code,
   mach_msg_type_number_t code_count
) {
    kern_return_t r;
    char *addr;
#ifdef __POWERPC__
    thread_state_flavor_t flavor = PPC_EXCEPTION_STATE;
    mach_msg_type_number_t exc_state_count = PPC_EXCEPTION_STATE_COUNT;
    ppc_exception_state_t exc_state;
#define exc_state_addr exc_state.dar
#endif
#ifdef __x86_64__ 
    thread_state_flavor_t flavor = x86_EXCEPTION_STATE;
    mach_msg_type_number_t exc_state_count = x86_EXCEPTION_STATE_COUNT;
    x86_exception_state_t exc_state;
#define exc_state_addr exc_state.ues.es64.__faultvaddr
#endif
#ifdef __x86__
    thread_state_flavor_t flavor = x86_EXCEPTION_STATE;
    mach_msg_type_number_t exc_state_count = x86_EXCEPTION_STATE_COUNT;
    x86_exception_state_t exc_state;
#define exc_state_addr exc_state.ues.es32.__faultvaddr
#endif
#ifndef exc_state_addr
#	error Unknown Mach architecture
#endif

    
    /* we should never get anything that isn't EXC_BAD_ACCESS, but just in case */
    if(exception != EXC_BAD_ACCESS) {
        /* We aren't interested, pass it on to the old handler */
        fprintf(stderr,"Exception: 0x%x Code: 0x%x 0x%x in catch....\n",
            exception,
            code_count > 0 ? code[0] : -1,
            code_count > 1 ? code[1] : -1); 
        return FWD();
    }

    r = thread_get_state(thread,flavor,
        (natural_t*)&exc_state,&exc_state_count);
    if(r != KERN_SUCCESS) DIE("thread_get_state");
    
    /* This is the address that caused the fault */
    addr = (char*) exc_state_addr;
#undef exc_state_addr

    /* you could just as easily put your code in here, I'm just doing this to 
       point out the required code */
    if(!my_handle_exn(addr, code[0])) return FWD();
    
    return KERN_SUCCESS;
}
#undef FWD

/* These should never be called, but just in case...  */
kern_return_t catch_exception_raise_state(mach_port_name_t exception_port,
    int exception, exception_data_t code, mach_msg_type_number_t codeCnt,
    int flavor, thread_state_t old_state, int old_stateCnt,
    thread_state_t new_state, int new_stateCnt)
{
    ABORT("catch_exception_raise_state");
    return(KERN_INVALID_ARGUMENT);
}
kern_return_t catch_exception_raise_state_identity(
    mach_port_name_t exception_port, mach_port_t thread, mach_port_t task,
    int exception, exception_data_t code, mach_msg_type_number_t codeCnt,
    int flavor, thread_state_t old_state, int old_stateCnt, 
    thread_state_t new_state, int new_stateCnt)
{
    ABORT("catch_exception_raise_state_identity");
    return(KERN_INVALID_ARGUMENT);
}

#ifndef my_handle_exn
static char *data;

static int my_handle_exn(char *addr, integer_t code) {
    if(code == KERN_INVALID_ADDRESS) {
        fprintf(stderr,"Got KERN_INVALID_ADDRESS at %p\n",addr);
        exit(1);
    }
    if(code == KERN_PROTECTION_FAILURE) {
        fprintf(stderr,"Got KERN_PROTECTION_FAILURE at %p\n",addr);
        if(addr == NULL) {
            fprintf(stderr,"Tried to dereference NULL");
            exit(1);
        }
        if(addr == data) {
            fprintf(stderr,"Making data (%p) writeable\n",addr);
            if(mprotect(addr,4096,PROT_READ|PROT_WRITE) < 0) DIE("mprotect");
            return 1; // we handled it
        }
        fprintf(stderr,"Got KERN_PROTECTION_FAILURE at %p\n",addr);
        return 0; // forward it
    }

    /* You should filter out anything you don't want in the catch_exception_raise... above
        and forward it */
    fprintf(stderr,"Got unknown code %d at %p\n",(int)code,addr);
    return 0;
}
#endif

#ifdef SMAL_LAB_TEST
int main(int argc, char *argv[]) {
    /* fire up the exception thread */
    exn_init();
    
    data = valloc(4096);
    if(data == 0) DIE("valloc");
    if(mprotect(data,4096,PROT_READ) < 0) DIE("mprotect");
    
    /* this will be fixed */
    strcpy(data,"foo");
    printf("%s\n",data);
    
    /* these will each exit */
    /* this will throw a KERN_INVALID_ADDRESS */
    printf("%d",*((int*)0xdeadbeef));
    /* this will throw a KERN_PROTECTION_FAILURE */
    printf("%d",*((int*)0));
    return 0;
}
#endif

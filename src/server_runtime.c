#include "server_runtime.h"
#include "command_service.h"
#include "protocol.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#ifdef BANK_TESTING
/* Test transport injects already-connected descriptors; supervisor is unchanged. */
extern int test_accept_connected(int listener);
#define accept_connected(fd) test_accept_connected(fd)
#else
#define accept_connected(fd) accept(fd,NULL,NULL)
#endif
static volatile sig_atomic_t stopping;
static void stop_handler(int number) { (void)number;stopping=1; }
static void reap(pid_t *children) {
    for(int i=0;i<WIRE_MAX_CLIENTS;i++)if(children[i]>0) {
        pid_t p=waitpid(children[i],NULL,WNOHANG);
        if(p==children[i]||(p<0&&errno==ECHILD))children[i]=0;
    }
}
int server_run(int listener,int frame_timeout_ms,int session_timeout_ms) {
    pid_t children[WIRE_MAX_CLIENTS]={0};stopping=0;
    struct sigaction sa;memset(&sa,0,sizeof(sa));sigemptyset(&sa.sa_mask);sa.sa_handler=stop_handler;
    if(sigaction(SIGTERM,&sa,NULL)!=0||sigaction(SIGINT,&sa,NULL)!=0)return -1;
    signal(SIGCHLD,SIG_DFL);signal(SIGPIPE,SIG_IGN);
    int result=0;
    while(!stopping) {
        reap(children);
        struct pollfd p={.fd=listener,.events=POLLIN};int rc=poll(&p,1,50);
        if(rc<0){if(errno==EINTR)continue;result=-1;break;}
        if(!rc)continue;
        if(!(p.revents&POLLIN)){result=-1;break;}
        int fd=accept_connected(listener);
        if(fd<0){if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)continue;result=-1;break;}
        int slot=0;while(slot<WIRE_MAX_CLIENTS&&children[slot])slot++;
        if(slot==WIRE_MAX_CLIENTS){close(fd);continue;}
        pid_t pid=fork();
        if(pid==0) {
            close(listener);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGALRM,SIG_DFL);
            alarm((unsigned)(session_timeout_ms+999)/1000);
            command_session(fd,frame_timeout_ms,session_timeout_ms);
            close(fd);_exit(0);
        }
        close(fd);
        if(pid<0){result=-1;break;}
        children[slot]=pid;
    }
    /* Stop accepting; let active operations finish briefly before termination. */
    int64_t end=wire_now_ms()+2000;
    do {
        reap(children);int active=0;
        for(int i=0;i<WIRE_MAX_CLIENTS;i++)if(children[i])active=1;
        if(!active)break;
        (void)poll(NULL,0,20);
    }while(wire_now_ms()<end);
    for(int i=0;i<WIRE_MAX_CLIENTS;i++)if(children[i])kill(children[i],SIGTERM);
    for(int i=0;i<WIRE_MAX_CLIENTS;i++)if(children[i]) {
        while(waitpid(children[i],NULL,0)<0&&errno==EINTR){}
    }
    return result;
}

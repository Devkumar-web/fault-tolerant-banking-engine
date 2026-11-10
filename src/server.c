#include "server_runtime.h"
#include "protocol.h"
#include "requests.h"
#include "txn_journal.h"
#include "app_config.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <limits.h>
static int parse_port(const char *s) {
    char *end;errno=0;long n=strtol(s,&end,10);
    return errno||!*s||*end||n<1||n>65535?-1:(int)n;
}
int main(int argc,char **argv) {
    int local=argc==3&&strcmp(argv[1],"--unix")==0;
    int port=argc==2?parse_port(argv[1]):-1;
    if(!local&&port<0){fprintf(stderr,"Usage: %s PORT | --unix SOCKET_PATH\n",argv[0]);return 1;}
    if(local&&strlen(argv[2])>=sizeof(((struct sockaddr_un *)0)->sun_path)) {
        fprintf(stderr,"UNIX socket path too long\n");return 1;
    }
    umask(0077);
    int guard=open(".server_v2.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(guard<0||flock(guard,LOCK_EX|LOCK_NB)!=0){perror("Data directory already in use or unavailable");return 1;}
    if(requests_init()!=0||migrate_customers_if_legacy(CUSPATH)<0||
       journal_recover_at_startup(CUSPATH)<0||journal_reconcile_history()!=0||
       journal_initialize_missing_account_history(CUSPATH)!=0) {
        fprintf(stderr,"Startup storage validation/recovery failed\n");close(guard);return 1;
    }
    int fd=socket(local?AF_UNIX:AF_INET,SOCK_STREAM,0);if(fd<0){perror("socket");close(guard);return 1;}
    int rc;
    if(local) {
        struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path,argv[2],strlen(argv[2])+1);
        /* Never unlink an existing endpoint owned by another process. */
        rc=bind(fd,(struct sockaddr *)&a,sizeof(a));
    } else {
        struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr={htonl(INADDR_LOOPBACK)}};
        int one=1;(void)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
        rc=bind(fd,(struct sockaddr *)&a,sizeof(a));
    }
    if(rc!=0){perror("bind");close(fd);close(guard);return 1;}
    if(listen(fd,WIRE_MAX_CLIENTS)!=0||fcntl(fd,F_SETFL,O_NONBLOCK)!=0){perror("listen");rc=-1;}
    else rc=server_run(fd,WIRE_TIMEOUT_MS,WIRE_SESSION_MS);
    close(fd);if(local)unlink(argv[2]);close(guard);return rc==0?0:1;
}

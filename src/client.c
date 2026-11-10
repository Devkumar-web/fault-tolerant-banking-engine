#include "protocol.h"
#include "financial_utils.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <stdint.h>
static int number(const char *s,uint64_t *out) {
    if(!s||!*s)return -1;
    for(const char *p=s;*p;p++)if(*p<'0'||*p>'9')return -1;
    errno=0;char *end;unsigned long long v=strtoull(s,&end,10);
    if(errno||*end||!v)return -1;
    *out=(uint64_t)v;return 0;
}
static int exchange(int fd,WireFrame *req,WireReply *reply) {
    WireFrame response;int64_t deadline=wire_now_ms()+WIRE_TIMEOUT_MS;
    if(wire_write(fd,req,deadline)!=0||wire_read(fd,&response,deadline)!=0)return -1;
    if(response.command!=(req->command|WIRE_RESPONSE)||response.request_id!=req->request_id||response.length!=20){errno=EPROTO;return -1;}
    reply->status=wire_u32(response.payload);reply->transaction_id=wire_u64(response.payload+4);
    reply->balance=wire_u64(response.payload+12);return 0;
}
int main(int argc,char **argv) {
    if(argc<6){fprintf(stderr,"Usage: %s PORT|unix:PATH ACCOUNT TYPE balance|deposit|withdraw|transfer REQUEST_ID [AMOUNT [DESTINATION]]\nPassword is read from stdin. TYPE is 1 (normal) or 2 (joint).\n",argv[0]);return 1;}
    uint64_t id,type,rid,dest=0;long long amount=0;char error[128];
    if(number(argv[2],&id)||id>INT_MAX||number(argv[3],&type)||type>2||number(argv[5],&rid))return 1;
    uint16_t command=!strcmp(argv[4],"balance")?CMD_BALANCE:!strcmp(argv[4],"deposit")?CMD_DEPOSIT:
                     !strcmp(argv[4],"withdraw")?CMD_WITHDRAW:!strcmp(argv[4],"transfer")?CMD_TRANSFER:0;
    if(!command||argc!=(command==CMD_BALANCE?6:command==CMD_TRANSFER?8:7))return 1;
    if(command!=CMD_BALANCE&&parse_monetary_amount(argv[6],&amount,error,sizeof(error))!=0){fprintf(stderr,"%s\n",error);return 1;}
    if(command==CMD_TRANSFER&&(number(argv[7],&dest)||dest>INT_MAX))return 1;
    int local=strncmp(argv[1],"unix:",5)==0;uint64_t port=0;
    if(!local&&(number(argv[1],&port)||port>65535))return 1;
    if(local&&strlen(argv[1]+5)>=sizeof(((struct sockaddr_un *)0)->sun_path))return 1;
    char password[130];struct termios previous,hidden;int tty=tcgetattr(STDIN_FILENO,&previous)==0;
    fprintf(stderr,"Password: ");fflush(stderr);
    if(tty){hidden=previous;hidden.c_lflag&=~ECHO;if(tcsetattr(STDIN_FILENO,TCSAFLUSH,&hidden)!=0)return 1;}
    char *got=fgets(password,sizeof(password),stdin);
    if(tty){(void)tcsetattr(STDIN_FILENO,TCSAFLUSH,&previous);fputc('\n',stderr);}
    if(!got)return 1;
    size_t len=strcspn(password,"\r\n");if(len==0||len>128)return 1;password[len]=0;
    int fd=socket(local?AF_UNIX:AF_INET,SOCK_STREAM,0);if(fd<0){perror("socket");return 1;}
    if(fcntl(fd,F_SETFL,O_NONBLOCK)!=0){close(fd);return 1;}
    int rc;
    if(local){struct sockaddr_un a={.sun_family=AF_UNIX};memcpy(a.sun_path,argv[1]+5,strlen(argv[1]+5)+1);rc=connect(fd,(struct sockaddr *)&a,sizeof(a));}
    else {struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr={htonl(INADDR_LOOPBACK)}};rc=connect(fd,(struct sockaddr *)&a,sizeof(a));}
    if(rc!=0&&errno==EINPROGRESS) {
        struct pollfd p={.fd=fd,.events=POLLOUT};int e=0;socklen_t n=sizeof(e);
        rc=poll(&p,1,WIRE_TIMEOUT_MS)>0&&getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&n)==0&&e==0?0:-1;
    }
    if(rc!=0){fprintf(stderr,"Connection failed\n");close(fd);return 1;}
    WireFrame req={.command=CMD_LOGIN,.request_id=rid,.length=(uint32_t)len+8};WireReply reply;
    wire_put32(req.payload,(uint32_t)id);wire_put32(req.payload+4,(uint32_t)type);memcpy(req.payload+8,password,len);memset(password,0,sizeof(password));
    if(exchange(fd,&req,&reply)!=0||reply.status!=WIRE_OK){fprintf(stderr,"Login failed\n");close(fd);return 1;}
    memset(&req,0,sizeof(req));req.command=command;req.request_id=rid;
    if(command!=CMD_BALANCE){req.length=command==CMD_TRANSFER?12:8;wire_put64(req.payload,(uint64_t)amount);if(dest)wire_put32(req.payload+8,(uint32_t)dest);}
    fprintf(stderr,"Request ID: %llu (reuse this ID and exact payload after an uncertain outcome)\n",(unsigned long long)rid);
    if(exchange(fd,&req,&reply)!=0){fprintf(stderr,"No confirmed response; reconnect with the SAME request ID and payload.\n");close(fd);return 2;}
    printf("status=%u transaction=%llu balance_paise=%llu request=%llu\n",reply.status,
        (unsigned long long)reply.transaction_id,(unsigned long long)reply.balance,(unsigned long long)rid);
    close(fd);return reply.status==WIRE_OK?0:2;
}

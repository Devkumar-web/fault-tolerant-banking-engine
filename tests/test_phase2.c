#include "server_runtime.h"
#include "requests.h"
#include "command_service.h"
#include "account_storage.h"
#include "txn_journal.h"
#include "financial_utils.h"
#include <assert.h>
#include <crypt.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
static void wait_exit(pid_t p,int expected) {
    int s;assert(waitpid(p,&s,0)==p);assert(WIFEXITED(s)&&WEXITSTATUS(s)==expected);
}
static void seed(void) {
    struct Customer c[3]={0};
    for(int i=0;i<3;i++) {
        c[i].accountNumber=101+i;c[i].balance=10000;c[i].activeStatus=1;c[i].accountType=1;
        strcpy(c[i].customerName,"Phase2 test");strcpy(c[i].password,crypt("testpass","$6$testsalt$"));
    }
    int fd=open("customers.txt",O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);
    assert(write(fd,c,sizeof(c))==sizeof(c));assert(close(fd)==0);assert(requests_init()==0);
}
static long long balance(int id) {
    int fd=open("customers.txt",O_RDONLY);assert(fd>=0);struct Customer c;
    assert(find_customer_record(fd,id,NULL,&c)==0);close(fd);return c.balance;
}
static WireFrame transaction(uint64_t id,uint16_t command,uint64_t amount,int dest) {
    WireFrame f={.command=command,.request_id=id,.length=command==CMD_TRANSFER?12:8};
    wire_put64(f.payload,amount);if(dest)wire_put32(f.payload+8,(uint32_t)dest);return f;
}
static WireReply execute(int account,WireFrame *f) { return request_execute(account,f,wire_now_ms()+5000); }
static void repeat_and_conflict(void) {
    WireFrame f=transaction(10,CMD_TRANSFER,100,102);WireReply a=execute(101,&f),b=execute(101,&f);
    assert(a.status==WIRE_OK&&a.transaction_id==b.transaction_id&&a.balance==b.balance);
    assert(balance(101)==9900&&balance(102)==10100);
    wire_put64(f.payload,101);assert(execute(101,&f).status==WIRE_CONFLICT);
    assert(balance(101)==9900);
    f=transaction(10,CMD_DEPOSIT,100,0);assert(execute(102,&f).status==WIRE_OK);
    assert(balance(102)==10200); /* Same request ID is independent for another identity. */
    f=transaction(11,CMD_WITHDRAW,99999,0);assert(execute(101,&f).status==WIRE_INSUFFICIENT);
    WireFrame add=transaction(12,CMD_DEPOSIT,100000,0);assert(execute(101,&add).status==WIRE_OK);
    assert(execute(101,&f).status==WIRE_INSUFFICIENT); /* Rejected outcome remains stable. */
    puts("PASS persistent replay, payload conflict, identity scope, cached rejection");
}
static void concurrent_duplicate(void) {
    long long before=balance(101);WireFrame f=transaction(20,CMD_DEPOSIT,50,0);
    pid_t p[4];
    for(int i=0;i<4;i++) {p[i]=fork();assert(p[i]>=0);if(!p[i]){assert(execute(101,&f).status==WIRE_OK);_exit(0);}}
    for(int i=0;i<4;i++)wait_exit(p[i],0);
    assert(balance(101)==before+50);puts("PASS concurrent duplicate requests commit once");
}
static void crash_retries(void) {
    const char *points[]={"reserved","intent","account_written","journal_commit","committed","completed"};
    for(size_t i=0;i<sizeof(points)/sizeof(points[0]);i++) {
        long long a=balance(101),b=balance(102);WireFrame f=transaction(100+i,CMD_TRANSFER,10,102);
        pid_t p=fork();assert(p>=0);
        if(!p){assert(setenv("BANK_TEST_CRASH",points[i],1)==0);(void)execute(101,&f);_exit(91);}
        wait_exit(p,90);
        assert(journal_recover_at_startup("customers.txt")>=0);assert(journal_reconcile_history()==0);
        WireReply first=execute(101,&f),second=execute(101,&f);
        assert(first.status==WIRE_OK&&second.status==WIRE_OK&&first.transaction_id==second.transaction_id);
        assert(first.balance==(uint64_t)(a-10)&&second.balance==first.balance);
        assert(balance(101)==a-10&&balance(102)==b+10);
    }
    puts("PASS retries across six process-crash boundaries and restart recovery");
}
static void protocol_edges(void) {
    int s[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);
    WireFrame f=transaction(500,CMD_DEPOSIT,20,0),readback;
    int rc=wire_write(s[0],&f,wire_now_ms()+500);if(rc)perror("wire_write test");assert(rc==0);
    uint8_t raw[WIRE_HEADER+8];assert(read(s[1],raw,sizeof(raw))==sizeof(raw));
    assert(wire_u32(raw)==WIRE_MAGIC&&raw[5]==2&&wire_u32(raw+8)==8&&wire_u64(raw+16)==500);
    pid_t p=fork();assert(p>=0);
    if(!p){close(s[1]);for(size_t i=0;i<sizeof(raw);i++){assert(write(s[0],raw+i,1)==1);(void)poll(NULL,0,1);}close(s[0]);_exit(0);}
    close(s[0]);assert(wire_read(s[1],&readback,wire_now_ms()+1000)==0);
    assert(readback.command==f.command&&readback.request_id==500&&wire_u64(readback.payload)==20);
    close(s[1]);wait_exit(p,0);
    for(int mode=0;mode<5;mode++) {
        assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);uint8_t bad[WIRE_HEADER];memcpy(bad,raw,sizeof(bad));
        if(mode==0)bad[0]=0;
        if(mode==1)bad[5]=99;
        if(mode==2)wire_put32(bad+8,WIRE_MAX_PAYLOAD+1);
        if(mode==3)wire_put32(bad+12,1);
        if(mode==4)wire_put64(bad+16,0);
        assert(write(s[0],bad,sizeof(bad))==sizeof(bad));assert(wire_read(s[1],&readback,wire_now_ms()+100)==-1&&errno==EPROTO);
        close(s[0]);close(s[1]);
    }
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);
    assert(write(s[0],raw,3)==3);int64_t start=wire_now_ms();
    assert(wire_read(s[1],&readback,start+60)==-1&&errno==ETIMEDOUT);
    assert(wire_now_ms()-start<500);close(s[0]);close(s[1]);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);assert(write(s[0],raw,10)==10);close(s[0]);
    assert(wire_read(s[1],&readback,wire_now_ms()+100)==-1);close(s[1]);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);close(s[1]);
    assert(wire_write(s[0],&f,wire_now_ms()+100)==-1);close(s[0]);
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);uint8_t fill[4096]={0};
    struct iovec iov={.iov_base=fill,.iov_len=sizeof(fill)};
    struct msghdr message={.msg_iov=&iov,.msg_iovlen=1};
    while(sendmsg(s[0],&message,MSG_DONTWAIT|MSG_NOSIGNAL)>0){}
    assert(wire_write(s[0],&f,wire_now_ms()+60)==-1&&errno==ETIMEDOUT);close(s[0]);close(s[1]);
    puts("PASS fragmentation, wire encoding, malformed headers, EOF, read/write deadlines");
}
static WireReply roundtrip(int fd,WireFrame *f) {
    WireFrame r;assert(wire_write(fd,f,wire_now_ms()+1000)==0);
    assert(wire_read(fd,&r,wire_now_ms()+1000)==0);
    assert(r.command==(f->command|WIRE_RESPONSE)&&r.request_id==f->request_id&&r.length==20);
    return (WireReply){wire_u32(r.payload),wire_u64(r.payload+4),wire_u64(r.payload+12)};
}
static void sessions(void) {
    long long before=balance(101);WireFrame money=transaction(600,CMD_DEPOSIT,25,0);uint64_t txn=0;
    for(int attempt=0;attempt<3;attempt++) {
        int s[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);pid_t p=fork();assert(p>=0);
        if(!p){close(s[0]);command_session(s[1],200,2000);close(s[1]);_exit(0);}
        close(s[1]);assert(roundtrip(s[0],&money).status==WIRE_AUTH);
        WireFrame login={.command=CMD_LOGIN,.request_id=1,.length=16};
        wire_put32(login.payload,101);wire_put32(login.payload+4,1);memcpy(login.payload+8,"bad_pass",8);
        assert(roundtrip(s[0],&login).status==WIRE_AUTH);memcpy(login.payload+8,"testpass",8);
        assert(roundtrip(s[0],&login).status==WIRE_OK);
        if(attempt==0) {
            assert(wire_write(s[0],&money,wire_now_ms()+1000)==0);
            close(s[0]);wait_exit(p,0);continue; /* Deliberately discard response. */
        }
        WireReply r=roundtrip(s[0],&money);assert(r.status==WIRE_OK);
        if(attempt==2)assert(r.transaction_id==txn);else txn=r.transaction_id;
        WireFrame unknown={.command=55,.request_id=8};assert(roundtrip(s[0],&unknown).status==WIRE_INVALID);
        close(s[0]);wait_exit(p,0);
    }
    assert(balance(101)==before+25);puts("PASS authenticated sessions, unknown commands, lost-response reconnect replay");
}
static void session_limits(void) {
    for(int mode=0;mode<2;mode++) {
        int s[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);pid_t p=fork();assert(p>=0);
        if(!p){close(s[0]);command_session(s[1],1000,mode?100:5000);close(s[1]);_exit(0);}
        close(s[1]);WireFrame r;
        if(!mode) {
            WireFrame f={.command=CMD_LOGIN,.request_id=1,.length=16};
            wire_put32(f.payload,101);wire_put32(f.payload+4,1);memcpy(f.payload+8,"testpass",8);
            assert(roundtrip(s[0],&f).status==WIRE_OK);
            f.command=CMD_BALANCE;f.length=0;
            for(unsigned i=1;i<WIRE_MAX_COMMANDS;i++)assert(roundtrip(s[0],&f).status==WIRE_OK);
        }
        int64_t start=wire_now_ms();
        assert(wire_read(s[0],&r,start+500)==-1&&errno!=ETIMEDOUT);
        if(mode)assert(wire_now_ms()-start<500);
        close(s[0]);wait_exit(p,0);
    }
    puts("PASS per-session command limit and absolute lifetime deadline");
}
/* Pass a connected endpoint to the supervisor without creating a listener. */
int test_accept_connected(int listener) {
    char byte;struct iovec iov={.iov_base=&byte,.iov_len=1};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
    if(recvmsg(listener,&msg,0)!=1)return -1;
    struct cmsghdr *c=CMSG_FIRSTHDR(&msg);assert(c&&c->cmsg_type==SCM_RIGHTS);
    int fd;memcpy(&fd,CMSG_DATA(c),sizeof(fd));return fd;
}
static int admit(int transport) {
    int s[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,s)==0);
    char byte='x';struct iovec iov={.iov_base=&byte,.iov_len=1};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    memset(&control,0,sizeof(control));
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof(control.bytes)};
    struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c),&s[1],sizeof(int));assert(sendmsg(transport,&msg,MSG_NOSIGNAL)==1);
    close(s[1]);return s[0];
}
static void supervisor(void) {
    int control[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,control)==0);
    pid_t parent=fork();assert(parent>=0);
    if(!parent){close(control[0]);int rc=server_run(control[1],5000,10000);close(control[1]);_exit(rc==0?0:1);}
    close(control[1]);int clients[WIRE_MAX_CLIENTS];
    WireFrame login={.command=CMD_LOGIN,.request_id=1,.length=16};
    wire_put32(login.payload,101);wire_put32(login.payload+4,1);memcpy(login.payload+8,"testpass",8);
    for(int i=0;i<WIRE_MAX_CLIENTS;i++){clients[i]=admit(control[0]);assert(roundtrip(clients[i],&login).status==WIRE_OK);}
    int extra=admit(control[0]);WireFrame reply;
    assert(wire_read(extra,&reply,wire_now_ms()+1000)==-1&&errno!=ETIMEDOUT);close(extra);
    assert(kill(parent,SIGTERM)==0);wait_exit(parent,0);
    for(int i=0;i<WIRE_MAX_CLIENTS;i++) {
        assert(wire_read(clients[i],&reply,wire_now_ms()+500)==-1&&errno!=ETIMEDOUT);close(clients[i]);
    }
    close(control[0]);puts("PASS supervisor admission cap, worker termination, and reaping");
}
static void lock_timeout(void) {
    int fd=open(REQUEST_STORE_PATH,O_RDWR);assert(fd>=0);
    assert(journal_lock_range(fd,0,0,F_WRLCK)==0);
    pid_t p=fork();assert(p>=0);
    if(!p) {
        WireFrame f=transaction(900,CMD_DEPOSIT,1,0);
        assert(request_execute(101,&f,wire_now_ms()+60).status==WIRE_BUSY);_exit(0);
    }
    wait_exit(p,0);close(fd);puts("PASS request coordinator lock timeout");
}
static uint32_t record_checksum(const uint8_t *data) {
    uint32_t h=2166136261u;for(size_t i=0;i<60;i++)h=(h^data[i])*16777619u;return h;
}
static void capacity(void) {
    assert(mkdir("capacity",0700)==0);assert(chdir("capacity")==0);seed();
    WireFrame f=transaction(10000,CMD_WITHDRAW,99999,0);
    assert(execute(101,&f).status==WIRE_INSUFFICIENT);
    int fd=open(REQUEST_STORE_PATH,O_RDWR);assert(fd>=0);uint8_t slot[REQUEST_SLOT_SIZE];
    assert(journal_pread_full(fd,slot,sizeof(slot),0)==0);
    for(int i=1;i<REQUEST_CAPACITY;i++) {
        wire_put64(slot+16,10000u+(uint64_t)i);wire_put64(slot+24,1001u+(uint64_t)i);
        wire_put32(slot+60,record_checksum(slot));
        assert(journal_pwrite_full(fd,slot,sizeof(slot),(off_t)i*REQUEST_SLOT_SIZE)==0);
    }
    assert(fsync(fd)==0);close(fd);
    assert(execute(101,&f).status==WIRE_INSUFFICIENT);
    f=transaction(99999,CMD_DEPOSIT,1,0);
    assert(execute(101,&f).status==WIRE_CAPACITY&&balance(101)==10000);
    assert(chdir("..")==0);puts("PASS bounded request store preserves old results at capacity");
}
static void corruption(void) {
    long long before=balance(101);int fd=open(REQUEST_STORE_PATH,O_WRONLY|O_APPEND);assert(fd>=0);
    assert(write(fd,"x",1)==1);close(fd);WireFrame f=transaction(999,CMD_DEPOSIT,1,0);
    assert(execute(101,&f).status==WIRE_IO&&balance(101)==before);
    fd=open(REQUEST_STORE_PATH,O_RDWR);assert(fd>=0);struct stat st;
    assert(fstat(fd,&st)==0&&ftruncate(fd,st.st_size-1)==0);
    uint8_t byte;assert(pread(fd,&byte,1,40)==1);byte^=1;assert(pwrite(fd,&byte,1,40)==1);close(fd);
    assert(execute(101,&f).status==WIRE_IO&&balance(101)==before);
    puts("PASS truncated request store and checksum corruption fail closed");
}
int main(int argc,char **argv) {
    alarm(40);seed();
    if(argc==2&&strcmp(argv[1],"--seed")==0)return 0;
    repeat_and_conflict();concurrent_duplicate();crash_retries();protocol_edges();sessions();session_limits();supervisor();lock_timeout();corruption();capacity();
    return 0;
}

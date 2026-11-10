#include "requests.h"
#include "account_service_internal.h"
#include "txn_journal.h"
#include "financial_utils.h"
#include <limits.h>
#include <poll.h>
_Static_assert((int)BANK_OK == (int)WIRE_OK && (int)BANK_INVALID == (int)WIRE_INVALID &&
    (int)BANK_NOT_FOUND == (int)WIRE_NOT_FOUND && (int)BANK_INACTIVE == (int)WIRE_INACTIVE &&
    (int)BANK_INSUFFICIENT == (int)WIRE_INSUFFICIENT && (int)BANK_LIMIT == (int)WIRE_LIMIT &&
    (int)BANK_IO == (int)WIRE_IO && (int)BANK_RECOVERY == (int)WIRE_RECOVERY,
    "Business statuses must match wire status mapping");
#define REQUEST_MAGIC UINT32_C(0x52513250)
#define RESULT_MAGIC UINT32_C(0x52513244)
#ifdef BANK_TESTING
static void crash_at(const char *point) {
    const char *s=getenv("BANK_TEST_CRASH");if(s&&strcmp(s,point)==0)_exit(90);
}
#else
#define crash_at(point) ((void)0)
#endif
static uint32_t checksum(const uint8_t *p) {
    uint32_t h=2166136261u;for(size_t i=0;i<60;i++)h=(h^p[i])*16777619u;return h;
}
static int zero(const uint8_t *p,size_t n) { for(size_t i=0;i<n;i++)if(p[i])return 0;return 1; }
static WireReply failure(uint32_t code) { WireReply r={.status=code};return r; }
int requests_init(void) {
    const char *paths[]={REQUEST_STORE_PATH,COUNTER_TXN_PATH,JOURNAL_PATH,JOURNAL_HISTORY_PATH};
    for(size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++) {
        int fd=open(paths[i],O_RDWR|O_CREAT|O_NOFOLLOW,0600);if(fd<0)return -1;
        int rc=fsync(fd);if(close(fd)!=0)rc=-1;if(rc!=0)return -1;
    }
    int fd=open(".",O_RDONLY|O_DIRECTORY);if(fd<0)return -1;
    int rc=fsync(fd);if(close(fd)!=0)rc=-1;
    return rc;
}
static int acquire(int fd,int64_t deadline) {
    struct flock l={.l_type=F_WRLCK,.l_whence=SEEK_SET};
    for(;;) {
        if(fcntl(fd,F_SETLK,&l)==0)return 0;
        if(errno!=EAGAIN&&errno!=EACCES&&errno!=EINTR)return -1;
        if(wire_now_ms()>=deadline){errno=ETIMEDOUT;return -1;}
        (void)poll(NULL,0,5);
    }
}
/* Under the request-store lock. A journal hit is the authoritative commit result. */
static int journal_result(uint64_t id,int account,uint16_t command,int destination,
                          uint64_t amount,WireReply *reply) {
    int fd=open(JOURNAL_PATH,O_RDONLY);if(fd<0)return -1;
    struct stat st;int rc=-1,found=0;
    if(fstat(fd,&st)!=0||st.st_size%(off_t)sizeof(struct JournalEntry))goto done;
    for(off_t off=0;off<st.st_size;off+=(off_t)sizeof(struct JournalEntry)) {
        struct JournalEntry e;
        if(journal_pread_full(fd,&e,sizeof(e),off)!=0||e.magic!=JOURNAL_MAGIC)goto done;
        if(e.txn_id!=id)continue;
        if(found)goto done;
        found=1;
        int type=command==CMD_DEPOSIT?TXN_TYPE_DEPOSIT:command==CMD_WITHDRAW?TXN_TYPE_WITHDRAW:TXN_TYPE_TRANSFER;
        if(e.txn_type!=type||e.amount_paise<0||(uint64_t)e.amount_paise!=amount||
           (command==CMD_DEPOSIT?(e.dst_acc!=account||e.src_acc!=0):
             (e.src_acc!=account||e.dst_acc!=(command==CMD_TRANSFER?destination:0))))goto done;
        if(e.state!=TXN_STATE_COMMITTED){rc=-1;goto done;}
        long long bal=command==CMD_DEPOSIT?e.dst_after_bal:e.src_after_bal;
        if(bal<0)goto done;
        *reply=(WireReply){.status=WIRE_OK,.transaction_id=id,.balance=(uint64_t)bal};
    }
    rc=found;
done:
    if(close(fd)!=0)rc=-1;
    return rc;
}
WireReply request_execute(int identity,const WireFrame *req,int64_t deadline) {
    if(!req||identity<=0||!req->request_id||
       (req->command!=CMD_DEPOSIT&&req->command!=CMD_WITHDRAW&&req->command!=CMD_TRANSFER)||
       req->length!=(req->command==CMD_TRANSFER?12u:8u))return failure(WIRE_INVALID);
    uint64_t amount=wire_u64(req->payload);
    uint32_t dest=req->command==CMD_TRANSFER?wire_u32(req->payload+8):0;
    if(amount==0||amount>MAX_TRANSACTION_PAISE||dest>INT_MAX||
       (req->command==CMD_TRANSFER&&(dest==0||dest==(uint32_t)identity)))return failure(WIRE_INVALID);
    int fd=open(REQUEST_STORE_PATH,O_RDWR|O_NOFOLLOW);if(fd<0)return failure(WIRE_IO);
    WireReply reply=failure(WIRE_IO);
    if(acquire(fd,deadline)!=0){reply=failure(errno==ETIMEDOUT?WIRE_BUSY:WIRE_IO);goto done;}
    struct stat st;
    if(fstat(fd,&st)!=0||st.st_size%REQUEST_SLOT_SIZE||
       st.st_size>(off_t)REQUEST_CAPACITY*REQUEST_SLOT_SIZE)goto done;
    off_t match=-1;uint8_t saved[REQUEST_SLOT_SIZE]={0};
    for(off_t off=0;off<st.st_size;off+=REQUEST_SLOT_SIZE) {
        uint8_t slot[REQUEST_SLOT_SIZE];
        if(journal_pread_full(fd,slot,sizeof(slot),off)!=0||wire_u32(slot)!=REQUEST_MAGIC||
           wire_u32(slot+4)!=2||wire_u32(slot+60)!=checksum(slot)||
           (!zero(slot+64,64)&&(wire_u32(slot+64)!=RESULT_MAGIC||
                                wire_u32(slot+124)!=checksum(slot+64))))goto done;
        if(wire_u32(slot+8)==(uint32_t)identity&&wire_u64(slot+16)==req->request_id) {
            if(match>=0)goto done;
            match=off;memcpy(saved,slot,sizeof(saved));
        }
    }
    if(match>=0) {
        if(wire_u32(saved+12)!=req->command||wire_u32(saved+32)!=dest||wire_u64(saved+40)!=amount) {
            reply=failure(WIRE_CONFLICT);goto done;
        }
        if(!zero(saved+64,64)) {
            reply=(WireReply){wire_u32(saved+68),wire_u64(saved+72),wire_u64(saved+80)};
            if(reply.status>WIRE_RECOVERY)reply=failure(WIRE_IO);
            goto done;
        }
    } else {
        if(st.st_size==(off_t)REQUEST_CAPACITY*REQUEST_SLOT_SIZE){reply=failure(WIRE_CAPACITY);goto done;}
        uint64_t id=get_next_txn_id();if(!id)goto done;
        wire_put32(saved,REQUEST_MAGIC);wire_put32(saved+4,2);
        wire_put32(saved+8,(uint32_t)identity);wire_put32(saved+12,req->command);
        wire_put64(saved+16,req->request_id);wire_put64(saved+24,id);
        wire_put32(saved+32,dest);wire_put64(saved+40,amount);wire_put32(saved+60,checksum(saved));
        match=st.st_size;
        if(journal_pwrite_full(fd,saved,sizeof(saved),match)!=0||fsync(fd)!=0)goto done;
    }
    crash_at("reserved");
    uint64_t id=wire_u64(saved+24);
    int hit=journal_result(id,identity,req->command,(int)dest,amount,&reply);
    if(hit<0){reply=failure(WIRE_RECOVERY);goto done;}
    if(!hit) {
        BankResult result;
        BankOperation op=req->command==CMD_DEPOSIT?BANK_DEPOSIT:req->command==CMD_WITHDRAW?BANK_WITHDRAW:BANK_TRANSFER;
        BankStatus status=bank_transact_reserved(op,identity,(int)dest,(long long)amount,id,&result);
        reply=(WireReply){.status=(uint32_t)status,.transaction_id=result.transaction_id,
                          .balance=status==BANK_OK?(uint64_t)result.balance:0};
        /* I/O may follow a durable intent: retry must inspect the journal first. */
        if(status==BANK_IO||status==BANK_RECOVERY)goto done;
    }
    crash_at("committed");
    wire_put32(saved+64,RESULT_MAGIC);wire_put32(saved+68,reply.status);
    wire_put64(saved+72,reply.transaction_id);wire_put64(saved+80,reply.balance);
    wire_put32(saved+124,checksum(saved+64));
    if(journal_pwrite_full(fd,saved+64,64,match+64)!=0||fsync(fd)!=0){reply=failure(WIRE_RECOVERY);goto done;}
    crash_at("completed");
done:
    /* Closing releases the process-associated coordinator lock. */
    if(close(fd)!=0)reply=failure(WIRE_IO);
    return reply;
}

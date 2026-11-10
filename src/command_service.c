#include "txn_journal.h"
#include "command_service.h"
#include "protocol.h"
#include "requests.h"
#include "account_storage.h"
#include "app_config.h"
#include <crypt.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
/* Credentials are compared only after verifying fixed records are terminated. */
static int authenticate(const WireFrame *req) {
    if(req->length<9||req->length>136)return 0;
    uint32_t id=wire_u32(req->payload),type=wire_u32(req->payload+4);
    if(id==0||id>INT_MAX||(type!=1&&type!=2)||memchr(req->payload+8,0,req->length-8))return 0;
    char password[129];memcpy(password,req->payload+8,req->length-8);password[req->length-8]=0;
    int fd=open(CUSPATH,O_RDONLY);if(fd<0)return 0;
    struct Customer c;off_t off;int ok=0;
    if(find_customer_record(fd,(int)id,&off,&c)==0&&lock_customer_record(fd,off,F_RDLCK)==0) {
        if(read_customer_record(fd,off,&c)==0&&c.activeStatus==1&&c.accountType==(int)type&&
           memchr(c.password,0,sizeof(c.password))) {
            char *hash=crypt(password,c.password);
            ok=hash&&strcmp(hash,c.password)==0;
        }
        (void)unlock_customer_record(fd,off);
    }
    close(fd);memset(password,0,sizeof(password));return ok?(int)id:0;
}
static WireReply get_balance(int id) {
    WireReply r={.status=WIRE_IO};int fd=open(CUSPATH,O_RDONLY);if(fd<0)return r;
    struct Customer c;off_t off;
    int found=find_customer_record(fd,id,&off,&c);
    if(found==1)r.status=WIRE_NOT_FOUND;
    if(found==0&&lock_customer_record(fd,off,F_RDLCK)==0) {
        if(read_customer_record(fd,off,&c)==0) {
            r.status=c.activeStatus==1?WIRE_OK:WIRE_INACTIVE;
            if(c.balance<0)r.status=WIRE_IO;
            if(journal_recovery_is_required()||journal_has_prepared_transactions()!=0)r.status=WIRE_RECOVERY;
            if(r.status==WIRE_OK)r.balance=(uint64_t)c.balance;
        }
        if(unlock_customer_record(fd,off)!=0)r.status=WIRE_IO;
    }
    if(close(fd)!=0)r.status=WIRE_IO;
    return r;
}
void command_session(int fd,int frame_timeout_ms,int session_timeout_ms) {
    int identity=0;int64_t end=wire_now_ms()+session_timeout_ms;
    for(unsigned count=0;count<WIRE_MAX_COMMANDS;count++) {
        int64_t deadline=wire_now_ms()+frame_timeout_ms;if(deadline>end)deadline=end;
        WireFrame req,response;
        if(wire_read(fd,&req,deadline)!=0)break;
        WireReply reply={.status=WIRE_INVALID};int quit=0;
        if(req.command==CMD_LOGIN) {
            if(!identity) { identity=authenticate(&req);reply.status=identity?WIRE_OK:WIRE_AUTH; }
        } else if(!identity)reply.status=WIRE_AUTH;
        else if(req.command==CMD_BALANCE&&req.length==0)reply=get_balance(identity);
        else if(req.command==CMD_DEPOSIT||req.command==CMD_WITHDRAW||req.command==CMD_TRANSFER)
            reply=request_execute(identity,&req,end);
        else if(req.command==CMD_LOGOUT&&req.length==0){reply.status=WIRE_OK;quit=1;}
        wire_reply_frame(&response,&req,reply);
        deadline=wire_now_ms()+frame_timeout_ms;if(deadline>end)deadline=end;
        if(wire_write(fd,&response,deadline)!=0||quit)break;
    }
}

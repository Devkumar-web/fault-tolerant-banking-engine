#include "protocol.h"
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
uint32_t wire_u32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
uint64_t wire_u64(const uint8_t *p) { return ((uint64_t)wire_u32(p)<<32)|wire_u32(p+4); }
void wire_put32(uint8_t *p,uint32_t v) { for(int i=3;i>=0;--i){p[i]=(uint8_t)v;v>>=8;} }
void wire_put64(uint8_t *p,uint64_t v) { wire_put32(p,(uint32_t)(v>>32));wire_put32(p+4,(uint32_t)v); }
int64_t wire_now_ms(void) {
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)!=0) return -1;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int io_all(int fd,void *data,size_t size,int writing,int64_t deadline) {
    uint8_t *p=data; size_t done=0;
    while(done<size) {
        int64_t now=wire_now_ms(); if(now<0) return -1;
        int64_t remaining=deadline-now;
        if(remaining<=0){errno=ETIMEDOUT;return -1;}
        struct pollfd pollfd={.fd=fd,.events=writing?POLLOUT:POLLIN};
        int rc=poll(&pollfd,1,remaining>INT_MAX?INT_MAX:(int)remaining);
        if(rc<0){if(errno==EINTR)continue;return -1;}
        if(!rc){errno=ETIMEDOUT;return -1;}
        struct iovec iov={.iov_base=p+done,.iov_len=size-done};
        struct msghdr message={.msg_iov=&iov,.msg_iovlen=1};
        ssize_t n=writing ? sendmsg(fd,&message,MSG_DONTWAIT|MSG_NOSIGNAL)
                          : recvmsg(fd,&message,MSG_DONTWAIT);
        if(n<0){if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)continue;return -1;}
        if(!n){errno=ECONNRESET;return -1;}
        done+=(size_t)n;
    }
    return 0;
}
int wire_read(int fd,WireFrame *f,int64_t deadline) {
    uint8_t h[WIRE_HEADER];
    if(!f){errno=EINVAL;return -1;}
    memset(f,0,sizeof(*f));
    if(io_all(fd,h,sizeof(h),0,deadline)!=0)return -1;
    uint16_t version=(uint16_t)((h[4]<<8)|h[5]);
    f->command=(uint16_t)((h[6]<<8)|h[7]);
    f->length=wire_u32(h+8); f->request_id=wire_u64(h+16);
    if(wire_u32(h)!=WIRE_MAGIC||version!=WIRE_VERSION||wire_u32(h+12)!=0||
       f->length>WIRE_MAX_PAYLOAD||!f->request_id){errno=EPROTO;return -1;}
    return io_all(fd,f->payload,f->length,0,deadline);
}
int wire_write(int fd,const WireFrame *f,int64_t deadline) {
    if(!f||f->length>WIRE_MAX_PAYLOAD||!f->request_id){errno=EINVAL;return -1;}
    uint8_t packet[WIRE_HEADER+WIRE_MAX_PAYLOAD]={0};
    wire_put32(packet,WIRE_MAGIC);packet[5]=WIRE_VERSION;
    packet[6]=(uint8_t)(f->command>>8);packet[7]=(uint8_t)f->command;
    wire_put32(packet+8,f->length);wire_put64(packet+16,f->request_id);
    memcpy(packet+WIRE_HEADER,f->payload,f->length);
    return io_all(fd,packet,WIRE_HEADER+f->length,1,deadline);
}
void wire_reply_frame(WireFrame *out,const WireFrame *req,WireReply reply) {
    memset(out,0,sizeof(*out));out->command=req->command|WIRE_RESPONSE;
    out->request_id=req->request_id;out->length=20;
    wire_put32(out->payload,reply.status);wire_put64(out->payload+4,reply.transaction_id);
    wire_put64(out->payload+12,reply.balance);
}

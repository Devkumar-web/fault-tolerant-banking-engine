#include "account_service.h"
#include "account_storage.h"
#include "financial_utils.h"
#include "txn_journal.h"
#include "session.h"
#include "customer_functions.h"
#include <assert.h>
#include <sys/wait.h>
#include <signal.h>
#include <crypt.h>

static void seed(void) {
    struct Customer rows[3] = {0};
    for (int i = 0; i < 3; ++i) {
        rows[i].accountNumber = 101+i;
        rows[i].balance = i == 2 ? MAX_BALANCE_PAISE : 10000;
        rows[i].activeStatus = 1; rows[i].accountType = 1;
        strcpy(rows[i].customerName, "Test account");
        strcpy(rows[i].password, crypt("testpass", "$6$saltsalt$"));
    }
    int fd = open("customers.txt", O_CREAT|O_TRUNC|O_WRONLY, 0600);
    assert(fd >= 0); assert(write(fd, rows, sizeof(rows)) == sizeof(rows)); assert(close(fd) == 0);
}
static long long balance(int id) {
    int fd = open("customers.txt", O_RDONLY); assert(fd >= 0);
    struct Customer c; assert(find_customer_record(fd,id,NULL,&c)==0);
    assert(close(fd)==0); return c.balance;
}
static void wait_ok(pid_t pid) {
    int status; assert(waitpid(pid,&status,0)==pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(void) {
    alarm(30);
    long long value; char error[128];
    assert(parse_monetary_amount(" 12.34 ",&value,error,sizeof(error))==0 && value==1234);
    const char *bad[]={"", "0", "-1", "1.001", "abc", "1x", "999999999999999999999999"};
    for (size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(parse_monetary_amount(bad[i],&value,error,sizeof(error))==-1);
    assert(parse_monetary_amount(NULL,&value,error,sizeof(error))==-1);
    assert(safe_add_paise(MAX_BALANCE_PAISE,1,&value)==-1);
    assert(safe_sub_paise(LLONG_MIN,1,&value)==-1);
    puts("PASS amount parsing and arithmetic boundaries");
    seed(); BankResult result;
    assert(bank_transact(BANK_DEPOSIT,101,0,500,&result)==BANK_OK && result.balance==10500);
    assert(bank_transact(BANK_WITHDRAW,101,0,500,&result)==BANK_OK && result.balance==10000);
    assert(bank_transact(BANK_WITHDRAW,101,0,10001,&result)==BANK_INSUFFICIENT);
    assert(balance(101)==10000);
    assert(bank_transact(BANK_DEPOSIT,103,0,1,&result)==BANK_LIMIT);
    assert(bank_transact(BANK_TRANSFER,101,103,1,&result)==BANK_LIMIT);
    assert(balance(101)==10000 && balance(103)==MAX_BALANCE_PAISE);
    assert(bank_transact(BANK_TRANSFER,101,101,1,&result)==BANK_INVALID);
    assert(bank_transact(BANK_DEPOSIT,999,0,1,&result)==BANK_NOT_FOUND);
    assert(bank_transact(BANK_DEPOSIT,101,0,0,&result)==BANK_INVALID);
    assert(bank_transact(BANK_TRANSFER,101,102,2500,&result)==BANK_OK);
    assert(balance(101)==7500 && balance(102)==12500);
    puts("PASS deposits, withdrawals, overdraft, limits, transfer conservation");
    pid_t children[2];
    for (int i=0;i<2;i++) {
        children[i]=fork(); assert(children[i]>=0);
        if (!children[i]) {
            for(int j=0;j<20;j++) assert(bank_transact(BANK_TRANSFER,101+i,102-i,1,&result)==BANK_OK);
            _exit(0);
        }
    }
    wait_ok(children[0]); wait_ok(children[1]);
    assert(balance(101)==7500 && balance(102)==12500);
    puts("PASS concurrent cross-transfers");
    int fd=open("customers.txt",O_RDWR); assert(fd>=0);
    struct Customer c; off_t off;
    assert(find_customer_record(fd,102,&off,&c)==0); c.activeStatus=0;
    assert(write_customer_record(fd,off,&c)==0); assert(close(fd)==0);
    assert(bank_transact(BANK_TRANSFER,101,102,1,&result)==BANK_INACTIVE);
    assert(balance(101)==7500);
    assert(loginCustomer(-1,101,"wrong",1)==-4);
    assert(loginCustomer(-1,101,"testpass",2)==-3);
    assert(loginCustomer(-1,101,"testpass",1)==1); release_session_semaphore();
    puts("PASS inactive account and customer authentication");
    int pipefd[2]; assert(pipe(pipefd)==0);
    pid_t owner=fork(); assert(owner>=0);
    if(!owner) {
        close(pipefd[0]); assert(acquire_session_lock("customer",101)==0);
        assert(write(pipefd[1],"x",1)==1); close(pipefd[1]);
        for(;;) pause();
    }
    close(pipefd[1]); char ch; assert(read(pipefd[0],&ch,1)==1); close(pipefd[0]);
    assert(acquire_session_lock("customer",101)==-1);
    assert(kill(owner,SIGKILL)==0); int st; assert(waitpid(owner,&st,0)==owner && WIFSIGNALED(st));
    assert(acquire_session_lock("customer",101)==0); release_session_semaphore();
    assert(acquire_session_lock("customer",101)==0); release_session_semaphore();
    puts("PASS session exclusion and cleanup after process termination");
    struct JournalEntry e={0}; e.txn_id=get_next_txn_id(); assert(e.txn_id);
    e.txn_type=TXN_TYPE_DEPOSIT; e.dst_acc=101; e.amount_paise=100;
    e.dst_before_bal=balance(101); e.dst_after_bal=e.dst_before_bal+100;
    assert(journal_log_prepared(&e)>=0);
    assert(bank_transact(BANK_DEPOSIT,101,0,1,&result)==BANK_RECOVERY);
    assert(journal_recover_at_startup("customers.txt")>=0);
    assert(balance(101)==7600);
    assert(journal_recover_at_startup("customers.txt")>=0 && balance(101)==7600);
    assert(journal_reconcile_history()==0);
    puts("PASS prepared transaction recovery and repeat recovery");
    int sockets[2]; assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    pid_t ui=fork(); assert(ui>=0);
    if(!ui) {
        close(sockets[0]); depositMoney(sockets[1],101); close(sockets[1]); _exit(0);
    }
    close(sockets[1]); char packet[4096];
    assert(socket_read_packet(sockets[0],packet,sizeof(packet))==0);
    assert(strstr(packet,"deposit amount"));
    assert(socket_write_packet(sockets[0],"1.25")==0);
    assert(socket_read_packet(sockets[0],packet,sizeof(packet))==0);
    assert(strstr(packet,"Deposit successful"));
    assert(socket_write_packet(sockets[0],"ACK")==0);
    close(sockets[0]); wait_ok(ui); assert(balance(101)==7725);
    puts("PASS menu adapter through socket transport to persistent transaction");
    return 0;
}

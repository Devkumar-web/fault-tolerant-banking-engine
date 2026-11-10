#ifndef BANK_FUN_H
#define BANK_FUN_H

struct Customer {
    int accountNumber;
    long long balance; /* Stored in minor currency units (paise): 100 paise = 1 INR */
    char customerName[50];
    char password[256];
    int accountType;  /* 1 -> Normal Account, 2 -> Joint Account */
    int activeStatus; /* 0 -> deactivate, 1 -> activate, -1 -> deleted */
};

struct Employee{
    int empID;
    char firstName[20], lastName[20], password[256];
    int role; /* 0 -> Manager, 1 -> Employee */
};

struct LoanDetails{
    int empID, accountNumber;
    int loanID, loanAmount;
    int status; /* 0 -> requested, 1 -> pending, 2 -> approved, 3 -> rejected */
};

struct trans_histroy{
    int acc_no;
    char hist[1024];
};

struct FeedBack{
    int accountNumber;
    char feedback[1024];
};

struct Counter{
    int count;
};

struct AdminCred {
    char username[50];
    char password[256];
};

#endif /* BANK_FUN_H */

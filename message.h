#include<stdio.h>

typedef struct message
{
    /* data */
    int msg_code;
    char msg[30];
    char buf[4096];
    int param1;
    int param2;
    int param3;
    char charParam[48];
} message;

/*
 * Copyright (C) 2015 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       Basic ccn-lite relay example (produce and consumer via shell)
 *
 * @author      Oliver Hahm <oliver.hahm@inria.fr>
 *
 * @}
 */

#include <stdio.h>

#include "tlsf-malloc.h"
#include "msg.h"
#include "shell.h"
#include "ccn-lite-riot.h"
#include "net/gnrc/netif.h"

#include "thread.h"
#include "msg.h"
#include "ringbuffer.h"
#include "periph/uart.h"
#include "uart_stdio.h"

#define UART_BUFSIZE        (128U)

#define PRINTER_PRIO        (THREAD_PRIORITY_MAIN - 1)
#define PRINTER_TYPE        (0xabcd)

#ifndef UART_STDIO_DEV
#define UART_STDIO_DEV      (UART_UNDEF)
#endif

typedef struct {
    char rx_mem[UART_BUFSIZE];
    ringbuffer_t rx_buf;
} uart_ctx_t;

static uart_ctx_t ctx[UART_NUMOF];

static kernel_pid_t printer_pid;
static char printer_stack[THREAD_STACKSIZE_MAIN];



/* main thread's message queue */
#define MAIN_QUEUE_SIZE     (8)
static msg_t _main_msg_queue[MAIN_QUEUE_SIZE];

/* 10kB buffer for the heap should be enough for everyone */
#define TLSF_BUFFER     (10240 / sizeof(uint32_t))
static uint32_t _tlsf_heap[TLSF_BUFFER];

char ndn_buf[512];
int index=4; //start from 3rd

// const  char temp_buf[45]={0x06,0x2b,0x07,0x16,0x08,0x03,0x6e,0x64,0x6e,0x08,0x04,0x74,0x65,0x73,0x74,0x08,0x09,0x6d,
 //                        0x79,0x63,0x6f,0x6e,0x74,0x65,0x6e,0x74,0x14,0x00,0x15,0x06,0x68,0x65,0x6c,0x6c,0x6f,0x0a,0x16,0x05,0x1b,0x01,0x00,0x1c,0x00,0x17,0x00};


void content_to_ndn(char* name_buf, unsigned char* content_buf)
{
    
   // char hex_buf[16]={"0123456789abcdef"};

    printf("Convert content to NDN pkt\n");
    memset(ndn_buf,'\0',sizeof(ndn_buf));

    index=4;
    int temp_index=-1;
    int name_len=0;
    ndn_buf[0]=0x06; //NDN-content
    ndn_buf[2]=0x07; //NDN_TLV_Name

    while(*name_buf!='\0')
    {
        if(*name_buf=='/')
        {
            ndn_buf[index++]=0x08;
            if(temp_index>0)
                ndn_buf[temp_index]=name_len;
            name_buf++;
            name_len=0;
            temp_index=index;
            index++;
        }
        else if(*name_buf >= 'a' && *name_buf <= 'z')
        {
            name_len++;
            ndn_buf[index++]=*name_buf;
            name_buf++;
        }
    }
    ndn_buf[temp_index]=name_len;
    ndn_buf[3]=index-4;

    ndn_buf[index++]=0x14;//NDN_TLV_MetaInfo
    ndn_buf[index++]=0x0;//NDN_TLV_MetaInfo
    ndn_buf[index++]=0x15;//NDN_TLV_Content

    ndn_buf[index++]=strlen((const char*)_cont_buf)+1;

    while(*content_buf!='\0')
    {
        ndn_buf[index++]=*content_buf;
        content_buf++;
    }

    ndn_buf[index++]=0x0a;//line end
    ndn_buf[index++]=0x16;//line end
    ndn_buf[index++]=0x05;//line end
    ndn_buf[index++]=0x1b;//line end
    ndn_buf[index++]=0x01;//line end
    ndn_buf[index++]=0x00;//line end
    ndn_buf[index++]=0x1c;//line end
    ndn_buf[index++]=0x00;//line end
    ndn_buf[index++]=0x17;//line end
    ndn_buf[index++]=0x00;//line end

    ndn_buf[1]=index-2; //length of pkt

}

static int parse_dev(char *arg)
{
    unsigned dev = atoi(arg);
    if (dev >= UART_NUMOF) {
        printf("Error: Invalid UART_DEV device specified (%u).\n", dev);
        return -1;
    }
    else if (UART_DEV(dev) == UART_STDIO_DEV) {
        printf("Error: The selected UART_DEV(%u) is used for the shell!\n", dev);
        return -2;
    }
    return dev;
}

static void rx_cb(void *arg, uint8_t data)
{
    uart_t dev = (uart_t)arg;

    ringbuffer_add_one(&(ctx[dev].rx_buf), data);
    if (data == '\0') {
        msg_t msg;
        msg.content.value = (uint32_t)dev;
        msg_send(&msg, printer_pid);
    }
}

static void *printer(void *arg)
{
    (void)arg;
    msg_t msg;
    msg_t msg_queue[8];
    msg_init_queue(msg_queue, 8);

  // uint8_t endline = (uint8_t)'\n';

    while (1) {
        msg_receive(&msg);
        uart_t dev = (uart_t)msg.content.value;
       // char c;
        char buf[512];
        char name_buf[512];
        printf("UART_DEV(%i) RX: ", dev);
        /*
        do {
            c = (int)ringbuffer_get_one(&(ctx[dev].rx_buf));
            if (c == '\n') {
                puts("\\n");
            }
            else if (c >= ' ' && c <= '~') {
                printf("%c", c);
            }
            else {
                printf("0x%02x", (unsigned char)c);
            }
        } while (c != '\n');
        */
        ringbuffer_get(&(ctx[dev].rx_buf), buf,512);
  
        strcpy(name_buf,buf);

        if(send_interest(buf)>=0)//content recieve success.
        {
        content_to_ndn(name_buf, _cont_buf);
        printf("Send NDN pkt to uart: length:%d\n",index);
       uart_write(UART_DEV(dev), (uint8_t *)ndn_buf, index);
        }

    }

    /* this should never be reached */
    return NULL;
}

static int cmd_init(int argc, char **argv)
{
    int dev, res;
    uint32_t baud;

    if (argc < 3) {
        printf("usage: %s <dev> <baudrate>\n", argv[0]);
        return 1;
    }
    /* parse parameters */
    dev = parse_dev(argv[1]);
    if (dev < 0) {
        return 1;
    }
    baud = atoi(argv[2]);

    /* initialize UART */
    res = uart_init(UART_DEV(dev), baud, rx_cb, (void *)dev);
    if (res == UART_NOBAUD) {
        printf("Error: Given baudrate (%u) not possible\n", (unsigned int)baud);
        return 1;
    }
    else if (res != UART_OK) {
        puts("Error: Unable to initialize UART device\n");
        return 1;
    }
    printf("Successfully initialized UART_DEV(%i)\n", dev);
    return 0;
}

static int cmd_send(int argc, char **argv)
{
    int dev;
    uint8_t endline = (uint8_t)'\n';

    if (argc < 3) {
        printf("usage: %s <dev> <data (string)>\n", argv[0]);
        return 1;
    }
    /* parse parameters */
    dev = parse_dev(argv[1]);
    if (dev < 0) {
        return 1;
    }
    

    printf("UART_DEV(%i) TX: %s\n", dev, argv[2]);
    uart_write(UART_DEV(dev), (uint8_t *)argv[2], sizeof(argv[2]));
    uart_write(UART_DEV(dev), &endline, 1);
    return 0;
}

static const shell_command_t shell_commands[] = {
    { "init", "Initialize a UART device with a given baudrate", cmd_init },
    { "send", "Send a string through given UART device", cmd_send },
    { NULL, NULL, NULL }
};



int main(void)
{
    tlsf_create_with_pool(_tlsf_heap, sizeof(_tlsf_heap));
    msg_init_queue(_main_msg_queue, MAIN_QUEUE_SIZE);

    puts("Basic CCN-Lite example");

    ccnl_core_init();

    ccnl_start();

        /* initialize ringbuffers */
    for (unsigned i = 0; i < UART_NUMOF; i++) {
        ringbuffer_init(&(ctx[i].rx_buf), ctx[i].rx_mem, UART_BUFSIZE);
    }
    /* start the printer thread */
    printer_pid = thread_create(printer_stack, sizeof(printer_stack),
                                PRINTER_PRIO, 0, printer, NULL, "printer");


    /* get the default interface */
    kernel_pid_t ifs[GNRC_NETIF_NUMOF];

    /* set the relay's PID, configure the interface to use CCN nettype */
    if ((gnrc_netif_get(ifs) == 0) || (ccnl_open_netif(ifs[0], GNRC_NETTYPE_CCN) < 0)) {
        puts("Error registering at network interface!");
        return -1;
    }
    
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(shell_commands, line_buf, SHELL_DEFAULT_BUFSIZE);
    return 0;
}

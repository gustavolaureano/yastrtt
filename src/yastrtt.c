
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <stlink.h>


typedef struct {
    uint32_t sName;         // Optional name. Standard names so far are: "Terminal", "SysView", "J-Scope_t4i4"
    uint32_t pBuffer;       // Pointer to start of buffer
    uint32_t SizeOfBuffer;  // Buffer size in bytes. Note that one byte is lost, as this implementation does not fill up the buffer in order to avoid the problem of being unable to distinguish between full and empty.
    uint32_t WrOff;         // Position of next item to be written by either target.
    uint32_t RdOff;         // Position of next item to be read by host. Must be volatile since it may be modified by host.
    uint32_t Flags;         // Contains configuration flags
} rtt_channel;

typedef struct {
  int8_t acID[16];                                 // Initialized to "SEGGER RTT"
  int32_t MaxNumUpBuffers;                          // Initialized to SEGGER_RTT_MAX_NUM_UP_BUFFERS (type. 2)
  int32_t MaxNumDownBuffers;                        // Initialized to SEGGER_RTT_MAX_NUM_DOWN_BUFFERS (type. 2)
  int32_t cb_size;
  int32_t cb_addr;
  rtt_channel *aUp;                                     // Up buffers, transferring information up from target via debug probe to host
  rtt_channel *aDown;                                   // Down buffers, transferring information down from host via debug probe to target
} rtt_cb;

stlink_t* sl = NULL;
rtt_cb *rtt_cb_ptr;

int read_mem(uint8_t *des, uint32_t addr, uint32_t len)
{
    uint32_t offset_addr = 0, offset_len, read_len;

    // address and read len need to align to 4
    read_len = len;
    offset_addr = addr % 4;
    if (offset_addr > 0) {
        addr -= offset_addr;
        read_len += offset_addr;
    }
    offset_len = read_len % 4;
    if (offset_len > 0)
        read_len += (4 - offset_len);

    stlink_read_mem32(sl, addr, read_len);

    // read data we actually need
    for (uint32_t i = 0; i < len; i++)
        des[i] = (uint8_t)sl->q_buf[i + offset_addr];

    return 0;
}

int write_mem(uint8_t *buf, uint32_t addr, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        sl->q_buf[i] = buf[i];

    stlink_write_mem8(sl, addr, len);

    return 0;
}

int get_channel_data(uint8_t *buf, rtt_channel *rtt_c, uint32_t rtt_channel_addr)
{
    uint32_t len;

    if (rtt_c->WrOff > rtt_c->RdOff) {
        len = rtt_c->WrOff - rtt_c->RdOff;
        read_mem(buf, rtt_c->pBuffer + rtt_c->RdOff, len);
        rtt_c->RdOff += len;
        write_mem((uint8_t *)&(rtt_c->RdOff), rtt_channel_addr + 4 * 4, 4);
        return 1;
    } else if (rtt_c->WrOff < rtt_c->RdOff) {
        len = rtt_c->SizeOfBuffer - rtt_c->RdOff;
        read_mem(buf, rtt_c->pBuffer + rtt_c->RdOff, len);
        read_mem(buf + len, rtt_c->pBuffer, rtt_c->WrOff);
        rtt_c->RdOff = rtt_c->WrOff;
        write_mem((uint8_t *)&(rtt_c->RdOff), rtt_channel_addr + 4 * 4, 4);
        return 1;
    } else {
        return 0;
    }
}

int cb_timer()
{
    uint8_t buf[1024];
    char str_buf[1024];
    int line_cnt;

    // update SEGGER_RTT_CB content
    read_mem(buf, rtt_cb_ptr->cb_addr + 24, rtt_cb_ptr->cb_size - 24);
    for (uint32_t j = 0; j < rtt_cb_ptr->MaxNumUpBuffers * sizeof(rtt_channel); j++)
        ((uint8_t *)rtt_cb_ptr->aUp)[j] = buf[j];
    for (uint32_t j = 0; j < rtt_cb_ptr->MaxNumDownBuffers * sizeof(rtt_channel); j++)
        ((uint8_t *)rtt_cb_ptr->aDown)[j] = buf[rtt_cb_ptr->MaxNumUpBuffers * sizeof(rtt_channel) + j];

    memset(buf, '\0', 1024);
    // if (get_channel_data(buf, &rtt_cb_ptr->aUp[0], rtt_cb_ptr->cb_addr + 24) > 0) {
    //     if (strlen(buf) > 0) {
    //         strcpy(str_buf, buf);
    //         IupSetAttribute(txt_message, "APPEND", str_buf);
    //         line_cnt = IupGetInt(txt_message, "LINECOUNT");
    //         IupSetStrf(txt_message, "SCROLLTO", "%d:1", line_cnt);
    //     }
    // }

    return 0;
}

void handle_sigint(int sig)
{
    printf("Caught signal %d\n", sig);
    exit(0);
}

int main(int ac, char **av)
{
    signal(SIGINT, handle_sigint);
    printf("Test\n");

    while (1)
    {
        static int test = 0;

        printf("%d\r", test++);
        fflush(stdout);

        usleep(100000);
    }

    return 0;
}
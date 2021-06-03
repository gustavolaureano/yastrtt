
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include <stlink.h>

typedef struct
{
    uint32_t sName;        // Optional name. Standard names so far are: "Terminal", "SysView", "J-Scope_t4i4"
    uint32_t pBuffer;      // Pointer to start of buffer
    uint32_t SizeOfBuffer; // Buffer size in bytes. Note that one byte is lost, as this implementation does not fill up the buffer in order to avoid the problem of being unable to distinguish between full and empty.
    uint32_t WrOff;        // Position of next item to be written by either target.
    uint32_t RdOff;        // Position of next item to be read by host. Must be volatile since it may be modified by host.
    uint32_t Flags;        // Contains configuration flags
} rtt_channel;

typedef struct
{
    int8_t acID[16];           // Initialized to "SEGGER RTT"
    int32_t MaxNumUpBuffers;   // Initialized to SEGGER_RTT_MAX_NUM_UP_BUFFERS (type. 2)
    int32_t MaxNumDownBuffers; // Initialized to SEGGER_RTT_MAX_NUM_DOWN_BUFFERS (type. 2)
    int32_t cb_size;
    int32_t cb_addr;
    rtt_channel *aUp;   // Up buffers, transferring information up from target via debug probe to host
    rtt_channel *aDown; // Down buffers, transferring information down from host via debug probe to target
} rtt_cb_t;

stlink_t *sl = NULL;
rtt_cb_t rtt_cb = {0};

int capt_signal = 0;

const char anim[4] = {'|', '/', '-', '\\'};
int anim_index = 0;

int close_device(void)
{
    if (sl)
    {
        stlink_exit_debug_mode(sl);
        stlink_close(sl);
        sl = NULL;
    }
}

int read_mem(uint8_t *des, uint32_t addr, uint32_t len)
{
    uint32_t offset_addr = 0, offset_len, read_len;

    // address and read len need to align to 4
    read_len = len;
    offset_addr = addr % 4;
    if (offset_addr > 0)
    {
        addr -= offset_addr;
        read_len += offset_addr;
    }
    offset_len = read_len % 4;
    if (offset_len > 0)
        read_len += (4 - offset_len);

    // if ((stlink_status(sl) != 0) ||
    //     (sl->core_stat != TARGET_RUNNING) ||
    //     (stlink_read_mem32(sl, addr, read_len) != 0))
    // {
    //     printf("Error reading RAM!\n");
    //     exit_clean(-1);
    // }
    /* No way of detecting if the target is still there, no error is returned, 
     * the only way is to detect during connect, that's why we disconnect and 
     * reconnect at every cycle */
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

    if (rtt_c->WrOff > rtt_c->RdOff)
    {
        len = rtt_c->WrOff - rtt_c->RdOff;
        read_mem(buf, rtt_c->pBuffer + rtt_c->RdOff, len);
        rtt_c->RdOff += len;
        write_mem((uint8_t *)&(rtt_c->RdOff), rtt_channel_addr + 4 * 4, 4);
        return 1;
    }
    else if (rtt_c->WrOff < rtt_c->RdOff)
    {
        len = rtt_c->SizeOfBuffer - rtt_c->RdOff;
        read_mem(buf, rtt_c->pBuffer + rtt_c->RdOff, len);
        read_mem(buf + len, rtt_c->pBuffer, rtt_c->WrOff);
        rtt_c->RdOff = rtt_c->WrOff;
        write_mem((uint8_t *)&(rtt_c->RdOff), rtt_channel_addr + 4 * 4, 4);
        return 1;
    }
    else
    {
        return 0;
    }
}

int Collect_RX()
{
    uint8_t buf[1024];
    char str_buf[1024];
    int line_cnt;

    // update SEGGER_RTT_CB content
    read_mem(buf, rtt_cb.cb_addr + 24, rtt_cb.cb_size - 24);
    for (uint32_t j = 0; j < rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel); j++)
        ((uint8_t *)rtt_cb.aUp)[j] = buf[j];
    for (uint32_t j = 0; j < rtt_cb.MaxNumDownBuffers * sizeof(rtt_channel); j++)
        ((uint8_t *)rtt_cb.aDown)[j] = buf[rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel) + j];

    memset(buf, '\0', 1024);
    if (get_channel_data(buf, &rtt_cb.aUp[0], rtt_cb.cb_addr + 24) > 0)
    {
        if (strlen(buf) > 0)
        {
            strcpy(str_buf, buf);
            printf("%s", str_buf);
            fflush(stdout);
            // IupSetAttribute(txt_message, "APPEND", str_buf);
            // line_cnt = IupGetInt(txt_message, "LINECOUNT");
            // IupSetStrf(txt_message, "SCROLLTO", "%d:1", line_cnt);
        }
    }

    return 0;
}

static stlink_t *stlink_open_first(void)
{
    stlink_t *sl = NULL;
    sl = stlink_v1_open(0, 1);
    if (sl == NULL)
        sl = stlink_open_usb(0, CONNECT_HOT_PLUG, NULL, 0);

    return sl;
}

void handle_sigint(int sig)
{
    printf("Caught signal %d\n", sig);
    capt_signal = sig;
}

int open_device(void)
{
    sl = stlink_open_first();

    if (sl == NULL)
    {
        printf("STLink not detected %c     \r", anim[anim_index]);
        fflush(stdout);
        return -1;
    }

    sl->verbose = 0;

    if (stlink_current_mode(sl) == STLINK_DEV_DFU_MODE)
    {
        stlink_exit_dfu_mode(sl);
    }

    if (stlink_current_mode(sl) != STLINK_DEV_DEBUG_MODE)
    {
        stlink_enter_swd_mode(sl);
    }

    /* That's how we know the STLINK lib has not detected a target */
    if (sl->sram_size == 0)
    {
        printf("Target not detected %c      \r", anim[anim_index]);
        fflush(stdout);
        return -1;
    }

    /* The target is not reset/stopped at any moment 
     * (checked with a logic analyzer on a Nucleo G071 board) */
    /* but we set it to run anyway */
    stlink_run(sl, RUN_NORMAL);
    return 0;
}

void locate_rtt_cb(void)
{
    // read the whole RAM
    uint8_t *buf = (uint8_t *)malloc(sl->sram_size);
    uint32_t r_cnt = sl->sram_size / 0x400;
    //printf("target have %u k ram\n\r", r_cnt);
    for (uint32_t i = 0; i < r_cnt; i++)
    {
        stlink_read_mem32(sl, 0x20000000 + i * 0x400, 0x400);
        for (uint32_t k = 0; k < 0x400; k++)
            (buf + i * 0x400)[k] = (uint8_t)(sl->q_buf[k]);
    }

    /* Reset the Control Block */
    rtt_cb.cb_addr = 0;
    free(rtt_cb.aUp); /* freeing NULL is allowed */
    rtt_cb.aUp = NULL;
    free(rtt_cb.aDown);
    rtt_cb.aDown = NULL;

    // find SEGGER_RTT_CB address
    uint32_t offset;
    for (offset = 0; offset < sl->sram_size - 16; offset++)
    {
        if (strncmp((char *)&buf[offset], "SEGGER RTT", 16) == 0)
        {
            rtt_cb.cb_addr = 0x20000000 + offset;
            printf("=> RTT addr = 0x%x         \n\r", rtt_cb.cb_addr);
            fflush(stdout);
            break;
        }
    }

    if (rtt_cb.cb_addr == 0)
    {
        printf("Searching SEGGER_RTT_CB %c        \r", anim[anim_index]);
        fflush(stdout);
    }
    else
    {
        // get SEGGER_RTT_CB content
        memcpy(rtt_cb.acID, ((rtt_cb_t *)(buf + offset))->acID, 16);
        rtt_cb.MaxNumUpBuffers = ((rtt_cb_t *)(buf + offset))->MaxNumUpBuffers;
        rtt_cb.MaxNumDownBuffers = ((rtt_cb_t *)(buf + offset))->MaxNumDownBuffers;
        rtt_cb.cb_size = 24 + (rtt_cb.MaxNumUpBuffers + rtt_cb.MaxNumDownBuffers) * sizeof(rtt_cb);
        rtt_cb.aUp = (rtt_channel *)malloc(rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel));
        rtt_cb.aDown = (rtt_channel *)malloc(rtt_cb.MaxNumDownBuffers * sizeof(rtt_channel));
        memcpy(rtt_cb.aUp, buf + offset + 24, rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel));
        memcpy(rtt_cb.aDown, buf + offset + 24 + rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel),
               rtt_cb.MaxNumDownBuffers * sizeof(rtt_channel));
    }

    free(buf);
}

int main(int ac, char **av)
{
    signal(SIGINT, handle_sigint);

    while (1)
    {
        if (capt_signal == SIGINT)
        {
            close_device();

            free(rtt_cb.aUp); /* freeing NULL is allowed */
            rtt_cb.aUp = NULL;
            free(rtt_cb.aDown);
            rtt_cb.aDown = NULL;

            break;
        }

        if (open_device() == 0)
        {
            if (rtt_cb.cb_addr == 0)
            {
                locate_rtt_cb();
            }

            if (rtt_cb.cb_addr != 0)
            {
                Collect_RX();
            }
        }
        else
        {
            /* We also need to relocate the CB when we lost connection to the target */
            rtt_cb.cb_addr = 0;
        }

        close_device();
        usleep(100000);
        anim_index = (anim_index + 1) % sizeof(anim);
    }

    return 0;
}
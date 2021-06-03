
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <termios.h>

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

struct termios orig_termios;

const char anim[4] = {'|', '/', '-', '\\'};
int anim_index = 0;

char txbuff[128];
int txbuff_len = 0;

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

/* buf = pointer to destination buffer
 * rtt_c = pointer to the updated copy of the channel ringbuffer control block
 * rtt_channel_addr = memory address on the target where the ringbuffer control block is located */
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

/* txbuffer = pointer to the source data
 * txlength = length of the source data
 * rtt_c = pointer to the updated copy of the channel ringbuffer control block
 * rtt_channel_addr = memory address on the target where the ringbuffer control block is located */
int write_channel_data(char *txbuffer, int txlength, rtt_channel *rtt_c, uint32_t rtt_channel_addr)
{
    int original_len = txlength;

    if (txlength == 0)
        return 0;

    /* we are ahead of the reading pointer, we are only limited by the size of the buffer */
    if (rtt_c->WrOff >= rtt_c->RdOff)
    {
        int to_write = txlength;
        int avail = rtt_c->SizeOfBuffer - rtt_c->WrOff;

        if (to_write > avail)
        {
            to_write = avail;
        }

        write_mem((uint8_t *)txbuffer, rtt_c->pBuffer + rtt_c->WrOff, to_write);
        txbuffer += to_write;
        txlength -= to_write;
        rtt_c->WrOff = (rtt_c->WrOff + to_write) % rtt_c->SizeOfBuffer; /* wrap to 0 if offset = size */
    }

    if ((txlength > 0) && (rtt_c->WrOff < rtt_c->RdOff))
    {
        int to_write = txlength;
        int avail = (rtt_c->RdOff - rtt_c->WrOff) -1;

        if (to_write > avail)
        {
            to_write = avail;
        }

        write_mem((uint8_t *)txbuffer, rtt_c->pBuffer + rtt_c->WrOff, to_write);
        txbuffer += to_write;
        txlength -= to_write;
        rtt_c->WrOff += to_write;
    }

    /* update the write offset on the target */
    write_mem((uint8_t *)&(rtt_c->WrOff), rtt_channel_addr + (4 * 3), 4); 

    /* return the number of written bytes */
    return (original_len - txlength); 
}

int Run_TXRX()
{
    uint8_t rxbuf[1024];

    /* update local copy of all ring-buffers control blocks */
    read_mem(rxbuf, rtt_cb.cb_addr + 24, rtt_cb.cb_size - 24); /* 24 = cb name (16 bytes) + MaxNumUpBuffers (uint32, 4 bytes) + MaxNumDownBuffers (uint32, 4 bytes) = rbcb start */
    memcpy(rtt_cb.aUp, rxbuf, rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel));
    memcpy(rtt_cb.aDown, rxbuf + (rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel)), rtt_cb.MaxNumDownBuffers * sizeof(rtt_channel));

    memset(rxbuf, '\0', 1024);

    /* the target's RAM address of the ringbuffer control block aUp[0] is the offset to the rbcb arrays */
    if (get_channel_data(rxbuf, &rtt_cb.aUp[0], rtt_cb.cb_addr + 24) > 0)
    {
        printf("%s", rxbuf);
        fflush(stdout);
    }

    if (txbuff_len > 0)
    {
        /* the target's RAM address of the ringbuffer control block aDown[0] is the offset to the rbcb arrays + the length of the aUp array of rbcb */
        write_channel_data(txbuff, txbuff_len, &rtt_cb.aDown[0], rtt_cb.cb_addr + 24 + (rtt_cb.MaxNumUpBuffers * sizeof(rtt_channel)));
        txbuff_len = 0;
        /* We are not dealing with remaining data that was not sent to the target and we just throw it away, this should not be an 
         * issue during normal usage as this data is only slow keyboard input and the target will likely consume it fast enough,
         * so the down buffer (H -> T) will never be full.
         * If necessary, the data input could be reimplemented with a ringbuffer, which would allow partial transfers to the target */
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

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON); /* disable echo and canonical mode */
    raw.c_cc[VMIN] = 0;  /* min number of chars that read() can return */
    raw.c_cc[VTIME] = 0; /* timeout for read() */
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(int ac, char **av)
{
    signal(SIGINT, handle_sigint);

    enableRawMode();

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

        /* raw input capture from terminal */
        char c;
        while (read(STDIN_FILENO, &c, 1) == 1)
        {
            if (txbuff_len < sizeof(txbuff))
            {
                txbuff[txbuff_len++] = c;
            }
        }

        if (open_device() == 0)
        {
            if (rtt_cb.cb_addr == 0)
            {
                locate_rtt_cb();

                /* We ignore anything that was input by the user while no RTT was available */
                txbuff_len = 0;
            }

            if (rtt_cb.cb_addr != 0)
            {
                Run_TXRX();
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
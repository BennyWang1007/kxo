#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <setjmp.h>

#include "coroutine.h"
#include "game.h"

#define XO_STATUS_FILE "/sys/module/kxo/initstate"
#define XO_DEVICE_FILE "/dev/kxo"
#define XO_DEVICE_ATTR_FILE "/sys/class/kxo/kxo/kxo_state"

static bool status_check(void)
{
    FILE *fp = fopen(XO_STATUS_FILE, "r");
    if (!fp) {
        printf("kxo status : not loaded\n");
        return false;
    }

    char read_buf[20];
    fgets(read_buf, 20, fp);
    read_buf[strcspn(read_buf, "\n")] = 0;
    if (strcmp("live", read_buf)) {
        printf("kxo status : %s\n", read_buf);
        fclose(fp);
        return false;
    }
    fclose(fp);
    return true;
}

static struct termios orig_termios;

static void raw_mode_disable(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

static void raw_mode_enable(void)
{
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(raw_mode_disable);
    struct termios raw = orig_termios;
    raw.c_iflag &= ~IXON;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static bool read_attr = true, end_attr = false;
static int device_fd = -1;

board_history_t histories[HISTORY_SIZE] = {0};
size_t board_index = 0;
char board_data[BOARD_DATA_SIZE];

static void listen_keyboard_handler(void)
{
    int attr_fd;
    char input;
    fd_set readset;
    struct timeval tv;

    /* Set up the select timeout */
    tv.tv_sec = 0;
    tv.tv_usec = 100000; /* 100ms timeout */

    FD_ZERO(&readset);
    FD_SET(STDIN_FILENO, &readset);

    /* Check for keyboard input */
    int result = select(STDIN_FILENO + 1, &readset, NULL, NULL, &tv);
    if (result == -1) {
        perror("select");
        exit(1);
    } else if (result == 0) {
        coroutine_yield(); /* Timeout, no input */
    }

    if (read(STDIN_FILENO, &input, 1) == 1) {
        attr_fd = open(XO_DEVICE_ATTR_FILE, O_RDWR);
        char buf[20];
        switch (input) {
        case 16: /* Ctrl-P */
            read(attr_fd, buf, 6);
            buf[0] = (buf[0] - '0') ? '0' : '1';
            read_attr ^= 1;
            write(attr_fd, buf, 6);
            break;
        case 17: /* Ctrl-Q */
            read(attr_fd, buf, 6);
            buf[4] = '1';
            read_attr = false;
            end_attr = true;
            write(attr_fd, buf, 6);
            printf("Stopping the kernel space tic-tac-toe game...\n");

            if (device_fd == -1) {
                perror("open");
                exit(1);
            }

            if (ioctl(device_fd, KXO_GET_BOARD_HISTORY, &histories) == -1) {
                perror("ioctl");
                exit(1);
            }

            printf("Game history:\n");
            for (size_t j = 0; j < HISTORY_SIZE; j++) {
                board_history_t *history = &histories[j];
                if (history->length == 0)
                    break;
                printf("Moves: ");
                for (size_t i = 0; i < history->length; i++) {
                    size_t idx = (i * BOARD_HISTORY_ELE_BITS) >> 3;
                    size_t bit = (i * BOARD_HISTORY_ELE_BITS) & 7;
                    int move = (history->moves[idx] >> bit) & 7;
                    int row = GET_ROW(move);
                    int col = GET_COL(move);
                    if (i != 0)
                        printf(" -> ");
                    printf("%c%d", (col + 'A'), (row + 1));
                }
                printf("\n");
            }
            break;
        }
        close(attr_fd);
    }
    coroutine_yield();
}

void print_board(const char *board_data)
{
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            size_t idx = GET_INDEX(i, j);
            size_t byte_idx = idx >> 2;
            size_t bit_idx = (idx & 3) << 1;
            if (board_data[byte_idx] & (1 << bit_idx))
                printf("X ");
            else if (board_data[byte_idx] & (1 << (bit_idx + 1)))
                printf("O ");
            else
                printf(". ");
        }
        printf("\n");
    }
    if (!read_attr) {
        printf("Stopping to display the chess board...\n");
    }
}

void read_and_print_board(void)
{
    device_fd = open(XO_DEVICE_FILE, O_RDONLY);

    if (read_attr && read(device_fd, board_data, BOARD_DATA_SIZE) == -1) {
        perror("read");
        exit(1);
    }
    printf("\033[H\033[J"); /* ASCII escape code to clear the screen */
    print_board(board_data);

    coroutine_yield();
}

/* Print the current time in YYYY-MM-DD HH:MM:SS format */
void print_time(void)
{
    struct timeval tv;
    char buff[20]; /* time string buffer */

    gettimeofday(&tv, NULL);
    strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", localtime(&tv.tv_sec));
    printf("Current time: %s\n", buff);

    coroutine_yield();
}

int main(int argc, char *argv[])
{
    if (!status_check())
        exit(1);

    raw_mode_enable();
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    device_fd = open(XO_DEVICE_FILE, O_RDONLY);
    if (device_fd == -1) {
        perror("Failed to open device file");
        raw_mode_disable();
        exit(1);
    }

    int coro_list[] = {
        coroutine_create(listen_keyboard_handler),
        coroutine_create(read_and_print_board),
        coroutine_create(print_time),
    };

    while (!end_attr) {
        for (size_t i = 0; i < sizeof(coro_list) / sizeof(coro_list[0]); i++) {
            int co = coro_list[i];
            if (coroutine_is_active(co)) {
                coroutine_resume(co);
            }
        }
    }

    raw_mode_disable();
    fcntl(STDIN_FILENO, F_SETFL, flags);

    close(device_fd);

    return 0;
}

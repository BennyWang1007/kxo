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

static bool read_attr, end_attr;

board_history_t histories[HISTORY_SIZE] = {0};
size_t board_index = 0;

static void listen_keyboard_handler(void)
{
    int attr_fd = open(XO_DEVICE_ATTR_FILE, O_RDWR);
    char input;

    if (read(STDIN_FILENO, &input, 1) == 1) {
        char buf[20];
        switch (input) {
        case 16: /* Ctrl-P */
            read(attr_fd, buf, 6);
            buf[0] = (buf[0] - '0') ? '0' : '1';
            read_attr ^= 1;
            write(attr_fd, buf, 6);
            if (!read_attr)
                printf("Stopping to display the chess board...\n");
            break;
        case 17: /* Ctrl-Q */
            read(attr_fd, buf, 6);
            buf[4] = '1';
            read_attr = false;
            end_attr = true;
            write(attr_fd, buf, 6);
            printf("Stopping the kernel space tic-tac-toe game...\n");

            int device_fd = open(XO_DEVICE_FILE, O_RDONLY);

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
                    continue;
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
            close(device_fd);

            break;
        }
    }
    close(attr_fd);
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
}

/* Print the current time in YYYY-MM-DD HH:MM:SS format */
void print_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S",
             localtime(&tv.tv_sec));
    printf("Current time: %s\n", time_str);
}

int main(int argc, char *argv[])
{
    if (!status_check())
        exit(1);

    raw_mode_enable();
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    char board_data[BOARD_DATA_SIZE];

    fd_set readset;
    int device_fd = open(XO_DEVICE_FILE, O_RDONLY);
    int max_fd = device_fd > STDIN_FILENO ? device_fd : STDIN_FILENO;
    read_attr = true;
    end_attr = false;

    while (!end_attr) {
        FD_ZERO(&readset);
        FD_SET(STDIN_FILENO, &readset);
        FD_SET(device_fd, &readset);

        int result = select(max_fd + 1, &readset, NULL, NULL, NULL);
        if (result < 0) {
            printf("Error with select system call\n");
            exit(1);
        }

        if (FD_ISSET(STDIN_FILENO, &readset)) {
            FD_CLR(STDIN_FILENO, &readset);
            listen_keyboard_handler();
        } else if (read_attr && FD_ISSET(device_fd, &readset)) {
            FD_CLR(device_fd, &readset);
            printf("\033[H\033[J"); /* ASCII escape code to clear the screen */
            read(device_fd, board_data, BOARD_DATA_SIZE);
            print_board(board_data);
            print_time();
        }
    }

    raw_mode_disable();
    fcntl(STDIN_FILENO, F_SETFL, flags);

    close(device_fd);

    return 0;
}

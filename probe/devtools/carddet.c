/*
 * The SD slot as the card-detect driver sees it, plus its detect and power
 * ioctls, from probing why the library loses the SD card when hagodaemons.sh
 * restarts the daemons. The connection manager learns of a card only from the
 * driver's poll, which reports the insert once, to the first reader after
 * boot. Neither CARD_DET_REQ_DETECT nor switching the slot power makes it
 * report again (tried 2026-10-10: state stays 1, no poll event). Taking
 * the card out and putting it back does, without a reboot, so a devmod run
 * keeps its overlay; so does a reboot, which drops the overlay.
 *
 *   carddet               print state/type/ro (state 1 detected, 3 inserted)
 *   carddet detect        CARD_DET_REQ_DETECT
 *   carddet power 0|1     CARD_DET_CONTROL_POWER
 *
 * Interface from the kernel's include/mach/icx_carddet.h.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef struct {
    int state, type, ro;
    int state1, type1, ro1;
} icx_card_info_t;

#define CARD_DET_GET_INFO _IOR('c', 1, icx_card_info_t)
#define CARD_DET_CONTROL_POWER _IOW('c', 2, int)
#define CARD_DET_REQ_DETECT _IO('c', 4)

int main(int argc, char **argv)
{
    int fd = open("/dev/icx_carddet", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/icx_carddet");
        return 1;
    }
    icx_card_info_t info;
    memset(&info, 0, sizeof(info));
    if (ioctl(fd, CARD_DET_GET_INFO, &info) == 0)
        printf("state=%d type=%d ro=%d\n", info.state, info.type, info.ro);
    else
        perror("CARD_DET_GET_INFO");
    if (argc > 1 && strcmp(argv[1], "detect") == 0) {
        if (ioctl(fd, CARD_DET_REQ_DETECT) != 0) {
            perror("CARD_DET_REQ_DETECT");
            close(fd);
            return 1;
        }
        puts("detect requested");
    } else if (argc > 2 && strcmp(argv[1], "power") == 0) {
        int on = atoi(argv[2]);
        if (ioctl(fd, CARD_DET_CONTROL_POWER, &on) != 0) {
            perror("CARD_DET_CONTROL_POWER");
            close(fd);
            return 1;
        }
        printf("power %d\n", on);
    }
    close(fd);
    return 0;
}

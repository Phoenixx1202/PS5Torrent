#include "ps5_jailbreak.h"
#include "net_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define ETAHEN_COMMAND_PORT 9028
#define ETAHEN_COMMAND_MAGIC ((int32_t)0xDEADBEEF)
#define ETAHEN_JAILBREAK_COMMAND 5
#define ETAHEN_PENDING_RESULT (-1337)

/* Wire layout published by etaHEN for its legacy command server. */
typedef struct {
    int32_t magic;
    int32_t command;
    int32_t pid;
    int32_t result;
    char message1[0x500];
    char message2[0x500];
} etahen_command_t;

_Static_assert(sizeof(etahen_command_t) == 0xA10,
               "etaHEN command wire layout changed");

int ps5_request_jailbreak(void)
{
    int sock = net_tcp_connect(htonl(INADDR_LOOPBACK), ETAHEN_COMMAND_PORT);
    if (sock < 0) return -1;

    /* Never let an unavailable daemon stall application startup. */
    net_set_timeout(sock, 2);

    etahen_command_t command;
    memset(&command, 0, sizeof(command));
    command.magic = ETAHEN_COMMAND_MAGIC;
    command.command = ETAHEN_JAILBREAK_COMMAND;
    command.pid = (int32_t)getpid();
    command.result = ETAHEN_PENDING_RESULT;

    int ok = net_send_all(sock, &command, sizeof(command)) == 0 &&
             net_recv_exact(sock, &command, sizeof(command)) == 0 &&
             (command.result == 0 ||
              command.result == ETAHEN_PENDING_RESULT);

    net_close(sock);
    return ok ? 0 : -1;
}

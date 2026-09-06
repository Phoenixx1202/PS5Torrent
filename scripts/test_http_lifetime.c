/* Regression tests for closed clients and signals interrupting the server loop. */
#include "http_server.h"
#include "net_utils.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static void alarm_handler(int sig) { (void)sig; }

int main(void)
{
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        signal(SIGPIPE, SIG_DFL);
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        close(pair[1]);
        http_response_t *response = http_response_new(200, "text/plain", "ok", 2);
        http_server_send_response(pair[0], response);
        assert(net_send_all(pair[0], "peer", 4) == -1);
        http_response_free(response); close(pair[0]);
        _exit(0);
    }
    int status; assert(waitpid(child, &status, 0) == child);
    if (WIFSIGNALED(status)) {
        fprintf(stderr, "Closed HTTP client killed server with signal %d\n", WTERMSIG(status));
        return 1;
    }
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(http_server_init(0) == 0);
    struct sigaction action; memset(&action, 0, sizeof(action));
    action.sa_handler = alarm_handler; sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGALRM, &action, NULL));
    struct itimerval timer = {0}; timer.it_value.tv_usec = 10000;
    assert(!setitimer(ITIMER_REAL, &timer, NULL));
    assert(http_server_poll(1000) >= 0);
    assert(http_server_get_fd() >= 0);
    assert(http_server_poll(1) >= 0);
    http_server_shutdown();
    puts("HTTP lifetime: disconnected clients and interrupted select do not terminate the service");
}

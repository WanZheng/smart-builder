#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/wait.h>

const char *SUFFIX[] = {
    ".swp", ".swpx"
};

const uint32_t MASK = IN_MODIFY | IN_CREATE | IN_DELETE;

static pid_t child = 0;
static fd_set rset;
static struct timeval tv;

static int strrcmp(const char *s1, const char *s2) {
    size_t l1 = strlen(s1);
    size_t l2 = strlen(s2);
    if (l1 < l2) {
        return -1;
    }
    return strcmp(s1+l1-l2, s2);
}

static bool filter(const char *s) {
    for (int n = sizeof(SUFFIX) / sizeof(SUFFIX[0]) - 1; n>=0; n--) {
        if (strrcmp(s, SUFFIX[n]) == 0) {
            return true;
        }
    }
    return false;
}

static pid_t do_fork(int fd, const char *cmd) {
    pid_t child = fork();
    if (child < 0) {
        perror("fork()");
        return child;
    }
    if (child == 0) {
        close(fd);
        if (setpgid(0, 0) < 0) {
            perror("setpgid()");
        }
        printf("pgid = %d\n", getpgid(0));
        printf("%s\n", cmd);
        system(cmd);
        exit(0);
    }
    setpgid(child, 0); // XXX: to avoid race condition
    printf("child = %d\n", child);
    return child;
}

static int kill_child() {
    printf("kill child group %d\n", -child);
    if (kill(-child, SIGTERM) < 0) {
        perror("kill()");
        return -1;
    }
    printf("  waiting child group %d\n", -child);
    pid_t killed = waitpid(-child, NULL, 0);
    printf("* killed = %d\n\n", killed);

    child = 0;
    return 0;
}

static int read_event(int fd, struct inotify_event *event, int size) {
    for (;;) {
        FD_ZERO(&rset);
        FD_SET(fd, &rset);
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int n = select(fd+1, &rset, NULL, NULL, &tv);
        if (n < 0) {
            perror("select()");
            return -1;
        }
        if (n == 0) {
            return 0;
        }

        n = read(fd, event, size);
        if (n <= 0) {
            printf("read() return %d: %s\n", n, strerror(errno));
            return -1;
        }
        if (filter(event->name)) {
            continue;
        }
        /*
           printf( "wd = %d, "
           "mask = %x, "
           "cookie = %x, "
           "len = %u, "
           "name = %s\n",
           event->wd, event->mask, event->cookie, event->len, event->name);
           */
        printf("%s: ", event->name);
        if ((event->mask & IN_CREATE)) {
            printf("Create ");
        }
        if ((event->mask & IN_MODIFY)) {
            printf("Modify ");
        }
        if ((event->mask & IN_DELETE)) {
            printf("Delete ");
        }
        if ((event->mask & IN_CLOSE)) {
            printf("Close ");
        }
        printf("\n");
        return 1;
    }
}

static void sighandler(int sig) {
    printf("sighandler(%d)\n", sig);
    if (child > 0) {
        kill_child();
    }
}

int main(int argc, const char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    if (signal(SIGINT, sighandler) == SIG_ERR) {
        perror("signal()");
        return 1;
    }

    const char *path = argv[1];
    char cmd[1024];
    sprintf(cmd, "cd %s && go install && %s", path, path);

    int inotify = inotify_init1(IN_NONBLOCK);
    assert(inotify >= 0);
    int wd = inotify_add_watch(inotify, path, MASK);
    assert(wd >= 0);

    printf("start ...\n");
    struct inotify_event *event;
    size_t size = sizeof(*event) + PATH_MAX + 1;
    event = (struct inotify_event *)malloc(size);

    child = do_fork(inotify, cmd);

    while (child >= 0) {
        int n = read_event(inotify, event, size);
        if (n < 0) {
            break;
        }
        if (n == 0) { // fork child if timeout
            if (child == 0) {
                child = do_fork(inotify, cmd);
            }
            continue;
        }

        if (child > 0){
            if (kill_child() < 0) {
                break;
            }
            child = 0;
        }
    }
    free(event);
    close(inotify);

    if (child > 0) {
        kill_child();
    }
    printf("closed.\n");

    return 0;
}

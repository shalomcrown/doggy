#include "system_control.h"

#include <sys/wait.h>
#include <unistd.h>

#include <vector>

// ================================================================================

void NullSystemControl::perform(SystemAction) {
}

// ================================================================================

static void run_systemctl(const std::vector<const char *> &args) {
    const pid_t pid = fork();
    if (pid == 0) {
        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const char *arg : args) {
            argv.push_back(const_cast<char *>(arg));
        }
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
    }
}

// ================================================================================

void SystemdControl::perform(SystemAction action) {
    switch (action) {
        case SystemAction::restart:
            run_systemctl({"/usr/bin/systemctl", "--no-ask-password", "restart",
                           "doggy.service"});
            return;
        case SystemAction::reboot:
            run_systemctl({"/usr/bin/systemctl", "--no-ask-password", "reboot"});
            return;
        case SystemAction::shutdown:
            run_systemctl({"/usr/bin/systemctl", "--no-ask-password", "poweroff"});
            return;
    }
}

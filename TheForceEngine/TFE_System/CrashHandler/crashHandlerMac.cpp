#include <cstring>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include "crashHandler.h"
#include <TFE_System/system.h>

// Use sysctl to determine if the process is being debugged on macOS
static bool are_we_being_debugged(void)
{
    int mib[4];
    struct kinfo_proc info;
    size_t size;

    // Initialize the flags so that, if sysctl fails, we get a predictable result.
    info.kp_proc.p_flag = 0;

    // Set up the mib for sysctl call to get the process info.
    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_PID;
    mib[3] = getpid();

    size = sizeof(info);

    if (sysctl(mib, 4, &info, &size, NULL, 0) < 0) {
        return false; // Assume not being debugged.
    }

    // P_TRACED flag indicates that the process is being traced (debugged).
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

static void tfe_sigaction(int signo, siginfo_t *siginfo, void *uctx)
{
    int entries, i;
    void *buf[512];
    char **ents;

    // Gently quit
    if ((signo == SIGINT) || (signo == SIGTERM)) {
        TFE_System::postQuitMessage();
        return;
    }

    TFE_System::logWrite(LOG_ERROR, "CrashHandler", "Received Signal %d errno %d code %d", signo, siginfo->si_addr, siginfo->si_errno, siginfo->si_code);

    switch (signo) {
    case SIGILL:
    case SIGFPE:
    case SIGSEGV:
    case SIGBUS:
        TFE_System::logWrite(LOG_ERROR, "CrashHandler", "Faulting address %p", siginfo->si_addr);
    }

    // Backtrace and logging
    signal(SIGSEGV, SIG_IGN); // Ignore SIGSEGV before calling backtrace.
    entries = backtrace(buf, 512);
    if (entries) {
        ents = backtrace_symbols(buf, entries);
        TFE_System::logWrite(LOG_ERROR, "CrashHandler", "Backtrace %d:", entries);
        for (i = 0; i < entries; i++) {
            TFE_System::logWrite(LOG_ERROR, "CrashHandler", "%03d %s", i, ents[i]);
        }
    } else {
        TFE_System::logWrite(LOG_ERROR, "CrashHandler", "No backtrace possible");
    }

    // Default signal handler to allow coredump if enabled by the system.
    signal(signo, SIG_DFL);
    kill(getpid(), signo);
}

static void setsig(int signo)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = tfe_sigaction;
    if (sigaction(signo, &sa, NULL)) {
        fprintf(stderr, "Crash Handler: setsig(%d) failed with %d (%s)\n", signo, errno, strerror(errno));
    }
}

namespace TFE_CrashHandler
{
    void setProcessExceptionHandlers()
    {
        // Skip signal hooking when running under debuggers.
        if (are_we_being_debugged())
            return;

        setsig(SIGINT);  // ctrl-c
        setsig(SIGTERM); // kill -15 received
        setsig(SIGFPE);  // FPU exception
        setsig(SIGILL);  // unknown opcode
        setsig(SIGBUS);  // alignment issues
        setsig(SIGSEGV); // pointer gone awry
        setsig(SIGABRT); // abort() was invoked
    }

    void setThreadExceptionHandlers()
    {
        // Thread-specific handlers can be added here if needed.
    }
} // namespace TFE_CrashHandler

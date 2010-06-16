#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <error.h>
#include <unistd.h>
#include <fcntl.h>
#include "signalfd.h"
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static int write_pipe = -1;
//-----------------------------------------------------------------------------
static void on_signal (int sig, siginfo_t *info, void *vp) {
	int ret;
	struct signalfd_siginfo i;

	memset(&i, 0, sizeof(i));

	i.ssi_signo	= info->si_signo	;
	i.ssi_errno	= info->si_errno	;
	i.ssi_code	= info->si_code		;
	i.ssi_pid	= info->si_pid		;
	i.ssi_uid	= info->si_uid		;
	i.ssi_fd	= info->si_fd		;
	i.ssi_tid	= info->si_timerid	;
	i.ssi_band	= info->si_band		;
	i.ssi_overrun	= info->si_overrun	;
	//i.ssi_trapno	= info->si_		;
	i.ssi_status	= info->si_status	;
	i.ssi_int	= info->si_int		;
	i.ssi_ptr	= (unsigned long)info->si_ptr;
	i.ssi_utime	= info->si_utime	;
	i.ssi_stime	= info->si_stime	;
	i.ssi_addr	= (unsigned long)info->si_addr;

	ret = write(write_pipe, &i, sizeof(i));
	if (ret < 0)
		error(1, errno, "write(sigpipe)");
}
//-----------------------------------------------------------------------------
static int setup_signal_pipe(int flags) {
	int ret;
	int pp[2];

	ret = pipe(pp);
	if (ret < 0)
		error(1, errno, "pipe()");
	
	// only handle the write end
	if (fcntl(pp[1], F_SETFD, FD_CLOEXEC) < 0)
		error(1, errno, "[%i] cloexec", pp[1]);
	// set pipe write end to non-blocking
	ret = fcntl(pp[1], F_GETFL);
	if (ret < 0)
		error(1, errno, "[%i] get flags", pp[1]);
	ret |= O_NONBLOCK;
	if (fcntl(pp[1], F_SETFL, ret) < 0)
		error(1, errno, "[%i] set non-block", pp[1]);
	write_pipe = pp[1];
	return pp[0];
}
//-----------------------------------------------------------------------------
int signalfd(int fd, const sigset_t *set, int flags) {
	int ret;
	int j;
	struct sigaction act;

	if (fd < 0) {
		ret = fd = setup_signal_pipe(flags);
		if (ret < 0)
			return ret;
		if (flags & SFD_NONBLOCK) {
			ret = fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL));
			if (ret < 0)
				error(0, errno, "fcntl() set nonblocking");
		}
		if (flags & SFD_CLOEXEC) {
			ret = fcntl(fd, F_SETFD, FD_CLOEXEC | fcntl(fd, F_GETFD));
			if (ret < 0)
				error(0, errno, "[%i] cloexec", fd);
		}
	} else if (fd != write_pipe) {
		error(1, errno, "signalfd with existing fd");
	}

	memset(&act, 0, sizeof(act));
	act.sa_sigaction = on_signal;
	act.sa_flags = SA_SIGINFO;

	for (j = 1; j < _NSIG; ++j) {
		switch (j) {
		case SIGKILL:
		case SIGSTOP:
			continue;
		}
		if (!sigismember(set, j))
			continue;
		ret = sigaction(j, &act, 0);
		if (ret < 0)
			return ret;
	}
	// be compatible with signalfd api
	// suppose the block mask has been set
	// and clear it here
	sigprocmask(SIG_UNBLOCK, set, 0);
	return fd;
}
//-----------------------------------------------------------------------------



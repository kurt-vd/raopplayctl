#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include <unistd.h>
#include <error.h>
#include <argp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>
#else
#warning using own signalfd
#include "signalfd.h"
#endif

#include <libconf.h>
#include <liburl.h>
#include "ev.h"
// ----------------------------------------------------------------------------
static struct {
	int verbose;
	const char *airport;
	const char *agent;
	const char *fifo;

	char *stopcmd;
	const char *default_stopcmd;

	int pid;
	int sk[2];
	int epfd;
	double volume;
	struct {
		int term;
	} sig;
	double deadtime;
	int playing;
#define PLAYING		1
#define PENDING		2
#define STOPPING	3
#define STOPPED		4
#define WAIT_PLAY	5 // resumed during stop
}s = {
	.volume = 1,
	.agent = "raop_play",
	.playing = STOPPED,
	.deadtime = 60,
	.default_stopcmd = "quit",
};
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static void start_airport(void) {
	int ret;

	ret = fork();
	if (ret < 0)
		error(1, errno, "fork()");
	if (!ret) {
		dup2(s.sk[1], STDIN_FILENO);
		close(s.sk[0]);
		close(s.sk[1]);
		execlp(s.agent, "airport", "-i", s.airport, s.fifo, (char *)0);
		error(1, errno, "execlp(%s, ...)", s.agent);
	}
	s.pid = ret;
	if (s.verbose)
		error(0, 0, "started %s", s.agent);
}
//-----------------------------------------------------------------------------
static int set_volume(double volume) {
	double tvol = (volume * 0.15) + 0.85;
	if (volume < 0)
		volume = 0;
	else if (volume > 1)
		volume = 1;
	s.volume = volume;
	if (s.verbose)
		error(0, 0, "volume %.3lf (%.3lf)", s.volume, tvol);
	if (!s.pid)
		return 0;
	return dprintf(s.sk[0], "volume %.0lf\n", tvol *100);
}
//-----------------------------------------------------------------------------
static void timed_stop(void *vp) {
	const char *cmd = s.stopcmd ?: s.default_stopcmd;
	dprintf(s.sk[0], "%s\n", cmd);
	s.playing = STOPPING;
	kill(s.pid, SIGTERM);
	if (s.verbose)
		error(0, 0, "%s %s", cmd, s.airport);
}
//-----------------------------------------------------------------------------
static void set_stop(void) {
	if (!s.pid)
		return;
	if (PLAYING != s.playing)
		return;
	s.playing = PENDING;
	ev_add_timeout(s.deadtime, timed_stop, 0);
	if (s.verbose)
		error(0, 0, "stop in %.1lf seconds", s.deadtime);
}
//-----------------------------------------------------------------------------
static void set_play(void) {
	if (!s.pid) {
		start_airport();
		s.playing = PLAYING;
		set_volume(s.volume);
		if (s.verbose)
			error(0, 0, "started %s", s.airport);
		return;
	}
	switch (s.playing) {
	case PLAYING:
		break;
	case PENDING:
		ev_remove_timeout(timed_stop, 0);
		if (s.verbose)
			error(0, 0, "removed pending stop");
		s.playing = PLAYING;
		break;
	case STOPPING:
		s.playing = WAIT_PLAY;
		break;
	case STOPPED:
		dprintf(s.sk[0], "play %s\n", s.fifo);
		if (s.verbose)
			error(0, 0, "play %s", s.airport);
		break;
	}
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static void client_read(int fd, void *vp) {
	int ret;
	static char line[1024];
	char *cmd, *tok;

	ret = read(fd, line, sizeof(line)-1);
	if (ret <= 0) {
		if (!ret && (s.verbose >= 2))
			error(0, 0, "[%i] EOF", fd);
		ev_remove_fd(fd);
		return;
	}
	if (line[ret-1] == '\n')
		line[ret-1] = 0;
	cmd = tok = strtok(line, " \t");
	if (!cmd)
		return;
	if (!strcmp(cmd, "ping"))
		dprintf(fd, "pong\n");
	else if (!strcmp(cmd, "quit")) {
		ev_remove_fd(fd);
		close(fd);
		if (s.verbose >= 2)
			error(0, 0, "[%i] requested disconnect", fd);
	} else if (!strcmp(cmd, "exit"))
		raise(SIGINT);
	else if (!strcmp(cmd, "volume")) {
		tok = strtok(0, " \t");
		if (tok)
			set_volume(strtod(tok, 0));
		else
			dprintf(fd, "%.3lf\n", s.volume);
	} else if (!strcmp(cmd, "stop")) {
		set_stop();
	} else if (!strcmp(cmd, "start")) {
		set_play();
	} else if (!strcmp(cmd, "time")) {
		tok = strtok(0, " \t");
		if (tok)
			s.deadtime = strtod(tok, 0);
		else
			dprintf(fd, "%.1lf sec\n", s.deadtime);
	} else if (!strcmp(cmd, "cmd")) {
		tok = strtok(0, " \t");
		if (tok) {
			if (s.stopcmd)
				free(s.stopcmd);
			s.stopcmd = strdup(tok);
		} else
			dprintf(fd, "command %s\n", s.stopcmd ?: s.default_stopcmd);
	}
	return;
}
//-----------------------------------------------------------------------------
static void connection(int fd, void *vp) {
	int ret, sk;
	const char *url = vp;

	if (!strncmp("fifo:", url, 5)) {
		ev_add_fd(fd, client_read, url);
		return;
	}
	ret = sk = accept(fd, 0, 0);
	if (ret < 0) {
		error(0, errno, "accept(%s)", url);
		return;
	}
	if (s.verbose >= 2)
		error(0, 0, "[%i] via [%i] %s", sk, fd, url);

	ev_add_fd(sk, client_read, (void *)url);
}
//-----------------------------------------------------------------------------
static void sighandler(int fd, void *vp) {
	int ret, status;
	struct signalfd_siginfo i;

	ret = read(fd, &i, sizeof(i));
	if (ret < 0) {
		if (EINTR == errno)
			return;
		error(1, errno, "read(sigpipe)");
	}
	if (ret == 0)
		error(1, 0, "sigpipe EOF");
	switch (i.ssi_signo) {
	case SIGALRM:
		set_stop();
		break;
	case SIGCHLD:
		// raop_play failed
		waitpid(-1, &status, WNOHANG);
		s.pid = 0;
		if (s.verbose)
			error(0, 0, "%s exited", s.agent);
		if (s.playing == WAIT_PLAY)
			set_play();
		else
			s.playing = STOPPED;
		break;
	case SIGTERM:
	case SIGINT:
		++s.sig.term;
		break;
	case SIGPIPE:
	case SIGHUP:
	case SIGUSR1:
	case SIGUSR2:
		if (s.verbose)
			error(0, 0, "signal %i, %s",
					i.ssi_signo, strsignal(i.ssi_signo));
		break;
	}
}
//-----------------------------------------------------------------------------
static const int sigs[] = {
	SIGPIPE, SIGALRM, SIGCHLD, SIGTERM, SIGINT,
	SIGHUP, SIGUSR1, SIGUSR2, 0, };
//-----------------------------------------------------------------------------
// saved sigmask, to restore before fork/exec
static sigset_t saved_sigs;
static int saved_sigs_valid;
//-----------------------------------------------------------------------------
__attribute__((destructor))
static void restore_signals(void) {
	if (saved_sigs_valid)
		sigprocmask(SIG_SETMASK, &saved_sigs, 0);
}
//-----------------------------------------------------------------------------
static int setup_signals(void) {
	int fd, ret;
	const int *lp;
	sigset_t sigset;

	sigemptyset(&sigset);
	for (lp = sigs; *lp; ++lp)
		sigaddset(&sigset, *lp);
	sigprocmask(SIG_BLOCK, &sigset, &saved_sigs);
	saved_sigs_valid = 1;

	ret = fd = signalfd(-1, &sigset, SFD_CLOEXEC);
	if (ret < 0)
		error(1, errno, "signalfd()");
	ev_add_fd(fd, sighandler, 0);
	return fd;
}
//-----------------------------------------------------------------------------
static struct argp argp;
int main (int argc, char *argv[]) {
	int ret;

	program_invocation_name = program_invocation_short_name;
	ret = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &s);
	if (ret)
		return 1;
	if (!s.airport)
		error(1, 0, "no airport IP");

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, s.sk);
	if (ret < 0)
		error(1, errno, "failed to create socket");

	setup_signals();
	set_play();

	while (!s.sig.term) {
		ret = ev_loop(1);
		if (ret < 0) {
			if ((EINTR == errno)||(514/*ptraced*/ == errno))
				continue;
			error(0, errno, "ev_loop()");
			break;
		}
	}

	if (s.pid)
		kill(s.pid, SIGTERM);
	if (s.verbose)
		error(0, 0, "shutdown");
	return 0;
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static struct argp_option opts [] = {
	{ "verbose"	,  'v', 0, OPTION_NO_USAGE, "more verbose", -1, },
	{ "quiet"	,  'q', 0, OPTION_NO_USAGE, "less verbose", },
	{ "silent"	,  's', 0, OPTION_NO_USAGE | OPTION_ALIAS, },

	{ 0, 0, 0, 0, "Host options", },
	{ "url"		,  'l', "URL", 0, "url to listen to", },
	{ "time"	,  't', "SEC", 0, "time to wait before playback is stopped", },

	{ 0, 0, 0, 0, "Remote options", },
	{ "remote"	,  'r', "IP", 0, "airport ip address", },
	{ "agent"	,  'a', "FILE", 0, "path to raop_play agent", },
	{ "fifo"	,  'f', "FIFO", 0, "play FIFO", },

	{ 0, },
};
//-----------------------------------------------------------------------------
static
error_t parse_opts (int key, char * arg, struct argp_state * state) {
	int ret;

	switch (key) {
	case 'q' :
	case 's' :
		if (s.verbose)
			--s.verbose;
		break;
	case 'v':
		++s.verbose;
		break;

	case 'l':
		ret = url_listen(arg);
		if (ret < 0)
			error(1, 0, "url_listen(%s) failed", arg);
		ev_add_fd(ret, connection, arg);
		break;
	case 't':
		s.deadtime = strtod(arg, 0);
		if (s.deadtime < 0)
			s.deadtime = 0;
		break;

	case 'a':
		s.agent = arg;
		break;
	case 'r':
		s.airport = arg;
		break;
	case 'f':
		s.fifo = arg;
		break;
	case ARGP_KEY_ARG:
		if (!s.airport)
			s.airport = arg;
		else if (!s.fifo)
			s.fifo = arg;
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}
	return 0;
}
//-----------------------------------------------------------------------------
static struct argp argp = {
	.options = opts,
	.parser = parse_opts,
	.doc = "airport player controller",
};
// ----------------------------------------------------------------------------


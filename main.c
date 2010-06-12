#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#include <unistd.h>
#include <error.h>
#include <argp.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include <urllisten.h>
#include <poll-core.h>
#include <ustl.h>
#include <libconf.h>
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
	double volume;
	struct {
		int term;
	} sig;
	double deadtime;
	int playing;
#define PLAYING	1
#define PENDING	2
#define STOPPED	3
}s = {
	.volume = 1,
	.agent = "raop_play",
	.playing = PLAYING,
	.deadtime = 60,
	.default_stopcmd = "quit",
};
//-----------------------------------------------------------------------------
static void start_airport(void) {
	int ret;

	ret = fork();
	if (ret < 0)
		error(1, errno, "fork()");
	if (!ret) {
		pollcore_fds_freeze();
		pollcore_shutdown();

		dup2(s.sk[1], STDIN_FILENO);
		close(s.sk[0]);
		close(s.sk[1]);
		execlp(s.agent, "airport", "-i", s.airport, s.fifo, (char *)0);
		error(1, errno, "execlp(%s, ...)", s.agent);
	}
	s.pid = ret;
}
//-----------------------------------------------------------------------------
static int set_volume(double volume) {
	if (volume < 0)
		volume = 0;
	else if (volume > 1)
		volume = 1;
	s.volume = volume;
	if (s.verbose)
		error(0, 0, "volume %.3lf", s.volume);
	if (!s.pid)
		return 0;
	return dprintf(s.sk[0], "volume %.0lf\n", s.volume *100);
}
//-----------------------------------------------------------------------------
static void timed_stop(void *vp) {
	const char *cmd = s.stopcmd ?: s.default_stopcmd;
	dprintf(s.sk[0], "%s\n", cmd);
	s.playing = STOPPED;
	if (s.verbose)
		error(0, 0, "%s %s", cmd, s.airport);
}
//-----------------------------------------------------------------------------
static void set_stop(void) {
	if (PLAYING != s.playing)
		return;
	s.playing = PENDING;
	ustl_add_timeout(s.deadtime, timed_stop, 0);
	if (s.verbose)
		error(0, 0, "stop in %.1lf seconds", s.deadtime);
}
//-----------------------------------------------------------------------------
static void set_play(void) {
	if (!s.pid) {
		start_airport();
		s.playing = PLAYING;
		set_volume(s.volume);
		return;
	}
	switch (s.playing) {
	case PLAYING:
		break;
	case PENDING:
	default:
		ustl_remove_timeout(timed_stop, 0);
		if (s.verbose)
			error(0, 0, "removed pending stop");
		break;
	case STOPPED:
		dprintf(s.sk[0], "play %s\n", s.fifo);
		if (s.verbose)
			error(0, 0, "started %s", s.airport);
		break;
	}
	s.playing = PLAYING;
}
//-----------------------------------------------------------------------------
static int client_read(struct pollh *ph) {
	int ret;
	int fd = pollh_fd(ph);
	static char line[1024];
	struct argv arg;

	ret = read(fd, line, sizeof(line)-1);
	if (ret <= 0) {
		pollh_del(ph);
		return 0;
	}
	if (line[ret-1] == '\n')
		line[ret-1] = 0;
	memset(&arg, 0, sizeof(arg));
	argv_parse(&arg, line);
	if (arg.c <= 0)
		return 0;
	if (!strcmp(arg.v[0], "volume")) {
		if (arg.c > 1)
			set_volume(strtod(arg.v[1], 0));
		else
			dprintf(fd, "%.3lf\n", s.volume);
	} else if (!strcmp(arg.v[0], "stop")) {
		set_stop();
	} else if (!strcmp(arg.v[0], "start")) {
		set_play();
	} else if (!strcmp(arg.v[0], "time")) {
		if (arg.c > 1)
			s.deadtime = strtod(arg.v[1], 0);
		else
			dprintf(fd, "%.1lf sec\n", s.deadtime);
	} else if (!strcmp(arg.v[0], "cmd")) {
		if (arg.c > 1) {
			if (s.stopcmd)
				free(s.stopcmd);
			s.stopcmd = strdup(arg.v[1]);
		} else
			dprintf(fd, "command %s\n", s.stopcmd ?: s.default_stopcmd);
	}
	return ret;
}
//-----------------------------------------------------------------------------
static int connection(struct urllisten *url, int fd) {
	struct pollh *ph;
	const char *remote;
	char *tmp = 0;

	if (s.verbose >= 2)
		error(0, 0, "[%i] via [%i] %s", fd, urllisten_fd(url), urllisten_url(url));

	remote = urllisten_remote(url);
	if (!remote) {
		int pid, uid, gid;
		socket_peer_cred(urllisten_fd(url), &uid, &gid, &pid);
		asprintf(&tmp, "%s, u%i g%i p%i",
			urllisten_vfs_path(url), uid, gid, pid);
	}


	ph = pollh_new("client", remote ?: tmp);
	pollh_set_fd(ph, fd);
	pollh_set_handler(ph, EV_RD, client_read);
	pollh_mod_event(ph, EV_RD, 1);
	pollh_add(ph);
	if (tmp)
		free(tmp);
	return 0;
}
//-----------------------------------------------------------------------------
static void sighandler(int sig) {
	int status;

	switch (sig) {
	case SIGALRM:
		break;
	case SIGCHLD:
		// raop_play failed
		waitpid(-1, &status, WNOHANG);
		s.pid = 0;
		if (s.verbose)
			error(0, 0, "child exited");
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
			error(0, 0, "signal %i, %s", sig, strsignal(sig));
		break;
	}
}
static const int sigs[] = {
	SIGPIPE, SIGALRM, SIGCHLD, SIGTERM, SIGINT,
	SIGHUP, SIGUSR1, SIGUSR2, 0, };
//-----------------------------------------------------------------------------
static struct argp argp;
int main (int argc, char *argv[]) {
	int ret;

	pollcore_init();
	ustl_init();
	pollcore_start();

	ret = argp_parse(&argp, argc, argv, ARGP_IN_ORDER, 0, &s);
	if (ret)
		return 1;
	if (!s.airport)
		error(1, 0, "no airport IP");

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, s.sk);
	if (ret < 0)
		error(1, errno, "failed to create socket");

	pollcore_signal_handler(sigs, sighandler, 0);
	set_play();

	while (!s.sig.term) {
		ustl_runm(0.005);
		ustl_set_itimer(1);
		ret = pollcore_wait(-1);
		if (ret < 0) {
			if ((EINTR == errno)||(514/*ptraced*/ == errno))
				continue;
			error(1, errno, "pollcore_wait()");
		}
	}

	pollcore_shutdown();
	ustl_shutdown();
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
	struct urllisten *urll;

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
		urll = urllisten(arg);
		if (!urll)
			error(1, errno, "bad url %s", arg);
		urllisten_set_main(urll, connection);
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


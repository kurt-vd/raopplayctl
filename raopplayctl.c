#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <signal.h>
#include <stdarg.h>

#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>

#define NAME	"raopplayctl"

/* ARGUMENTS */
static const char help_msg[] =
	NAME ": Control airport device\n"
	"Usage:	" NAME " [OPTIONS] AIRPORT_URI\n"
	"	" NAME " [OPTIONS] -c play FILE\n"
	"	" NAME " [OPTIONS] -c stop\n"
	"	" NAME " [OPTIONS] -c volume VOLUME\n"
	"\n"
	"Options:\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Be more verbose\n"
	" -u, --uri=URI		Change server URI (default @raop_play_ctl)\n"
	" -c, --client		Send command to SERVER\n"
;

#ifdef _GNU_SOURCE
static const struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "uri", required_argument, NULL, 'u', },
	{ "client", no_argument, NULL, 'c', },
	{ },
};

#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif

static const char optstring[] = "?Vvu:c";

/* data */
static struct {
	int verbose;
	int client;
	const char *airport;
	const char *agent;
	const char *uri;

	const char *stopcmd;

	int agentpid;
	int agentfd;
	double volume;

	struct sig {
		int term;
		int alrm;
		int chld;
	} sig;

	int deadtime;
}s = {
	.volume = 1,
	.agent = "raop_play",
	.deadtime = 60,
	.stopcmd = "quit",
	.uri = "@raop_play_ctl",
};

/* logging */
	__attribute__((format(printf,3,4)))
void elog(int prio, int errnum, const char *fmt, ...)
{
	char *str;
	va_list va;

	va_start(va, fmt);
	vasprintf(&str, fmt, va);
	va_end(va);
	if (errnum) {
		char *tmp = str;

		asprintf(&str, "%s: %s", tmp, strerror(errnum));
		free(tmp);
	}
	if (s.client) {
		fprintf(stderr, NAME ": %s\n", str);
		fflush(stderr);
	}
	syslog(prio, "%s\n", str);
	free(str);
	if (prio <= LOG_CRIT)
		exit(1);
}

/* ctrl iface */
static int open_sock(const char *uri)
{
	int namelen, saved_umask, ret, sk;
	union {
		struct sockaddr sa;
		struct sockaddr_un un;
	} name = {};

	name.un.sun_family = AF_UNIX;
	strncpy(name.un.sun_path, uri, sizeof(name.un.sun_path));
	namelen = SUN_LEN(&name.un);
	if (name.un.sun_path[0] == '@')
		name.un.sun_path[0] = 0;

	/* socket creation */
	ret = sk = socket(name.sa.sa_family, SOCK_DGRAM/* | SOCK_CLOEXEC*/, 0);
	if (ret < 0) {
		elog(LOG_WARNING, errno, "socket %i dgram 0", name.sa.sa_family);
		return ret;
	}
	fcntl(sk, F_SETFD, fcntl(sk, F_GETFD) | FD_CLOEXEC);

	saved_umask = umask(0);
	ret = bind(sk, &name.sa, namelen);
	umask(saved_umask);
	return sk;
}

static int connect_sock(int sk, const char *uri)
{
	int namelen, ret;
	union {
		struct sockaddr sa;
		struct sockaddr_un un;
	} name = {};

	name.un.sun_family = AF_UNIX;
	strncpy(name.un.sun_path, uri, sizeof(name.un.sun_path));
	namelen = SUN_LEN(&name.un);
	if (name.un.sun_path[0] == '@')
		name.un.sun_path[0] = 0;

	ret = connect(sk, &name.sa, namelen);
	if (ret < 0)
		elog(LOG_CRIT, errno, "connect %s", uri);
	return ret;
}

/* child control */
static void start_airport(void)
{
	int ret;

	ret = fork();
	if (ret < 0)
		elog(LOG_CRIT, errno, "fork()");
	if (!ret) {
		dup2(s.agentfd, STDIN_FILENO);
		dup2(s.agentfd, STDOUT_FILENO);
		close(s.agentfd);
		execlp(s.agent, s.agent, "-i", s.airport, NULL);
		elog(LOG_CRIT, errno, "execlp %s -i %s", s.agent, s.airport);
	}
	s.agentpid = ret;
	elog(LOG_INFO, 0, "started %s", s.agent);
}

static void set_volume(double volume)
{
	double tvol = (volume * 0.15) + 0.85;

	if (volume < 0)
		volume = 0;
	else if (volume > 1)
		volume = 1;
	s.volume = volume;
	elog(LOG_INFO, 0, "volume %.3lf (%.3lf)", s.volume, tvol);
	if (s.agentpid)
		printf("volume %.0lf\n", tvol *100);
}

static void stop_playing(void)
{
	printf("%s\n", s.stopcmd);
	elog(LOG_INFO, 0, "%s %s", s.stopcmd, s.airport);
	kill(s.agentpid, SIGTERM);
	elog(LOG_INFO, 0, "killed");
}

static void schedule_stop(void)
{
	if (!s.agentpid)
		return;
	alarm(s.deadtime);
	elog(LOG_INFO, 0, "stop in %i seconds", s.deadtime);
}

static void set_play(const char *path)
{
	/* cancel any scheduled stop */
	alarm(0);

	/* start agent if necessary */
	if (!s.agentpid) {
		start_airport();
		set_volume(s.volume);
	}
	/* do command */
	printf("play %s\n", path);
	elog(LOG_INFO, 0, "play %s", path);
}

/* signal handlers */
static void sighandler(int sig)
{
	switch (sig) {
	case SIGALRM:
		s.sig.alrm = 1;
		break;
	case SIGCHLD:
		s.sig.chld = 1;
		break;
	case SIGTERM:
	case SIGINT:
		s.sig.term = 1;
		break;
	}
}

/* signal installer helper */
static void setup_signals(void (*handler)(int sig), const int *table)
{
	/* setup signals */
	struct sigaction sa = { .sa_handler = handler, };
	int j;

	sigfillset(&sa.sa_mask);
	for (j = 0; table[j]; ++j) {
		if (sigaction(table[j], &sa, NULL) < 0)
			elog(LOG_CRIT, errno, "sigaction %i", table[j]);
	}
}

int main(int argc, char *argv[])
{
	int ret, status, opt, sock, sk[2], j;
	char *tok, *argument;
	static char buf[1024];
  
	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s\n", NAME, VERSION);
		return 0;
	case 'v':
		++s.verbose;
		break;
	case 'u':
		s.uri = optarg;
		break;
	case 'c':
		s.client = 1;
		break;

	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	if (s.client) {
		/* client mode */
		sprintf(buf, "@raop_play_client-%i", getpid());
		sock = open_sock(buf);
		connect_sock(sock, s.uri);
		ret = 0;
		for (j = optind; j < argc; ++j) {
			ret += sprintf(buf+ret, "%s%s", ret ? " " : "", argv[j]);
		}
		if (send(sock, buf, ret, 0) < 0)
			elog(LOG_CRIT, errno, "send %s", buf);
		return 0;
	}

	/* start syslog */
	openlog(NAME, LOG_PERROR | LOG_PID, LOG_DAEMON);

	/* server mode */
	s.airport = argv[optind++];

	if (!s.airport)
		elog(LOG_CRIT, 0, "no airport IP");

	ret = sock = open_sock(s.uri);
	if (ret < 0)
		elog(LOG_CRIT, errno, "open socket");

	ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sk);
	if (ret < 0)
		elog(LOG_CRIT, errno, "failed to create socketpair");
	dup2(sk[0], STDIN_FILENO);
	dup2(sk[0], STDOUT_FILENO);
	close(sk[0]);
	s.agentfd = sk[1];

	setup_signals(sighandler, (const int []){ SIGINT, SIGTERM, SIGCHLD, SIGALRM, 0});

	while (!s.sig.term || s.agentpid) {
		if (s.sig.alrm) {
			s.sig.alrm = 0;
			stop_playing();
		}
		if (s.sig.chld) {
			s.sig.chld = 0;
			/* agent exited */
			waitpid(-1, &status, WNOHANG);
			s.agentpid = 0;
			elog(LOG_INFO, 0, "%s exited", s.agent);
		}
		if (s.sig.term && s.agentpid) {
			elog(LOG_INFO, 0, "stop airport now");
			/* cancel any pending alarm */
			alarm(0);
			/* actually stop */
			stop_playing();
		}
		/* receive commands from ctl socket */
		ret = recv(sock, buf, sizeof(buf)-1, 0);
		if (ret < 0) {
			if (EINTR == errno)
				continue;
			elog(LOG_CRIT, errno, "recv %s", s.uri);
		}
		tok = strtok(buf, " \t\r\n\v\f");
		argument = strtok(NULL, "\t\r\n\v\f"); /* allow spaces in argument */
		if (!strcmp(tok, "play"))
			set_play(argument);
		else if (!strcmp(tok, "stop"))
			schedule_stop();
		else if (!strcmp(tok, "volume"))
			set_volume(strtod(argument, 0));
		else if (!strcmp(tok, "exit"))
			s.sig.term = 1;
		else
			/* pass-through */
			printf("%s %s\n", tok, argument ?: "");
		fflush(stdout);
	}

	elog(LOG_INFO, 0, "shutdown");
	return 0;
}

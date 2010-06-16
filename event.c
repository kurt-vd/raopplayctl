#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <error.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/time.h>

#include <llist.h>
#include "ev.h"
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static inline void *zmalloc(unsigned int size) {
	void *vp;

	vp = malloc(size);
	if (!vp)
		error(1, errno, "malloc()");
	memset(vp, 0, size);
	return vp;
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static struct {
	int epfd;
	struct llist fds;
	struct llist timers;
	int nevs;
	#define NEVS	16
	struct epoll_event evs[NEVS];
	struct {
		int revs;
	} curr;
} s = {
	.epfd = -1,
};
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
struct fentry {
	struct llist list;
	int fd;
	void (*fn)(int fn, void *vp);
	void *vp;
	int evs;
	int revs;
};
#define list2fentry(x)	container_of(struct fentry, list, (x))
struct tentry {
	struct llist list;
	void (*fn)(void *vp);
	void *vp;
	double wakeup;
};
#define list2tentry(x)	container_of(struct tentry, list, (x))
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static struct fentry * find_fd(int fd) {
	struct llist *lst;
	struct fentry *ent;

	llist_foreach(&s.fds, lst) {
		ent = list2fentry(lst);
		if (fd == ent->fd)
			return ent;
	}
	return 0;
}
//-----------------------------------------------------------------------------
static struct tentry * find_timer(void (*fn)(void *), const void *vp) {
	struct llist *lst;
	struct tentry *ent;

	llist_foreach(&s.timers, lst) {
		ent = list2tentry(lst);
		if ((ent->fn == fn)&&(ent->vp == vp))
			return ent;
	}
	return 0;
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
double ev_now (void) {
#if defined(HAVE_CLOCK_GETTIME)
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) < 0)
		error(1, errno, "clock_gettime()");
	return t.tv_sec + (t.tv_nsec / 1e9);
#elif defined(HAVE_GETTIMEOFDAY)
	struct timeval t;
	if (0 != gettimeofday(&t, 0))
		error(1, errno, "gettimeofday()");
	return t.tv_sec + ((t.tv_usec % 1000000) / 1e6);
#else
#error timer_now not implemented
	return time(0);
#endif
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static int epoll_add(struct fentry *ent) {
	int ret;
	struct epoll_event ev;

	if (s.epfd < 0)
		return 0;

	ev.events = 0;
	if (ent->evs & EVENT_RD)
		ev.events |= EPOLLIN;
	if (ent->evs & EVENT_WR)
		ev.events |= EPOLLOUT;
	ev.data.ptr = ent;
	ret = epoll_ctl(s.epfd, EPOLL_CTL_ADD, ent->fd, &ev);
	if (ret < 0)
		error(1, errno, "epoll_add %i", ent->fd);
	return ret;
}
//-----------------------------------------------------------------------------
static void epoll_remove(struct fentry *ent) {
	int j;

	if (s.epfd < 0)
		return;
	epoll_ctl(s.epfd, EPOLL_CTL_DEL, ent->fd, 0);
	for (j = 0; j < s.nevs; ++j) {
		if (s.evs[j].data.ptr == ent)
			s.evs[j].data.ptr = 0;
	}
}
//-----------------------------------------------------------------------------
static int itimer_cancel(void) {
	int ret;
	struct itimerval setp;

	memset(&setp, 0, sizeof(setp));
	ret = setitimer(ITIMER_REAL, &setp, 0);
	if (ret < 0)
		error(1, errno, "clear itimer");
	return ret;
}
//-----------------------------------------------------------------------------
static int itimer_schedule(double delay) {
	int ret;
	unsigned long v;
	struct itimerval setp;

	v = lround(delay *1e6);
	memset(&setp, 0, sizeof(setp));
	setp.it_value.tv_sec  = v / 1000000;
	setp.it_value.tv_usec = v % 1000000;
	if (!setp.it_value.tv_sec && !setp.it_value.tv_usec)
		setp.it_value.tv_usec = 1;
	ret = setitimer(ITIMER_REAL, &setp, 0);
	if (ret < 0)
		error(1, errno, "setitimer");
	return ret;
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int ev_init(void) {
	int ret;
	struct llist *lst;

	ret = s.epfd = epoll_create(16);
	if (ret < 0)
		error(1, errno, "epoll_create()");

	llist_foreach(&s.fds, lst)
		epoll_add(list2fentry(lst));

	return s.epfd;
}
//-----------------------------------------------------------------------------
__attribute__((destructor))
void ev_shutdown(void) {
	struct llist *lst;
	struct fentry *ent;

	if (s.epfd >= 0)
		close(s.epfd);
	s.epfd = -1;

	while (0 != (lst = llist_pop(&s.fds))) {
		ent = list2fentry(lst);
		close(ent->fd);
		free(ent);
	}
	while (0 != (lst = llist_pop(&s.timers)))
		free(list2tentry(lst));
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
int ev_add_fd(int fd, void (*fn)(int fd, void *vp), const void *vp) {
	struct fentry *ent;

	ent = find_fd(fd);
	if (!ent) {
		ent = zmalloc(sizeof(*ent));
		ent->fd = fd;
		ent->evs |= EVENT_RD;
		llist_add(&s.fds, &ent->list);
		epoll_add(ent);
	}
	ent->fn = fn;
	ent->vp = (void *)vp;
	return 0;
}
//-----------------------------------------------------------------------------
void ev_remove_fd(int fd) {
	struct fentry *ent;

	ent = find_fd(fd);
	if (ent) {
		epoll_remove(ent);
		llist_del(&ent->list);
		free(ent);
	}
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
void ev_add_timeout(double timeout, void (*fn)(void *), const void *vp) {
	struct tentry *ent;

	ent = find_timer(fn, vp);
	if (!ent) {
		ent = zmalloc(sizeof(*ent));
		ent->fn = fn;
		ent->vp = (void *)vp;
	}
	ent->wakeup = ev_now() + timeout;
	llist_add(&s.timers, &ent->list);
}
//-----------------------------------------------------------------------------
void ev_repeat_timeout(double increment, void (*fn)(void *), const void *vp) {
	struct tentry *ent;

	ent = find_timer(fn, vp);
	if (!ent)
		ev_add_timeout(increment, fn, vp);
	else {
		ent->wakeup += increment;
		llist_add(&s.timers, &ent->list);
	}
}
//-----------------------------------------------------------------------------
void ev_remove_timeout(void (*fn)(void *), const void *vp) {
	struct tentry *ent;

	ent = find_timer(fn, vp);
	if (ent) {
		llist_del(&ent->list);
		free(ent);
	}
}
//-----------------------------------------------------------------------------
static int cmp_timer(struct llist *la, struct llist *lb) {
	struct tentry *a = list2tentry(la);
	struct tentry *b = list2tentry(lb);

	return lround(a->wakeup - b->wakeup);
}
//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static void run_timed_evs(double now, double *pfirst) {
	struct llist *lst;
	struct tentry *ent;
	int cnt;
	struct llist done;

	llist_init(&done);
	do {
		cnt = 0;
		llist_sort(&s.timers, cmp_timer);
		while (!llist_empty(&done)) {
			ent = list2tentry(&s.timers);
			if (ent->wakeup > now) {
				if (pfirst && (ent->wakeup < *pfirst))
					*pfirst = ent->wakeup;
				break;
			}
			llist_add(&done, &ent->list);
			ent->fn(ent->vp);
			++cnt;
		}
	} while (cnt);
	// clean up cache
	while (0 != (lst = llist_pop(&done)))
		free(list2tentry(lst));
}
//-----------------------------------------------------------------------------
int ev_current_evs(void) {
	int ret = 0;
	if (s.curr.revs & (EPOLLIN | EPOLLRDHUP))
		ret |= EVENT_RD;
	if (s.curr.revs & (EPOLLOUT | EPOLLHUP))
		ret |= EVENT_WR;
	if (s.curr.revs & EPOLLERR)
		ret |= EVENT_ERR;
	return ret;
}
//-----------------------------------------------------------------------------
int ev_loop(double delay) {
	int ret, j;
	struct fentry *ent;
	double now, next;

	if (s.epfd < 0) {
		ret = ev_init();
		if (ret < 0)
			return ret;
	}
	now = ev_now();
	next = now + delay;
	run_timed_evs(now, &next);
	itimer_schedule(next);
	ret = s.nevs = epoll_wait(s.epfd, s.evs, NEVS, -1);
	if (ret < 0)
		return ret;
	itimer_cancel();
	for (j = 0; j < s.nevs; ++j) {
		if (!s.evs[j].data.ptr)
			// cleared by ev_remove
			continue;
		s.curr.revs = s.evs[j].events;
		ent = s.evs[j].data.ptr;
		ent->fn(ent->fd, ent->vp);
	}
	s.curr.revs = 0;
	return ret;
}
//-----------------------------------------------------------------------------


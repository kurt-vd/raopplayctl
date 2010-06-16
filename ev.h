#ifndef _ev_h_
#define _ev_h_

#ifdef __cplusplus
extern "C" {
#endif
//-----------------------------------------------------------------------------
extern double ev_now(void);
//-----------------------------------------------------------------------------
extern int ev_init(void);
extern int ev_loop(double max_delay);
extern void ev_shutdown(void);
//-----------------------------------------------------------------------------
extern int ev_add_fd(int fd, void (*fn)(int fd, void *vp), const void *vp);
extern void ev_remove_fd(int fd);
extern int ev_current_evs(void);
#define EVENT_RD	0x01
#define EVENT_WR	0x02
#define EVENT_ERR	0x03
//-----------------------------------------------------------------------------
extern void ev_add_timeout(double timeout, void (*fn)(void *), const void *vp);
extern void ev_repeat_timeout(double timeout, void (*fn)(void *), const void *vp);
extern void ev_remove_timeout(void (*fn)(void *), const void *vp);
//-----------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
#endif


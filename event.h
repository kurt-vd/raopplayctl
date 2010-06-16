#include <inttypes.h>

#ifndef _event_h_
#define _event_h_

#ifdef __cplusplus
extern "C" {
#endif
//-----------------------------------------------------------------------------
extern double event_now(void);
//-----------------------------------------------------------------------------
extern int event_init(void);
extern int event_loop(double max_delay);
extern void event_shutdown(void);
//-----------------------------------------------------------------------------
extern int event_add_fd(int fd, void (*fn)(int fd, void *vp), const void *vp);
extern void event_remove_fd(int fd);
extern int event_current_events(void);
#define EVENT_RD	0x01
#define EVENT_WR	0x02
#define EVENT_ERR	0x03
//-----------------------------------------------------------------------------
extern void event_add_timeout(double timeout, void (*fn)(void *), const void *vp);
extern void event_repeat_timeout(double timeout, void (*fn)(void *), const void *vp);
extern void event_remove_timeout(void (*fn)(void *), const void *vp);
//-----------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
#endif


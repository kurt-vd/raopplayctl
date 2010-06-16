#include <inttypes.h>

#ifndef signalfd_h
#define signalfd_h

#ifdef __cplusplus
extern "C" {
#endif
//-----------------------------------------------------------------------------
struct signalfd_siginfo {
	uint32_t ssi_signo;   /* Signal number */
	int32_t  ssi_errno;   /* Error number (unused) */
	int32_t  ssi_code;    /* Signal code */
	uint32_t ssi_pid;     /* PID of sender */
	uint32_t ssi_uid;     /* Real UID of sender */
	int32_t  ssi_fd;      /* File descriptor (SIGIO) */
	uint32_t ssi_tid;     /* Kernel timer ID (POSIX timers) */
	uint32_t ssi_band;    /* Band event (SIGIO) */
	uint32_t ssi_overrun; /* POSIX timer overrun count */
	uint32_t ssi_trapno;  /* Trap number that caused signal */
	int32_t  ssi_status;  /* Exit status or signal (SIGCHLD) */
	int32_t  ssi_int;     /* Integer sent by sigqueue(2) */
	uint64_t ssi_ptr;     /* Pointer sent by sigqueue(2) */
	uint64_t ssi_utime;   /* User CPU time consumed (SIGCHLD) */
	uint64_t ssi_stime;   /* System CPU time consumed (SIGCHLD) */
	uint64_t ssi_addr;    /* Address that generated signal
				 (for hardware-generated signals) */
	uint8_t  pad[48];      /* Pad size to 128 bytes (allow for
				 additional fields in the future) */
};
//-----------------------------------------------------------------------------
#define SFD_NONBLOCK	1
#define SFD_CLOEXEC	2
//-----------------------------------------------------------------------------
int signalfd(int fd, const sigset_t *set, int flags);
//-----------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
#endif


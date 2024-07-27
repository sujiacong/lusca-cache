#ifndef __LIBIAPP_COMM_TYPES_H__
#define __LIBIAPP_COMM_TYPES_H__

/* Special case pending filedescriptors. Set in fd_table[fd].read/write_pending
 */
typedef enum {
    COMM_PENDING_NORMAL,        /* No special processing required */
    COMM_PENDING_WANTS_READ,    /* need to read, no matter what commSetSelect indicates */
    COMM_PENDING_WANTS_WRITE,   /* need to write, no matter what commSetSelect indicates */
    COMM_PENDING_NOW            /* needs to be called again, without needing to wait for readiness
                                 * for example when data is already buffered etc */
} comm_pending;


#define COMM_OK           (0)
#define COMM_ERROR       (-1)
#define COMM_NOMESSAGE   (-3)
#define COMM_TIMEOUT     (-4)
#define COMM_SHUTDOWN    (-5)
#define COMM_INPROGRESS  (-6)
#define COMM_ERR_CONNECT (-7)
#define COMM_ERR_DNS     (-8)
#define COMM_ERR_CLOSING (-9)

/* Select types. */
#define COMM_SELECT_READ   (0x1)
#define COMM_SELECT_WRITE  (0x2)

typedef enum {
        COMM_NONBLOCKING = 1,
        COMM_NOCLOEXEC = 2,
        COMM_REUSEADDR = 4,
        COMM_DOBIND = 8,
        COMM_TPROXY_LCL = 16,
        COMM_TPROXY_REM = 32
} comm_flags_t;


/* The default "no TOS" value */
#define COMM_TOS_DEFAULT        0

#endif

#ifndef	__LIBSQSTORE_STORE_LOG_H__
#define	__LIBSQSTORE_STORE_LOG_H__

typedef enum {
    SWAP_LOG_NOP,
    SWAP_LOG_ADD,
    SWAP_LOG_DEL,
    SWAP_LOG_VERSION,
    SWAP_LOG_PROGRESS,		/* XXX to be later defined */
    SWAP_LOG_COMPLETED,		/* XXX to be later defined */
    SWAP_LOG_ERROR,		/* XXX to be later defined */
    SWAP_LOG_MAX
} swap_log_op;

/*
 * Do we need to have the dirn in here? I don't think so, since we already
 * know the dirn ..
 */
struct _storeSwapLogData {
    char op;
    sfileno swap_filen;
    time_t timestamp;
    time_t lastref;
    time_t expires;
    time_t lastmod;
    squid_file_sz swap_file_sz;
    u_short refcount;
    u_short flags;
    unsigned char key[SQUID_MD5_DIGEST_LENGTH];
};
typedef struct _storeSwapLogData storeSwapLogData;

struct _storeSwapLogHeader {
    char op;
    int version;
    int record_size;
};
typedef struct _storeSwapLogHeader storeSwapLogHeader;

struct _storeSwapLogCompleted {
	char op;
};
typedef struct _storeSwapLogCompleted storeSwapLogCompleted;

struct _storeSwapLogProgress {
	char op;
	u_int32_t progress;
	u_int32_t total;
};
typedef struct _storeSwapLogProgress storeSwapLogProgress;

struct _storeSwapLogDataOld {
    char op;
    sfileno swap_filen;
    time_t timestamp;
    time_t lastref;
    time_t expires;
    time_t lastmod;
    size_t swap_file_sz;
    u_short refcount;
    u_short flags;
    unsigned char key[SQUID_MD5_DIGEST_LENGTH];
};
typedef struct _storeSwapLogDataOld storeSwapLogDataOld;

extern const char * swap_log_op_str[];

extern int storeSwapLogUpgradeEntry(storeSwapLogData *dst, storeSwapLogDataOld *src);
extern int storeSwapLogPrintHeader(FILE *fp);
extern int storeSwapLogPrintProgress(FILE *fp, u_int32_t progress, u_int32_t total);
extern int storeSwapLogPrintCompleted(FILE *fp);

#endif

#ifndef	__LUSCA_FTPTYPES_H__
#define	__LUSCA_FTPTYPES_H__

#define FTP_LOGIN_ESCAPED 1
#define FTP_LOGIN_NOT_ESCAPED 0

typedef enum {
    BEGIN,
    SENT_USER,
    SENT_PASS,
    SENT_TYPE,
    SENT_MDTM,
    SENT_SIZE,
    SENT_PORT,
    SENT_PASV,
    SENT_CWD,
    SENT_LIST,
    SENT_NLST,
    SENT_REST,
    SENT_RETR,
    SENT_STOR,
    SENT_QUIT,
    READING_DATA,
    WRITING_DATA,
    SENT_MKDIR
} ftp_state_t;

struct _ftp_flags {
    unsigned int isdir:1;
    unsigned int pasv_supported:1;
    unsigned int skip_whitespace:1;
    unsigned int rest_supported:1;
    unsigned int pasv_only:1;
    unsigned int authenticated:1;
    unsigned int http_header_sent:1;
    unsigned int tried_nlst:1;
    unsigned int use_base:1;
    unsigned int dir_slash:1;
    unsigned int root_dir:1;
    unsigned int no_dotdot:1;
    unsigned int html_header_sent:1;
    unsigned int binary:1;
    unsigned int try_slash_hack:1;
    unsigned int put:1;
    unsigned int put_mkdir:1;
    unsigned int listformat_unknown:1;
};

#endif

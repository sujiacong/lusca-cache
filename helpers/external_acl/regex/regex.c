/*
 * #insert GPLv2 licence here.
 */

/*
 * This external_acl helper is designed for evaluating the throughput of
 * regular expression rule matching.
 *
 * It takes a URL only on STDIN and returns ERR if the URL matches one of
 * the regex rules; it returns OK otherwise.
 *
 * It is not designed for general production use.
 *   -- adrian
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>
#include <regex.h>

#define	HELPERBUFSZ	16384
#define	MAXLINE		8192
#define	RELOAD_TIME	5

static int debug = 0;

struct _regex_entry {
	int linenum;
	const char *entry;
	regex_t re;
	int re_flags;
};
typedef struct _regex_entry regex_entry_t;

struct {
	regex_entry_t *r;
	int count;
	int alloc;
} re_list = { NULL, 0, 0 };

regex_entry_t *
re_list_get(void)
{
	regex_entry_t *r;

	if (re_list.count <= re_list.alloc) {
		r = realloc(re_list.r,
		    sizeof(regex_entry_t) * (re_list.alloc + 16));
		if (r == NULL) {
			perror("re_list_get: realloc");
			return NULL;
		}
		re_list.r = r;
		re_list.alloc += 16;
	}
	
	/* Reuse r */
	r = &re_list.r[re_list.count];
	bzero(r, sizeof(regex_entry_t));
	/* The caller needs to bump re_list.count if they're using it */
	return r;
}

void
re_list_free(void)
{
	int i;
	for (i = 0; i < re_list.count; i++) {
		regfree(&re_list.r[i].re);
		bzero(&re_list.r[i], sizeof(regex_entry_t));
	}
	re_list.count = 0;
}

int
regex_init(regex_entry_t *r, const char *entry, int linenum, int re_flags)
{
	int i;

	bzero(r, sizeof(*r));
	r->entry = strdup(entry);
	r->linenum = linenum;
	r->re_flags = re_flags;

	if (debug) fprintf(stderr, "compiling: '%s'\n", entry);
	i = regcomp(&r->re, entry, re_flags);
	if (i) {	/* error condition */ 
		perror("regcomp");	/* XXX should output i instead */
		/* XXX should regfree be called here? */
		return 0;
	}

	return 1;
}

int
regex_parse_line(const char *line, int linenum)
{
	int i;
	regex_entry_t *r;

	/* Comment? skip */
	if (line[0] == '#')
		return 0;
	if (line[0] == '\r' || line[0] == '\n' || line[0] == '\0')
		return 0;

	/* Get the latest unallocated entry */
	r = re_list_get();
	if (r == NULL)
		return -1;

	/* For now, just bump the thing entirely to the line parser */
	i = regex_init(r, line, linenum, REG_EXTENDED | REG_NOSUB);
	if (i <= 0)
		return -1;

	/* success - use */
	re_list.count++;

	return 1;

}

static void
trim_trailing_crlf(char *buf)
{
	int n;

	for (n = strlen(buf) - 1;
	    n >= 0 && (buf[n] == '\r' || buf[n] == '\n');
	    n --)
		buf[n] = '\0';
}


void
load_regex_file(const char *file)
{
	FILE *fp;
	char buf[MAXLINE];
	int linenum;
	int n;

	fp = fopen(file, "r");
	if (! fp) {
		perror("fopen");
		exit(127);
	}

	linenum = 0;
	while (!feof(fp)) {
		linenum++;
		if (! fgets(buf, MAXLINE, fp))
			break;	/* XXX should check for error or EOF */

		/* Trim trailing \r\n's */
		trim_trailing_crlf(buf);
		n = regex_parse_line(buf, linenum);
	}

	fclose(fp);
}

static void
check_file_update(const char *fn, struct timeval *m)
{
	/* For now, always reload */
	re_list_free();
	load_regex_file(fn);
}

static int
re_lookup(const char *url)
{
	int r, i;

	for (i = 0; i < re_list.count; i++) {
		if (debug) fprintf(stderr, "checking '%s' against '%s'\n", url, re_list.r[i].entry);
		r = regexec(&re_list.r[i].re, url, 0, NULL, 0);
		if (r == 0) {	/* Success */
			return i;
		}
	}
	return 0;
}


int
main(int argc, const char *argv[])
{
	const char *fn;
	char buf[HELPERBUFSZ];
	char url[HELPERBUFSZ];
	int seqnum;
	time_t ts;
	int r;
	struct stat sb;
	time_t last_mtime = 0;

	if (argc < 2) {
		printf("%s: <config file>\n", argv[0]);
		exit(127);
	}
	fn = argv[1];

	/* set stdout/stderr unbuffered */
	(void) setvbuf(stdout, NULL, _IONBF, 0);
	(void) setvbuf(stderr, NULL, _IONBF, 0);

	/* initial load */
	if (stat(fn, &sb) < 0) {
		perror("stat");
		exit(127);
	}
	last_mtime = sb.st_mtimespec.tv_sec;

	load_regex_file(fn);
	ts = time(NULL);

	while (!feof(stdin)) {
		if (time(NULL) - ts > RELOAD_TIME) {
			if (debug) fprintf(stderr, "re-check\n");
			ts = time(NULL);
			/* re-stat the file */
			if (stat(fn, &sb) < 0) {
				perror("stat");
			} else if (sb.st_mtimespec.tv_sec > last_mtime) {
				last_mtime = sb.st_mtimespec.tv_sec;
				check_file_update(fn, NULL);
			}
		}

		if (! fgets(buf, HELPERBUFSZ, stdin))
			break;
		trim_trailing_crlf(buf);
		if (debug) fprintf(stderr, "read: %s\n", buf);

		/* Split out the seqnum and URL */
		sscanf(buf, "%d %s", &seqnum, url);
		if (debug) fprintf(stderr, "seqnum: %d; url: '%s'\n", seqnum, url);
 
		/* XXX and the URL should be unescaped and normalised properly */
		r = re_lookup(url);
		if (r > 0) {
			if (debug) fprintf(stderr, "HIT: line %d; rule %s\n",
			    re_list.r[r].linenum, re_list.r[r].entry);
			printf("%d ERR message=line%s%d log=line%s%d\n", seqnum,
			    "%20", re_list.r[r].linenum,
			    "%20", re_list.r[r].linenum);
		} else {
			printf("%d OK\n", seqnum);
		}
	}
	re_list_free();	
	exit(0);
}

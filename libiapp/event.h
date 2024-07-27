#ifndef	__LIBIAPP_EVENT_H__
#define	__LIBIAPP_EVENT_H__

typedef void EVH(void *);

typedef int EVCDT(int, void *);

/* The list of event processes */
struct ev_entry {
    EVH *func;
    void *arg;
    const char *name;
    double when;
    struct ev_entry *next;
    int weight;
    int id;
};

extern struct ev_entry *tasks;
extern const char * last_event_ran;

extern void eventAdd(const char *name, EVH * func, void *arg, double when, int);
extern void eventAddIsh(const char *name, EVH * func, void *arg, double delta_ish, int);
extern void eventRun(void);
extern int eventNextTime(void);
extern void eventDelete(EVH * func, void *arg);
void eventDeleteNoAssert(EVH * func, void *arg);
void eventTravel(int fd, EVH * func, EVCDT* travel);
extern void eventConditionDelete(int fd, EVH * func, EVCDT* condition);
extern void eventInit(void);
extern void eventCleanup(void);
extern void eventFreeMemory(void);
extern int eventFind(EVH *, void *);


#endif

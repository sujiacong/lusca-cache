#ifndef	__LIB__MULTI_PROCESS_H__
#define	__LIB__MULTI_PROCESS_H__

#ifndef __USE_GNU
#define __USE_GNU
#endif

#include "config.h"


#if HAVE_ERRNO_H
#include <errno.h> /* for ENOTSUP */
#endif
#if HAVE_SCHED_H
#include <sched.h>
#endif

#define MAX_KID_SUPPORT 32
enum { badFailureLimit = 4 };
enum { fastFailureTimeLimit = 10 };

#define APP_SHORTNAME "squid"
#define MAX_CHILDREN 3
#define MAX_KIDNAM_LEN 32


#if !HAVE_CPU_AFFINITY
/* failing replacements to minimize the number of if-HAVE_CPU_AFFINITYs */
#if !defined(__cpu_set_t_defined)
typedef struct {
    int bits;
} cpu_set_t;
#endif
inline int sched_setaffinity(int, size_t, cpu_set_t *) { return ENOTSUP; }
inline int sched_getaffinity(int, size_t, cpu_set_t *) { return ENOTSUP; }
#endif /* HAVE_CPU_AFFINITY */

#if !defined(CPU_SETSIZE)
#define CPU_SETSIZE 0
#endif

#if !defined(CPU_ZERO)
#define CPU_ZERO(set) (void)0
#endif

#if !defined(CPU_SET)
#define CPU_SET(cpu, set) (void)0
#endif

#if !defined(CPU_CLR)
#define CPU_CLR(cpu, set) (void)0
#endif

#if !defined(CPU_ISSET)
#define CPU_ISSET(cpu, set) false
#endif


typedef struct _CPUAffinitySet
{
	cpu_set_t theCpuSet; ///< configured CPU affinity for this process
	cpu_set_t theOrigCpuSet; ///< CPU affinity for this process before apply()
}CPUAffinitySet;

/// stores cpu_affinity_map configuration
typedef struct _CPUAffinityMap
{
	int	processessize;
    int theProcesses[MAX_KIDNAM_LEN]; 			///< list of process numbers
    int theCores[MAX_KIDNAM_LEN]; 				///< list of cores
}CPUAffinityMap;


typedef struct _Kid
{
    // Information preserved across restarts
    char theName[MAX_KIDNAM_LEN]; ///< process name
    int badFailures; ///< number of "repeated frequent" failures

    // Information specific to a running or stopped kid
    pid_t  pid; ///< current (for a running kid) or last (for stopped kid) PID
    time_t startTime; ///< last start time
    int   isRunning; ///< whether the kid is assumed to be alive
    int status; ///< exit status of a stopped kid
}Kid;

typedef struct _Kids
{
	  int kidcount;
	  Kid storage[MAX_KID_SUPPORT];
}Kids;

/// process kinds
typedef enum {
    pkOther  = 0, ///< we do not know or do not care
    pkCoordinator = 1, ///< manages all other kids
    pkWorker = 2, ///< general-purpose worker bee
    pkDisker = 4, ///< cache_dir manager
    pkHelper = 8  ///< general-purpose helper child
} ProcessKind;

extern int shutting_down;
extern int KidIdentifier; 
extern int TheProcessKind;
extern char TheKidName[64];
extern Kids AllKids; ///< All kids being maintained
extern CPUAffinitySet *TheCpuAffinitySet;


int IamPrimaryProcess();

extern void CpuAffinityCheck();
extern void CpuAffinityInit();
extern void CpuAffinityPrint();
extern void CpuAffinityReconfigure();
extern void ConfigureCurrentKid(const char *processName);
extern int IamMasterProcess();
extern int IamWorkerProcess();
extern int IamDiskProcess();
extern int IamCoordinatorProcess();
extern int InDaemonMode();
extern int UsingSmp();
extern int NumberOfKids();
extern int cpuAffinityMapAdd(CPUAffinityMap* cpumap, int arrprocess[], int arrcores[], int processsize, int coressize);

int initAllkids();
int kidShouldRestart(Kid* kid);
int kidStart(Kid* kid, pid_t cpid);
Kid* kidFind(pid_t pid);
int isKidCalledExit();
void stopKid(Kid* kid, int exitStatus);
int kidExitStatus();
int isKidSignaled(Kid* kid);
int isKidCalledExit(Kid* kid);
int kidTermSignal(Kid* kid);
int isKidHopeless(Kid* kid);
int isKidsSomeRunning();
int isKidsShouldRestartSome();
int kidsSomeSignaledWithSig(const int sgnl);
int isKidsAllHopeless();

#endif


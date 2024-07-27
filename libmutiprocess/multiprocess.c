#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <syslog.h>
#include <errno.h>
#include "multiprocess.h"
#include "../src/squid.h"


int KidIdentifier; 
int TheProcessKind = pkOther;
char TheKidName[64];
Kids AllKids; ///< All kids being maintained
CPUAffinitySet *TheCpuAffinitySet = NULL;

// glibc prior to 2.6 lacks CPU_COUNT
#ifndef CPU_COUNT
#define CPU_COUNT(set) CpuCount(set)
/// CPU_COUNT replacement
static int CpuCount(const cpu_set_t *set)
{
    int count = 0; 
    int i = 0;
    for (i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, set))
            ++count;
    }
    return count;
}
#endif /* CPU_COUNT */

// glibc prior to 2.7 lacks CPU_AND
#ifndef CPU_AND
#define CPU_AND(destset, srcset1, srcset2) CpuAnd((destset), (srcset1), (srcset2))
/// CPU_AND replacement
static void CpuAnd(cpu_set_t *destset, const cpu_set_t *srcset1, const cpu_set_t *srcset2)
{
    int i = 0;
    for (i = 0; i < CPU_SETSIZE; ++i) {
        if (CPU_ISSET(i, srcset1) && CPU_ISSET(i, srcset2))
            CPU_SET(i, destset);
        else
            CPU_CLR(i, destset);
    }
}
#endif /* CPU_AND */

int initAllkids(size_t n)
{
    int i;
    char kid_name[32];

	if(n >= MAX_KID_SUPPORT)
	return -1;
	
    // add Kid records for all n main strands
    for (i = 0; i < n; ++i) {
        snprintf(kid_name, sizeof(kid_name), "(%s-%d)", APP_SHORTNAME, i+1);
        strncpy(AllKids.storage[i].theName, kid_name, MAX_KIDNAM_LEN-1);
		++AllKids.kidcount;
    }

    // if coordination is needed, add a Kid record for Coordinator
    if (i > 1) {
        snprintf(kid_name, sizeof(kid_name), "(squid-coord-%u)", i+1);
		strncpy(AllKids.storage[i].theName, kid_name, MAX_KIDNAM_LEN-1);
		++AllKids.kidcount;
    }

	assert(AllKids.kidcount == NumberOfKids());
	
	return 0;
}

int kidShouldRestart(Kid* kid)
{
	if(kid->isRunning)
	{
		return 0;
	}
	else
	{
		if(kid->pid > 0 && WIFEXITED(kid->status) && WEXITSTATUS(kid->status) == 0)
		{
			return 0;
		}
		else if (kid->badFailures > badFailureLimit)
		{
			return 0;
		}
		else if(shutting_down)
		{
			return 0;
		}
		else if(kid->pid > 0 && WIFSIGNALED(kid->status) &&  WTERMSIG(kid->status) == SIGKILL)
		{
			return 0;
		}
		else if(kid->pid > 0 && WIFSIGNALED(kid->status) &&  WTERMSIG(kid->status) == SIGINT)
		{
			return 0;
		}
		else if(kid->pid > 0 && WIFSIGNALED(kid->status) &&  WTERMSIG(kid->status) == SIGTERM)
		{
			return 0;
		}
	}

	return 1;
}

int kidStart(Kid* kid, pid_t cpid)
{
	if(kid->isRunning)
	{
		return -1;
	}
	if(cpid <= 0)
	{
		return -1;
	}

	kid->isRunning = 1; 
	kid->pid = cpid;
	time(&kid->startTime);
	return 0;
}


Kid* kidFind(pid_t pid)
{
    size_t i;
    if(pid < 0)
	{
		return NULL;
    }
    if(AllKids.kidcount <= 0)
    {
		return NULL;
    }	
    for (i = 0; i < AllKids.kidcount; ++i) {
        if (AllKids.storage[i].pid == pid)
            return &AllKids.storage[i];
    }
    return NULL;
}

void stopKid(Kid* kid, int exitStatus)
{
	if(!kid->isRunning)
	{
		return ;
	}
	if(kid->startTime == 0)
	{
		return ;
	}	
	kid->isRunning = 0;
    time_t stop_time;
    time(&stop_time);
    if ((stop_time - kid->startTime) < fastFailureTimeLimit)
        kid->badFailures++;
	else
		kid->badFailures = 0;

	kid->status = exitStatus;
	
}

int kidExitStatus(Kid* kid)
{
	 return WEXITSTATUS(kid->status);
}

int isKidCalledExit(Kid* kid)
{
	return (kid->pid > 0) && !kid->isRunning && WIFEXITED(kid->status);
}

int isKidSignaled(Kid* kid)
{

	 return (kid->pid > 0) && !kid->isRunning && WIFSIGNALED(kid->status);
}

int isKidHopeless(Kid* kid)
{
	 return kid->badFailures > badFailureLimit;
}

int kidTermSignal(Kid* kid)
{
	return WTERMSIG(kid->status);
}

int kidSignaledWithSig(Kid* kid, int sgnl)
{
	 return isKidSignaled(kid) && (kidTermSignal(kid) == sgnl);
}

int isKidsSomeRunning()
{
    size_t i;
    for (i = 0; i < AllKids.kidcount; ++i) {
        if (AllKids.storage[i].isRunning)
            return 1; 
    }
    return 0;
}

int isKidsShouldRestartSome()
{
    size_t i ;
    for (i = 0; i < AllKids.kidcount; ++i) {
        if (kidShouldRestart(&AllKids.storage[i]))
            return 1;
    }
    return 0;

}

int isKidsAllHopeless()
{
    size_t i;
    for (i = 0; i < AllKids.kidcount; ++i) {
        if (!isKidHopeless(&AllKids.storage[i]))
            return 0;
    }
    return 1;
}

int kidsSomeSignaledWithSig(const int sgnl)
{
    size_t i;
    for (i = 0; i < AllKids.kidcount; ++i) {
        if (kidSignaledWithSig(&AllKids.storage[i], sgnl))
            return 1;
    }
    return 0;
}

int kidsSomeRunning()
{
    size_t i;
    for (i = 0; i < AllKids.kidcount; ++i) {
        if (AllKids.storage[i].isRunning)
            return 1;
    }
    return 0;
}

int getMaxProcesses()
{
	int max = 0;
	int i;
	for(i = 0;i<Config.cpuAffinityMap->processessize;i++)
	{
		if(Config.cpuAffinityMap->theProcesses[i] > max)
			max = Config.cpuAffinityMap->theProcesses[i];
	}
	return max;
}

void CpuAffinitySetSet(CPUAffinitySet* set, const cpu_set_t* aCpuSet)
{
	memcpy(&set->theCpuSet, aCpuSet, sizeof(set->theCpuSet));
}

void CpuAffinityCheck()
{
    if (Config.cpuAffinityMap) {
        assert(Config.cpuAffinityMap->processessize > 0);
		
        const int maxProcess = getMaxProcesses();
            
        // in no-deamon mode, there is one process regardless of squid.conf
        const int numberOfProcesses = InDaemonMode() ? NumberOfKids() : 1;

        if (maxProcess > numberOfProcesses) {
            debugs(54, 0, "WARNING: 'cpu_affinity_map' has "
                   "non-existing process number(s)");
        }
    }
}

int
CpuAffinitySetApplied(CPUAffinitySet* ct)
{
    // NOTE: cannot be const.
    // According to CPU_SET(3) and, apparently, on some systems (e.g.,
    // OpenSuSE 10.3) CPU_COUNT macro expects a non-const argument.
    return (CPU_COUNT(&ct->theOrigCpuSet) > 0);
}

void
CpuAffinitySetApply(CPUAffinitySet* ct)
{
    assert(CPU_COUNT(&ct->theCpuSet) > 0); // CPU affinity mask set
    
    assert(!CpuAffinitySetApplied(ct));

    int success = 0;
    if (sched_getaffinity(0, sizeof(ct->theOrigCpuSet), &ct->theOrigCpuSet)) {
        debugs(54, 0, "ERROR: failed to get CPU affinity for process PID %d, ignoring CPU affinity for this process: %s", getpid(), xstrerror());
    } else {
        cpu_set_t cpuSet;
        memcpy(&cpuSet, &ct->theCpuSet, sizeof(cpuSet));
        (void) CPU_AND(&cpuSet, &cpuSet, &ct->theOrigCpuSet);
        if (CPU_COUNT(&cpuSet) <= 0) {
            debugs(54, 0, "ERROR: invalid CPU affinity for process PID %d, "
				"may be caused by an invalid core in 'cpu_affinity_map' or by external affinity restrictions", getpid());
        } else if (sched_setaffinity(0, sizeof(cpuSet), &cpuSet)) {
            debugs(54, 0, "ERROR: failed to set CPU affinity for process PID %d : %s",getpid(), xstrerror());
        } else
            success = 1;
    }
    if (!success)
        CPU_ZERO(&ct->theOrigCpuSet);
}

void CpuAffinitySetUndo(CPUAffinitySet* cpuset)
{
    if (CpuAffinitySetApplied(cpuset)) {
        if (sched_setaffinity(0, sizeof(cpuset->theOrigCpuSet), &cpuset->theOrigCpuSet)) {
            debugs(54, 0, "ERROR: failed to restore original CPU affinity for process PID %d : %s", getpid(), xstrerror());
        }
        CPU_ZERO(&cpuset->theOrigCpuSet);
    }
}


int cpuAffinityMapAdd(CPUAffinityMap* cpumap, int arrprocess[], int arrcores[], int processsize, int coressize)
{
    if (!cpumap || processsize != coressize)
        return 0;
	
	size_t i = 0;

	cpumap->processessize = processsize;
	
    for (i = 0; i < processsize; ++i) {
		
        int process = arrprocess[i];
		
        int core = arrcores[i];
		
        if (process <= 0 || core <= 0)
            return 0;

        cpumap->theProcesses[i] = process;
		
        cpumap->theCores[i] = core;
    }
	return 1;
}


CPUAffinitySet* CpuAffinityMapCalculateSet(const CPUAffinityMap* cpumap, const int targetProcess)
{
    int core = 0;
	size_t i = 0;
    for (; i < cpumap->processessize; ++i) {
        const int process = cpumap->theProcesses[i];
        if (process == targetProcess) {
            if (core > 0) {
                debugs(54, 0, "WARNING: conflicting 'cpu_affinity_map' for process number %d, using the last core seen: %d", process, cpumap->theCores[i]);
            }
            core =  cpumap->theCores[i];
        }
    }
    CPUAffinitySet *cpuAffinitySet = NULL;
    if (core > 0) {
        cpuAffinitySet = xcalloc(1, sizeof(CPUAffinitySet));
        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(core - 1, &cpuSet);
        CpuAffinitySetSet(cpuAffinitySet, &cpuSet);
    }
    return cpuAffinitySet;
}

void
CpuAffinityPrint()
{
	int i,num;
	cpu_set_t get;
	num = sysconf(_SC_NPROCESSORS_CONF);  
	int len = 0;
    if (Config.cpuAffinityMap) {
		char tmpstr[1024]= {0};
		
		len = snprintf(tmpstr, 1024, "processes=");
		
        for (i = 0; i < Config.cpuAffinityMap->processessize; ++i) {
			len += snprintf(tmpstr + len, 1024-len, "%s%d", (i ? "," : ""), Config.cpuAffinityMap->theProcesses[i]);
        }
		
		len += snprintf(tmpstr+len, 1024-len, " cores=");
		
        for (i = 0; i < Config.cpuAffinityMap->processessize; ++i) {
            len += snprintf(tmpstr + len, 1024-len, "%s%d", (i ? "," : ""), Config.cpuAffinityMap->theCores[i]);
        }
		
		debugs(54, DBG_IMPORTANT,"this squid cpuAffinityMap %s", tmpstr);
    }
	
	CPU_ZERO(&get);  
	if(sched_getaffinity(0, sizeof(get), &get) != -1)
	{  
		for (i = 0; i < num; i++)  
		{  
			if(CPU_ISSET(i, &get))
			{	
				debugs(54, DBG_IMPORTANT,"this squid is running on processor %d",i);
			}
		}	    
	}
	else
	{
		debugs(54, 0,"CpuAffinityPrint Failed");
	}	
}

void
CpuAffinityInit()
{
    assert(!TheCpuAffinitySet);
    if (Config.cpuAffinityMap) {
		
		debugs(54, DBG_IMPORTANT,"apply squid cpuAffinityMap");
        const int processNumber = InDaemonMode() ? KidIdentifier : 1;
        TheCpuAffinitySet = CpuAffinityMapCalculateSet(Config.cpuAffinityMap, processNumber);
        if (TheCpuAffinitySet)
            CpuAffinitySetApply(TheCpuAffinitySet);
    }
}

void
CpuAffinityReconfigure()
{
    if (TheCpuAffinitySet) {
        CpuAffinitySetUndo(TheCpuAffinitySet);
        xfree(TheCpuAffinitySet);
        TheCpuAffinitySet = NULL;
    }
    CpuAffinityInit();
}

/// computes name and ID for the current kid process
void 
ConfigureCurrentKid(const char *processName)
{
	// kids are marked with parenthesis around their process names
	if (processName && processName[0] == '(') {
		const char *idStart;
		if ((idStart = strrchr(processName, '-'))) {
			KidIdentifier = atoi(idStart + 1);
			
			const size_t nameLen = idStart - (processName + 1);
			assert(nameLen < sizeof(TheKidName));
			xstrncpy(TheKidName, processName + 1, nameLen + 1);
			
			if (!strcmp(TheKidName, "squid-coord"))
				TheProcessKind = pkCoordinator;
			else if (!strcmp(TheKidName, "squid"))
				TheProcessKind = pkWorker;
			else if (!strcmp(TheKidName, "squid-disk"))
				TheProcessKind = pkDisker;
			else
				TheProcessKind = pkOther; // including coordinator
		}
	} else {
		xstrncpy(TheKidName, APP_SHORTNAME, sizeof(TheKidName));
		KidIdentifier = 0;
	}
}

int
IamMasterProcess()
{
    return KidIdentifier == 0;
}

int
IamWorkerProcess()
{
    // when there is only one process, it has to be the worker
    if (opt_no_daemon || Config.workers == 0)
        return 1;

    return TheProcessKind == pkWorker;
}

int
IamDiskProcess()
{
    return TheProcessKind == pkDisker;
}

int
InDaemonMode()
{
    return !opt_no_daemon && Config.workers > 0;
}


int
NumberOfKids()
{
    // no kids in no-daemon mode
    if (!InDaemonMode())
        return 0;
 
    const int needCoord = Config.workers > 1 ;
    return (needCoord ? 1 : 0) + Config.workers;
}


int
UsingSmp()
{
    return InDaemonMode() && NumberOfKids() > 1;
}


int
IamCoordinatorProcess()
{
    return TheProcessKind == pkCoordinator;
}

int
IamPrimaryProcess()
{
    // when there is only one process, it has to be primary
    if (opt_no_daemon || Config.workers == 0)
        return 1;

    // when there is a master and worker process, the master delegates
    // primary functions to its only kid
    if (NumberOfKids() == 1)
        return IamWorkerProcess();

    // in SMP mode, multiple kids delegate primary functions to the coordinator
    return IamCoordinatorProcess();
}


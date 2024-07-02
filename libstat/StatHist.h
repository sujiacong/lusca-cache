#ifndef	__LIBSTAT_STATHIST_H__
#define	__LIBSTAT_STATHIST_H__

typedef double hbase_f(double);

/*
 * "very generic" histogram;
 * see important comments on hbase_f restrictions in StatHist.c
 */
struct _StatHist {
    int *bins;
    int capacity;
    double min;
    double max;
    double scale;
    hbase_f *val_in;            /* e.g., log() for log-based histogram */
    hbase_f *val_out;           /* e.g., exp() for log based histogram */
};
typedef struct _StatHist StatHist;

extern void statHistClean(StatHist * H);
extern void statHistCount(StatHist * H, double val);
extern void statHistCopy(StatHist * Dest, const StatHist * Orig);
extern void statHistSafeCopy(StatHist * Dest, const StatHist * Orig);
extern double statHistDeltaMedian(const StatHist * A, const StatHist * B);
extern void statHistLogInit(StatHist * H, int capacity, double min, double max);
extern void statHistEnumInit(StatHist * H, int last_enum);
extern void statHistIntInit(StatHist * H, int n);
extern double statHistVal(const StatHist * H, int bin);



#endif

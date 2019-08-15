/* GT-ID : KKIM651 	*/
/* vcpu_scheduler.c */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <limits.h>
#include <libvirt/libvirt.h>

static const int MAX_THRESHOLD = 2147483647;

struct ActiveDomains {
	virDomainPtr *domainsList;
	int numOfActiveDomains;
};

struct ActiveDomainStats {
	virDomainPtr actDomain;
	double *dom_usage;
	int *dom_usage_round;
	unsigned long long int *vcpu_time;
	int vCPUCnt;
	int f1;
	int f2;
};

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  vcpu_scheduler [time interval]\n"                                          \

int endsWith(const char * , const char * );
struct ActiveDomains getActiveDomains(virConnectPtr );
virDomainStatsRecordPtr *collectvCPUDomainsStats(struct ActiveDomains );
int compare (const void *c1, const void *c2);
int compareD (const void *c1, const void *c2);

int main(int argc, char *argv[])
{
	/* Input argument check : Time interval */
	if (argc < 2) {
		fprintf(stderr, "%s", USAGE);
		exit(1);
	}

    virConnectPtr conn;
    char *uri;

    conn = virConnectOpen("qemu:///system");
    if (NULL == conn) {
        fprintf(stderr, "[Error]: Failed to open connection to qemu:///system.\n");
        return 1;
    }

    uri = virConnectGetURI(conn);
    if (!uri) {
        fprintf(stdout, "[Error]: Unable to get URI for local hypervisor connection.\n");
        fflush(stdout);
    	//goto release;
    }

    /* Collect VCPU statistics */
    int numberOfCPUs, c = 0, init = 1;
    struct ActiveDomains listOfDomains;
    struct ActiveDomainStats *actDomainUsageStat, *oldDomainUsageStat;
    virDomainStatsRecordPtr *vCPuDomainsStat = NULL;
    numberOfCPUs = virNodeGetCPUMap(conn, NULL, NULL, 0);
    //fprintf(stdout, "Number of CPUs on the host: %i\n", numberOfCPUs);
    //fflush(stdout);
    while (1)
    {
    	/* Get all active running virtual machines within "qemu:///system" */
    	listOfDomains = getActiveDomains(conn);
    	if (listOfDomains.numOfActiveDomains <= 0)
    	{
    		goto release;
    	}

	 	vCPuDomainsStat = collectvCPUDomainsStats(listOfDomains);

		actDomainUsageStat = (struct ActiveDomainStats *) calloc(listOfDomains.numOfActiveDomains,
			sizeof(struct ActiveDomainStats));

		int vcpu_value = 0;
		unsigned long long int *new_vcpu_time;
		virDomainStatsRecordPtr *ds;
		for (ds=vCPuDomainsStat; *ds; ds++)
		{
			for (int k = 0; k < (*ds)->nparams; k++)
			{
				if (endsWith((*ds)->params[k].field, ".time"))
				{
					vcpu_value = atoi(&(*ds)->params[k].field[strlen((*ds)->params[k].field) - 6]);
					new_vcpu_time = (unsigned long long int *) calloc(1, sizeof(unsigned long long int));
					if (NULL == new_vcpu_time)
					{
						fprintf(stderr, "[Error]: Calloc failed on new_vcpu_time.\n");
						exit(1);
					}
					new_vcpu_time[vcpu_value] = (*ds)->params[k].value.ul;
				}
			}
			actDomainUsageStat[c].actDomain = (*ds)->dom;
			actDomainUsageStat[c].vcpu_time = new_vcpu_time;
			c++;
		}

		if (init) {
			oldDomainUsageStat = (struct ActiveDomainStats *)
				calloc(listOfDomains.numOfActiveDomains, sizeof(struct ActiveDomainStats));
			memcpy(oldDomainUsageStat, actDomainUsageStat,
			       listOfDomains.numOfActiveDomains * sizeof(struct ActiveDomainStats));
		}

		unsigned long long oldnewUsageDiff;
		virVcpuInfoPtr info;
		size_t maplen;
		unsigned char *cpumaps;
		double *host_pCpu_Util;
		long nacs = 1000000000;

		for (int i = 0; i < listOfDomains.numOfActiveDomains; i++) {
			actDomainUsageStat[i].dom_usage = calloc(1, sizeof(double));
			oldnewUsageDiff = actDomainUsageStat[i].vcpu_time[0] - oldDomainUsageStat[i].vcpu_time[0];
			actDomainUsageStat[i].dom_usage[0] = ( 100 * ((double) oldnewUsageDiff / (double) (atoi(argv[1]) * nacs)) );
		}

		oldnewUsageDiff = 0;
		memcpy(oldDomainUsageStat, actDomainUsageStat,
			listOfDomains.numOfActiveDomains * sizeof(struct ActiveDomainStats));

		host_pCpu_Util = calloc(numberOfCPUs, sizeof(double));

		for (int i = 0; i < listOfDomains.numOfActiveDomains; i++) {
			info = calloc(1, sizeof(virVcpuInfo));
			maplen = VIR_CPU_MAPLEN(numberOfCPUs);
			cpumaps = calloc(1, maplen);
			virDomainGetVcpus(actDomainUsageStat[i].actDomain, info, 1, cpumaps, maplen);
			host_pCpu_Util[info[0].cpu] += actDomainUsageStat[i].dom_usage[0];
		}

		qsort ((void *)host_pCpu_Util, numberOfCPUs, sizeof(double), compareD);

		int i, j , unBalanced;
		double highest_pCpu = host_pCpu_Util[numberOfCPUs-1], lowest_pCPu = host_pCpu_Util[0];
		double allowance = 4.0;

		if ((highest_pCpu - lowest_pCPu) > allowance) {
			//fprintf(stdout, "[Info]: Unbalanced.\n");
			//fflush(stdout);
			unBalanced = 1;
		}
		else {
			//fprintf(stdout, "[Info]: Balanced.\n");
			//fflush(stdout);
			unBalanced = 0;
		}

		if (unBalanced)
		{
			virVcpuInfoPtr info;
			unsigned char *cpumaps;
			size_t maplen;
			double temp;
			int nsize, nbins, allTot, binsAvg;
		    int moE, amoE, targetBinNum, *bins;
		    int domArray[listOfDomains.numOfActiveDomains];
			for (int i = 0; i < listOfDomains.numOfActiveDomains; i++) {
				actDomainUsageStat[i].dom_usage_round = calloc(1, sizeof(int));
				temp = actDomainUsageStat[i].dom_usage[0];
				actDomainUsageStat[i].dom_usage_round[0] = (int) round(temp);
				//printf("Domain: %s, DUsage: %f, Usage: %i\n",
				//	virDomainGetName(actDomainUsageStat[i].actDomain),
				//	actDomainUsageStat[i].dom_usage[0],
				//	actDomainUsageStat[i].dom_usage_round[0]);
			}

		    for (i = 0; i < listOfDomains.numOfActiveDomains; i++) {
		     	domArray[i] = actDomainUsageStat[i].dom_usage_round[0];
		    }

		    for (i = 0; i < listOfDomains.numOfActiveDomains; i++) {
		        actDomainUsageStat[i].f1 = 0;
		        actDomainUsageStat[i].f2 = 0;
		    }

		    nbins = numberOfCPUs;

		    nsize = sizeof(domArray)/sizeof(domArray[0]);

		    qsort (domArray, nsize, sizeof(domArray[0]), compare);

		    bins = (int *)malloc(nbins * sizeof(int));
		    for (i=0; i < nbins; i++)
		        bins[i] = 0;

		    allTot = 0;
		    for (i=0; i < nsize; i++)
		        allTot += domArray[i];

		    binsAvg = allTot;

		    for (i=0; i < nbins; i++)
		        bins[i] += domArray[nsize - nbins + i];

		    unsigned char map;
		    int map_counter = 0;
		    for (i = 0; i < nbins; i++)
		    {
	            info = calloc(1, sizeof(virVcpuInfo));
                maplen = VIR_CPU_MAPLEN(numberOfCPUs);
                cpumaps = calloc(1, maplen);
                virDomainGetVcpus(actDomainUsageStat[i].actDomain, info, 1, cpumaps, maplen);
                for (j = 0; j < listOfDomains.numOfActiveDomains; j++) {
	                if (bins[i] == actDomainUsageStat[j].dom_usage_round[0] &&
	                	actDomainUsageStat[j].f1 != 1)
			        {
			        	map = 0x1 << map_counter;
			            map_counter++;
			            actDomainUsageStat[j].f1 = 1;
			            virDomainPinVcpu(actDomainUsageStat[j].actDomain, 0, &map, maplen);
			            break;
			        }
                }
                free(info);
                free(cpumaps);
            }

			nsize -= nbins;

		    int fnum;
		    for (i = nsize-1; i >= 0; i--)
		    {
		        amoE = MAX_THRESHOLD;
		        targetBinNum = 0;
		        for (j=0; j < nbins; j++)
		        {
		            moE = (bins[j] + domArray[i]) - binsAvg;
		            if (moE < amoE)
		            {
		                targetBinNum = j;
		                amoE = moE;
		            }
		        }
		        fnum = domArray[i];
		        info = calloc(1, sizeof(virVcpuInfo));
		        maplen = VIR_CPU_MAPLEN(numberOfCPUs);
		        cpumaps = calloc(1, maplen);
		        for (j = 0; j < listOfDomains.numOfActiveDomains; j++)
		        {
		        	if (fnum == actDomainUsageStat[j].dom_usage_round[0] &&
		            	actDomainUsageStat[j].f2 != 1)
		            {
		                map = 0x1 << targetBinNum;
		                virDomainGetVcpus(actDomainUsageStat[i].actDomain, info, 1, &map, maplen);
		                actDomainUsageStat[j].f2 = 1;
		                break;
		            }
		        }
		        bins[targetBinNum] += domArray[i];
		    }
		} // outermost if unbalanced

		free(host_pCpu_Util);
		c  = 0;
		init = 0;
		free(actDomainUsageStat);
		virDomainStatsRecordListFree(vCPuDomainsStat);
		sleep(atoi(argv[1]));

    } // top-most while loop

    release:
	    virConnectClose(conn);
	    return 0;
} // end of MAIN

int compare (const void *c1, const void *c2)
{
    return *((int *)c1) - *((int *)c2);
}

int compareD (const void *c1, const void *c2)
{
    return *((double *)c1) - *((double *)c2);
}

int endsWith(const char * str1, const char * ends)
{
  int str1len = strlen(str1);
  int endslen = strlen(ends);

  if ( (str1len >= endslen) && (0 == strcmp(str1 + (str1len - endslen), ends)) )
  	return 1;
  else
  	return 0;
}

/* Get all active running virtual machines within "qemu:///system" */
struct ActiveDomains getActiveDomains(virConnectPtr conn)
{
	virDomainPtr *domainsObj;
	unsigned int flags = VIR_CONNECT_LIST_DOMAINS_ACTIVE | VIR_CONNECT_LIST_DOMAINS_RUNNING;
	int allDomains;
	allDomains = virConnectListAllDomains(conn, &domainsObj, flags);
	if (allDomains < 1) {
		fprintf(stdout, "[Info]: No Domains found in the Node.\n");
		fflush(stdout);
		exit(0);
	}
	struct ActiveDomains *listOfDomains = malloc(sizeof(struct ActiveDomains));
	listOfDomains->numOfActiveDomains = allDomains;
	listOfDomains->domainsList = domainsObj;

	return *listOfDomains;
}

/* Collect VCPU statistics */
virDomainStatsRecordPtr *collectvCPUDomainsStats(struct ActiveDomains listOfDomains)
{
	//unsigned int statType = VIR_DOMAIN_STATS_VCPU;
	unsigned int statType = 0;
	virDomainStatsRecordPtr *retvCPUStats = NULL;

	statType = VIR_DOMAIN_STATS_VCPU;

	if (virDomainListGetStats(listOfDomains.domainsList, statType, &retvCPUStats, 0) < 0) {
		fprintf(stderr, "[Error]: Unable to get domain vCPU stat.\n");
		exit(1);
	}

	return retvCPUStats;
}

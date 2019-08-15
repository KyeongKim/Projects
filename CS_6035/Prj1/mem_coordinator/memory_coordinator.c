/* GT-ID : KKIM651 		*/
/* memory_coordinator.c */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libvirt/libvirt.h>

static const int INFLATE_THRESHOLD = 150 * 1024;
static const int DEFLATE_THRESHOLD = 350 * 1024;
static const int INFLATE_AMT = 300 * 1024;
static const int DEFLATE_AMT = 100 * 1024;
static const int NODE_MEM_THRESHOLD = 80;
static const int NODE_BUFF_THRESHOLD = 17;

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  memory_coordinator [time interval]\n"                                      \

struct ActiveDomainMemInfo{
	virDomainPtr domain;
	unsigned long memory;
	unsigned long unused_memory;
	int stat; // 0: Inflate, 1: Deflate, 2: Inactive, 3: Active
};

struct ActiveDomains {
	virDomainPtr *domainsList;
	int numOfActiveDomains;
};

struct ActiveDomains getActiveDomains(virConnectPtr);
struct ActiveDomainMemInfo *getDomMemInfo(virConnectPtr conn, struct ActiveDomains actDom, unsigned long *);
long long *GetNodeFreeMem(virConnectPtr);

int main(int argc, char *argv[])
{
	/* Input argument check : Time interval */
	if (argc < 2) {
		fprintf(stderr, "%s", USAGE);
		exit(1);
	}

	/* Connect to the local hypervisor - "qemu:///system" */
    virConnectPtr conn;
    char *uri;

    conn = virConnectOpen("qemu:///system");
    if (conn == NULL) {
        fprintf(stderr, "[Error]: Failed to open connection to qemu:///system.\n");
        return 1;
    }

    uri = virConnectGetURI(conn);
    if (!uri) {
        fprintf(stderr, "[Error]: Unable to get URI for local hypervisor connection.\n");
    	goto release;
    }

    struct ActiveDomains listOfDomains;
    int memStat;
    int isAnyVMInflated = 0;
    int isAnyVMLevied = 0;
    unsigned long *prior_unused_mem = NULL;
    while (1)
    {
    	struct ActiveDomainMemInfo *vDomainInfo;
    	/* Get all active running virtual machines within "qemu:///system" */
    	listOfDomains = getActiveDomains(conn);
    	if (listOfDomains.numOfActiveDomains <= 0)
    	{
    		goto release;
    	}

    	isAnyVMInflated = 0;
    	vDomainInfo = getDomMemInfo(conn, listOfDomains, prior_unused_mem);
    	if (prior_unused_mem != NULL)
    		free(prior_unused_mem);
    	prior_unused_mem = malloc(sizeof(unsigned long) * listOfDomains.numOfActiveDomains);
    	for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
    	{
    		prior_unused_mem[i] = vDomainInfo[i].unused_memory;
    	}
    	unsigned long total_freed_mem = 0;
    	for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
    	{
    		if (vDomainInfo[i].stat == 1)
    		{
    			memStat = virDomainSetMemory(vDomainInfo[i].domain,
    				(vDomainInfo[i].memory - DEFLATE_AMT));
    			if (memStat < 0) {
    				fprintf(stderr, "[Error]: Unable to Deflate balloon memory.\n");
    				break;
    			}
    			vDomainInfo[i].memory = (vDomainInfo[i].memory - DEFLATE_AMT);
    			//fprintf(stdout, "[Info - Deflate]: Deflate memory from %s\n",
    			//	virDomainGetName(vDomainInfo[i].domain));
    			//fflush(stdout);
    			total_freed_mem += (vDomainInfo[i].memory / 2);
    		}
    	}

    	long long *nodeMonStat;
    	long long nodeFree;
    	long long nodeBuff;
    	if (total_freed_mem > 0)
    	{
    		//fprintf(stdout, "[Info]: Total freed memory > 0 found - %lu\n", total_freed_mem);
		    //fflush(stdout);
    		for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
    		{
	    		if (vDomainInfo[i].stat == 0)
	    		{
	    			nodeMonStat = GetNodeFreeMem(conn);
	    			nodeFree = nodeMonStat[0];
	    			nodeBuff = nodeMonStat[1];

		    		//fprintf(stdout, "[Info - Inflate]: Node Free Buffer (%lld) MB, Node Buffer (%lld)\n", nodeFree,
			   		//	nodeBuff);
		    		if ( nodeFree > NODE_MEM_THRESHOLD && nodeBuff > NODE_BUFF_THRESHOLD)
		    		{
		    			if ( (total_freed_mem > 0)  && (total_freed_mem > INFLATE_AMT) )
		    			{
		    				memStat = virDomainSetMemory(vDomainInfo[i].domain,
		    					(vDomainInfo[i].memory + INFLATE_AMT));
		    				if (memStat < 0) {
    							fprintf(stderr, "[Error]: Unable to Inflate balloon memory.\n");
    							break;
		    				}
		    				isAnyVMInflated++;
		    				total_freed_mem = total_freed_mem - INFLATE_AMT;;
		    				//fprintf(stdout, "[Info]: Inflate memory : %s (%zu)\n",
		    				//	virDomainGetName(vDomainInfo[i].domain),
		    				//(vDomainInfo[i].memory + INFLATE_AMT));
		    				//vDomainInfo[i].memory = (vDomainInfo[i].memory + INFLATE_AMT);
		    				//fflush(stdout);
		    			}
		    			else if ( (total_freed_mem > 0)  && (total_freed_mem < INFLATE_AMT) )
		    			{
		    				memStat = virDomainSetMemory(vDomainInfo[i].domain,
		    					(vDomainInfo[i].memory + total_freed_mem));
		    				if (memStat < 0) {
    							fprintf(stderr, "[Error]: Unable to Inflate balloon memory.\n");
    							break;
		    				}
		    				isAnyVMInflated++;
		    				//fprintf(stdout, "[Info]: Inflate memory : %s (%zu)\n",
		    				//	virDomainGetName(vDomainInfo[i].domain),
		    				//(vDomainInfo[i].memory + total_freed_mem));
		    				//fflush(stdout);
		    				vDomainInfo[i].memory = (vDomainInfo[i].memory + total_freed_mem);
		    				total_freed_mem = 0;
		    			}
		    			else
		    			{
		    				memStat = virDomainSetMemory(vDomainInfo[i].domain,
		    					(vDomainInfo[i].memory + INFLATE_AMT));
		    				if (memStat < 0) {
    							fprintf(stderr, "[Error]: Unable to Inflate balloon memory.\n");
    							break;
		    				}
		    				isAnyVMInflated++;
		    				//fprintf(stdout, "[Info]: Inflate memory : %s (%zu)\n",
		    				//	virDomainGetName(vDomainInfo[i].domain),
		    				//(vDomainInfo[i].memory + INFLATE_AMT));
		    				//fflush(stdout);
		    				vDomainInfo[i].memory = (vDomainInfo[i].memory + INFLATE_AMT);
		    				total_freed_mem = 0;
		    			}
		    		}
	    		}
    		} // for loop
    	} else {
    		for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
    		{
	    		if (vDomainInfo[i].stat == 0)
	    		{
	    			nodeMonStat = GetNodeFreeMem(conn);
	    			nodeFree = nodeMonStat[0];
	    			nodeBuff = nodeMonStat[1];
			   		//fprintf(stdout, "[Info - Inflate]: Node Free Buffer (%lld) MB, Node Buffer (%lld)\n", nodeFree,
			   		//	nodeBuff);
		    		if ( nodeFree > NODE_MEM_THRESHOLD && nodeBuff > NODE_BUFF_THRESHOLD )
			   		{
		    			memStat = virDomainSetMemory(vDomainInfo[i].domain,
		    				(vDomainInfo[i].memory + INFLATE_AMT));
		    			if (memStat < 0) {
    						fprintf(stderr, "[Error]: Unable to Inflate balloon memory.\n");
    						break;
		    			}
		    			//fprintf(stdout, "[Info]: Inflate memory : %s (%zu)\n",
		    			//	virDomainGetName(vDomainInfo[i].domain),
		    			//	(vDomainInfo[i].memory + INFLATE_AMT));
		    			//fflush(stdout);
		    			vDomainInfo[i].memory = (vDomainInfo[i].memory + INFLATE_AMT);
		    			isAnyVMInflated++;
	    			}
	    		}
    		} // for loop
    	} // if

    	// If there are total_freed_mem left here, then, let's see if there are any active domains.
    	// If there are active domains, let's give these resoruces to them.
    	if (total_freed_mem > 0)
    	{
	    	int numOfActiveDomains = 0;
	    	unsigned long memShare;
	    	for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
	    	{
	    		if (vDomainInfo[i].stat == 3)
		    		numOfActiveDomains++;
	    	}
	    	if (numOfActiveDomains > 0)
	    	{
	    		//fprintf(stdout, "[Info]: Dis.freed resources (%lui) MB to (%i) domains.\n",
	    		//	(total_freed_mem / 1024), numOfActiveDomains);
	    		//fflush(stdout);
		    	for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
		    	{
		    		if (vDomainInfo[i].stat == 3)
		    		{
		    			nodeMonStat = GetNodeFreeMem(conn);
		    			nodeFree = nodeMonStat[0];
		    			nodeBuff = nodeMonStat[1];
				   		//fprintf(stdout, "[Info - Inflate]: Node Free Buffer (%lld) MB, Node Buffer (%lld)\n", nodeFree,
				   		//	nodeBuff);
			    		if ( nodeFree > NODE_MEM_THRESHOLD && nodeBuff > NODE_BUFF_THRESHOLD )
				   		{
				   			memShare = total_freed_mem / numOfActiveDomains;
		    				total_freed_mem -= memShare;
		    				//fprintf(stdout, "[Info] Shared mem. resoruce (%lui)\n", memShare);
		    				//fflush(stdout);
				   			memStat = virDomainSetMemory(vDomainInfo[i].domain,
				   				(vDomainInfo[i].memory + memShare));
			    			if (memStat < 0) {
	    						fprintf(stderr, "[Error]: Unable to Inflate balloon memory.\n");
	    						break;
			    			}
			    			//fprintf(stdout, "[Info]: Inflate memory : %s (%zu)\n",
			    			//	virDomainGetName(vDomainInfo[i].domain),
			    			//	(vDomainInfo[i].memory + memShare));
			    			//fflush(stdout);
			    			vDomainInfo[i].memory = (vDomainInfo[i].memory + memShare);
				   		}
		    		}
		    	}
		    }
	    }

    	// If Any VM is inflated, we need to tax those inactive domain(s) to share some resources
    	// Tax levy = 2 MB
    	long long total_mem_tax = 0;
    	if (isAnyVMInflated > 0)
    	{
    		for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
    		{
    			if (vDomainInfo[i].stat == 2)
	    		{
	    			memStat = virDomainSetMemory(vDomainInfo[i].domain,
	    				(vDomainInfo[i].memory - (2 * 1024)));
	    			if (memStat < 0) {
    						fprintf(stderr, "[Error]: Unable to Inflate balloon memory on inactive domain.\n");
    						break;
		    		}
		    		//fprintf(stdout, "[Info]: Deflate memory on Inactive domain : %s (%lu) -> (%lu) MB\n",
		    		//	virDomainGetName(vDomainInfo[i].domain),
		    		//	(vDomainInfo[i].memory) / 1024,
		    		//	((vDomainInfo[i].memory - (2 * 1024)) / 1024));
		    		//fflush(stdout);
		    		total_mem_tax += (2 * 1024);
		    		isAnyVMLevied ++;
	    		}
    		}

    		// If there are any active domains that are inflated and we have inactive domains, levied mem tax
    		// will be shared among active domains, 2 MB each in circular fashion equally.
    		if (isAnyVMLevied > 1)
    		{
	    		for (int i = 0 ; i < listOfDomains.numOfActiveDomains ; i++)
	    		{
	    			if (vDomainInfo[i].stat == 0 && isAnyVMInflated != 0)
		    		{
		    			nodeMonStat = GetNodeFreeMem(conn);
			    		nodeFree = nodeMonStat[0];
			    		nodeBuff = nodeMonStat[1];
				   		//fprintf(stdout, "[Info - Inflate levied tax mem]: Node Free Buffer (%lld) MB, Node Buffer (%lld)\n",
				   		//	nodeFree, nodeBuff);
				   		//fflush(stdout);
				   		if ( nodeFree > NODE_MEM_THRESHOLD && nodeBuff > NODE_BUFF_THRESHOLD )
						{
							memStat = virDomainSetMemory(vDomainInfo[i].domain,
								(vDomainInfo[i].memory + (2 * 1024)));
				   			if (memStat < 0) {
		    					fprintf(stderr, "[Error]: Unable to Inflate balloon memory.\n");
		    					break;
				   			}
				   			//fprintf(stdout, "[Info]: Inflate levied tax memory : %s (%zu)\n",
				   			//	virDomainGetName(vDomainInfo[i].domain),
				   			//	(vDomainInfo[i].memory + (2 * 1024)));
				   			//fflush(stdout);
				   			isAnyVMInflated --;
				  		}
		    		}
	    		}
	    	} // outer if isAnyVMLevied
    	}

    	free(vDomainInfo);
    	isAnyVMInflated = 0;
    	isAnyVMLevied = 0;
    	sleep(atoi(argv[1]));
    } // end of while infinite loop

    release:
	    /* Close the connection */
	    virConnectClose(conn);
	    return 0;


} // end of MAIN

long long *GetNodeFreeMem(virConnectPtr conn)
{
	int nparams = 0;
	long long *nodeMonStat;
	virNodeMemoryStatsPtr nodeStats = malloc(sizeof(virNodeMemoryStats));
	nodeMonStat = malloc(sizeof(long long) * 2);

	if (virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, NULL, &nparams, 0) == 0 && nparams != 0) {
		nodeStats = malloc(sizeof(virNodeMemoryStats) * nparams);
		memset(nodeStats, 0, sizeof(virNodeMemoryStats) * nparams);
		virNodeGetMemoryStats(conn, VIR_NODE_MEMORY_STATS_ALL_CELLS, nodeStats, &nparams, 0);
	}

	for (int i = 0; i < nparams; i++)
	{
		if (strcmp("free", nodeStats[i].field) == 0) {
			nodeMonStat[0] = nodeStats[i].value/1024;
			//fprintf(stdout, "[Info]: %8s : %lld MB\n", nodeStats[i].field, nodeMonStat[0]);
			//fflush(stdout);
		}
		if (strcmp("buffers", nodeStats[i].field) == 0) {
			nodeMonStat[1] = nodeStats[i].value/1024;
			//fprintf(stdout, "[Info]: %8s : %lld MB\n", nodeStats[i].field, nodeMonStat[1]);
			//fflush(stdout);
		}
	}

	return nodeMonStat;
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
} // getActiveDomains

struct ActiveDomainMemInfo *getDomMemInfo(virConnectPtr conn, struct ActiveDomains actDom,
	unsigned long *prior_unused_mem)
{
	struct ActiveDomainMemInfo *domMemList;

	domMemList = malloc(sizeof(struct ActiveDomainMemInfo) * actDom.numOfActiveDomains);
	if (domMemList == NULL)
	{
		fprintf(stderr, "[Error]: Unable to alloate memory to domMemList.\n");
		virConnectClose(conn);
		exit(1);
	}

	for (int i = 0; i < actDom.numOfActiveDomains; i++) {
		virDomainMemoryStatStruct memstats[VIR_DOMAIN_MEMORY_STAT_NR];
		unsigned int flags = VIR_DOMAIN_AFFECT_CURRENT;
		unsigned int num_stats;

		virDomainSetMemoryStatsPeriod(actDom.domainsList[i], 1, flags);

		if (virDomainSetMemoryStatsPeriod(actDom.domainsList[i], 1, VIR_DOMAIN_AFFECT_CURRENT) < 0)
		{
			fprintf(stderr, "[Error]: Unable to enable balloon driver.\n");
			virConnectClose(conn);
			exit(1);
		}

		num_stats = virDomainMemoryStats(actDom.domainsList[i],
						memstats,
						VIR_DOMAIN_MEMORY_STAT_NR,
						0);

		if (num_stats < 0)
		{
			fprintf(stderr, "[Error]: Unable to collect domain memory statistics.\n");
			virConnectClose(conn);
			exit(1);
		}

		unsigned long actualMem;
		unsigned long unusedMem;
		//unsigned long availableMem;

		for (int j = 0; j < num_stats; j++) {
			if (memstats[j].tag == VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON)
				actualMem = memstats[j].val;
			if (memstats[j].tag == VIR_DOMAIN_MEMORY_STAT_UNUSED)
				unusedMem = memstats[j].val;
			//if (memstats[j].tag == VIR_DOMAIN_MEMORY_STAT_AVAILABLE)
			//	availableMem = memstats[j].val;
		}

		if (prior_unused_mem != NULL)
		{
			//fprintf(stdout,"[Info]: %s Total (%lu) MB, Unused (%lu) MB, Available (%lu) MB, Prior Un-Used (%lu) MB\n",
			//	virDomainGetName(actDom.domainsList[i]),
	 		//	actualMem/1024,
			//	unusedMem/1024,
			//	availableMem/1024,
			//	prior_unused_mem[i]/1024);
		}
		else
		{
			//fprintf(stdout,"[Info]: %s Total (%lu) MB, Unused (%lu) MB, Available (%lu) MB\n",
			//	virDomainGetName(actDom.domainsList[i]),
	 		//	actualMem/1024,
			//	unusedMem/1024,
			//	availableMem/1024);
		}

		if (unusedMem <= INFLATE_THRESHOLD)
		{
			domMemList[i].domain = actDom.domainsList[i];
			domMemList[i].stat = 0;
			domMemList[i].memory = actualMem;
			domMemList[i].unused_memory = unusedMem;
		} else if (unusedMem >= DEFLATE_THRESHOLD) {
			// For active domain, do not deflate.
			if (prior_unused_mem != NULL)
			{
				if ( (unusedMem/1024) == (prior_unused_mem[i]/1024) ) {
					domMemList[i].domain = actDom.domainsList[i];
					domMemList[i].stat = 1;
					domMemList[i].memory = actualMem;
					domMemList[i].unused_memory = unusedMem;
				}
				else
				{
					domMemList[i].domain = actDom.domainsList[i];
					domMemList[i].stat = 3;
					domMemList[i].memory = actualMem;
					domMemList[i].unused_memory = unusedMem;
				}
			}
			else
			{
				domMemList[i].domain = actDom.domainsList[i];
				domMemList[i].stat = 1;
				domMemList[i].memory = actualMem;
				domMemList[i].unused_memory = unusedMem;
			}
		} else {
			// No change on unused memory = Inactive
			// Change on unused memory = Active
			if (prior_unused_mem != NULL)
			{
				if ( (unusedMem/1024) == (prior_unused_mem[i]/1024) ) {
					domMemList[i].domain = actDom.domainsList[i];
					domMemList[i].stat = 2;
					domMemList[i].memory = actualMem;
					domMemList[i].unused_memory = unusedMem;
				}
				else
				{
					domMemList[i].domain = actDom.domainsList[i];
					domMemList[i].stat = 3;
					domMemList[i].memory = actualMem;
					domMemList[i].unused_memory = unusedMem;
				}
			}
			else
			{
				domMemList[i].domain = actDom.domainsList[i];
				domMemList[i].stat = 3;
				domMemList[i].memory = actualMem;
				domMemList[i].unused_memory = unusedMem;
			}
		}
	} // for loop

	return domMemList;

} // getDomMemInfo

#ifndef __LAGDATA_H
#define __LAGDATA_H


/* querying lag data */

struct PingSummary
{
	int cur, avg, min, max;
	/* only used for QueryCPing: */
	int s2cslowtotal;
	int s2cfasttotal;
	short s2cslowcurrent;
	short s2cfastcurrent;
};

struct PLossSummary
{
	double s2c, c2s, s2cwpn;
};

struct ReliableLagData
{
	/* dups is the total number of duplicates that have been recieved,
	 * c2sn is the reliable seqnum so far (i.e., the number of reliable
	 * packets that should have been recieved, excluding dups). */
	unsigned int reldups, c2sn;
	/* retries is the number of times the server has had to re-send a
	 * reliable packet. s2cn is the number of reliable packets that
	 * should have been sent, excluding retries. */
	unsigned int retries, s2cn;
};


#define I_LAGQUERY "lagquery-1"

typedef struct Ilagquery
{
	INTERFACE_HEAD_DECL

	void (*QueryPPing)(int pid, struct PingSummary *p);
	void (*QueryCPing)(int pid, struct PingSummary *p);
	void (*QueryRPing)(int pid, struct PingSummary *p);

	void (*QueryPLoss)(int pid, struct PLossSummary *d);
	void (*QueryRelLag)(int pid, struct ReliableLagData *d);

	void (*DoPHistogram)(int pid,
			void (*callback)(int pid, int bucket, int count, int maxcount, void *clos),
			void *clos);

	void (*DoRHistogram)(int pid,
			void (*callback)(int pid, int bucket, int count, int maxcount, void *clos),
			void *clos);
} Ilagquery;



/* collecting data */

struct ClientLatencyData
{
	/* all what the client reports */
	unsigned int weaponcount;
	unsigned int s2cslowtotal;
	unsigned int s2cfasttotal;
	unsigned short s2cslowcurrent;
	unsigned short s2cfastcurrent;
	unsigned short unknown1;
	short lastping;
	short averageping;
	short lowestping;
	short highestping;
};

struct ClientPLossData
{
	/* what the server thinks */
	unsigned int s_pktrcvd, s_pktsent;
	/* what the client reports */
	unsigned int c_pktrcvd, c_pktsent;
};


#define I_LAGCOLLECT "lagcollect-1"

typedef struct Ilagcollect
{
	INTERFACE_HEAD_DECL

	void (*Position)(int pid, int ms, unsigned int wpnsent);
	void (*RelDelay)(int pid, int ms);
	void (*ClientLatency)(int pid, struct ClientLatencyData *data);
	void (*ClientPLoss)(int pid, struct ClientPLossData *data);
	void (*RelStats)(int pid, struct ReliableLagData *data);

	void (*Clear)(int pid);

} Ilagcollect;

#endif


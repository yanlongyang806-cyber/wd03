#include "sock.h"
#include "net/net.h"
#include "url.h"
#include "timing.h"
#include "earray.h"
#include "estring.h"
#include "mathutil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

NetComm	*comm;

typedef struct
{
	int		num_monkeys;
} NetTestClient;

#include "net/netprivate.h"
void netTestConnect(NetLink *link,NetTestClient *client)
{
	printf("connect %d\n",eaSize(&link->listen->links));
}

void netTestDisconnect(NetLink *link,NetTestClient *client)
{
	printf("disconnect %d monkeys\n",client ? client->num_monkeys : 0);
}

void netTestMsg(Packet *pkt,int cmd,NetLink *link,NetTestClient *client)
{

	if (cmd == 1)
	{
#if 0
		char	monkey_buf[100],zebra_buf[100],bytes_buf[100];
		int		t,val;
		F32		f;

		t = pktEnd(pkt);
		pktGetString(pkt,monkey_buf,sizeof(monkey_buf));
		val = pktGetBits(pkt,32);
		pktGetString(pkt,zebra_buf,sizeof(zebra_buf));
		f = pktGetF32(pkt);
		pktGetBytes(pkt,5,bytes_buf);
		t = pktEnd(pkt);
		val = pktGetBits(pkt,9);
		t = pktEnd(pkt);
		if (client)
			client->num_monkeys++;
		if (linkIsServer(link))
		{
			pkt = pktCreate(link,2);
			pktSend(&pkt);
			linkFlush(link);
		}
#endif
	}
}

void dataTest(NetLink *link)
{
	Packet	*pkt = pktCreate(link,1);
	pktSendString(pkt,"monkey");
	pktSendBits(pkt,32,1234567);
	pktSendString(pkt,"zebra");
	pktSendF32(pkt,4.2);
	pktSendBytes(pkt,5,"five5");
	pktSendBits(pkt,9,43);
	pktSend(&pkt);
}

void randTest(NetLink *link)
{
	int		i;
	Packet	*pkt = pktCreate(link,1);

	for(i=0;i<100;i++)
		pktSendBits(pkt,32,randInt(999));
	pktSend(&pkt);
}

void netTest(char *ip,int port)
{
	NetLink		*link=0;
	U32			last_pkt_sent=0,last_bytes_sent=0,last_real_bytes=0;
	int			i,timer = timerAlloc(),count=0;
	char		str[10000] = "12 monkeys!\r\n\r\n";
	F32			dt;
	int			server = !port;
	char		buffer[] = "monkey";
	NetListen	*nl;

	comm = commCreate(server * 20,1);
	if (server)
	{
		nl = commListen(comm,LINKTYPE_UNSPEC, 0,8080,netTestMsg,netTestConnect,netTestDisconnect,sizeof(NetTestClient));
		nl = commListen(comm,LINKTYPE_UNSPEC, 0,8081,netTestMsg,netTestConnect,netTestDisconnect,sizeof(NetTestClient));
		for(;;)
			commMonitor(comm);
	}
	else
	{
		link = commConnect(comm,LINKTYPE_UNSPEC, 0,ip,port,netTestMsg,netTestConnect,netTestDisconnect,0);
		linkFlushLimit(link,5000);
		//linkSetMaxSend(link,1000);
		//linkSetTimeout(link,5);
		for(i=0;i<1400;i++)
			str[i] = 'x';
	//	strcat(str,"\r\n\r\n");
		for(;;)
		{
			if (linkConnected(link))
			{
				if (!linkSendBufFull(link))
				{
					for(i=0;i<1;i++)
					{
						linkCompress(link,1);//++count & 1);
						randTest(link);
					}
					//linkFlush(link);
					if ((dt=timerElapsed(timer)) > 1)
					{
						const LinkStats *stats = linkStats(link);
						int packets = stats->send.packets - last_pkt_sent;
						U32 bytes = stats->send.bytes - last_bytes_sent;
						U32 compressed = stats->send.real_bytes - last_real_bytes;

						printf("%.1f msgs/sec   %.1fM B/sec   %.1fM CB/sec   elapsed %f\n",packets/dt,bytes / (dt * 1000000.0),compressed / (dt * 1000000.0),linkRecvTimeElapsed(link));
						timerStart(timer);
						last_pkt_sent = stats->send.packets;
						last_bytes_sent = stats->send.bytes;
						last_real_bytes = stats->send.real_bytes;
					}
				}
				else
					Sleep(1);
			}
			commMonitor(comm);

#if 0
			if (randInt(10000) == 0)
			{
				linkRemove(&link);
				link = commConnect(comm,0,ip,port,netTestMsg,netTestConnect,netTestDisconnect,0);
				linkFlushLimit(link,5000);
			}
#endif
			if (linkDisconnected(link))
			{
				printf("reconnecting..\n");
				free(link);
				link = commConnect(comm,LINKTYPE_UNSPEC, 0,ip,port,netTestMsg,netTestConnect,netTestDisconnect,0);
				linkFlushLimit(link,5000);
				//linkSetTimeout(link,5);
			}
			Sleep(0);
		}
	}
}

AUTO_COMMAND ACMD_NAME(net_client);
void NetClient(char *server,int port)
{
	netTest(server,port);
}

#if 0
void main(int argc,char **argv)
{
	if (argc == 2)
		test("127.0.0.1",atoi(argv[1]));
	else if (argc == 3)
		test(argv[1],atoi(argv[2]));
	else
		test(0,0);
}
#endif

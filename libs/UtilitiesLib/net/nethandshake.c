#include "../../3rdparty/zlib/zlib.h"
#include "sock.h"
#include "net.h"
#include "netprivate.h"
#include "crypt.h"
#include "utils.h"
#include "netlink.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

static void *my_zcalloc(void *opaque,uInt num,uInt size)
{
	return calloc(num,size);
}

static void my_zfree(void *opaque,void *data)
{
	free(data);
}

S32 netCompressLevel = LINK_COMPRESS_LEVEL;
AUTO_CMD_INT(netCompressLevel, netCompressLevel) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

// Use an additional zlib stream to verify the consistency of our send stream.
static bool verifySendZstream = false;
AUTO_CMD_INT(verifySendZstream, netVerifySendZstream) ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(CommandLine) ACMD_CMDLINE ACMD_EARLYCOMMANDLINE ACMD_HIDE;

void linkCompressInit(NetLink *link)
{
	NetCompress	*compress;
	S32 level;

	if (link->compress)
		return;
	compress = link->compress = callocStruct(NetCompress);
	compress->send = callocStruct(z_stream);
	compress->recv = callocStruct(z_stream);
	compress->recv->zalloc = my_zcalloc;
	compress->recv->zfree = my_zfree;
	inflateInit(compress->recv);
	compress->send->zalloc = my_zcalloc;
	compress->send->zfree = my_zfree;
	level = netCompressLevel;
	MINMAX1(level, Z_NO_COMPRESSION, Z_BEST_COMPRESSION);
	deflateInit(compress->send,level);

	// If requested, create an extra zstream for verifying the send zstream.
	if (verifySendZstream)
	{
		compress->send_verify = callocStruct(z_stream);
		compress->send_verify->zalloc = my_zcalloc;
		compress->send_verify->zfree = my_zfree;
		inflateInit(compress->send_verify);
	}
}

static void linkSendKeyPair(Packet *pak,U32 *private_key,U32 *public_key)
{
	char	key_string[1024],encrypt_str[1200];
	int		len;

	cryptMakeKeyPair(private_key,public_key);
	len = (int)escapeDataStatic((char*)public_key, 512/8, key_string, sizeof(key_string), 0);
	key_string[len] = 0;
	sprintf(encrypt_str,"Encrypt \"%s\"\n",key_string);
	pktSendStringRaw(pak,encrypt_str);
}

void linkInitEncryption(NetLink *link,char *their_key_str)
{
	U32			their_public_key[512/32];
	NetEncrypt	*encrypt = link->encrypt;

	unescapeDataStatic(their_key_str,strlen(their_key_str),(char*)their_public_key,sizeof(their_public_key),0);
	cryptMakeSharedSecret(encrypt->shared_secret,encrypt->private_key,their_public_key);
	if (!encrypt->encode)
	{
		encrypt->encode = callocStruct(rc4_key);
		encrypt->decode = callocStruct(rc4_key);
	}
	cryptInitRc4(encrypt->encode,(char*)encrypt->shared_secret,ARRAY_SIZE(encrypt->shared_secret));
	memcpy(encrypt->decode,encrypt->encode,sizeof(*encrypt->decode));
	//cryptInitRc4(encrypt->decode,(char*)encrypt->shared_secret,ARRAY_SIZE(encrypt->shared_secret));
	//printf("key: %08x %08x %08x %08x\n",encrypt->shared_secret[0],encrypt->shared_secret[15]);
}

int netSendConnectPkt(NetLink *link)
{
	Packet	*pak = pktCreateRaw(link);
	char	protocol_str[100];

#if PLATFORM_CONSOLE
	// Client Type Introduction
	char clientTypeString[7];
#if _XBOX
	sprintf(clientTypeString,"CT %d\n", CLIENT_TYPE_XBOX);
#else
	sprintf(clientTypeString,"CT %d\n", CLIENT_TYPE_PS3);
#endif

#endif

	sprintf(protocol_str,"Cryptic %d\n",link->protocol);
	pktSendStringRaw(pak,protocol_str);
	if (link->flags & LINK_PACKET_VERIFY)
		pktSendStringRaw(pak,"PacketVerify 1\n");
	if (link->flags & LINK_COMPRESS)
		pktSendStringRaw(pak,"Compress 1\n");
	if (link->flags & LINK_FORCE_FLUSH)
		pktSendStringRaw(pak,"ForceFlush 1\n");
	if (link->flags & LINK_CRC)
		pktSendStringRaw(pak,"CRC 1\n");
	if (link->flags & LINK_CRAZY_DEBUGGING)
		pktSendStringRaw(pak,"CrazyDebugging 1\n");
	if (link->flags & LINK_ENCRYPT)
	{
		NetEncrypt	*encrypt;

		if (!link->encrypt)
			link->encrypt = callocStruct(NetEncrypt);
		encrypt = link->encrypt;
		linkSendKeyPair(pak,encrypt->private_key,encrypt->public_key);
	}

#if PLATFORM_CONSOLE
	// Send the client type
	pktSendStringRaw(pak, clientTypeString);
#endif

	pktSendStringRaw(pak,"\r\n\r\n");
	return pktSendRaw(&pak);
}

int getConnectionInfo(NetLink* link,char *data,LinkFlags *flags,U32 *protocol,U8 **their_key_str,char **err)
{
	char	*s = data,*args[2];
	int		count,valid=0;
	ClientType clientType = CLIENT_TYPE_PC;

	for(;;)
	{
		if (!s)
			break;
		count = tokenize_line(s,args,&s);
		if (count != 2)
			continue;
		if (stricmp("Cryptic",args[0]) == 0)
		{
			*protocol = atoi(args[1]);
			valid = 1;
		}
		else if (stricmp("PacketVerify",args[0]) == 0)
		{
			if (atoi(args[1])==1)
			{
				*flags |= LINK_PACKET_VERIFY;
			}
		}
		else if (stricmp("Encrypt",args[0]) == 0)
		{
			*flags |= LINK_ENCRYPT;
			*their_key_str = args[1];
		}
		else if (stricmp("Compress",args[0]) == 0)
		{
			*flags |= LINK_COMPRESS;
		}
		else if (stricmp("ForceFlush",args[0]) == 0)
		{
			*flags |= LINK_FORCE_FLUSH;
		}
		else if (stricmp("CRC",args[0]) == 0)
		{
			*flags |= LINK_CRC;
		}
		else if (stricmp("CrazyDebugging",args[0]) == 0)
		{
			*flags |= LINK_CRAZY_DEBUGGING;
		}
		else if (stricmp("Error",args[0]) == 0 && err)
		{
			*err = args[1];
			valid = 0;
		}
		else if (stricmp("CT",args[0]) == 0)
		{
			// Verify the client type
			clientType = atoi(args[1]);
			if (clientType == CLIENT_TYPE_PS3 || clientType == CLIENT_TYPE_XBOX)
			{
				link->eClientType = clientType;
			}			
		}
	}
	return valid;
}

void netFirstClientPacket(NetLink *link,Packet *pak)
{
	LinkFlags	flags=0;
	U32				protocol=0;
	U8				*their_key_str=0;
	char			*err=0;

	if (!getConnectionInfo(link, pak->data,&flags,&protocol,&their_key_str,&err))
	{
		if (err)
			link->error = strdup(err);
		return;
	}
	link->flags = flags;
	if(link->recv_pak){
		link->recv_pak->has_verify_data = !!(link->flags & LINK_PACKET_VERIFY);
	}
	link->protocol = protocol;
	link->connected = 1;
	linkStatus(link,"client connected");
	if (flags & LINK_ENCRYPT)
		linkInitEncryption(link,their_key_str);
	if (flags & LINK_COMPRESS)
		linkCompressInit(link);
}

void netFirstServerPacket(NetLink *link,Packet *pak)
{
	LinkFlags	flags=0;
	U32				protocol=0;
	U8				*their_key_str=0;
	char			*err=0;

	if (!getConnectionInfo(link, pak->data,&flags,&protocol,&their_key_str,&err))
	{
		err = "bad connection header";
		goto fail;
	}

	if ((link->listen->required_flags & LINK_ENCRYPT) && // Required
		!(link->listen->required_flags & LINK_ENCRYPT_OPTIONAL) && // Not optional
		!(flags & LINK_ENCRYPT) // and not supplied by the client
		)
	{
		err = "client must request encrypt"; // Note: the string "request encrypt" is searched for in linkErrorNeedsEncryption()
		goto fail;
	}

	flags &= ~link->listen->unallowed_flags;
	if (link->listen->required_flags & LINK_ENCRYPT_OPTIONAL) {
		flags |= link->listen->required_flags & ~(LINK_ENCRYPT|LINK_ENCRYPT_OPTIONAL);
	} else {
		flags |= link->listen->required_flags;
	}
	link->flags = flags;
	if(link->recv_pak){
		link->recv_pak->has_verify_data = !!(link->flags & LINK_PACKET_VERIFY);
	}
	link->protocol = MIN(protocol,LINK_PROTOCOL_VER);
	netSendConnectPkt(link);
	if (flags & LINK_ENCRYPT)
		linkInitEncryption(link,their_key_str);
	if (flags & LINK_COMPRESS)
		linkCompressInit(link);
	link->connected = 1;
fail:
	if (err)
	{
		char	err_msg[200];
		Packet	*pkt = pktCreateRaw(link);

		sprintf(err_msg,"Error \"%s\"\n\r\n\r\n",err);
		pktSendStringRaw(pkt,err_msg);
		pktSendRaw(&pkt);
		linkFlush(link);
		linkRemove_wReason(&link, "hit fail: in netFirstServerPacket");
	}
}
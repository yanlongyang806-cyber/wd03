#ifndef _NETRECEIVE_H
#define _NETRECEIVE_H

int asyncRead(NetLink *link, int async_bytes_read, PacketCallback *msg_cb);
void commProcessPackets(NetComm *comm, S32 timeoutOverrideMilliseconds);
int linkAsyncReadRaw(NetLink *link, int async_bytes_read, PacketCallback *msg_cb);
void commFlushDisconnects(NetComm *comm);

// If raw_data_left is set to this, all incoming data will be sent raw to the 
// message handler until the link closes.
#define PERPETUALLY_RAW_DATA (-1)

int linkGetRawContentLength(char *pHTTPHeader);
int linkReceiveCore(NetLink *link,int bytes_xferred);

#endif

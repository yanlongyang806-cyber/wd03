#ifndef _WTCMDPACKET_H_
#define _WTCMDPACKET_H_
#pragma once
GCC_SYSTEM

#include "WorkerThread.h"

// TODO  - add WorkerThreadPrivate.h, export in WorkerThread.h, or make an accessor to patch the size
#define OVERRUN_CHECKS_ON 1

// TODO  - add WorkerThreadPrivate.h, export in WorkerThread.h, or make an accessor to patch the size
#define WT_OVERRUN_VALUE 0x4BADBEEF


// TODO  - export in WTCmdPacket.h, or make the accessor to patch the size not inline

typedef struct QueuedCmd
{
	int			type;
	U32			num_blocks;
	U32			size;
	void		*data;
} QueuedCmd;

__forceinline static void *wtGetQueuedCmdData(const QueuedCmd *cmd)
{
	return cmd->data ? cmd->data : cmd + 1;
}

typedef struct WTCmdPacket
{
	WorkerThread	*worker_thread;
	// For patching the length of the command if we overflow the parameter buffer.
	QueuedCmd *	queued_cmd;
	int			packet_type;
	U8 *		head;
	U32			length;
	U8 *		write_pointer;
	U8 *		write_mark;
} WTCmdPacket;

SA_RET_OP_VALID QueuedCmd *wtAllocCmdPkt(WorkerThread *wt, int cmd_type, int size);

__forceinline static U32 wtCmdCurrentLength(SA_PARAM_NN_VALID const WTCmdPacket *command_header)
{
	return command_header->write_pointer - command_header->head;
}

__forceinline static U32 wtCmdMarkSize(SA_PARAM_NN_VALID const WTCmdPacket *command_header)
{
	return command_header->write_pointer - command_header->write_mark;
}

__forceinline static U32 wtCmdPacketSize(SA_PARAM_NN_VALID const WTCmdPacket *command_header)
{
	return command_header->length;
}

__forceinline static U32 wtCmdPacketBytesRemain(SA_PARAM_NN_VALID const WTCmdPacket *command_header)
{
	return command_header->length - wtCmdCurrentLength(command_header);
}

__forceinline static void wtCmdSetupPacket(SA_PRE_NN_FREE SA_POST_NN_VALID WTCmdPacket *command_header, int command, void * data, U32 length)
{
	command_header->worker_thread = NULL;
	command_header->packet_type = command;
	command_header->queued_cmd = NULL;
	command_header->head = data;
	command_header->length = length;
	command_header->write_pointer = data;
	command_header->write_mark = data;
}

__forceinline static int wtCmdPacketOpen(SA_PARAM_NN_VALID WorkerThread *thread, SA_PRE_NN_FREE SA_POST_NN_VALID WTCmdPacket *command_header, int command, U32 expected_data_size)
{
	command_header->worker_thread = thread;
	command_header->packet_type = command;
	command_header->queued_cmd = wtAllocCmdPkt(command_header->worker_thread, command, expected_data_size);
	command_header->head = wtGetQueuedCmdData(command_header->queued_cmd);
	command_header->length = expected_data_size;
	command_header->write_pointer = command_header->head;
	command_header->write_mark = command_header->head;
	return command_header->queued_cmd != 0;
}

// TODO  - add WorkerThreadPrivate.h, export in WorkerThread.h, or make an accessor to patch the size
__forceinline static void wtTruncateQueuedCmd(QueuedCmd *queued_cmd, U32 length)
{
#if OVERRUN_CHECKS_ON
	U8 *data;
 	data = wtGetQueuedCmdData(queued_cmd);
	// test and then move the buffer overrun check
	devassert(length <= queued_cmd->size);
	assertmsg(*(U32*)(data + queued_cmd->size - sizeof(U32)) == WT_OVERRUN_VALUE, "This command overran the CmdQueue");
	*(U32*)(data + length) = WT_OVERRUN_VALUE;
	length += sizeof(U32);
#endif
	queued_cmd->size = length;
}

// TODO  - reenable __forceinline static 
void wtCmdPacketClose(SA_PARAM_NN_VALID WTCmdPacket *command_header);

__forceinline static void wtCmdWriteMark(SA_PARAM_NN_VALID WTCmdPacket *command_header)
{
	command_header->write_mark = command_header->write_pointer;
}

SA_ORET_NN_VALID void * wtCmdWrite(SA_PARAM_NN_VALID WTCmdPacket *command_header, SA_PRE_OP_RBYTES_VAR(data_size) const void *data, U32 data_size);

SA_ORET_NN_VALID void * wtCmdRead(SA_PARAM_NN_VALID WTCmdPacket *command_header, SA_PRE_VALID SA_POST_OP_BYTES_VAR(data_size) void *dest, U32 data_size);

__forceinline static U8 * wtCmdGetWritePointer(SA_PARAM_NN_VALID WTCmdPacket *command_header, int data_size)
{
	// reserve the requested space by writing NULL. this skips the write pointer 
	// past the requested data size.
	return wtCmdWrite(command_header, NULL, data_size);
}

__forceinline static U8 * wtCmdGetWriteMark(SA_PARAM_NN_VALID WTCmdPacket *command_header)
{
	return command_header->write_mark;
}

#define wtCmdWriteStruct(header, struct_expression, type) \
	((type*)wtCmdWrite((header), (struct_expression), sizeof(type)))

#define wtCmdGetWritePointerT(header, type) \
	((type*)wtCmdGetWritePointer((header), sizeof(type)))


#endif

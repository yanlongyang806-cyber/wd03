// Network debugging interface for replaying a NetLink stream internally.

#ifndef CRYPTIC_NETREPLAY_H
#define CRYPTIC_NETREPLAY_H

typedef struct NetReplay NetReplay;

// Initialize a replay object.
// Returns pointer to the first real stream payload byte.
const char *netReplayInit(NetReplay **replay, const char *stream, size_t stream_len);

// Destroy a replay object.
void netReplayDestroy(NetReplay *replay);

// Replay and find the next packet demarcation point.
const char *netReplayNext(NetReplay *replay, const char *stream, size_t stream_len);

#endif  // CRYPTIC_NETREPLAY_H

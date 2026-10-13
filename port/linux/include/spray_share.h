/* Optional fork extension. No game protocol/version or saved-state changes. */
#ifndef SPRAY_SHARE_H
#define SPRAY_SHARE_H
#include <stddef.h>
#include <stdint.h>
#define SPRAY_SHARE_LIMIT (5u * 1024u * 1024u)
#define SPRAY_SHARE_SLOTS 16
#define SPRAY_SHARE_PEERS 128
#define SPRAY_SHARE_PACKET 1152
#define SPRAY_SHARE_TYPE 250
struct spray_pose
{
	float origin[3], direction[3];
	int32_t bsp;
};
struct spray_share;
struct spray_share_callbacks
{
	int (*send)(void *, int, const void *, size_t, int);
	/* The host checks the sender's unit, wall ray and cooldown, and supplies its name. */
	int (*accept)(void *, int, struct spray_pose *, char[32]);
	/* Validate/decode and persist before an image becomes visible or is relayed. */
	int (*ready)(void *, int, int, const void *, size_t, const struct spray_pose *, const char *,
				 int);
	/* Optional persistent lookup. Fill exactly size bytes in caller-owned storage. */
	int (*cached)(void *, const unsigned char[32], size_t, void *);
};
struct spray_share *spray_share_new(int host, uint64_t nonce,
									struct spray_share_callbacks callbacks, void *context);
void spray_share_free(struct spray_share *);
void spray_share_peer(struct spray_share *, int peer, int present);
void spray_share_update(struct spray_share *, uint32_t milliseconds);
void spray_share_receive(struct spray_share *, int peer, const void *, size_t);
int spray_share_publish(struct spray_share *, int owner, const void *, size_t, struct spray_pose,
						const char *);
int spray_share_available(const struct spray_share *);
int spray_share_handles(const void *, size_t);
#endif

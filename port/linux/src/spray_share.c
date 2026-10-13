/* Host-mediated peer sharing over the existing distributed transport.
 * Small reliable offers; windowed datagrams with individual acknowledgements.
 * A global 64 KiB/s budget bounds traffic regardless of player count. */
#include "../include/spray_share.h"
#include "../../third_party/monocypher/monocypher.h"
#include <stdlib.h>
#include <string.h>
#define CHUNK 1024u
#define RECEIVE_BITMAP_BYTES ((SPRAY_SHARE_LIMIT + CHUNK * 8u - 1u) / (CHUNK * 8u))
#define WINDOW 8
#define HEADER 24u
#define OFFER 100u
#define RETRY 400u
#define TIMEOUT 60000u
enum
{
	HELLO = 1,
	WELCOME,
	OFFER_IMAGE,
	DATA,
	ACK,
	DONE
};
struct image
{
	unsigned char *data;
	uint32_t size, id;
	int owner, local;
	unsigned char hash[32];
	struct spray_pose pose;
	char name[32];
};
struct incoming
{
	struct image image;
	unsigned char bits[RECEIVE_BITMAP_BYTES];
	uint32_t count, last;
	int slot;
};
struct flight
{
	uint32_t chunk, at;
	int active;
};
struct peer
{
	int present, capable, sending;
	uint64_t token, hello;
	uint32_t pending, next, sent_at, offer_at, hello_at, completed;
	struct flight window[WINDOW];
	struct incoming receive;
};
struct spray_share
{
	int host, cursor, active_receives, hello_count;
	uint64_t nonce;
	uint32_t now, serial, budget_at, budget;
	struct image images[SPRAY_SHARE_SLOTS];
	struct peer peers[SPRAY_SHARE_PEERS];
	struct spray_share_callbacks cb;
	void *context;
};
static uint32_t get32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(unsigned char *p, uint32_t n)
{
	p[0] = (unsigned char)n;
	p[1] = (unsigned char)(n >> 8);
	p[2] = (unsigned char)(n >> 16);
	p[3] = (unsigned char)(n >> 24);
}
static uint64_t get64(const unsigned char *p)
{
	return get32(p) | (uint64_t)get32(p + 4) << 32;
}
static void put64(unsigned char *p, uint64_t n)
{
	put32(p, (uint32_t)n);
	put32(p + 4, (uint32_t)(n >> 32));
}
int spray_share_handles(const void *data, size_t size)
{
	const unsigned char *p = data;
	return p && size >= HEADER && p[2] == SPRAY_SHARE_TYPE && p[3] == 1 &&
		   !memcmp(p + 8, "HSPR", 4) && p[12] == 1;
}
static int packet(struct spray_share *s, int peer, int kind, uint32_t id, const void *payload,
				  size_t length, int reliable)
{
	unsigned char p[SPRAY_SHARE_PACKET] = {0};
	if (length > sizeof(p) - HEADER)
		return 0;
	p[2] = SPRAY_SHARE_TYPE;
	p[3] = 1;
	memcpy(p + 8, "HSPR", 4);
	p[12] = 1;
	p[13] = (unsigned char)kind;
	put64(p + 16, kind == HELLO ? s->nonce : s->peers[peer].token);
	put32(p + 4, id);
	if (length)
		memcpy(p + HEADER, payload, length);
	return s->cb.send(s->context, peer, p, HEADER + length, reliable);
}
static void cancel_receive(struct spray_share *s, struct peer *p)
{
	if (p->receive.image.data)
	{
		free(p->receive.image.data);
		s->active_receives--;
	}
	memset(&p->receive, 0, sizeof(p->receive));
}
static void peer_clear(struct spray_share *s, int peer)
{
	cancel_receive(s, &s->peers[peer]);
	memset(&s->peers[peer], 0, sizeof(s->peers[peer]));
	s->peers[peer].sending = -1;
}
struct spray_share *spray_share_new(int host, uint64_t nonce, struct spray_share_callbacks cb,
									void *context)
{
	struct spray_share *s = calloc(1, sizeof(*s));
	int i;
	if (!s || !nonce || !cb.send || !cb.accept || !cb.ready)
	{
		free(s);
		return NULL;
	}
	s->host = host;
	s->nonce = nonce;
	s->cb = cb;
	s->context = context;
	s->budget = 8192;
	for (i = 0; i < SPRAY_SHARE_PEERS; i++)
		s->peers[i].sending = -1;
	return s;
}
void spray_share_free(struct spray_share *s)
{
	int i;
	if (!s)
		return;
	for (i = 0; i < SPRAY_SHARE_PEERS; i++)
		cancel_receive(s, &s->peers[i]);
	for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
		free(s->images[i].data);
	free(s);
}
void spray_share_peer(struct spray_share *s, int peer, int present)
{
	if (!s || peer < 0 || peer >= SPRAY_SHARE_PEERS)
		return;
	if (!present)
	{
		peer_clear(s, peer);
		return;
	}
	s->peers[peer].present = 1;
}
static int slot_for(struct spray_share *s, int owner)
{
	int i, slot = 0;
	for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
	{
		if (s->images[i].data && s->images[i].owner == owner)
			return i;
		if (!s->images[i].data)
			return i;
		if (s->images[i].id < s->images[slot].id)
			slot = i;
	}
	return slot;
}
static void distribute(struct spray_share *s, int slot)
{
	int i;
	for (i = 0; i < SPRAY_SHARE_PEERS; i++)
		if (s->peers[i].present && s->peers[i].capable)
		{
			/* An in-flight image replaced at its slot starts again under a new id. */
			if (s->peers[i].sending == slot)
				s->peers[i].sending = -1;
			s->peers[i].pending |= 1u << slot;
		}
}
static int commit(struct spray_share *s, struct image *image, int slot, int local)
{
	int i;
	if (!s->cb.ready(s->context, slot, image->owner, image->data, image->size, &image->pose,
					 image->name, local))
		return 0;
	for (i = 0; i < SPRAY_SHARE_PEERS; i++)
	{
		if (s->peers[i].sending == slot)
			s->peers[i].sending = -1;
		s->peers[i].pending &= ~(1u << slot);
	}
	free(s->images[slot].data);
	image->local = local;
	s->images[slot] = *image;
	image->data = NULL;
	if (s->host)
		distribute(s, slot);
	return 1;
}
int spray_share_publish(struct spray_share *s, int owner, const void *data, size_t size,
						struct spray_pose pose, const char *name)
{
	struct image im;
	int slot;
	if (!s || !data || !size || size > SPRAY_SHARE_LIMIT || (!s->host && !s->peers[0].capable))
		return 0;
	memset(&im, 0, sizeof(im));
	im.pose = pose;
	im.owner = owner;
	if (name)
	{
		strncpy(im.name, name, sizeof(im.name) - 1);
	}
	if (s->host && !s->cb.accept(s->context, owner, &im.pose, im.name))
		return 0;
	im.data = malloc(size);
	if (!im.data)
		return 0;
	memcpy(im.data, data, size);
	im.size = (uint32_t)size;
	im.id = ++s->serial;
	crypto_blake2b(im.hash, 32, im.data, size);
	slot = slot_for(s, owner);
	if (!commit(s, &im, slot, 1))
	{
		free(im.data);
		return 0;
	}
	if (!s->host)
	{
		s->peers[0].sending = -1;
		s->peers[0].pending |= 1u << slot;
	}
	return 1;
}
int spray_share_available(const struct spray_share *s)
{
	return s && (s->host || s->peers[0].capable);
}
static void offer(struct spray_share *s, int peer, int slot)
{
	unsigned char b[OFFER] = {0};
	struct image *im = &s->images[slot];
	int i;
	put32(b, im->size);
	put32(b + 4, (uint32_t)im->owner);
	memcpy(b + 8, im->hash, 32);
	memcpy(b + 40, im->name, 32);
	for (i = 0; i < 6; i++)
	{
		uint32_t f;
		memcpy(&f, i < 3 ? &im->pose.origin[i] : &im->pose.direction[i - 3], 4);
		put32(b + 72 + 4 * i, f);
	}
	put32(b + 96, (uint32_t)im->pose.bsp);
	packet(s, peer, OFFER_IMAGE, im->id, b, sizeof(b), 1);
}
void spray_share_receive(struct spray_share *s, int peer, const void *data, size_t size)
{
	const unsigned char *p = data, *b;
	struct peer *q;
	uint32_t id;
	int kind, i;
	if (!s || peer < 0 || peer >= SPRAY_SHARE_PEERS || size > SPRAY_SHARE_PACKET ||
		!spray_share_handles(data, size))
		return;
	q = &s->peers[peer];
	if (!q->present)
		return;
	b = p + HEADER;
	size -= HEADER;
	kind = p[13];
	id = get32(p + 4);
	if (kind == HELLO && s->host && size == 0 && get64(p + 16))
	{
		uint64_t hello = get64(p + 16);
		unsigned char reply[8];
		if (q->hello && q->hello != hello && s->now - q->hello_at < 2000)
			return;
		/* Welcome is on the authenticated stream. Datagram data needs its secret. */
		if (q->hello != hello)
		{
			peer_clear(s, peer);
			q = &s->peers[peer];
			q->present = 1;
			q->hello = hello;
			unsigned char key[8], material[16], hash[8];
			put64(key, s->nonce);
			put64(material, hello);
			put32(material + 8, (uint32_t)peer);
			put32(material + 12, ++s->serial);
			crypto_blake2b_keyed(hash, 8, key, 8, material, 16);
			q->token = get64(hash);
			if (!q->token)
				q->token = s->nonce;
			q->hello_at = s->now;
		}
		put64(reply, hello);
		packet(s, peer, WELCOME, 0, reply, 8, 1);
		return;
	}
	if (kind == WELCOME && !s->host && peer == 0 && size == 8 && get64(b) == s->nonce &&
		get64(p + 16))
	{
		if (q->token && q->token != get64(p + 16))
		{
			cancel_receive(s, q);
			q->sending = -1;
			q->pending = 0;
			q->completed = 0;
			for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
				if (s->images[i].data && s->images[i].local)
					q->pending |= 1u << i;
		}
		q->token = get64(p + 16);
		q->capable = 1;
		packet(s, peer, ACK, 0, NULL, 0, 1);
		return;
	}
	if (!q->token || get64(p + 16) != q->token)
		return;
	if (kind == ACK && s->host && !id && size == 0)
	{
		if (!q->capable)
		{
			q->capable = 1;
			for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
				if (s->images[i].data)
					q->pending |= 1u << i;
		}
		return;
	}
	if (!q->capable)
		return;
	if (id == q->completed && (kind == DATA || kind == OFFER_IMAGE))
	{
		packet(s, peer, DONE, id, NULL, 0, 1);
		return;
	}
	if (kind == OFFER_IMAGE && size == OFFER)
	{
		struct image im;
		uint32_t length = get32(b);
		int slot;
		if (!id || !length || length > SPRAY_SHARE_LIMIT)
			return;
		if (q->receive.image.data && q->receive.image.id == id)
			return;
		memset(&im, 0, sizeof(im));
		im.size = length;
		im.id = id;
		im.owner = s->host ? peer : (int)get32(b + 4);
		if (im.owner < 0 || im.owner >= SPRAY_SHARE_PEERS)
			return;
		memcpy(im.hash, b + 8, 32);
		memcpy(im.name, b + 40, 31);
		for (i = 0; i < 6; i++)
		{
			uint32_t f = get32(b + 72 + 4 * i);
			memcpy(i < 3 ? &im.pose.origin[i] : &im.pose.direction[i - 3], &f, 4);
		}
		im.pose.bsp = (int32_t)get32(b + 96);
		if (s->host && !s->cb.accept(s->context, peer, &im.pose, im.name))
			return;
		slot = slot_for(s, im.owner);
		/* Content identity is independent of owner, slot, placement and session. */
		for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
			if (s->images[i].data && s->images[i].size == length &&
				!memcmp(s->images[i].hash, im.hash, 32))
				break;
		im.data = malloc(length);
		if (!im.data)
			return;
		if (i < SPRAY_SHARE_SLOTS || (s->cb.cached && s->cb.cached(s->context, im.hash, length, im.data)))
		{
			int local = i < SPRAY_SHARE_SLOTS && im.owner == s->images[i].owner && s->images[i].local;
			if (i < SPRAY_SHARE_SLOTS)
				memcpy(im.data, s->images[i].data, length);
			cancel_receive(s, q);
			/* Relay ids must remain host-wide even when no DATA is needed. */
			if (s->host)
				im.id = ++s->serial;
			if (commit(s, &im, slot, local))
			{
				q->completed = id;
				packet(s, peer, DONE, id, NULL, 0, 1);
			}
			free(im.data);
			return;
		}
		cancel_receive(s, q);
		if (s->active_receives >= 4)
		{
			free(im.data);
			return;
		}
		q->receive.image = im;
		q->receive.slot = slot;
		q->receive.last = s->now;
		s->active_receives++;
		return;
	}
	if (kind == DATA && size >= 4)
	{
		struct incoming *r = &q->receive;
		uint32_t chunk = get32(b), offset, expected;
		unsigned char ack[4];
		if (!r->image.data || r->image.id != id || chunk >= (r->image.size + CHUNK - 1) / CHUNK)
			return;
		offset = chunk * CHUNK;
		expected = r->image.size - offset;
		if (expected > CHUNK)
			expected = CHUNK;
		if (size != expected + 4)
			return;
		if (!(r->bits[chunk / 8] & (1u << (chunk % 8))))
		{
			memcpy(r->image.data + offset, b + 4, expected);
			r->bits[chunk / 8] |= (unsigned char)(1u << (chunk % 8));
			r->count++;
			r->last = s->now;
		}
		put32(ack, chunk);
		packet(s, peer, ACK, id, ack, 4, 0);
		if (r->count == (r->image.size + CHUNK - 1) / CHUNK)
		{
			unsigned char hash[32];
			crypto_blake2b(hash, 32, r->image.data, r->image.size);
			if (!memcmp(hash, r->image.hash, 32))
			{
				/* Host relay ids are host-wide, independent of each client's counter. */
				if (s->host)
					r->image.id = ++s->serial;
				if (commit(s, &r->image, slot_for(s, r->image.owner), 0))
				{
					s->active_receives--;
					memset(r, 0, sizeof(*r));
					q->completed = id;
					packet(s, peer, DONE, id, NULL, 0, 1);
					return;
				}
			}
			cancel_receive(s, q);
		}
		return;
	}
	if (kind == ACK && size == 4 && q->sending >= 0 && s->images[q->sending].id == id)
	{
		uint32_t chunk = get32(b);
		for (i = 0; i < WINDOW; i++)
			if (q->window[i].active && q->window[i].chunk == chunk)
			{
				q->window[i].active = 0;
				q->sent_at = s->now;
			}
		return;
	}
	if (kind == DONE && size == 0 && q->sending >= 0 && s->images[q->sending].id == id)
	{
		q->sending = -1;
		return;
	}
}
void spray_share_update(struct spray_share *s, uint32_t now)
{
	int n;
	if (!s)
		return;
	s->now = now;
	if (now - s->budget_at >= 16)
	{
		uint32_t elapsed = now - s->budget_at;
		s->budget += (elapsed > 100 ? 100 : elapsed) * 64;
		if (s->budget > 8192)
			s->budget = 8192;
		s->budget_at = now;
	}
	if (!s->host && s->peers[0].present &&
		((!s->peers[0].capable && s->hello_count < 3 &&
		  (!s->hello_count || now - s->peers[0].hello_at >= 2000)) ||
		 (s->peers[0].capable && now - s->peers[0].hello_at >= 5000)))
	{
		s->peers[0].hello_at = now;
		if (!s->peers[0].capable)
			s->hello_count++;
		packet(s, 0, HELLO, 0, NULL, 0, 1);
	}
	for (n = 0; n < SPRAY_SHARE_PEERS; n++)
	{
		int peer = (s->cursor + n) % SPRAY_SHARE_PEERS, i;
		struct peer *q = &s->peers[peer];
		struct image *im;
		if (q->receive.image.data && now - q->receive.last >= TIMEOUT)
			cancel_receive(s, q);
		if (!q->present || !q->capable)
			continue;
		if (q->sending < 0 && q->pending)
		{
			for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
				if (q->pending & (1u << i))
					break;
			q->pending &= ~(1u << i);
			if (!s->images[i].data)
				continue;
			q->sending = i;
			q->next = 0;
			q->sent_at = q->offer_at = now;
			memset(q->window, 0, sizeof(q->window));
			offer(s, peer, i);
		}
		if (q->sending < 0)
			continue;
		im = &s->images[q->sending];
		/* Give a cached receiver time to answer DONE before sending image bytes. */
		if (!q->next && now - q->offer_at < RETRY)
			continue;
		if (now - q->sent_at >= TIMEOUT)
		{
			q->sending = -1;
			continue;
		}
		/* Repeat the offer until the receiver can allocate its bounded receive slot. */
		if (now - q->offer_at >= 2000)
		{
			offer(s, peer, q->sending);
			q->offer_at = now;
		}
		for (i = 0; i < WINDOW && s->budget >= CHUNK + HEADER + 4; i++)
		{
			struct flight *f = &q->window[i];
			unsigned char b[CHUNK + 4];
			uint32_t offset, length;
			if (!f->active)
			{
				if (q->next >= (im->size + CHUNK - 1) / CHUNK)
					continue;
				f->chunk = q->next++;
				f->active = 1;
				f->at = now - RETRY;
			}
			if (now - f->at < RETRY)
				continue;
			offset = f->chunk * CHUNK;
			length = im->size - offset;
			if (length > CHUNK)
				length = CHUNK;
			put32(b, f->chunk);
			memcpy(b + 4, im->data + offset, length);
			if (packet(s, peer, DATA, im->id, b, length + 4, 0))
			{
				f->at = now;
				s->budget -= length + HEADER + 4;
			}
		}
	}
	s->cursor = (s->cursor + 1) % SPRAY_SHARE_PEERS;
}

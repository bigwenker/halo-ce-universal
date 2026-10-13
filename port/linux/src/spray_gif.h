/* The pinned stb decoder supplies GIF compositing. Preflight bounds its frame
 * count/storage before decoding; keep the correct two-back frame for disposal 3. */
#define SPRAY_ANIMATION_LIMIT (64u * 1024u * 1024u)
#define SPRAY_GIF_FRAMES 256

static const char *spray_image_extension(const void *data, size_t size)
{
	const unsigned char *p = data;
	if (!p) return NULL;
	if (size >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8)) return "png";
	if (size >= 3 && p[0] == 255 && p[1] == 216 && p[2] == 255) return "jpg";
	if (size >= 6 && (!memcmp(p, "GIF87a", 6) || !memcmp(p, "GIF89a", 6))) return "gif";
	return NULL;
}

static int spray_gif_blocks(const unsigned char *p, size_t size, size_t *at)
{
	while (*at < size)
	{
		size_t count = p[(*at)++];
		if (!count) return 1;
		if (count > size - *at) return 0;
		*at += count;
	}
	return 0;
}

static int spray_gif_info(const unsigned char *p, size_t size, int *width, int *height)
{
	size_t at = 13, stride;
	int frames = 0;
	if (size < at) return 0;
	*width = p[6] | p[7] << 8;
	*height = p[8] | p[9] << 8;
	if (!*width || !*height || *width > STBI_MAX_DIMENSIONS || *height > STBI_MAX_DIMENSIONS) return 0;
	stride = (size_t)*width * *height * 4;
	if (p[10] & 128) at += (size_t)3 << ((p[10] & 7) + 1);
	while (at < size)
	{
		int kind = p[at++];
		if (kind == 0x3b) return frames;
		if (kind == 0x21)
		{
			if (at >= size) return 0;
			at++; /* extension label; its data uses the same bounded subblocks */
			if (!spray_gif_blocks(p, size, &at)) return 0;
		}
		else if (kind == 0x2c)
		{
			unsigned x, y, w, h, flags;
			if (size - at < 9) return 0;
			x = p[at] | p[at + 1] << 8; y = p[at + 2] | p[at + 3] << 8;
			w = p[at + 4] | p[at + 5] << 8; h = p[at + 6] | p[at + 7] << 8;
			flags = p[at + 8]; at += 9;
			if (!w || !h || x + w > (unsigned)*width || y + h > (unsigned)*height ||
				++frames > SPRAY_GIF_FRAMES || (size_t)frames > SPRAY_ANIMATION_LIMIT / stride) return 0;
			if (flags & 128) at += (size_t)3 << ((flags & 7) + 1);
			if (at >= size || p[at] < 2 || p[at] > 8) return 0;
			at++; /* LZW minimum code size */
			if (!spray_gif_blocks(p, size, &at)) return 0;
		}
		else return 0;
	}
	return 0;
}

static unsigned char *spray_decode_image(const void *data, size_t size, int *width,
	int *height, int *frames, int **delays)
{
	const char *extension = spray_image_extension(data, size);
	int channels;
	*frames = 1; *delays = NULL;
	if (!extension) return NULL;
	if (strcmp(extension, "gif"))
		return stbi_load_from_memory(data, (int)size, width, height, &channels, 4);
	{
		stbi__context context;
		stbi__gif gif;
		unsigned char *output = NULL;
		size_t stride;
		int i, success = 0;
		*frames = spray_gif_info(data, size, width, height);
		if (!*frames) return NULL;
		stride = (size_t)*width * *height * 4;
		memset(&gif, 0, sizeof(gif));
		output = spray_decode_malloc(stride * *frames);
		*delays = spray_decode_malloc(sizeof(int) * *frames);
		if (!output || !*delays) goto done;
		stbi__start_mem(&context, data, (int)size);
		for (i = 0; i < *frames; i++)
		{
			unsigned char *frame = stbi__gif_load_next(&context, &gif, &channels, 4,
				i >= 2 ? output + (i - 2) * stride : NULL);
			if (!frame || frame == (unsigned char *)&context || gif.w != *width || gif.h != *height) goto done;
			memcpy(output + i * stride, frame, stride);
			(*delays)[i] = gif.delay > 0 ? gif.delay : 100;
		}
		success = 1;
	done:
		spray_decode_free(gif.out); spray_decode_free(gif.history); spray_decode_free(gif.background);
		if (!success)
		{
			spray_decode_free(output); spray_decode_free(*delays); *delays = NULL; return NULL;
		}
		return output;
	}
}

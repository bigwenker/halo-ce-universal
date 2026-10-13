#include "../include/halo_spray.h"
#include "xgpu.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../third_party/monocypher/monocypher.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096
#define SPRAY_DECODE_LIMIT (320u * 1024u * 1024u)
#define SPRAY_TEXTURE_LIMIT (128u * 1024u * 1024u)
#define STBI_FAILURE_USERMSG
/* Bound stb's inflated scanlines and output, including a 4096-square
   16-bit RGBA PNG's working buffers and its final 8-bit pixels. */
static size_t spray_decode_bytes;
static void *spray_decode_malloc(size_t size)
{
	size_t *allocation;
	if (size > SPRAY_DECODE_LIMIT || spray_decode_bytes > SPRAY_DECODE_LIMIT - size)
		return NULL;
	allocation = malloc(sizeof(*allocation) + size);
	if (!allocation)
		return NULL;
	*allocation = size;
	spray_decode_bytes += size;
	return allocation + 1;
}
static void spray_decode_free(void *data)
{
	if (data)
	{
		size_t *allocation = (size_t *)data - 1;
		spray_decode_bytes -= *allocation;
		free(allocation);
	}
}
static void *spray_decode_realloc(void *data, size_t size)
{
	void *new_data;
	size_t old_size = data ? *((size_t *)data - 1) : 0;
	/* Allocate before releasing: peak residency is bounded as well. */
	new_data = spray_decode_malloc(size);
	if (!new_data)
		return NULL;
	if (data)
		memcpy(new_data, data, old_size < size ? old_size : size);
	spray_decode_free(data);
	return new_data;
}
#define STBI_MALLOC spray_decode_malloc
#define STBI_REALLOC spray_decode_realloc
#define STBI_FREE spray_decode_free
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"

static GLuint spray_program, spray_array, spray_buffer;
static GLuint spray_textures[SPRAY_SHARE_SLOTS];
static size_t spray_texture_bytes[SPRAY_SHARE_SLOTS];
void console_printf(BOOL warning, const char *format, ...);

static int spray_source_path(char *path, size_t capacity)
{
	const char *base = SDL_GetBasePath();
	char directory[4096];
	if (!base ||
		SDL_snprintf(directory, sizeof(directory), "%ssprays", base) >= (int)sizeof(directory))
		return 0;
	if (!SDL_CreateDirectory(directory))
		return 0;
	return SDL_snprintf(path, capacity, "%s/spray.png", directory) < (int)capacity;
}

int halo_spray_file_read(void **data, size_t *size)
{
	char path[4096];
	FILE *file;
	long length;
	void *bytes;
	*data = NULL;
	*size = 0;
	if (!spray_source_path(path, sizeof(path)) || !(file = fopen(path, "rb")))
	{
		console_printf(FALSE, "Spray Image: put spray.png in sprays/ beside halo.exe");
		return 0;
	}
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 || length > SPRAY_SHARE_LIMIT ||
		fseek(file, 0, SEEK_SET))
	{
		fclose(file);
		console_printf(FALSE, "Spray Image: PNG must be at most 5 MiB");
		return 0;
	}
	bytes = malloc((size_t)length);
	if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length || fgetc(file) != EOF)
	{
		free(bytes);
		fclose(file);
		return 0;
	}
	fclose(file);
	*data = bytes;
	*size = (size_t)length;
	return 1;
}

void halo_spray_file_free(void *data)
{
	free(data);
}

int halo_spray_png_aspect(const void *data, size_t size, float *aspect)
{
	int width, height, channels;
	unsigned char *pixels;
	if (!data || !size || size > SPRAY_SHARE_LIMIT)
		return 0;
	pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
	if (!pixels)
		return 0;
	stbi_image_free(pixels);
	*aspect = (float)height / width;
	return 1;
}

/* The sidecar index keeps the human-readable download names. Only its digest
   is trusted: the indexed file is length-checked and hashed again on reuse. */
static int spray_cache_paths(const unsigned char hash[32], char *index, size_t capacity,
	char *directory, size_t directory_capacity)
{
	char hex[65], folder[4096];
	int i;
	if (!spray_source_path(directory, directory_capacity))
		return 0;
	*strrchr(directory, '/') = 0;
	for (i = 0; i < 32; i++)
		SDL_snprintf(hex + i * 2, 3, "%02x", hash[i]);
	if (SDL_snprintf(folder, sizeof(folder), "%s/.cache", directory) >= (int)sizeof(folder) ||
		!SDL_CreateDirectory(folder))
		return 0;
	return SDL_snprintf(index, capacity, "%s/%s.ref", folder, hex) < (int)capacity;
}

static void spray_cache_record(const unsigned char hash[32], const char *path);
struct spray_legacy_lookup
{
	const unsigned char *hash;
	size_t size, capacity;
	void *data;
	char *path;
	int found;
};
static SDL_EnumerationResult SDLCALL spray_legacy_entry(void *context, const char *directory, const char *name)
{
	struct spray_legacy_lookup *lookup = context;
	SDL_PathInfo info;
	char path[4096];
	unsigned char hash[32];
	FILE *file;
	size_t n;
	size_t directory_length = strlen(directory);
	if (directory_length && (directory[directory_length - 1] == '/' || directory[directory_length - 1] == '\\')) directory_length--;
	if (strncmp(name, "spray_", 6) ||
		SDL_snprintf(path, sizeof(path), "%.*s/%s", (int)directory_length, directory, name) >= (int)sizeof(path) ||
		!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE || info.size != lookup->size ||
		!(file = fopen(path, "rb"))) return SDL_ENUM_CONTINUE;
	n = fread(lookup->data, 1, lookup->size, file);
	if (n != lookup->size || ferror(file) || fgetc(file) != EOF) { fclose(file); return SDL_ENUM_CONTINUE; }
	fclose(file);
	crypto_blake2b(hash, 32, lookup->data, lookup->size);
	spray_cache_record(hash, path);
	if (memcmp(hash, lookup->hash, 32) || strlen(path) >= lookup->capacity) return SDL_ENUM_CONTINUE;
	memcpy(lookup->path, path, strlen(path) + 1); lookup->found = 1;
	return SDL_ENUM_SUCCESS;
}

/* Adopt pre-index downloads lazily. Only equal-size candidates are read, once
   per offered size; subsequent lookups use the digest index directly. */
static int spray_legacy_find(const char *directory, const unsigned char hash[32], size_t size,
	void *data, char *path, size_t capacity)
{
	static size_t scanned[64];
	static int count;
	struct spray_legacy_lookup lookup = {hash, size, capacity, data, path, 0};
	int i;
	for (i = 0; i < count; i++) if (scanned[i] == size) return 0;
	if (count < 64) scanned[count++] = size;
	SDL_EnumerateDirectory(directory, spray_legacy_entry, &lookup);
	return lookup.found;
}

static int spray_cached_read(const unsigned char hash[32], size_t size, void *data,
	char *path, size_t capacity)
{
	char index[4096], directory[4096], name[256];
	unsigned char digest[32];
	FILE *file;
	size_t n, i;
	if (!data || !size || size > SPRAY_SHARE_LIMIT ||
		!spray_cache_paths(hash, index, sizeof(index), directory, sizeof(directory)))
		return 0;
	file = fopen(index, "rb");
	if (!file) return spray_legacy_find(directory, hash, size, data, path, capacity);
	n = fread(name, 1, sizeof(name) - 1, file);
	if (ferror(file) || fgetc(file) != EOF) { fclose(file); return 0; }
	fclose(file);
	name[n] = 0;
	if (!n || strstr(name, ".."))
		return 0;
	for (i = 0; i < n; i++)
		if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
			(name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-' || name[i] == '.'))
			return 0;
	if (SDL_snprintf(path, capacity, "%s/%s", directory, name) >= (int)capacity ||
		!(file = fopen(path, "rb")))
		return 0;
	n = fread(data, 1, size, file);
	if (n != size || ferror(file) || fgetc(file) != EOF) { fclose(file); return 0; }
	fclose(file);
	crypto_blake2b(digest, 32, data, size);
	return !memcmp(hash, digest, 32);
}

int halo_spray_file_cached(const unsigned char hash[32], size_t size, void *data)
{
	char path[4096];
	return spray_cached_read(hash, size, data, path, sizeof(path));
}

static void spray_cache_record(const unsigned char hash[32], const char *path)
{
	char index[4096], directory[4096], temporary[4096];
	const char *name = strrchr(path, '/');
	FILE *file;
	if (!name || !spray_cache_paths(hash, index, sizeof(index), directory, sizeof(directory)) ||
		SDL_snprintf(temporary, sizeof(temporary), "%s.part", index) >= (int)sizeof(temporary))
		return;
	file = fopen(temporary, "wb");
	if (file)
	{
		size_t n = strlen(++name), written = fwrite(name, 1, n, file);
		int closed = fclose(file);
		if (!closed && written == n)
		{
			/* A stale entry may refer to a file the user deleted or changed. */
			remove(index);
			if (!rename(temporary, index))
				return;
		}
		remove(temporary);
	}
}

int halo_spray_file_save(const void *data, size_t size, const char *name, char *path,
						 size_t capacity, float *aspect)
{
	char base_path[4096], safe[32], temporary[4096];
	SDL_Time time;
	SDL_DateTime date;
	SDL_PathInfo info;
	FILE *file;
	int i, n = 0, attempt;
	size_t written;
	unsigned char hash[32];
	void *cached;
	if (!halo_spray_png_aspect(data, size, aspect) ||
		!spray_source_path(base_path, sizeof(base_path)) || !SDL_GetCurrentTime(&time) ||
		!SDL_TimeToDateTime(time, &date, TRUE))
		return 0;
	crypto_blake2b(hash, 32, data, size);
	cached = malloc(size);
	if (cached)
	{
		int found = spray_cached_read(hash, size, cached, path, capacity);
		free(cached);
		if (found)
			return 1;
	}
	for (i = 0; name && name[i] && n < 31; i++)
	{
		unsigned char c = (unsigned char)name[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-')
			safe[n++] = (char)c;
	}
	if (!n)
	{
		memcpy(safe, "player", 6);
		n = 6;
	}
	safe[n] = 0;
	/* Only an executable-relative directory and a sanitized basename are used. */
	*strrchr(base_path, '/') = 0;
	for (attempt = 0; attempt < 1000; attempt++)
	{
		if (SDL_snprintf(path, capacity, "%s/spray_%s_%04d-%02d-%02d_%02d.%02d.%02d_%03d.png",
						 base_path, safe, date.year, date.month, date.day, date.hour, date.minute,
						 date.second, attempt) >= (int)capacity)
			return 0;
		if (SDL_GetPathInfo(path, &info))
			continue;
		if (SDL_snprintf(temporary, sizeof(temporary), "%s.part", path) >= (int)sizeof(temporary))
			return 0;
		file = fopen(temporary, "wb");
		if (!file)
			return 0;
		written = fwrite(data, 1, size, file);
		if (fclose(file) || written != size)
		{
			remove(temporary);
			return 0;
		}
		if (rename(temporary, path))
		{
			remove(temporary);
			return 0;
		}
		spray_cache_record(hash, path);
		return 1;
	}
	return 0;
}

int spray_image_load(float *aspect)
{
	char path[4096];
	if (!spray_source_path(path, sizeof(path)))
		return 0;
	return spray_image_load_slot(0, path, aspect);
}

int spray_image_load_slot(int slot, const char *path, float *aspect)
{
	SDL_IOStream *file;
	Sint64 length;
	size_t size = 0;
	void *data;
	int result;
	if (slot < 0 || slot >= SPRAY_SHARE_SLOTS || !path)
		return 0;
	file = SDL_IOFromFile(path, "rb");
	if (!file)
	{
		console_printf(FALSE, "Spray Image: put spray.png in sprays/ beside halo.exe");
		return 0;
	}
	length = SDL_GetIOSize(file);
	if (length <= 0 || length > SPRAY_SHARE_LIMIT)
	{
		SDL_CloseIO(file);
		console_printf(FALSE, "Spray Image: PNG must be at most 5 MiB");
		return 0;
	}
	data = SDL_LoadFile_IO(file, &size, TRUE);
	/* A changing file must not bypass the encoded-size limit. */
	if (size > SPRAY_SHARE_LIMIT)
	{
		SDL_free(data);
		return 0;
	}
	result = spray_image_load_bytes(slot, data, size, aspect);
	SDL_free(data);
	return result;
}

int spray_image_load_bytes(int slot, const void *data, size_t size, float *aspect)
{
	unsigned char *pixels;
	int width, height, channels;
	GLuint texture;
	GLint active, bound, unpack_buffer, alignment, row_length, skip_rows, skip_pixels, maximum_size;
	if (slot < 0 || slot >= SPRAY_SHARE_SLOTS || !data || !size || size > SPRAY_SHARE_LIMIT)
		return 0;
	pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
	if (!pixels)
	{
		console_printf(FALSE, "Spray Image: invalid PNG (maximum 4096 x 4096)");
		return 0;
	}
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum_size);
	if (width > maximum_size || height > maximum_size)
	{
		stbi_image_free(pixels);
		console_printf(FALSE, "Spray Image: PNG exceeds this device's texture size limit");
		return 0;
	}
	/* Uploads must not inherit a previous draw's pixel-unpack buffer or strides. */
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
	glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
	glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
	glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
	glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, bound);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
	glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
	glActiveTexture(active);
	stbi_image_free(pixels);
	/* Bound aggregate GPU residency to 128 MiB, including mipmaps. A single
	   4096-square RGBA texture with its mipmaps needs about 85 MiB. */
	{
		size_t bytes = (size_t)width * height * 4 * 4 / 3, total = 0;
		int i;
		for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
			if (i != slot)
				total += spray_texture_bytes[i];
		for (i = 0; i < SPRAY_SHARE_SLOTS && total + bytes > SPRAY_TEXTURE_LIMIT; i++)
			if (i != slot && spray_textures[i])
			{
				glDeleteTextures(1, &spray_textures[i]);
				spray_textures[i] = 0;
				total -= spray_texture_bytes[i];
				spray_texture_bytes[i] = 0;
			}
		if (spray_textures[slot])
			glDeleteTextures(1, &spray_textures[slot]);
		spray_textures[slot] = texture;
		spray_texture_bytes[slot] = bytes;
	}
	*aspect = (float)height / width;
	xgpu_gl_state_invalidate();
	return 1;
}

void halo_spray_image_forget(void)
{
	int i;
	for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
		if (spray_textures[i])
		{
			glDeleteTextures(1, &spray_textures[i]);
			spray_textures[i] = 0;
			spray_texture_bytes[i] = 0;
			xgpu_gl_state_invalidate();
		}
}

static int spray_prepare(void)
{
	GLuint vertex, fragment;
	if (spray_program)
		return 1;
#ifdef HALO_ANDROID
	/* GLES has no native upper-left / zero-to-one clip control. */
	vertex = xgpu_compile_shader(
		GL_VERTEX_SHADER,
		"#version 300 es\nprecision highp float;\n"
		"layout(location=0) in vec4 p; layout(location=1) in vec2 t; out vec2 uv;\n"
		"void main(){gl_Position=vec4(p.x,-p.y,2.0*p.z-p.w,p.w); uv=t;}\n",
		"spray");
	fragment =
		xgpu_compile_shader(GL_FRAGMENT_SHADER,
							"#version 300 es\nprecision highp float;\n"
#else
	vertex = xgpu_compile_shader(
		GL_VERTEX_SHADER,
		"#version 450 core\n"
		"layout(location=0) in vec4 p; layout(location=1) in vec2 t; out vec2 uv;\n"
		"void main(){gl_Position=p; uv=t;}\n",
		"spray");
	fragment =
		xgpu_compile_shader(GL_FRAGMENT_SHADER,
							"#version 450 core\n"
#endif
							"uniform sampler2D image; in vec2 uv; out vec4 color;\n"
							"void main(){color=texture(image,uv); if(color.a==0.0) discard;}\n",
							"spray");
	spray_program = vertex && fragment ? xgpu_link_program(vertex, fragment, "spray") : 0;
	if (vertex)
		glDeleteShader(vertex);
	if (fragment)
		glDeleteShader(fragment);
	if (!spray_program)
		return 0;
	glGenVertexArrays(1, &spray_array);
	glGenBuffers(1, &spray_buffer);
	return 1;
}

void halo_spray_image_draw(struct halo_spray_clip_vertex const *vertices, int count)
{
	halo_spray_image_draw_slot(0, vertices, count);
}

void halo_spray_image_draw_slot(int slot, struct halo_spray_clip_vertex const *vertices, int count)
{
	GLint program, array, buffer, active, texture, sampler, function;
	GLint src_rgb, dst_rgb, src_alpha, dst_alpha, equation_rgb, equation_alpha;
	GLboolean depth, blend, stencil, cull, mask[4], depth_mask;
	if (slot < 0 || slot >= SPRAY_SHARE_SLOTS || !spray_textures[slot] || count <= 0 ||
		count > HALO_SPRAY_MAXIMUM_VERTICES || !spray_prepare())
		return;
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &array);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	glGetIntegerv(GL_SAMPLER_BINDING, &sampler);
	glGetIntegerv(GL_DEPTH_FUNC, &function);
	glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
	glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation_rgb);
	glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation_alpha);
	depth = glIsEnabled(GL_DEPTH_TEST);
	blend = glIsEnabled(GL_BLEND);
	stencil = glIsEnabled(GL_STENCIL_TEST);
	cull = glIsEnabled(GL_CULL_FACE);
	glGetBooleanv(GL_COLOR_WRITEMASK, mask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
	glUseProgram(spray_program);
	glUniform1i(glGetUniformLocation(spray_program, "image"), 0);
	glBindVertexArray(spray_array);
	glBindBuffer(GL_ARRAY_BUFFER, spray_buffer);
	glBufferData(GL_ARRAY_BUFFER, count * sizeof(*vertices), vertices, GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(*vertices), (void *)0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(*vertices), (void *)(4 * sizeof(float)));
	glBindTexture(GL_TEXTURE_2D, spray_textures[slot]);
	glBindSampler(0, 0);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_CULL_FACE);
	/* Halo uses destination alpha for subsequent lighting and fog passes. */
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	glDrawArrays(GL_TRIANGLES, 0, count);
	glColorMask(mask[0], mask[1], mask[2], mask[3]);
	glDepthMask(depth_mask);
	glDepthFunc(function);
	glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
	glBlendEquationSeparate(equation_rgb, equation_alpha);
	if (!depth)
		glDisable(GL_DEPTH_TEST);
	if (!blend)
		glDisable(GL_BLEND);
	if (stencil)
		glEnable(GL_STENCIL_TEST);
	if (cull)
		glEnable(GL_CULL_FACE);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindSampler(0, sampler);
	glActiveTexture(active);
	glBindVertexArray(array);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glUseProgram(program);
	xgpu_gl_state_invalidate();
}

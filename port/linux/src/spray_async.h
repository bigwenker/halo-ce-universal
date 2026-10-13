/* One worker owns file I/O, decoding and CPU mipmaps. It never calls GL or
   game code. Short mutex sections transfer ownership; a map reset cancels
   generations without waiting for a decode. Included by spray_image.c. */
#define SPRAY_UPLOAD_BUDGET (1024u * 1024u)
#define SPRAY_MIP_LEVELS 13
struct spray_prepared
{
 unsigned references;
 void *encoded;
 size_t size, stride, offsets[SPRAY_MIP_LEVELS];
 unsigned char hash[32], *pixels;
 int *delays, width, height, frames, levels;
};
struct spray_job
{
 unsigned generation;
 int state, save;
 void *data;
 size_t size;
 unsigned char hash[32];
 char name[32];
 struct spray_prepared *result;
};
struct spray_upload
{
 GLuint texture;
 int active, initial, allocate, frame, level, row;
};
static pthread_mutex_t spray_work_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t spray_work_changed = PTHREAD_COND_INITIALIZER;
static struct spray_job spray_jobs[SPRAY_SHARE_SLOTS];
static struct spray_prepared *spray_source, *spray_recent;
static struct spray_prepared *spray_slot_images[SPRAY_SHARE_SLOTS];
static struct spray_upload spray_uploads[SPRAY_SHARE_SLOTS];
static GLuint spray_back_textures[SPRAY_SHARE_SLOTS];
static int spray_visible[SPRAY_SHARE_SLOTS], spray_async_used[SPRAY_SHARE_SLOTS];
static int spray_worker_started, spray_worker_stop, spray_worker_exited;
static int spray_source_refresh, spray_source_checked;
static Uint64 spray_source_check_tick;

static void spray_prepared_release(struct spray_prepared *image)
{
 int destroy;
 if (!image) return;
 pthread_mutex_lock(&spray_work_lock);
 destroy = --image->references == 0;
 pthread_mutex_unlock(&spray_work_lock);
 if (destroy)
 {
  spray_decode_free(image->pixels); spray_decode_free(image->delays);
  free(image->encoded); free(image);
 }
}

/* The worker serializes all decodes. Existing prepared content is shared even
   when another player's decal uses it; encoded bytes remain immutable. */
static struct spray_prepared *spray_prepared_find(const unsigned char hash[32], size_t size)
{
 struct spray_prepared *image = NULL;
 int i;
 pthread_mutex_lock(&spray_work_lock);
 if (spray_source && spray_source->size == size && !memcmp(spray_source->hash, hash, 32)) image = spray_source;
 else if (spray_recent && spray_recent->size == size && !memcmp(spray_recent->hash, hash, 32)) image = spray_recent;
 for (i = 0; !image && i < SPRAY_SHARE_SLOTS; i++)
  if (spray_slot_images[i] && spray_slot_images[i]->size == size && !memcmp(spray_slot_images[i]->hash, hash, 32)) image = spray_slot_images[i];
 if (image) image->references++;
 pthread_mutex_unlock(&spray_work_lock);
 return image;
}

static struct spray_prepared *spray_prepared_decode(void *data, size_t size, const unsigned char hash[32])
{
 struct spray_prepared *image = spray_prepared_find(hash, size);
 unsigned char *decoded;
 size_t base;
 int frame, level, width, height;
 if (image) { free(data); return image; }
 image = calloc(1, sizeof(*image));
 if (!image) { free(data); return NULL; }
 image->references = 1; image->encoded = data; image->size = size;
 memcpy(image->hash, hash, 32);
 decoded = spray_decode_image(data, size, &image->width, &image->height, &image->frames, &image->delays);
 if (!decoded) { spray_prepared_release(image); return NULL; }
 width = image->width; height = image->height;
 base = (size_t)width * height * 4;
 do
 {
  image->offsets[image->levels++] = image->stride;
  image->stride += (size_t)width * height * 4;
  if (width == 1 && height == 1) break;
  width = width > 1 ? width / 2 : 1; height = height > 1 ? height / 2 : 1;
 } while (image->levels < SPRAY_MIP_LEVELS);
 image->pixels = spray_decode_malloc(image->stride * image->frames);
 if (!image->pixels) { spray_decode_free(decoded); spray_prepared_release(image); return NULL; }
 for (frame = 0; frame < image->frames; frame++)
 {
  unsigned char *pixels = image->pixels + frame * image->stride;
  memcpy(pixels, decoded + frame * base, base);
  width = image->width; height = image->height;
  for (level = 1; level < image->levels; level++)
  {
   int x, y, channel, next_width = width > 1 ? width / 2 : 1, next_height = height > 1 ? height / 2 : 1;
   const unsigned char *previous = pixels + image->offsets[level - 1];
   unsigned char *next = pixels + image->offsets[level];
   for (y = 0; y < next_height; y++) for (x = 0; x < next_width; x++) for (channel = 0; channel < 4; channel++)
   {
    unsigned sum = 0, samples = 0;
    int dx, dy;
    for (dy = y * height / next_height; dy < (y + 1) * height / next_height; dy++)
     for (dx = x * width / next_width; dx < (x + 1) * width / next_width; dx++)
     { sum += previous[(dy * width + dx) * 4 + channel]; samples++; }
    next[(y * next_width + x) * 4 + channel] = (unsigned char)((sum + samples / 2) / samples);
   }
   width = next_width; height = next_height;
  }
 }
 spray_decode_free(decoded);
 /* Keep the most recent preparation for repeated placements, not a second
    decoded copy. Its residency is included in the decoder allocation cap. */
 pthread_mutex_lock(&spray_work_lock);
 { struct spray_prepared *old = spray_recent; image->references++; spray_recent = image;
   pthread_mutex_unlock(&spray_work_lock); spray_prepared_release(old); }
 return image;
}

static void spray_refresh_source(void)
{
 static char previous_path[4096];
 static Uint64 previous_size;
 static SDL_Time previous_time;
 char path[4096] = {0};
 SDL_PathInfo info;
 struct spray_prepared *image = NULL, *old;
 unsigned char hash[32];
 void *data = NULL;
 FILE *file;
 size_t size;
 int valid = spray_find_source_path(path, sizeof(path)) && SDL_GetPathInfo(path, &info);
 if (valid && !strcmp(path, previous_path) && previous_size == info.size && previous_time == info.modify_time) return;
 if (!valid && !previous_path[0] && spray_source_checked) return;
 previous_path[0] = 0;
 if (valid)
 {
  strcpy(previous_path, path); previous_size = info.size; previous_time = info.modify_time;
  if (info.size && info.size <= SPRAY_SHARE_LIMIT && (file = fopen(path, "rb")) != NULL)
  {
   size = (size_t)info.size; data = malloc(size);
   if (data && fread(data, 1, size, file) == size && fgetc(file) == EOF && !ferror(file))
   { crypto_blake2b(hash, 32, data, size); image = spray_prepared_decode(data, size, hash); data = NULL; }
   fclose(file); free(data);
  }
 }
 pthread_mutex_lock(&spray_work_lock);
 old = spray_source; spray_source = image; spray_source_checked = 1;
 pthread_mutex_unlock(&spray_work_lock);
 spray_prepared_release(old);
}

static void *spray_worker(void *unused)
{
 (void)unused;
 for (;;)
 {
  struct spray_job task;
  struct spray_prepared *image;
  int slot, refresh;
  pthread_mutex_lock(&spray_work_lock);
  for (;;)
  {
   for (slot = 0; slot < SPRAY_SHARE_SLOTS && spray_jobs[slot].state != 1; slot++) {}
   if (spray_worker_stop || slot < SPRAY_SHARE_SLOTS || spray_source_refresh) break;
   pthread_cond_wait(&spray_work_changed, &spray_work_lock);
  }
  if (spray_worker_stop) { spray_worker_exited = 1; pthread_cond_broadcast(&spray_work_changed); pthread_mutex_unlock(&spray_work_lock); return NULL; }
  refresh = slot == SPRAY_SHARE_SLOTS;
  if (refresh) spray_source_refresh = 0;
  else { task = spray_jobs[slot]; spray_jobs[slot].data = NULL; spray_jobs[slot].result = NULL; spray_jobs[slot].state = 2; }
  pthread_mutex_unlock(&spray_work_lock);
  if (refresh) { spray_refresh_source(); continue; }
  image = task.result ? task.result : spray_prepared_decode(task.data, task.size, task.hash);
  if (image && task.save)
  { char path[4096]; float aspect; halo_spray_file_save(image->encoded, image->size, task.name, path, sizeof(path), &aspect); }
  pthread_mutex_lock(&spray_work_lock);
  if (spray_jobs[slot].generation == task.generation)
  { spray_jobs[slot].result = image; spray_jobs[slot].state = image ? 3 : 4; image = NULL; }
  pthread_mutex_unlock(&spray_work_lock);
  spray_prepared_release(image);
 }
}

static int spray_worker_start(void)
{
 pthread_t thread;
 if (spray_worker_started) return 1;
 if (pthread_create(&thread, NULL, spray_worker, NULL)) return 0;
 pthread_detach(thread); spray_worker_started = 1; atexit(halo_spray_worker_shutdown);
 return 1;
}

int halo_spray_source_acquire(struct halo_spray_source *source)
{
 int result;
 memset(source, 0, sizeof(*source));
 pthread_mutex_lock(&spray_work_lock);
 result = spray_source ? 1 : (spray_source_checked ? 0 : -1);
 if (spray_source)
 {
  spray_source->references++;
  source->prepared = spray_source; source->data = spray_source->encoded; source->size = spray_source->size;
  source->aspect = (float)spray_source->height / spray_source->width; memcpy(source->hash, spray_source->hash, 32);
 }
 pthread_mutex_unlock(&spray_work_lock);
 return result;
}
void halo_spray_source_release(struct halo_spray_source *source)
{ spray_prepared_release(source->prepared); memset(source, 0, sizeof(*source)); }

int halo_spray_image_enqueue(int slot, const void *data, size_t size, const unsigned char hash[32], const char *name, int save)
{
 struct spray_prepared *image, *old;
 void *copy = NULL, *old_data;
 float aspect;
 if (slot < 0 || slot >= SPRAY_SHARE_SLOTS || !hash || !halo_spray_image_aspect(data, size, &aspect) || !spray_worker_start()) return 0;
 pthread_mutex_lock(&spray_work_lock);
 if (spray_jobs[slot].state && spray_jobs[slot].state != 4 && spray_jobs[slot].state != 6 &&
  !(spray_jobs[slot].state == 5 && !spray_slot_images[slot]) && !memcmp(spray_jobs[slot].hash, hash, 32))
 { pthread_mutex_unlock(&spray_work_lock); return 1; }
 pthread_mutex_unlock(&spray_work_lock);
 image = spray_prepared_find(hash, size);
 if (!image)
 { copy = malloc(size); if (!copy) return 0; memcpy(copy, data, size); }
 pthread_mutex_lock(&spray_work_lock);
 old = spray_jobs[slot].result; old_data = spray_jobs[slot].data;
 spray_jobs[slot].generation++; spray_jobs[slot].state = image && !save ? 3 : 1; spray_jobs[slot].save = save;
 spray_jobs[slot].result = image; spray_jobs[slot].data = copy; spray_jobs[slot].size = size;
 memcpy(spray_jobs[slot].hash, hash, 32);
 SDL_strlcpy(spray_jobs[slot].name, name ? name : "", sizeof(spray_jobs[slot].name));
 pthread_cond_signal(&spray_work_changed); pthread_mutex_unlock(&spray_work_lock);
 spray_async_used[slot] = 1; spray_visible[slot] = 0; spray_uploads[slot].active = 0;
 free(old_data); spray_prepared_release(old);
 return 1;
}

int spray_image_enqueue_local(float *aspect)
{
 struct halo_spray_source source;
 int result = halo_spray_source_acquire(&source);
 if (result <= 0) return 0;
 *aspect = source.aspect;
 result = halo_spray_image_enqueue(0, source.data, source.size, source.hash, "", 0);
 halo_spray_source_release(&source);
 return result;
}

static size_t spray_async_animation_size(int slot)
{
 struct spray_prepared *image = spray_slot_images[slot];
 return image && image->frames > 1 ? (size_t)image->width * image->height * 4 * image->frames : 0;
}
static int spray_async_visible(int slot)
{ return !spray_async_used[slot] || spray_visible[slot]; }
static void spray_async_release_slot(int slot)
{
 struct spray_prepared *image;
 pthread_mutex_lock(&spray_work_lock);
 image = spray_slot_images[slot]; spray_slot_images[slot] = NULL;
 pthread_mutex_unlock(&spray_work_lock);
 spray_prepared_release(image);
 if (spray_back_textures[slot]) glDeleteTextures(1, &spray_back_textures[slot]);
 if (spray_uploads[slot].texture) glDeleteTextures(1, &spray_uploads[slot].texture);
 spray_back_textures[slot] = 0; memset(&spray_uploads[slot], 0, sizeof(spray_uploads[slot])); spray_visible[slot] = 0;
}
static void spray_async_forget(void)
{
 int i;
 for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
 {
  struct spray_prepared *image;
  void *data;
  pthread_mutex_lock(&spray_work_lock);
  image = spray_jobs[i].result; data = spray_jobs[i].data;
  spray_jobs[i].result = NULL; spray_jobs[i].data = NULL; spray_jobs[i].state = 0; spray_jobs[i].generation++; spray_async_used[i] = 0;
  pthread_mutex_unlock(&spray_work_lock);
  free(data); spray_prepared_release(image);
 }
}

/* Shutdown waits only at process exit, never at a map reset. */
void halo_spray_worker_shutdown(void)
{
 struct spray_prepared *source, *recent;
 if (!spray_worker_started) return;
 pthread_mutex_lock(&spray_work_lock);
 spray_worker_stop = 1; pthread_cond_signal(&spray_work_changed);
 while (!spray_worker_exited) pthread_cond_wait(&spray_work_changed, &spray_work_lock);
 source = spray_source; recent = spray_recent; spray_source = spray_recent = NULL;
 pthread_mutex_unlock(&spray_work_lock);
 spray_async_forget(); spray_prepared_release(source); spray_prepared_release(recent);
}

static int spray_async_animate(int slot, Uint64 now)
{
 struct spray_prepared *image = spray_slot_images[slot];
 struct spray_animation *a = &spray_animations[slot];
 struct spray_upload *upload = &spray_uploads[slot];
 Uint64 elapsed;
 int frame;
 if (!image) return 0;
 if (image->frames < 2 || !a->duration || upload->active) return 1;
 elapsed = (now - a->started) % a->duration;
 for (frame = 0; frame + 1 < image->frames && elapsed >= (Uint64)image->delays[frame]; frame++) elapsed -= image->delays[frame];
 if (frame != a->frame)
 {
  memset(upload, 0, sizeof(*upload)); upload->active = 1; upload->frame = frame;
  upload->texture = spray_back_textures[slot]; spray_back_textures[slot] = 0;
  upload->allocate = !upload->texture;
 }
 return 1;
}

/* CPU-built mipmaps remove per-frame glGenerateMipmap. Never expose a partial
   texture: animated frames use a back texture, swapped only when complete. */
static size_t spray_upload_step(int slot, size_t budget)
{
 struct spray_upload *u = &spray_uploads[slot];
 struct spray_prepared *image = spray_slot_images[slot];
 GLint active, bound, pbo, alignment, row_length, skip_rows, skip_pixels;
 size_t used = 0;
 if (!image || !u->active) return 0;
 glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE0);
 glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound); glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);
 glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
 glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
 glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
 glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
 if (!u->texture) glGenTextures(1, &u->texture);
 glBindTexture(GL_TEXTURE_2D, u->texture);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
 glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, image->levels - 1);
 while (u->level < image->levels)
 {
  int width = image->width >> u->level, height = image->height >> u->level, rows;
  size_t row_bytes;
  if (!width) width = 1; if (!height) height = 1;
  row_bytes = (size_t)width * 4;
  rows = (int)((budget - used) / row_bytes);
  if (!rows) break;
  if (rows > height - u->row) rows = height - u->row;
  if (!u->row && u->allocate) glTexImage2D(GL_TEXTURE_2D, u->level, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexSubImage2D(GL_TEXTURE_2D, u->level, 0, u->row, width, rows, GL_RGBA, GL_UNSIGNED_BYTE,
   image->pixels + u->frame * image->stride + image->offsets[u->level] + u->row * row_bytes);
  used += rows * row_bytes; u->row += rows;
  if (u->row == height) { u->level++; u->row = 0; }
 }
 if (u->level == image->levels)
 {
  if (!u->initial) spray_back_textures[slot] = spray_textures[slot];
  spray_textures[slot] = u->texture; u->texture = 0; u->active = 0;
  spray_animations[slot].frame = u->frame; spray_visible[slot] = 1;
 }
 glBindTexture(GL_TEXTURE_2D, bound); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
 glPixelStorei(GL_UNPACK_ALIGNMENT, alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
 glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows); glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels); glActiveTexture(active);
 return used;
}

void spray_images_update(void)
{
 static int next_slot;
 Uint64 now = SDL_GetTicks();
 size_t budget = SPRAY_UPLOAD_BUDGET;
 int i;
 if (!spray_worker_start()) return;
 pthread_mutex_lock(&spray_work_lock);
 if (!spray_source_check_tick || now - spray_source_check_tick >= 1000)
 { spray_source_check_tick = now; spray_source_refresh = 1; pthread_cond_signal(&spray_work_changed); }
 pthread_mutex_unlock(&spray_work_lock);
 for (i = 0; i < SPRAY_SHARE_SLOTS; i++)
 {
  struct spray_prepared *image = NULL;
  int failed = 0;
  pthread_mutex_lock(&spray_work_lock);
  if (spray_jobs[i].state == 3) { image = spray_jobs[i].result; spray_jobs[i].result = NULL; spray_jobs[i].state = 5; }
  else if (spray_jobs[i].state == 4) { failed = 1; spray_jobs[i].state = 6; }
  pthread_mutex_unlock(&spray_work_lock);
  if (failed) console_printf(FALSE, "Spray Image: invalid image or animation exceeds memory limits");
  if (image)
  {
   size_t bytes = image->stride * (image->frames > 1 ? 2 : 1), total = 0, animations = 0;
   size_t animation = image->frames > 1 ? (size_t)image->width * image->height * 4 * image->frames : 0;
   GLint maximum;
   int j;
   glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
   if (image->width > maximum || image->height > maximum)
   { spray_prepared_release(image); console_printf(FALSE, "Spray Image: image exceeds this device's texture size limit"); continue; }
   spray_release_slot(i);
   for (j = 0; j < SPRAY_SHARE_SLOTS; j++) { total += spray_texture_bytes[j]; animations += spray_animation_size(j); }
   for (j = 0; j < SPRAY_SHARE_SLOTS && (total + bytes > SPRAY_TEXTURE_LIMIT || animations + animation > SPRAY_ANIMATION_LIMIT); j++)
    if (j != i && spray_texture_bytes[j]) { total -= spray_texture_bytes[j]; animations -= spray_animation_size(j); spray_release_slot(j); }
   pthread_mutex_lock(&spray_work_lock); spray_slot_images[i] = image; pthread_mutex_unlock(&spray_work_lock);
   spray_texture_bytes[i] = bytes; spray_texture_aspects[i] = (float)image->height / image->width;
   memcpy(spray_texture_hashes[i], image->hash, 32);
   spray_animations[i].width = image->width; spray_animations[i].height = image->height; spray_animations[i].frames = image->frames;
   spray_animations[i].started = now;
   for (j = 0; j < image->frames; j++) if (image->delays) spray_animations[i].duration += image->delays[j];
   spray_uploads[i].active = spray_uploads[i].initial = spray_uploads[i].allocate = 1;
  }
 }
 for (i = 0; i < SPRAY_SHARE_SLOTS && budget; i++)
 { int slot = (next_slot + i) % SPRAY_SHARE_SLOTS; budget -= spray_upload_step(slot, budget); }
 next_slot = (next_slot + 1) % SPRAY_SHARE_SLOTS;
 if (budget != SPRAY_UPLOAD_BUDGET) xgpu_gl_state_invalidate();
}

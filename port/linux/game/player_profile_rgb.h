/* Optional per-profile RGB color; the original profile layout stays intact. */
#ifndef PROFILE_RGB_H
#define PROFILE_RGB_H
#include "cseries.h"
boolean profile_rgb_parse(char const *text, unsigned long *rgb);
void profile_rgb_format(unsigned long rgb, char text[8]);
boolean profile_rgb_read(long profile, unsigned long *rgb);
boolean profile_rgb_write(long profile, unsigned long rgb);
short profile_rgb_nearest(unsigned long rgb);
void profile_rgb_to_hsv(unsigned long rgb, real *h, real *s, real *v);
unsigned long profile_rgb_from_hsv(real h, real s, real v);
#endif

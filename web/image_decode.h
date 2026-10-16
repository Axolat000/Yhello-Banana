#ifndef WEB_IMAGE_DECODE_H
#define WEB_IMAGE_DECODE_H
#include "page.h"
/* Shared browser adapter, also used by the host's live website tests. */
int web_decode_image(void* ctx,const uint8_t* data,uint32_t len,img_data_t* out,arena_t* A);
#endif

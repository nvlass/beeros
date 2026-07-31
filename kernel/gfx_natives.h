#ifndef GFX_NATIVES_H
#define GFX_NATIVES_H

/* Register the beer.gfx namespace and its 6 native primitives. */
void gfx_register_natives(void);

/* Compile and run the embedded lib/gfx.beer source (line!, circle!, etc.). */
void gfx_load_library(void);

#endif /* GFX_NATIVES_H */

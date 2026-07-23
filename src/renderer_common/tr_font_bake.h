#ifndef TR_FONT_BAKE_H
#define TR_FONT_BAKE_H

qboolean Font_BakeHudcharsAtlas( const char *name, byte **pic, int *width, int *height );
qboolean Font_IsHudcharsImageName( const char *name );

qboolean Font_BakeConsoleAtlas( const char *name, byte **pic, int *width, int *height );
qboolean Font_IsConsoleImageName( const char *name );

#endif

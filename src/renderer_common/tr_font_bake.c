// Bakes the hudchars*.tga replacement TTFs (see tools/font_convert/) into the
// same 16x16 grid atlas layout via stb_truetype. Shared between renderer_gl
// and renderer_vk -- see R_LoadImage in each backend's tr_image.c.

#include "tr_local.h"
#include "../qcommon/qcommon.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "../thirdparty/stb/stb_truetype.h"

#include "tr_font_bake.h"

#define FONT_GRID_CELLS       16
#define FONT_BAKE_GLYPH_PX    64
// Transparent padding to keep mip generation from bleeding neighboring glyphs together.
#define FONT_BAKE_GUTTER_PX   4
#define FONT_BAKE_PX_PER_CELL ( FONT_BAKE_GLYPH_PX + 2 * FONT_BAKE_GUTTER_PX )
#define FONT_BAKE_ATLAS_PX    ( FONT_GRID_CELLS * FONT_BAKE_PX_PER_CELL )

// hudchars_OSP1/OSP2 stay plain bitmaps, not handled here.
static const char *hudcharsImageName = "gfx/2d/hudchars.tga"; // exact string as it reaches R_LoadImage

// Names the .ttf under fonts/ that both bakes below load. CVAR_LATCH: takes
// effect on the next vid_restart.
static void Font_GetActiveTtfPath( char *buf, int bufSize ) {
	cvar_t *fontFile = ri.Cvar_Get( "r_hudFontFile", "hudchars.ttf", CVAR_ARCHIVE | CVAR_LATCH );
	Com_sprintf( buf, bufSize, "fonts/%s", fontFile->string );
}

// Kill switch for the whole vector-font system; 0 restores pre-project
// bitmap-only behavior. CVAR_LATCH.
static qboolean Font_VectorFontsEnabled( void ) {
	cvar_t *enabled = ri.Cvar_Get( "r_hudFontEnabled", "1", CVAR_ARCHIVE | CVAR_LATCH );
	return enabled->integer != 0;
}

#define FONT_DEFAULT_TTF_PATH "fonts/hudchars.ttf"

// Loads r_hudFontFile, falling back to the default shipped font if it's
// missing/unparseable. On success, *outBuffer must be freed via ri.FS_FreeFile.
static qboolean Font_LoadActiveTtf( stbtt_fontinfo *font, void **outBuffer, const char *callerTag ) {
	char ttfPath[MAX_QPATH];
	void *ttfBuffer;
	int ttfLen;

	Font_GetActiveTtfPath( ttfPath, sizeof( ttfPath ) );
	ttfLen = ri.FS_ReadFile( ttfPath, &ttfBuffer );
	if ( ttfLen > 0 && ttfBuffer &&
		 stbtt_InitFont( font, (const unsigned char *)ttfBuffer, stbtt_GetFontOffsetForIndex( (const unsigned char *)ttfBuffer, 0 ) ) ) {
		*outBuffer = ttfBuffer;
		return qtrue;
	}
	if ( ttfBuffer ) {
		ri.FS_FreeFile( ttfBuffer );
	}

	if ( !Q_stricmp( ttfPath, FONT_DEFAULT_TTF_PATH ) ) {
		ri.Printf( PRINT_WARNING, "%s: couldn't load %s\n", callerTag, ttfPath );
		return qfalse;
	}

	ri.Printf( PRINT_WARNING, "%s: couldn't load %s, falling back to %s\n", callerTag, ttfPath, FONT_DEFAULT_TTF_PATH );
	ttfLen = ri.FS_ReadFile( FONT_DEFAULT_TTF_PATH, &ttfBuffer );
	if ( ttfLen <= 0 || !ttfBuffer ||
		 !stbtt_InitFont( font, (const unsigned char *)ttfBuffer, stbtt_GetFontOffsetForIndex( (const unsigned char *)ttfBuffer, 0 ) ) ) {
		if ( ttfBuffer ) {
			ri.FS_FreeFile( ttfBuffer );
		}
		ri.Printf( PRINT_WARNING, "%s: couldn't load fallback %s either\n", callerTag, FONT_DEFAULT_TTF_PATH );
		return qfalse;
	}

	*outBuffer = ttfBuffer;
	return qtrue;
}

// Single reusable buffer, not a heap allocation -- avoids tying its
// lifetime to vid_restart's asymmetric renderer-memory teardown.
static byte bakedAtlas[FONT_BAKE_ATLAS_PX * FONT_BAKE_ATLAS_PX * 4];

// Rasterizes one glyph's coverage into a cellPxW x cellPxH cell; shared by
// the square hudchars bake and the non-square console bake below.
static void Font_BakeGlyphCoverage( const stbtt_fontinfo *font, int glyphIndex, float scaleX, float scaleY,
									 int glyphPxW, int glyphPxH, float baselinePxY, int gutterPx, int cellPxW, int cellPxH,
									 byte *coverageOut ) {
	int ix0, iy0, ix1, iy1, gw, gh;
	byte *glyphBuf;

	Com_Memset( coverageOut, 0, cellPxW * cellPxH );

	if ( glyphIndex <= 0 ) {
		return;
	}

	stbtt_GetGlyphBitmapBoxSubpixel( font, glyphIndex, scaleX, scaleY, 0.0f, baselinePxY, &ix0, &iy0, &ix1, &iy1 );
	gw = ix1 - ix0;
	gh = iy1 - iy0;
	if ( gw <= 0 || gh <= 0 ) {
		return; // no ink in this glyph (e.g. the space cell)
	}

	glyphBuf = ri.Hunk_AllocateTempMemory( gw * gh );
	stbtt_MakeGlyphBitmapSubpixel( font, glyphBuf, gw, gh, gw, scaleX, scaleY, 0.0f, baselinePxY, glyphIndex );
	{
		// Clip per pixel rather than dropping the glyph if it overshoots its cell.
		int x, y;
		for ( y = 0; y < gh; y++ ) {
			int localY = iy0 + y;
			if ( localY < 0 || localY >= glyphPxH ) {
				continue;
			}
			for ( x = 0; x < gw; x++ ) {
				int localX = ix0 + x;
				if ( localX < 0 || localX >= glyphPxW ) {
					continue;
				}
				coverageOut[ ( gutterPx + localY ) * cellPxW + ( gutterPx + localX ) ] = glyphBuf[ y * gw + x ];
			}
		}
	}
	ri.Hunk_FreeTempMemory( glyphBuf );
}

// Baseline offset from the top of a glyphScaleY-scaled cell. glyphScaleY MUST
// be stbtt_ScaleForPixelHeight's scale, not stbtt_ScaleForMappingEmToPixels's.
static float Font_GetBaselinePx( const stbtt_fontinfo *font, float glyphScaleY ) {
	int ascent, descent, lineGap;
	stbtt_GetFontVMetrics( font, &ascent, &descent, &lineGap );
	return (float)ascent * glyphScaleY;
}

static qboolean Font_BakeHudcharsGlyphs( void ) {
	void *ttfBuffer;
	stbtt_fontinfo font;
	float scaleX, scaleY, baselinePxY;
	int cellIndex;
	byte glyphCoverage[FONT_BAKE_PX_PER_CELL * FONT_BAKE_PX_PER_CELL];

	if ( !Font_LoadActiveTtf( &font, &ttfBuffer, "Font_BakeHudcharsGlyphs" ) ) {
		return qfalse;
	}

	// X/Y use different scale denominators deliberately -- reusing Y for X narrows
	// every glyph whenever the font's ascent-descent exceeds its unitsPerEm.
	scaleY = stbtt_ScaleForPixelHeight( &font, (float)FONT_BAKE_GLYPH_PX );
	scaleX = stbtt_ScaleForMappingEmToPixels( &font, (float)FONT_BAKE_GLYPH_PX );
	baselinePxY = Font_GetBaselinePx( &font, scaleY );

	for ( cellIndex = 0; cellIndex < 256; cellIndex++ ) {
		// cellIndex is the Unicode codepoint (RTCW's extended range is
		// Windows-1252-compatible), not a raw glyph index.
		int glyphIndex = stbtt_FindGlyphIndex( &font, cellIndex );
		int row = cellIndex >> 4;
		int col = cellIndex & 15;
		int x, y;

		Font_BakeGlyphCoverage( &font, glyphIndex, scaleX, scaleY, FONT_BAKE_GLYPH_PX, FONT_BAKE_GLYPH_PX, baselinePxY, FONT_BAKE_GUTTER_PX,
								 FONT_BAKE_PX_PER_CELL, FONT_BAKE_PX_PER_CELL, glyphCoverage );

		for ( y = 0; y < FONT_BAKE_PX_PER_CELL; y++ ) {
			for ( x = 0; x < FONT_BAKE_PX_PER_CELL; x++ ) {
				int destX = col * FONT_BAKE_PX_PER_CELL + x;
				int destY = row * FONT_BAKE_PX_PER_CELL + y;
				byte *dest = bakedAtlas + ( destY * FONT_BAKE_ATLAS_PX + destX ) * 4;
				byte cov = glyphCoverage[ y * FONT_BAKE_PX_PER_CELL + x ];

				dest[0] = 255;
				dest[1] = 255;
				dest[2] = 255;
				dest[3] = cov;
			}
		}
	}

	ri.FS_FreeFile( ttfBuffer );
	return qtrue;
}

qboolean Font_BakeHudcharsAtlas( const char *name, byte **pic, int *width, int *height ) {
	*pic = NULL;
	*width = 0;
	*height = 0;

	if ( Q_stricmp( name, hudcharsImageName ) ) {
		return qfalse;
	}
	if ( !Font_VectorFontsEnabled() ) {
		return qfalse; // r_hudFontEnabled 0 -- let R_LoadImage load the real bitmap file instead
	}

	if ( !Font_BakeHudcharsGlyphs() ) {
		return qfalse; // load/bake failed, already warned above -- let the caller fall back
	}
	ri.Printf( PRINT_ALL, "Font_BakeHudcharsAtlas: rebaked %s (%ix%i)\n", hudcharsImageName, FONT_BAKE_ATLAS_PX, FONT_BAKE_ATLAS_PX );

	*pic = bakedAtlas;
	*width = FONT_BAKE_ATLAS_PX;
	*height = FONT_BAKE_ATLAS_PX;
	return qtrue;
}

qboolean Font_IsHudcharsImageName( const char *name ) {
	return !Q_stricmp( name, hudcharsImageName ) ? qtrue : qfalse;
}

// Console atlas: baked at the console's exact physical pixel size (it draws
// at literal device pixels, not cgame's virtual-640 scale), never minified/magnified.
#define CONSOLE_CHAR_CELL_W 8   // SMALLCHAR_WIDTH, src/game/q_shared.h
#define CONSOLE_CHAR_CELL_H 16  // SMALLCHAR_HEIGHT, src/game/q_shared.h
#define CONSOLE_BAKE_MAX_SCALE 4.0f
#define CONSOLE_BAKE_MAX_CELL_W ( (int)( CONSOLE_CHAR_CELL_W * CONSOLE_BAKE_MAX_SCALE ) )
#define CONSOLE_BAKE_MAX_CELL_H ( (int)( CONSOLE_CHAR_CELL_H * CONSOLE_BAKE_MAX_SCALE ) )
#define CONSOLE_BAKE_MAX_ATLAS_W ( FONT_GRID_CELLS * CONSOLE_BAKE_MAX_CELL_W )
#define CONSOLE_BAKE_MAX_ATLAS_H ( FONT_GRID_CELLS * CONSOLE_BAKE_MAX_CELL_H )

static const char *consoleImageName = "gfx/2d/consolechars.tga";

// Sized for the clamped worst case; only a FONT_GRID_CELLS*cellPxW x
// FONT_GRID_CELLS*cellPxH prefix is used for the current con_scale.
static byte consoleBakedAtlas[CONSOLE_BAKE_MAX_ATLAS_W * CONSOLE_BAKE_MAX_ATLAS_H * 4];

static qboolean Font_BakeConsoleGlyphs( int cellPxW, int cellPxH ) {
	void *ttfBuffer;
	stbtt_fontinfo font;
	float scaleX, scaleY, baselinePxY;
	int cellIndex;
	int atlasPxW = FONT_GRID_CELLS * cellPxW;
	byte *glyphCoverage;

	if ( !Font_LoadActiveTtf( &font, &ttfBuffer, "Font_BakeConsoleGlyphs" ) ) {
		return qfalse;
	}

	{
		float unitScaleY = stbtt_ScaleForPixelHeight( &font, 1.0f );
		float unitScaleX = stbtt_ScaleForMappingEmToPixels( &font, 1.0f );
		scaleX = unitScaleX * (float)cellPxW;
		scaleY = unitScaleY * (float)cellPxH;
	}
	baselinePxY = Font_GetBaselinePx( &font, scaleY );

	glyphCoverage = ri.Hunk_AllocateTempMemory( cellPxW * cellPxH );

	for ( cellIndex = 0; cellIndex < 256; cellIndex++ ) {
		int glyphIndex = stbtt_FindGlyphIndex( &font, cellIndex );
		int row = cellIndex >> 4;
		int col = cellIndex & 15;
		int x, y;

		// gutterPx=0: this atlas never gets a mip chain, so no bleed to guard against.
		Font_BakeGlyphCoverage( &font, glyphIndex, scaleX, scaleY, cellPxW, cellPxH, baselinePxY, 0, cellPxW, cellPxH, glyphCoverage );

		for ( y = 0; y < cellPxH; y++ ) {
			for ( x = 0; x < cellPxW; x++ ) {
				int destX = col * cellPxW + x;
				int destY = row * cellPxH + y;
				byte *dest = consoleBakedAtlas + ( destY * atlasPxW + destX ) * 4;
				byte cov = glyphCoverage[ y * cellPxW + x ];

				dest[0] = 255;
				dest[1] = 255;
				dest[2] = 255;
				dest[3] = cov;
			}
		}
	}

	ri.Hunk_FreeTempMemory( glyphCoverage );
	ri.FS_FreeFile( ttfBuffer );
	return qtrue;
}

// Rounds a cell dimension up to a power of two so Upload32 never stretches this atlas.
static int Font_NextPowerOfTwo( int value ) {
	int p = 1;
	while ( p < value ) {
		p <<= 1;
	}
	return p;
}

qboolean Font_BakeConsoleAtlas( const char *name, byte **pic, int *width, int *height ) {
	cvar_t *conScaleCvar;
	float conScale;
	int cellPxW, cellPxH;

	*pic = NULL;
	*width = 0;
	*height = 0;

	if ( Q_stricmp( name, consoleImageName ) ) {
		return qfalse;
	}
	if ( !Font_VectorFontsEnabled() ) {
		return qfalse; // cl_main.c routes disabled mode to gfx/2d/hudchars instead; fail safe anyway
	}

	// Cvar_Get here (not just in cl_console.c) promotes any pending CVAR_LATCH change.
	conScaleCvar = ri.Cvar_Get( "con_scale", "1.0", CVAR_ARCHIVE | CVAR_LATCH );
	conScale = conScaleCvar->value;
	if ( conScale <= 0.0f ) {
		conScale = 1.0f;
	}
	if ( conScale > CONSOLE_BAKE_MAX_SCALE ) {
		ri.Printf( PRINT_WARNING, "Font_BakeConsoleAtlas: con_scale %g exceeds max %g, clamping\n", conScale, CONSOLE_BAKE_MAX_SCALE );
		conScale = CONSOLE_BAKE_MAX_SCALE;
	}

	cellPxW = (int)( CONSOLE_CHAR_CELL_W * conScale + 0.5f );
	cellPxH = (int)( CONSOLE_CHAR_CELL_H * conScale + 0.5f );
	if ( cellPxW < 1 ) { cellPxW = 1; }
	if ( cellPxH < 1 ) { cellPxH = 1; }
	cellPxW = Font_NextPowerOfTwo( cellPxW );
	cellPxH = Font_NextPowerOfTwo( cellPxH );

	if ( !Font_BakeConsoleGlyphs( cellPxW, cellPxH ) ) {
		return qfalse;
	}
	ri.Printf( PRINT_ALL, "Font_BakeConsoleAtlas: rebaked %s (%ix%i per cell, con_scale %g)\n",
			   consoleImageName, cellPxW, cellPxH, conScale );

	*pic = consoleBakedAtlas;
	*width = FONT_GRID_CELLS * cellPxW;
	*height = FONT_GRID_CELLS * cellPxH;
	return qtrue;
}

qboolean Font_IsConsoleImageName( const char *name ) {
	return !Q_stricmp( name, consoleImageName ) ? qtrue : qfalse;
}

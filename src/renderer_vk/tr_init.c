/*
===========================================================================

Return to Castle Wolfenstein multiplayer GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the Return to Castle Wolfenstein multiplayer GPL Source Code (RTCW MP Source Code).  

RTCW MP Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

RTCW MP Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RTCW MP Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the RTCW MP Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the RTCW MP Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// tr_init.c -- functions that are not called every frame

#include "tr_local.h"
#ifdef _WIN32
#include <Windows.h>
#endif

//#ifdef __USEA3D
//// Defined in snd_a3dg_refcommon.c
//void RE_A3D_RenderGeometry (void *pVoidA3D, void *pVoidGeom, void *pVoidMat, void *pVoidGeomStatus);
//#endif

glconfig_t glConfig;
glinfo_t	glInfo;

static void GfxInfo_f( void );

cvar_t  *r_flareSize;
cvar_t  *r_flareFade;

cvar_t  *r_railWidth;
cvar_t  *r_railCoreWidth;
cvar_t  *r_railSegmentLength;

cvar_t  *r_ignoreFastPath;

cvar_t  *r_verbose;
cvar_t  *r_ignore;

cvar_t  *r_displayRefresh;

cvar_t  *r_detailTextures;

cvar_t  *r_znear;
cvar_t  *r_zfar;

cvar_t  *r_skipBackEnd;

cvar_t  *r_inGameVideo;
cvar_t  *r_fastsky;
cvar_t  *r_drawSun;
cvar_t  *r_dynamiclight;
cvar_t  *r_dlightBacks;

cvar_t  *r_lodbias;
cvar_t  *r_lodscale;

cvar_t  *r_norefresh;
cvar_t  *r_drawentities;
cvar_t  *r_drawworld;
cvar_t  *r_speeds;
//cvar_t	*r_fullbright; // JPW NERVE removed per atvi request
cvar_t  *r_novis;
cvar_t  *r_nocull;
cvar_t  *r_facePlaneCull;
cvar_t  *r_showcluster;
cvar_t  *r_nocurves;

cvar_t  *r_allowExtensions;


//----(SA)	added
cvar_t  *r_ext_texture_filter_anisotropic;

//----(SA)	end

cvar_t  *r_logFile;

cvar_t  *r_stereo;
cvar_t  *r_primitives;

cvar_t  *r_lightmap;
cvar_t  *r_vertexLight;
cvar_t  *r_uiFullScreen;
cvar_t  *r_shadows;
cvar_t  *r_portalsky;   //----(SA)	added
cvar_t  *r_flares;
cvar_t  *r_nobind;
cvar_t  *r_singleShader;
cvar_t  *r_roundImagesDown;
cvar_t  *r_colorMipLevels;
cvar_t  *r_picmip;
cvar_t  *r_showtris;
cvar_t  *r_showsky;
cvar_t  *r_shownormals;
cvar_t  *r_finish;
cvar_t  *r_clear;
cvar_t  *r_swapInterval;
cvar_t  *r_textureMode;
cvar_t  *r_gamma;
cvar_t  *r_intensity;
cvar_t  *r_lockpvs;
cvar_t  *r_noportals;
cvar_t  *r_portalOnly;

cvar_t  *r_subdivisions;
cvar_t  *r_lodCurveError;

cvar_t  *r_fullscreen;
cvar_t  *r_fullscreenDesktop;


cvar_t  *r_windowedWidth;
cvar_t  *r_windowedHeight;
cvar_t  *r_fullscreenWidth;
cvar_t  *r_fullscreenHeight;

cvar_t  *r_overBrightBits;
cvar_t  *r_mapOverBrightBits;

cvar_t  *r_debugSurface;
cvar_t  *r_simpleMipMaps;


cvar_t  *r_ambientScale;
cvar_t  *r_directedScale;
cvar_t  *r_debugLight;
cvar_t  *r_debugSort;
cvar_t  *r_printShaders;
cvar_t  *r_saveFontData;

// Ridah
cvar_t  *r_exportCompressedModels;

cvar_t  *r_buildScript;

cvar_t  *r_bonesDebug;
// done.

// Rafael - wolf fog
cvar_t  *r_wolffog;
// done

cvar_t  *r_highQualityVideo;
cvar_t  *r_rmse;

cvar_t  *r_maxpolys;
int max_polys;
cvar_t  *r_maxpolyverts;
int max_polyverts;

cvar_t  *r_gpu;

cvar_t *r_debugUI;
cvar_t *r_debugInput;
cvar_t *r_mipFilter;

cvar_t *r_sleepThreshold;

cvar_t *r_msaa;
cvar_t *r_alphaboost;

//-------------------------------------------------------------------------------
// Ridah, mesh compression
float r_anormals[NUMMDCVERTEXNORMALS][3] = {
#include "anorms256.h"
};


static void AssertCvarRange( cvar_t *cv, float minVal, float maxVal, qboolean shouldBeIntegral ) {
	if ( shouldBeIntegral ) {
		if ( ( int ) cv->value != cv->integer ) {
			ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' must be integral (%f)\n", cv->name, cv->value );
			ri.Cvar_Set( cv->name, va( "%d", cv->integer ) );
		}
	}

	if ( cv->value < minVal ) {
		ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' out of range (%f < %f)\n", cv->name, cv->value, minVal );
		ri.Cvar_Set( cv->name, va( "%f", minVal ) );
	} else if ( cv->value > maxVal )   {
		ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' out of range (%f > %f)\n", cv->name, cv->value, maxVal );
		ri.Cvar_Set( cv->name, va( "%f", maxVal ) );
	}
}



static void InitVulkan( void ) {
	RHI_Init();

	rhiBufferDesc vertexBufferDesc = {};
	vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
	vertexBufferDesc.memoryUsage = RHI_MemoryUsage_Upload;
	

	rhiBufferDesc uniformBufferDesc = {};
	uniformBufferDesc.initialState = RHI_ResourceState_CopySourceBit;
	uniformBufferDesc.memoryUsage = RHI_MemoryUsage_Upload;
	uniformBufferDesc.allowedStates = RHI_ResourceState_CopySourceBit;

	for(int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++){
		uniformBufferDesc.name = va("%s %d", "Scene View Upload", i);
		uniformBufferDesc.byteCount = SCENEVIEW_MAX * sizeof(SceneView);
		backEnd.sceneViewUploadBuffers[i] = RHI_CreateBuffer(&uniformBufferDesc);

		backEnd.commandBuffers[i] = RHI_CreateCommandBuffer(qfalse);

		vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
		vertexBufferDesc.allowedStates = RHI_ResourceState_VertexBufferBit;
		vertexBufferDesc.name = va("%s %d", "Position Buffer", i);
		vertexBufferDesc.byteCount = VBA_MAX * sizeof(tess.xyz[0]);
		backEnd.vertexBuffers[i].position = RHI_CreateBuffer(&vertexBufferDesc);

		vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
		vertexBufferDesc.allowedStates = RHI_ResourceState_VertexBufferBit;
		vertexBufferDesc.name = va("%s %d", "Normal Buffer", i);
		vertexBufferDesc.byteCount = VBA_MAX * sizeof(tess.normal[0]);
		backEnd.vertexBuffers[i].normal = RHI_CreateBuffer(&vertexBufferDesc);

		vertexBufferDesc.initialState = RHI_ResourceState_IndexBufferBit;
		vertexBufferDesc.allowedStates = RHI_ResourceState_IndexBufferBit;
		vertexBufferDesc.name = va("%s %d", "Index Buffer", i);
		vertexBufferDesc.byteCount = IDX_MAX * sizeof(tess.indexes[0]);
		backEnd.vertexBuffers[i].index = RHI_CreateBuffer(&vertexBufferDesc);

		for(int stage = 0; stage < MAX_SHADER_STAGES; stage++){
			vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
			vertexBufferDesc.allowedStates = RHI_ResourceState_VertexBufferBit;
			vertexBufferDesc.name = va("%s %d #%d", "Texture Coordinates Buffer", i, stage+1);
			vertexBufferDesc.byteCount = VBA_MAX * sizeof(tess.texCoords[0][0]);
			backEnd.vertexBuffers[i].textureCoord[stage] = RHI_CreateBuffer(&vertexBufferDesc);

			vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
			vertexBufferDesc.allowedStates = RHI_ResourceState_VertexBufferBit;
			vertexBufferDesc.name = va("%s %d #%d", "Color Buffer", i, stage+1);
			vertexBufferDesc.byteCount = VBA_MAX * sizeof(tess.vertexColors[0]);
			backEnd.vertexBuffers[i].color[stage] = RHI_CreateBuffer(&vertexBufferDesc);
		}
		vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
		vertexBufferDesc.allowedStates = RHI_ResourceState_VertexBufferBit;
		vertexBufferDesc.name = va("%s %d", "Texture Coordinates Buffer LM", i);
		vertexBufferDesc.byteCount = VBA_MAX * sizeof(tess.texCoords[0]);
		backEnd.vertexBuffers[i].textureCoordLM = RHI_CreateBuffer(&vertexBufferDesc);

	}

	uniformBufferDesc.initialState = RHI_ResourceState_CopyDestinationBit;
	uniformBufferDesc.allowedStates = RHI_ResourceState_ShaderInputBit | RHI_ResourceState_CopyDestinationBit;
	uniformBufferDesc.memoryUsage = RHI_MemoryUsage_DeviceLocal;
	uniformBufferDesc.name = "Scene View GPU";
	uniformBufferDesc.byteCount = sizeof(SceneView);
	backEnd.sceneViewGPUBuffer = RHI_CreateBuffer(&uniformBufferDesc);

	rhiBufferDesc shaderIndexBufferDesc = {};
	shaderIndexBufferDesc.initialState = RHI_ResourceState_ShaderReadWriteBit;
	shaderIndexBufferDesc.memoryUsage = RHI_MemoryUsage_DeviceLocal;
	shaderIndexBufferDesc.allowedStates = RHI_ResourceState_ShaderReadWriteBit | RHI_ResourceState_CopySourceBit;
	shaderIndexBufferDesc.byteCount = 8;
	shaderIndexBufferDesc.name = "Shader Index";
	backEnd.shaderIndexBuffer = RHI_CreateBuffer(&shaderIndexBufferDesc);

	rhiBufferDesc shaderIndexReadbackDesc = {};
	shaderIndexReadbackDesc.initialState = RHI_ResourceState_CopyDestinationBit;
	shaderIndexReadbackDesc.memoryUsage = RHI_MemoryUsage_Readback;
	shaderIndexReadbackDesc.allowedStates = RHI_ResourceState_CopyDestinationBit;
	shaderIndexReadbackDesc.byteCount = 8;
	shaderIndexReadbackDesc.name = "Shader Index Readback";
	backEnd.shaderIndexReadbackBuffer = RHI_CreateBuffer(&shaderIndexReadbackDesc);

	backEnd.renderComplete = RHI_CreateTimelineSemaphore(qfalse);
	backEnd.renderCompleteBinary = RHI_CreateBinarySemaphore();
	backEnd.imageAcquiredBinary = RHI_CreateBinarySemaphore();
	backEnd.swapChainTextures = RHI_GetSwapChainImages();
	backEnd.swapChainTextureCount = RHI_GetSwapChainImageCount();

	rhiDescriptorSetLayoutDesc descSetLayoutDesc = {};
	descSetLayoutDesc.name = "Shared Game Textures";
	
	descSetLayoutDesc.bindings[0].descriptorCount = MAX_IMAGEDESCRIPTORS;
	descSetLayoutDesc.bindings[0].descriptorType = RHI_DescriptorType_ReadOnlyTexture;
	descSetLayoutDesc.bindings[0].stageFlags = RHI_PipelineStage_PixelBit;

	descSetLayoutDesc.bindings[1].descriptorCount = 6;
	descSetLayoutDesc.bindings[1].descriptorType = RHI_DescriptorType_Sampler;
	descSetLayoutDesc.bindings[1].stageFlags = RHI_PipelineStage_PixelBit;

	descSetLayoutDesc.bindings[2].descriptorCount = 1;
	descSetLayoutDesc.bindings[2].descriptorType = RHI_DescriptorType_ReadOnlyBuffer;
	descSetLayoutDesc.bindings[2].stageFlags = RHI_PipelineStage_VertexBit | RHI_PipelineStage_PixelBit;

	descSetLayoutDesc.bindings[3].descriptorCount = 1;
	descSetLayoutDesc.bindings[3].descriptorType = RHI_DescriptorType_ReadWriteBuffer;
	descSetLayoutDesc.bindings[3].stageFlags = RHI_PipelineStage_PixelBit;

	descSetLayoutDesc.bindingCount = 4;
	backEnd.descriptorSetLayout = RHI_CreateDescriptorSetLayout(&descSetLayoutDesc);



	backEnd.descriptorSet = RHI_CreateDescriptorSet("Game Textures", backEnd.descriptorSetLayout, qfalse);

	for(int a = 0; a < 2; a++){
		for(int c = 0; c < 2; c++){
			int i = RB_GetSamplerIndex(c, a);
			int anisotropy = a && r_ext_texture_filter_anisotropic->integer > 1 ? r_ext_texture_filter_anisotropic->integer : 1;
			backEnd.sampler[i] = RHI_CreateSampler(va("Sampler C:%d A:%d", c, a), c? RHI_TextureAddressing_Clamp: RHI_TextureAddressing_Repeat, anisotropy);
		}

	}
	
	RHI_UpdateDescriptorSet(backEnd.descriptorSet, 1, RHI_DescriptorType_Sampler, 0, ARRAY_LEN(backEnd.sampler), backEnd.sampler, 0);
	RHI_UpdateDescriptorSet(backEnd.descriptorSet, 2, RHI_DescriptorType_ReadOnlyBuffer, 0, 1, &backEnd.sceneViewGPUBuffer, 0);
	RHI_UpdateDescriptorSet(backEnd.descriptorSet, 3, RHI_DescriptorType_ReadWriteBuffer, 0, 1, &backEnd.shaderIndexBuffer, 0);

	rhiTextureDesc depthTextureDesc = {};
	depthTextureDesc.allowedStates = RHI_ResourceState_DepthWriteBit;
	depthTextureDesc.format = D32_SFloat;
	depthTextureDesc.height = glConfig.vidHeight;
	depthTextureDesc.initialState = RHI_ResourceState_DepthWriteBit;
	depthTextureDesc.longLifetime = qfalse;
	depthTextureDesc.mipCount = 1;
	depthTextureDesc.name = "Depth Buffer";
	depthTextureDesc.sampleCount = 1;
	depthTextureDesc.width = glConfig.vidWidth;
	backEnd.depthBuffer = RHI_CreateTexture(&depthTextureDesc);


	if(RB_IsMSAARequested()){
		depthTextureDesc.name = "Depth Buffer MS";
		depthTextureDesc.sampleCount = RB_GetMSAASampleCount();
		backEnd.depthBufferMS = RHI_CreateTexture(&depthTextureDesc);
	}
	

	rhiTextureDesc colorTextureDesc = {};
	colorTextureDesc.allowedStates = RHI_ResourceState_RenderTargetBit | RHI_ResourceState_ShaderInputBit | RHI_ResourceState_CopySourceBit | RHI_ResourceState_ShaderReadWriteBit;
	colorTextureDesc.format = R8G8B8A8_UNorm;
	colorTextureDesc.height = glConfig.vidHeight;
	colorTextureDesc.initialState = RHI_ResourceState_RenderTargetBit;
	colorTextureDesc.longLifetime = qfalse;
	colorTextureDesc.mipCount = 1;
	colorTextureDesc.name = "Color Buffer";
	colorTextureDesc.sampleCount = 1;
	colorTextureDesc.width = glConfig.vidWidth;
	backEnd.colorBuffers[0] = RHI_CreateTexture(&colorTextureDesc);
	colorTextureDesc.name = "Color Buffer 2";
	backEnd.colorBuffers[1] = RHI_CreateTexture(&colorTextureDesc);

	if(RB_IsMSAARequested()){
		colorTextureDesc.name = "Color Buffer MS";
		colorTextureDesc.sampleCount = RB_GetMSAASampleCount();
		colorTextureDesc.allowedStates = RHI_ResourceState_RenderTargetBit | RHI_ResourceState_ShaderInputBit | RHI_ResourceState_CopySourceBit;
		backEnd.colorBufferMS = RHI_CreateTexture(&colorTextureDesc);
	}
	
	RB_InitGamma();
	RB_InitBlit();
	RB_MSAA_Init();
	RB_ImGUI_Init();
	RB_CreateDynamicLightPipelines();
	
}




/*
==============================================================================

						SCREEN SHOTS

==============================================================================
*/

/*
==================
R_TakeScreenshot
==================
*/
void R_TakeScreenshot(char *fileName ) {
	byte        *buffer;
	
	int i, c, temp;
	int width = glConfig.vidWidth;
	int height = glConfig.vidHeight;


	buffer = ri.Hunk_AllocateTempMemory( glConfig.vidWidth * glConfig.vidHeight * 4 );
	
	byte tgaHeader[18] = {};
	tgaHeader[2] = 2;      // uncompressed type
	tgaHeader[12] = width & 255;
	tgaHeader[13] = width >> 8;
	tgaHeader[14] = height & 255;
	tgaHeader[15] = height >> 8;
	tgaHeader[16] = 32;    // pixel size

	RHI_Screenshot( buffer, backEnd.colorBuffer );
	//swap rgb to bgr
	c = width * height * 4;
	for ( i = 0 ; i < c ; i += 4 ) {
		temp = buffer[i];
		buffer[i] = buffer[i + 2];
		buffer[i + 2] = temp;
		buffer[i + 3] = 255;
	}

	fileHandle_t f = ri.FS_FOpenFileWrite( fileName );
	if ( f ) {
		ri.FS_Write( tgaHeader, sizeof(tgaHeader), f );
		ri.FS_Write( buffer, width * height * 4, f );
		ri.FS_FCloseFile( f );
	} else {
		ri.Printf( PRINT_WARNING, "Failed to open %s\n", fileName );
	}

	ri.Hunk_FreeTempMemory( buffer );
}

/*
==============
R_TakeScreenshotJPEG
==============
*/
void R_TakeScreenshotJPEG(char *fileName ) {
	byte        *buffer;

	buffer = ri.Hunk_AllocateTempMemory( glConfig.vidWidth * glConfig.vidHeight * 4 );

	RHI_Screenshot( buffer, backEnd.colorBuffer );


	ri.FS_WriteFile( fileName, buffer, 1 );     // create path
	SaveJPG( fileName, 95, glConfig.vidWidth, glConfig.vidHeight, buffer );

	ri.Hunk_FreeTempMemory( buffer );
}

/*
==================
R_ScreenshotFilename
==================
*/
void R_ScreenshotFilename( int lastNumber, char *fileName ) {
	int a,b,c,d;

	if ( lastNumber < 0 || lastNumber > 9999 ) {
		Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot9999.tga" );
		return;
	}

	a = lastNumber / 1000;
	lastNumber -= a * 1000;
	b = lastNumber / 100;
	lastNumber -= b * 100;
	c = lastNumber / 10;
	lastNumber -= c * 10;
	d = lastNumber;

	Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot%i%i%i%i.tga"
				 , a, b, c, d );
}

/*
==============
R_ScreenshotFilenameJPEG
==============
*/
void R_ScreenshotFilenameJPEG( int lastNumber, char *fileName ) {
	int a,b,c,d;

	if ( lastNumber < 0 || lastNumber > 9999 ) {
		Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot9999.jpg" );
		return;
	}

	a = lastNumber / 1000;
	lastNumber -= a * 1000;
	b = lastNumber / 100;
	lastNumber -= b * 100;
	c = lastNumber / 10;
	lastNumber -= c * 10;
	d = lastNumber;

	Com_sprintf( fileName, MAX_OSPATH, "screenshots/shot%i%i%i%i.jpg"
				 , a, b, c, d );
}

/*
====================
R_LevelShot

levelshots are specialized 128*128 thumbnails for
the menu system, sampled down from full screen distorted images
====================
*/
void R_LevelShot( void ) {
	//@TODO 
}

/*
==================
R_ScreenShot_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
==================
*/
void R_ScreenShot_f( void ) {
	char checkname[MAX_OSPATH];
	int len;
	static int lastNumber = -1;
	qboolean silent;

	if ( !strcmp( ri.Cmd_Argv( 1 ), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv( 1 ), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, MAX_OSPATH, "screenshots/%s.tga", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ ) {
			R_ScreenshotFilename( lastNumber, checkname );

			len = ri.FS_ReadFile( checkname, NULL );
			if ( len <= 0 ) {
				break;  // file doesn't exist
			}
		}

		if ( lastNumber >= 9999 ) {
			ri.Printf( PRINT_ALL, "ScreenShot: Couldn't create a file\n" );
			return;
		}

		lastNumber++;
	}


	R_TakeScreenshot( checkname );

	if ( !silent ) {
		ri.Printf( PRINT_ALL, "Wrote %s\n", checkname );
	}
}

/*
================
RTCWPro - reqSS
================
*/
void R_GenerateSS_f(char* filename) {
	char filepath[MAX_OSPATH];

	Com_sprintf(filepath, sizeof(filepath), "screenshots/%s.jpg", filename);
	R_TakeScreenshotJPEG(filepath);
}

void R_ScreenShotJPEG_f( void ) {
	char checkname[MAX_OSPATH];
	int len;
	static int lastNumber = -1;
	qboolean silent;

	if ( !strcmp( ri.Cmd_Argv( 1 ), "levelshot" ) ) {
		R_LevelShot();
		return;
	}

	if ( !strcmp( ri.Cmd_Argv( 1 ), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if (!strcmp(ri.Cmd_Argv(1), "reqss")) {
		if (strlen(ri.Cmd_Argv(2))) {
			R_GenerateSS_f(ri.Cmd_Argv(2));
		}
		return;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		Com_sprintf( checkname, MAX_OSPATH, "screenshots/%s.jpg", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ ) {
			R_ScreenshotFilenameJPEG( lastNumber, checkname );

			len = ri.FS_ReadFile( checkname, NULL );
			if ( len <= 0 ) {
				break;  // file doesn't exist
			}
		}

		if ( lastNumber == 10000 ) {
			ri.Printf( PRINT_ALL, "ScreenShot: Couldn't create a file\n" );
			return;
		}

		lastNumber++;
	}


	R_TakeScreenshotJPEG( checkname );

	if ( !silent ) {
		ri.Printf( PRINT_ALL, "Wrote %s\n", checkname );
	}
}



/*
================
GfxInfo_f
================
*/
void GfxInfo_f( void ) {
	cvar_t *sys_cpustring = ri.Cvar_Get( "sys_cpustring", "", 0 );
	const char *enablestrings[] =
	{
		"disabled",
		"enabled"
	};
	const char *fsstrings[] =
	{
		"windowed",
		"fullscreen"
	};

	ri.Printf( PRINT_ALL, "\nGL_VENDOR: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_ALL, "GL_VERSION: %s\n", glConfig.version_string );
	ri.Printf( PRINT_ALL, "GL_EXTENSIONS: %s\n", glConfig.extensions_string );
	ri.Printf( PRINT_ALL, "GL_MAX_TEXTURE_SIZE: %d\n", glConfig.maxTextureSize );
	ri.Printf( PRINT_ALL, "GL_MAX_ACTIVE_TEXTURES_ARB: %d\n", glConfig.maxActiveTextures );
	ri.Printf( PRINT_ALL, "\nPIXELFORMAT: color(%d-bits) Z(%d-bit) stencil(%d-bits)\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );
	ri.Printf( PRINT_ALL, "Resolution: %d x %d %s ", glConfig.vidWidth, glConfig.vidHeight, fsstrings[r_fullscreen->integer == 1] );
	if ( glConfig.displayFrequency ) {
		ri.Printf( PRINT_ALL, "%d hz\n", glConfig.displayFrequency );
	}
	else {
		ri.Printf(PRINT_ALL, "\n");
	}
	if ( glConfig.deviceSupportsGamma ) {
		ri.Printf( PRINT_ALL, "GAMMA: hardware w/ %d overbright bits\n", tr.overbrightBits );
	} else
	{
		ri.Printf( PRINT_ALL, "GAMMA: software w/ %d overbright bits\n", tr.overbrightBits );
	}
	ri.Printf( PRINT_ALL, "CPU: %s\n", sys_cpustring->string );


	ri.Printf( PRINT_ALL, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_ALL, "picmip: %d\n", r_picmip->integer );
	ri.Printf( PRINT_ALL, "texenv add: %s\n", enablestrings[glConfig.textureEnvAddAvailable != 0] );
	ri.Printf( PRINT_ALL, "compressed textures: %s\n", enablestrings[glConfig.textureCompression != TC_NONE] );

	ri.Printf( PRINT_ALL, "NV distance fog: %s\n", enablestrings[glConfig.NVFogAvailable != 0] );


	if ( r_vertexLight->integer || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		ri.Printf( PRINT_ALL, "HACK: using vertex lightmap approximation\n" );
	}
	if ( glConfig.hardwareType == GLHW_RAGEPRO ) {
		ri.Printf( PRINT_ALL, "HACK: ragePro approximations\n" );
	}
	if ( glConfig.hardwareType == GLHW_RIVA128 ) {
		ri.Printf( PRINT_ALL, "HACK: riva128 approximations\n" );
	}
	if ( r_finish->integer ) {
		ri.Printf( PRINT_ALL, "Forcing glFinish\n" );
	}
}

/*
===============
R_Register
===============
*/
void R_Register( void ) {
	//
	// latched and archived variables
	r_ext_texture_filter_anisotropic    = ri.Cvar_Get( "r_anisotropy", "16", CVAR_ARCHIVE | CVAR_LATCH );


	r_picmip = ri.Cvar_Get( "r_picmip", "1", CVAR_ARCHIVE | CVAR_LATCH ); //----(SA)	mod for DM and DK for id build.  was "1" // JPW NERVE pushed back to 1
	r_roundImagesDown = ri.Cvar_Get( "r_roundImagesDown", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_rmse = ri.Cvar_Get( "r_rmse", "0.0", CVAR_ARCHIVE | CVAR_LATCH );
	r_colorMipLevels = ri.Cvar_Get( "r_colorMipLevels", "0", CVAR_LATCH );
	AssertCvarRange( r_picmip, 0, 16, qtrue );
	r_detailTextures = ri.Cvar_Get( "r_detailtextures", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereo = ri.Cvar_Get( "r_stereo", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_overBrightBits = ri.Cvar_Get( "r_overBrightBits", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullscreen = ri.Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullscreenDesktop = ri.Cvar_Get( "r_fullscreenDesktop", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_windowedWidth = ri.Cvar_Get( "r_windowedWidth", "1280", CVAR_ARCHIVE | CVAR_LATCH );
	r_windowedHeight = ri.Cvar_Get( "r_windowedHeight", "720", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullscreenWidth = ri.Cvar_Get( "r_fullscreenWidth", "1920", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullscreenHeight = ri.Cvar_Get( "r_fullscreenHeight", "1080", CVAR_ARCHIVE | CVAR_LATCH );
	r_simpleMipMaps = ri.Cvar_Get( "r_simpleMipMaps", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_vertexLight = ri.Cvar_Get( "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_uiFullScreen = ri.Cvar_Get( "r_uifullscreen", "0", 0 );
	r_subdivisions = ri.Cvar_Get( "r_subdivisions", "4", CVAR_ARCHIVE | CVAR_LATCH );
	r_ignoreFastPath = ri.Cvar_Get( "r_ignoreFastPath", "1", CVAR_ARCHIVE | CVAR_LATCH );

	//
	// temporary latched variables that can only change over a restart
	//
	r_displayRefresh = ri.Cvar_Get( "r_displayRefresh", "0", CVAR_LATCH );
	AssertCvarRange( r_displayRefresh, 0, 200, qtrue );
//	r_fullbright = ri.Cvar_Get ("r_fullbright", "0", CVAR_LATCH|CVAR_CHEAT ); // JPW NERVE removed per atvi request
	r_mapOverBrightBits = ri.Cvar_Get( "r_mapOverBrightBits", "2", CVAR_LATCH );
	r_intensity = ri.Cvar_Get( "r_intensity", "1", CVAR_LATCH );
	r_singleShader = ri.Cvar_Get( "r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get( "r_lodCurveError", "250", CVAR_ARCHIVE );
	r_lodbias = ri.Cvar_Get( "r_lodbias", "0", CVAR_ARCHIVE );
	r_flares = ri.Cvar_Get( "r_flares", "1", CVAR_ARCHIVE );
	r_znear = ri.Cvar_Get( "r_znear", "4", CVAR_CHEAT );
	AssertCvarRange( r_znear, 0.001f, 200, qtrue );
//----(SA)	added
	r_zfar = ri.Cvar_Get( "r_zfar", "0", CVAR_CHEAT );
//----(SA)	end
	r_fastsky = ri.Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE );
	r_inGameVideo = ri.Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE );
	r_drawSun = ri.Cvar_Get( "r_drawSun", "1", CVAR_ARCHIVE );
	r_dynamiclight = ri.Cvar_Get( "r_dynamiclight", "1", CVAR_ARCHIVE );
	r_dlightBacks = ri.Cvar_Get( "r_dlightBacks", "1", CVAR_ARCHIVE );
	r_finish = ri.Cvar_Get( "r_finish", "0", CVAR_ARCHIVE );
	r_textureMode = ri.Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
	r_swapInterval = ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE );
	r_gamma = ri.Cvar_Get( "r_gamma", "1.3", CVAR_ARCHIVE );

	r_facePlaneCull = ri.Cvar_Get( "r_facePlaneCull", "1", CVAR_ARCHIVE );

	r_railWidth = ri.Cvar_Get( "r_railWidth", "16", CVAR_ARCHIVE );
	r_railCoreWidth = ri.Cvar_Get( "r_railCoreWidth", "1", CVAR_ARCHIVE );
	r_railSegmentLength = ri.Cvar_Get( "r_railSegmentLength", "32", CVAR_ARCHIVE );

	r_ambientScale = ri.Cvar_Get( "r_ambientScale", "0.5", CVAR_CHEAT );
	r_directedScale = ri.Cvar_Get( "r_directedScale", "1", CVAR_CHEAT );

	//
	// temporary variables that can change at any time
	//

	r_debugLight = ri.Cvar_Get( "r_debuglight", "0", CVAR_TEMP );
	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );
	r_printShaders = ri.Cvar_Get( "r_printShaders", "0", 0 );
	r_saveFontData = ri.Cvar_Get( "r_saveFontData", "0", 0 );


	r_exportCompressedModels = ri.Cvar_Get( "r_exportCompressedModels", "0", 0 ); // saves compressed models
	r_buildScript = ri.Cvar_Get( "com_buildscript", "0", 0 );
	r_bonesDebug = ri.Cvar_Get( "r_bonesDebug", "0", CVAR_CHEAT );
	// done.

	// Rafael - wolf fog
	r_wolffog = ri.Cvar_Get( "r_wolffog", "1", CVAR_CHEAT ); // JPW NERVE cheat protected per id request
	// done

	r_nocurves = ri.Cvar_Get( "r_nocurves", "0", CVAR_CHEAT );
	r_drawworld = ri.Cvar_Get( "r_drawworld", "1", CVAR_CHEAT );
	r_lightmap = ri.Cvar_Get( "r_lightmap", "0", CVAR_CHEAT ); // DHM - NERVE :: cheat protect
	r_portalOnly = ri.Cvar_Get( "r_portalOnly", "0", CVAR_CHEAT );

	r_flareSize = ri.Cvar_Get( "r_flareSize", "40", CVAR_CHEAT );
	ri.Cvar_Set( "r_flareFade", "5" ); // to force this when people already have "7" in their config
	r_flareFade = ri.Cvar_Get( "r_flareFade", "5", CVAR_CHEAT );
	r_skipBackEnd = ri.Cvar_Get( "r_skipBackEnd", "0", CVAR_CHEAT );

	r_lodscale = ri.Cvar_Get( "r_lodscale", "5", CVAR_CHEAT );
	r_norefresh = ri.Cvar_Get( "r_norefresh", "0", CVAR_CHEAT );
	r_drawentities = ri.Cvar_Get( "r_drawentities", "1", CVAR_CHEAT );
	r_ignore = ri.Cvar_Get( "r_ignore", "1", CVAR_CHEAT );
	r_nocull = ri.Cvar_Get( "r_nocull", "0", CVAR_CHEAT );
	r_novis = ri.Cvar_Get( "r_novis", "0", CVAR_CHEAT );
	r_showcluster = ri.Cvar_Get( "r_showcluster", "0", CVAR_CHEAT );
	r_speeds = ri.Cvar_Get( "r_speeds", "0", CVAR_CHEAT );
	r_verbose = ri.Cvar_Get( "r_verbose", "0", CVAR_CHEAT );
	r_logFile = ri.Cvar_Get( "r_logFile", "0", CVAR_CHEAT );
	r_debugSurface = ri.Cvar_Get( "r_debugSurface", "0", CVAR_CHEAT );
	r_nobind = ri.Cvar_Get( "r_nobind", "0", CVAR_CHEAT );
	r_showtris = ri.Cvar_Get( "r_showtris", "0", CVAR_CHEAT );
	r_showsky = ri.Cvar_Get( "r_showsky", "0", CVAR_CHEAT );
	r_shownormals = ri.Cvar_Get( "r_shownormals", "0", CVAR_CHEAT );
	r_clear = ri.Cvar_Get( "r_clear", "0", CVAR_CHEAT );
	r_lockpvs = ri.Cvar_Get( "r_lockpvs", "0", CVAR_CHEAT );
	r_noportals = ri.Cvar_Get( "r_noportals", "0", CVAR_CHEAT );
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0 );
	r_shadows = ri.Cvar_Get( "cg_shadows", "1", 0 );
	r_portalsky = ri.Cvar_Get( "cg_skybox", "1", 0 );

	r_maxpolys = ri.Cvar_Get( "r_maxpolys", va( "%d", MAX_POLYS ), 0 );
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", va( "%d", MAX_POLYVERTS ), 0 );

	r_highQualityVideo = ri.Cvar_Get( "r_highQualityVideo", "1", CVAR_ARCHIVE );

	r_gpu = ri.Cvar_Get( "r_gpu", "0", CVAR_ARCHIVE | CVAR_LATCH);

	r_debugUI = ri.Cvar_Get( "r_debugUI", "0", CVAR_TEMP);
	r_debugInput = ri.Cvar_Get( "r_debugInput", "0", CVAR_TEMP);

	r_mipFilter = ri.Cvar_Get( "r_mipFilter", "1", CVAR_ARCHIVE | CVAR_LATCH);

	r_sleepThreshold = ri.Cvar_Get("r_sleepThreshold", "2500", CVAR_ARCHIVE);

	r_msaa = ri.Cvar_Get("r_msaa", "8", CVAR_ARCHIVE | CVAR_LATCH);
	r_alphaboost = ri.Cvar_Get("r_alphaboost", "1.0", CVAR_ARCHIVE);

	// make sure all the commands added here are also
	// removed in R_Shutdown
	ri.Cmd_AddCommand( "imagelist", R_ImageList_f );
	ri.Cmd_AddCommand( "shaderlist", R_ShaderList_f );
	ri.Cmd_AddCommand( "skinlist", R_SkinList_f );
	ri.Cmd_AddCommand( "modellist", R_Modellist_f );
	ri.Cmd_AddCommand( "screenshot", R_ScreenShot_f );
	ri.Cmd_AddCommand( "screenshotJPEG", R_ScreenShotJPEG_f );
	ri.Cmd_AddCommand( "gfxinfo", GfxInfo_f );
	ri.Cmd_AddCommand("printpools", RHI_PrintPools);
	ri.Cmd_AddCommand("gpulist", R_Gpulist_f);

	// Ridah
	{
		void R_CropImages_f( void );
		ri.Cmd_AddCommand( "cropimages", R_CropImages_f );
	}
	// done.
}


//---------------------------------------------------------------------------
// Virtual Memory, used for model caching, since we can't allocate them
// in the main Hunk (since it gets cleared on level changes), and they're
// too large to go into the Zone, we have a special memory chunk just for
// caching models in between levels.
//
// Optimized for Win32 systems, so that they'll grow the swapfile at startup
// if needed, but won't actually commit it until it's needed.
//
// GOAL: reserve a big chunk of virtual memory for the media cache, and only
// use it when we actually need it. This will make sure the swap file grows
// at startup if needed, rather than each allocation we make.
byte    *membase = NULL;
int hunkmaxsize;
int cursize;

#define R_HUNK_MEGS     24
#define R_HUNK_SIZE     ( R_HUNK_MEGS*1024*1024 )

void *R_Hunk_Begin( void ) {
	int maxsize = R_HUNK_SIZE;

	//Com_Printf("R_Hunk_Begin\n");

	// reserve a huge chunk of memory, but don't commit any yet
	cursize = 0;
	hunkmaxsize = maxsize;

#ifdef _WIN32

	// this will "reserve" a chunk of memory for use by this application
	// it will not be "committed" just yet, but the swap file will grow
	// now if needed
	membase = VirtualAlloc( NULL, maxsize, MEM_RESERVE, PAGE_NOACCESS );

#else

	// show_bug.cgi?id=440
	// if not win32, then just allocate it now
	// it is possible that we have been allocated already, in case we don't do anything
	if ( !membase ) {
		membase = malloc( maxsize );
		// TTimo NOTE: initially, I was doing the memset even if we had an existing membase
		// but this breaks some shaders (i.e. /map mp_beach, then go back to the main menu .. some shaders are missing)
		// I assume the shader missing is because we don't clear memory either on win32
		// meaning even on win32 we are using memory that is still reserved but was uncommited .. it works out of pure luck
		memset( membase, 0, maxsize );
	}

#endif

	if ( !membase ) {
		ri.Error( ERR_DROP, "R_Hunk_Begin: reserve failed" );
	}

	return (void *)membase;
}

// this is only called when we shutdown GL
void R_Hunk_End( void ) {
	//Com_Printf("R_Hunk_End\n");

	if ( membase ) {
#ifdef _WIN32
		VirtualFree( membase, 0, MEM_RELEASE );
#else
		free( membase );
#endif
	}

	membase = NULL;
}

/*
===============
R_Init
===============
*/
void R_Init( void ) {
	int i;

	ri.Printf( PRINT_ALL, "----- R_Init -----\n" );

	// clear all our internal state
	memset( &tr, 0, sizeof( tr ) );
	memset( &backEnd, 0, sizeof( backEnd ) );
	memset( &tess, 0, sizeof( tess ) );


	if ( (intptr_t)tess.xyz & 15 ) {
		Com_Printf( "WARNING: tess.xyz not 16 byte aligned\n" );
	}
	memset( tess.constantColor255, 255, sizeof( tess.constantColor255 ) );

	//
	// init function tables
	//
	for ( i = 0; i < FUNCTABLE_SIZE; i++ )
	{
		tr.sinTable[i]      = sin( DEG2RAD( i * 360.0f / ( ( float ) ( FUNCTABLE_SIZE - 1 ) ) ) );
		tr.squareTable[i]   = ( i < FUNCTABLE_SIZE / 2 ) ? 1.0f : -1.0f;
		tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];

		if ( i < FUNCTABLE_SIZE / 2 ) {
			if ( i < FUNCTABLE_SIZE / 4 ) {
				tr.triangleTable[i] = ( float ) i / ( FUNCTABLE_SIZE / 4 );
			} else
			{
				tr.triangleTable[i] = 1.0f - tr.triangleTable[i - FUNCTABLE_SIZE / 4];
			}
		} else
		{
			tr.triangleTable[i] = -tr.triangleTable[i - FUNCTABLE_SIZE / 2];
		}
	}

	// Ridah, init the virtual memory
	R_Hunk_Begin();

	R_InitFogTable();

	R_NoiseInit();

	R_Register();

	max_polys = r_maxpolys->integer;
	if ( max_polys < MAX_POLYS ) {
		max_polys = MAX_POLYS;
	}

	max_polyverts = r_maxpolyverts->integer;
	if ( max_polyverts < MAX_POLYVERTS ) {
		max_polyverts = MAX_POLYVERTS;
	}

	backEndData = ri.Hunk_Alloc( sizeof( *backEndData ) + sizeof( srfPoly_t ) * max_polys + sizeof( polyVert_t ) * max_polyverts, h_low );

	InitVulkan();

	glConfig.anisotropicAvailable = qtrue;
	glConfig.ATIMaxTruformTess = qfalse;
	glConfig.ATINormalMode = qfalse;
	glConfig.ATIPointMode = qfalse;
	glConfig.colorBits = 32;
	glConfig.depthBits = 32;
	glConfig.deviceSupportsGamma = qtrue;
	glConfig.displayFrequency = 0; //@TODO
	glConfig.driverType = GLDRV_STANDALONE; //@TODO
	glConfig.hardwareType = GLHW_GENERIC;
	glConfig.isFullscreen = r_fullscreen->integer;
	glConfig.maxActiveTextures = MAX_DRAWIMAGES;
	glConfig.maxAnisotropy = 16;
	glConfig.maxTextureSize = 2048;
	glConfig.NVFogAvailable = qfalse;
	glConfig.NVFogMode = qfalse;
	glConfig.textureCompression = TC_NONE;
	glConfig.stencilBits = 0;
	glConfig.smpActive = qfalse;
	glConfig.stereoEnabled = r_stereo->integer;
	glConfig.textureEnvAddAvailable = qfalse;
	glConfig.textureFilterAnisotropicAvailable = qtrue;

	GfxInfo_f();

	R_InitImages();

	R_InitShaders();

	R_InitSkins();

	R_ModelInit();

	R_InitFreeType();



	ri.Printf( PRINT_ALL, "----- finished R_Init -----\n" );


}

/*
===============
RE_Shutdown
===============
*/
void RE_Shutdown( qboolean destroyWindow ) {

	ri.Printf( PRINT_ALL, "RE_Shutdown( %i )\n", destroyWindow );

	ri.Cmd_RemoveCommand( "modellist" );
	ri.Cmd_RemoveCommand( "screenshotJPEG" );
	ri.Cmd_RemoveCommand( "screenshot" );
	ri.Cmd_RemoveCommand( "imagelist" );
	ri.Cmd_RemoveCommand( "shaderlist" );
	ri.Cmd_RemoveCommand( "skinlist" );
	ri.Cmd_RemoveCommand( "gfxinfo" );
	ri.Cmd_RemoveCommand( "modelist" );
	ri.Cmd_RemoveCommand( "shaderstate" );
	ri.Cmd_RemoveCommand( "taginfo" );

	// Ridah
	ri.Cmd_RemoveCommand( "cropimages" );
	// done.

	if ( tr.registered ) {
		R_DeleteTextures();
	}

	R_DoneFreeType();

	// shut down platform specific stuff
	if ( destroyWindow ) {
		// Ridah, release the virtual memory
		R_Hunk_End();
		R_FreeImageBuffer();
		ri.Tag_Free();  // wipe all render alloc'd zone memory
	}

	RHI_Shutdown(destroyWindow);
	R_ClearFrame();
	RB_ClearPipelineCache();
	tr.registered = qfalse;
}




/*
=============
RE_EndRegistration

Touch all images to make sure they are resident
=============
*/
void RE_EndRegistration( void ) {
}

void R_ComputeCursorPosition( int* x, int* y )
{ 
	if(r_fullscreenDesktop->integer == 0 && r_fullscreen->integer == 1){
		*x += (glConfig.vidWidth - glInfo.winWidth)/2;
		*y += (glConfig.vidHeight - glInfo.winHeight)/2;
	}
}

/*
** RE_BeginRegistration
*/
void RE_BeginRegistration( glconfig_t *glconfigOut ) {
	ri.Hunk_Clear();    // (SA) MEM NOTE: not in missionpack

	R_Init();
	*glconfigOut = glConfig;


	tr.viewCluster = -1;        // force markleafs to regenerate
	R_ClearFlares();
	RE_ClearScene();

	tr.registered = qtrue;

}


/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t *GetRefAPI( int apiVersion, refimport_t *rimp ) {
	static refexport_t re;

	ri = *rimp;

	memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		ri.Printf( PRINT_ALL, "Mismatched REF_API_VERSION: expected %i, got %i\n",
				   REF_API_VERSION, apiVersion );
		return NULL;
	}

	// the RE_ functions are Renderer Entry points

	re.Shutdown = RE_Shutdown;

	re.BeginRegistration = RE_BeginRegistration;
	//re.RegisterModel    = RE_RegisterModel;
	re.RegisterSkin     = RE_RegisterSkin;
//----(SA) added
	re.GetSkinModel         = RE_GetSkinModel;
	re.GetShaderFromModel   = RE_GetShaderFromModel;
//----(SA) end
	re.RegisterShader   = RE_RegisterShader;
	re.RegisterShaderNoMip = RE_RegisterShaderNoMip;
	re.LoadWorld        = RE_LoadWorldMap;
	re.SetWorldVisData  = RE_SetWorldVisData;
	re.EndRegistration  = RE_EndRegistration;

	re.BeginFrame       = RE_BeginFrame;
	re.EndFrame         = RE_EndFrame;

	re.MarkFragments    = R_MarkFragments;
	//re.LerpTag          = R_LerpTag;
	re.ModelBounds      = R_ModelBounds;

	re.ClearScene       = RE_ClearScene;
	re.AddRefEntityToScene = RE_AddRefEntityToScene;
	re.AddPolyToScene   = RE_AddPolyToScene;
	// Ridah
	re.AddPolysToScene  = RE_AddPolysToScene;
	// done.
	re.AddLightToScene  = RE_AddLightToScene;
//----(SA)
	re.AddCoronaToScene = RE_AddCoronaToScene;
	re.SetFog           = R_SetFog;
//----(SA)
	re.RenderScene      = RE_RenderScene;

	re.SetColor         = RE_SetColor;
	re.DrawStretchPic   = RE_StretchPic;
	re.DrawRotatedPic   = RE_RotatedPic;        // NERVE - SMF
	re.DrawStretchPicGradient   = RE_StretchPicGradient;
	re.DrawStretchRaw   = RE_StretchRaw;
	re.UploadCinematic  = RE_UploadCinematic;
	re.RegisterFont     = RE_RegisterFont;
	re.RemapShader      = R_RemapShader;
	re.GetEntityToken   = R_GetEntityToken;
	re.ConfigureVideoMode = RE_ConfigureVideoMode;
	re.ComputeCursorPosition = R_ComputeCursorPosition;
	re.IsFrameSleepEnabled = RHI_IsFrameSleepEnabled;

	return &re;
}


void RE_ConfigureVideoMode( int desktopWidth, int desktopHeight )
{
	glInfo.winFullscreen = !!r_fullscreen->integer;

#if 0
	glInfo.vidFullscreen = r_fullscreen->integer && r_mode->integer == VIDEOMODE_CHANGE;

	if (r_fullscreen->integer && r_mode->integer == VIDEOMODE_DESKTOPRES) {
		glConfig.vidWidth = desktopWidth;
		glConfig.vidHeight = desktopHeight;
		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
		glInfo.winWidth = desktopWidth;
		glInfo.winHeight = desktopHeight;
		return;
	}

	if (r_fullscreen->integer && r_mode->integer == VIDEOMODE_UPSCALE) {
		glConfig.vidWidth = r_windowedWidth->integer;
		glConfig.vidHeight = r_windowedHeight->integer;
		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
		glInfo.winWidth = desktopWidth;
		glInfo.winHeight = desktopHeight;
		return;
	}
#endif	

	if (r_fullscreenDesktop->integer && r_fullscreen->integer) {
		glConfig.vidWidth = desktopWidth;
		glConfig.vidHeight = desktopHeight;
		glInfo.winWidth = desktopWidth;
		glInfo.winHeight = desktopHeight;
		return;
	}

	if (!r_fullscreenDesktop->integer && r_fullscreen->integer) {
		glConfig.vidWidth = r_fullscreenWidth->integer;
		glConfig.vidHeight = r_fullscreenHeight->integer;
		glInfo.winWidth = desktopWidth;
		glInfo.winHeight = desktopHeight;
		return;
	}

	glConfig.vidWidth = r_windowedWidth->integer;
	glConfig.vidHeight = r_windowedHeight->integer;
	glInfo.winWidth = r_windowedWidth->integer;
	glInfo.winHeight = r_windowedHeight->integer;
}

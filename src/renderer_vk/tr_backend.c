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

#include "tr_local.h"
#include "../client/cl_imgui.h"
#include "../client/cl_profiler.h"

backEndData_t   *backEndData;
backEndState_t backEnd;

int totalPipelines = 0;


static void RHI_SubmitGraphicsDesc_Signal(rhiSubmitGraphicsDesc *graphicsDesc, rhiSemaphore semaphore, uint64_t semaphoreValue){
    assert(graphicsDesc->signalSemaphoreCount < ARRAY_LEN(graphicsDesc->signalSemaphores));
    int newIndex = graphicsDesc->signalSemaphoreCount++;
    graphicsDesc->signalSemaphores[newIndex] = semaphore;
    graphicsDesc->signalSemaphoreValues[newIndex] = semaphoreValue;
}

static void RHI_SubmitGraphicsDesc_Wait(rhiSubmitGraphicsDesc *graphicsDesc, rhiSemaphore semaphore){
    assert(graphicsDesc->waitSemaphoreCount < ARRAY_LEN(graphicsDesc->waitSemaphores));
    int newIndex = graphicsDesc->waitSemaphoreCount++;
    graphicsDesc->waitSemaphores[newIndex] = semaphore;
}

static void RHI_SubmitGraphicsDesc_Wait_Timeline(rhiSubmitGraphicsDesc *graphicsDesc, rhiSemaphore semaphore, uint64_t timelineValue){
    assert(graphicsDesc->waitSemaphoreCount < ARRAY_LEN(graphicsDesc->waitSemaphores));
    int newIndex = graphicsDesc->waitSemaphoreCount++;
    graphicsDesc->waitSemaphores[newIndex] = semaphore;
	graphicsDesc->waitSemaphoreValues[newIndex] = timelineValue;
}



/*
================
RB_Hyperspace

A player has predicted a teleport, but hasn't arrived yet
================
*/
static void RB_Hyperspace( void ) {

	if ( !backEnd.isHyperspace ) {
		// do initialization shit
	}

	//@TODO clear color buffer

	backEnd.isHyperspace = qtrue;
}


static void SetViewportAndScissor( void ) {
	RHI_CmdSetScissor( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
				backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight );
	RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
				 backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 1.0f );

}


/*
=================
RB_BeginDrawingView

Any mirrored or portaled views have already been drawn, so prepare
to actually render the visible surfaces for this view
=================
*/
void RB_BeginDrawingView( void ) {
	int clearBits = 0;
	vec4_t clearColor = {};

	// we will need to change the projection matrix before drawing
	// 2D images again
	backEnd.projection2D = qfalse;

////////// (SA) modified to ensure one glclear() per frame at most

	// clear relevant buffers
	clearBits = 0;

	if ( r_shadows->integer == 2 ) {
		clearBits |= GL_STENCIL_BUFFER_BIT;
	}

	if ( r_uiFullScreen->integer ) {
		clearBits = GL_DEPTH_BUFFER_BIT;    // (SA) always just clear depth for menus

	} else if ( skyboxportal ) {
		if ( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) { // portal scene, clear whatever is necessary

			clearBits |= GL_DEPTH_BUFFER_BIT;

			if ( r_fastsky->integer || backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {  // fastsky: clear color

				// try clearing first with the portal sky fog color, then the world fog color, then finally a default
				clearBits |= GL_COLOR_BUFFER_BIT;
				if ( glfogsettings[FOG_PORTALVIEW].registered ) {
					Vector4Set(clearColor, glfogsettings[FOG_PORTALVIEW].color[0], glfogsettings[FOG_PORTALVIEW].color[1], glfogsettings[FOG_PORTALVIEW].color[2], glfogsettings[FOG_PORTALVIEW].color[3] );
				} else if ( glfogNum > FOG_NONE && glfogsettings[FOG_CURRENT].registered )      {
					Vector4Set(clearColor, glfogsettings[FOG_CURRENT].color[0], glfogsettings[FOG_CURRENT].color[1], glfogsettings[FOG_CURRENT].color[2], glfogsettings[FOG_CURRENT].color[3] );
				} else {
					Vector4Set(clearColor, 0.5, 0.5, 0.5, 1.0);
					
				}
			} else {                                                    // rendered sky (either clear color or draw quake sky)
				if ( glfogsettings[FOG_PORTALVIEW].registered ) {
					Vector4Set(clearColor, glfogsettings[FOG_PORTALVIEW].color[0], glfogsettings[FOG_PORTALVIEW].color[1], glfogsettings[FOG_PORTALVIEW].color[2], glfogsettings[FOG_PORTALVIEW].color[3] );
					if ( glfogsettings[FOG_PORTALVIEW].clearscreen ) {    // portal fog requests a screen clear (distance fog rather than quake sky)
						clearBits |= GL_COLOR_BUFFER_BIT;
					}
				}

			}
		} else {                                        // world scene with portal sky, don't clear any buffers, just set the fog color if there is one

			clearBits |= GL_DEPTH_BUFFER_BIT;   // this will go when I get the portal sky rendering way out in the zbuffer (or not writing to zbuffer at all)

			if ( glfogNum > FOG_NONE && glfogsettings[FOG_CURRENT].registered ) {
				if ( backEnd.refdef.rdflags & RDF_UNDERWATER ) {
					if ( glfogsettings[FOG_CURRENT].mode == GL_LINEAR ) {
						clearBits |= GL_COLOR_BUFFER_BIT;
					}

				} else if ( !( r_portalsky->integer ) ) {    // portal skies have been manually turned off, clear bg color
					clearBits |= GL_COLOR_BUFFER_BIT;
				}
				Vector4Set(clearColor, glfogsettings[FOG_CURRENT].color[0], glfogsettings[FOG_CURRENT].color[1], glfogsettings[FOG_CURRENT].color[2], glfogsettings[FOG_CURRENT].color[3] );
			}
		}
	} else {                                              // world scene with no portal sky
		clearBits |= GL_DEPTH_BUFFER_BIT;

		// NERVE - SMF - we don't want to clear the buffer when no world model is specified
		if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
			clearBits &= ~GL_COLOR_BUFFER_BIT;
		}
		// -NERVE - SMF
		else if ( r_fastsky->integer || backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {

			clearBits |= GL_COLOR_BUFFER_BIT;

			if ( glfogsettings[FOG_CURRENT].registered ) { // try to clear fastsky with current fog color
				Vector4Set(clearColor, glfogsettings[FOG_CURRENT].color[0], glfogsettings[FOG_CURRENT].color[1], glfogsettings[FOG_CURRENT].color[2], glfogsettings[FOG_CURRENT].color[3] );
			} else {
				Vector4Set(clearColor, 0.05, 0.05, 0.05, 1.0 ); 
			}
		} else {        // world scene, no portal sky, not fastsky, clear color if fog says to, otherwise, just set the clearcolor
			if ( glfogsettings[FOG_CURRENT].registered ) { // try to clear fastsky with current fog color
				Vector4Set(clearColor, glfogsettings[FOG_CURRENT].color[0], glfogsettings[FOG_CURRENT].color[1], glfogsettings[FOG_CURRENT].color[2], glfogsettings[FOG_CURRENT].color[3] );
				if ( glfogsettings[FOG_CURRENT].clearscreen ) {   // world fog requests a screen clear (distance fog rather than quake sky)
					clearBits |= GL_COLOR_BUFFER_BIT;
				}
			}
		}
	}

//----(SA)	done

	if ( ( backEnd.refdef.rdflags & RDF_HYPERSPACE ) ) {
		RB_Hyperspace();
		return;
	} else
	{
		backEnd.isHyperspace = qfalse;
	}

	// we will only draw a sun if there was sky rendered in this view
	backEnd.skyRenderedThisView = qfalse;

	RB_EndRenderPass();
	// clip to the plane of the portal
	if ( backEnd.viewParms.isPortal ) {
		float plane[4];
		float plane2[4];

		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		plane2[0] = DotProduct( backEnd.viewParms.or.axis[0], plane );
		plane2[1] = DotProduct( backEnd.viewParms.or.axis[1], plane );
		plane2[2] = DotProduct( backEnd.viewParms.or.axis[2], plane );
		plane2[3] = DotProduct( plane, backEnd.viewParms.or.origin ) - plane[3];

		//@TODO: flip plane s_flipMatrix
		RB_UploadSceneView(backEnd.viewParms.vulkanProjectionMatrix, plane2);
	} else {
		vec4_t zeroPlane = {0};
		RB_UploadSceneView(backEnd.viewParms.vulkanProjectionMatrix, zeroPlane);
	}
	const qbool prevFullscreen3D = backEnd.fullscreen3D;
	backEnd.fullscreen3D = RB_IsViewportFullscreen(&backEnd.viewParms);
	RB_FinishFullscreen3D(prevFullscreen3D);


	RHI_RenderPass renderPass = {};
	if(RB_IsMSAARequested() && backEnd.fullscreen3D){
		backEnd.msaaActive = qtrue;
		renderPass.colorTexture = backEnd.colorBufferMS;
		renderPass.depthTexture = backEnd.depthBufferMS;
		
		RHI_CmdBeginBarrier();
		RHI_CmdTextureBarrier(backEnd.depthBufferMS, RHI_ResourceState_DepthWriteBit);
		RHI_CmdTextureBarrier(backEnd.colorBufferMS, RHI_ResourceState_RenderTargetBit);
		RHI_CmdEndBarrier();
	}else{
		backEnd.msaaActive = qfalse;
		renderPass.colorTexture = backEnd.colorBuffer;
		renderPass.depthTexture = backEnd.depthBuffer;
		
		RHI_CmdBeginBarrier();
		RHI_CmdTextureBarrier(backEnd.depthBuffer, RHI_ResourceState_DepthWriteBit);
		RHI_CmdTextureBarrier(backEnd.colorBuffer, RHI_ResourceState_RenderTargetBit);
		RHI_CmdEndBarrier();
	}
	
	renderPass.depth = 1.0f;
	Vector4Copy(clearColor, renderPass.color);
	//renderPass.depthLoad = (clearBits & GL_DEPTH_BUFFER_BIT)? RHI_LoadOp_Clear : RHI_LoadOp_Load; 
	renderPass.depthLoad = RHI_LoadOp_Clear; 
	renderPass.colorLoad = (clearBits & GL_COLOR_BUFFER_BIT) || backEnd.clearColor ? RHI_LoadOp_Clear : RHI_LoadOp_Load; 
	RB_BeginRenderPass("3D", &renderPass);

	backEnd.clearColor = qfalse;

	SetViewportAndScissor(); //@TODO is this correct to call after renderpass has started
	
}

#define MAC_EVENT_PUMP_MSEC     5

/*
==================
RB_RenderDrawSurfList
==================
*/
void RB_RenderDrawSurfList( drawSurf_t *drawSurfs, int firstSurfIndex, int lastSurfIndex) {
	shader_t        *shader, *oldShader;
	int fogNum, oldFogNum;
	int entityNum, oldEntityNum;
	int dlighted;
	qboolean depthRange, oldDepthRange;
	int i;
	drawSurf_t      *drawSurf;
	int oldSort;
	double originalTime;


	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	tess.renderType = RT_GENERIC;
	

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldFogNum = -1;
	oldDepthRange = qfalse;
	oldSort = -1;
	depthRange = qfalse;

	backEnd.pc.c_surfaces += lastSurfIndex - firstSurfIndex;

	for ( i = firstSurfIndex, drawSurf = drawSurfs + firstSurfIndex ; i < lastSurfIndex ; i++, drawSurf++ ) {
		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			continue;
		}
		oldSort = drawSurf->sort;
		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( shader != oldShader || fogNum != oldFogNum
			 || ( entityNum != oldEntityNum && !shader->entityMergable ) ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			oldShader = shader;
			oldFogNum = fogNum;
			
		}

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = qfalse;

			if ( entityNum != ENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				backEnd.refdef.floatTime = originalTime; // - backEnd.currentEntity->e.shaderTime; // JPW NERVE pulled this to match q3ta

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );

				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;

			}

			// set up the dynamic lighting if needed
			// if ( backEnd.currentEntity->needDlights ) {
			// 	R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );
			// }

			//
			// change depthrange if needed
			//
			if ( oldDepthRange != depthRange ) {
				if ( depthRange ) {
					RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
						backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 0.3f );
				} else {
					RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
						backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 1.0f );
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.refdef.floatTime = originalTime;
	backEnd.or = backEnd.viewParms.world;
	// R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );

	if ( depthRange ) {
		RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
			backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 1.0f );
	}

	if(firstSurfIndex > 0){
		// (SA) draw sun
		RB_DrawSun();

		// darken down any stencil shadows
		RB_ShadowFinish();

		// add light flares on lights that aren't obscured
		RB_RenderFlares();
	}
	

}


void RB_RenderLitSurfList( drawSurf_t *drawSurfs, int firstSurfIndex, int lastSurfIndex, dlight_t *dlight) {
	shader_t        *shader, *oldShader;
	int fogNum;
	int entityNum, oldEntityNum;
	int dlighted;
	qboolean depthRange, oldDepthRange;
	drawSurf_t      *drawSurf;
	int oldSort;
	double originalTime;
	int i;


	// save original time for entity shader offsets
	originalTime = backEnd.refdef.floatTime;

	//tess.currentStageIteratorFunc = RB_DynamicLightIterator;
	tess.renderType = RT_DYNAMICLIGHT;
	tess.dlight = dlight;

	// draw everything
	oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	oldShader = NULL;
	oldDepthRange = qfalse;
	oldSort = -1;
	depthRange = qfalse;

	// backEnd.pc.c_surfaces += lastSurfIndex - firstSurfIndex;
	


	for ( i = firstSurfIndex, drawSurf = drawSurfs + firstSurfIndex ; i < lastSurfIndex ; i++, drawSurf++ ) {
		if ( drawSurf->sort == oldSort ) {
			// fast path, same as previous sort
			rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
			continue;
		}
		oldSort = drawSurf->sort;
		R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );

		//
		// change the tess parameters if needed
		// a "entityMergable" shader is a shader that can have surfaces from seperate
		// entities merged into a single batch, like smoke and blood puff sprites
		if ( shader != oldShader  || ( entityNum != oldEntityNum && !shader->entityMergable ) ) {
			if ( oldShader != NULL ) {
				RB_EndSurface();
			}
			RB_BeginSurface( shader, fogNum );
			
			oldShader = shader;
			
		}

		//
		// change the modelview matrix if needed
		//
		if ( entityNum != oldEntityNum ) {
			depthRange = qfalse;

			if ( entityNum != ENTITYNUM_WORLD ) {
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				backEnd.refdef.floatTime = originalTime; // - backEnd.currentEntity->e.shaderTime; // JPW NERVE pulled this to match q3ta

				// set up the transformation matrix
				R_RotateForEntity( backEnd.currentEntity, &backEnd.viewParms, &backEnd.or );

				if ( backEnd.currentEntity->e.renderfx & RF_DEPTHHACK ) {
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			} else {
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.or = backEnd.viewParms.world;

			}

			// set up the dynamic lighting if needed
			R_TransformDlights( 1, dlight, &backEnd.or );

			//
			// change depthrange if needed
			//
			if ( oldDepthRange != depthRange ) {
				if ( depthRange ) {
					RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
						backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 0.3f );
				} else {
					RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
						backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 1.0f );
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		// add the triangles for this surface
		rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );
	}

	// draw the contents of the last shader batch
	if ( oldShader != NULL ) {
		RB_EndSurface();
	}

	// go back to the world modelview matrix
	backEnd.currentEntity = &tr.worldEntity;
	backEnd.refdef.floatTime = originalTime;
	backEnd.or = backEnd.viewParms.world;
	// R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.or );

	if ( depthRange ) {
		RHI_CmdSetViewport( backEnd.viewParms.viewportX, backEnd.viewParms.viewportY,
			backEnd.viewParms.viewportWidth, backEnd.viewParms.viewportHeight, 0.0f, 1.0f );
	}

}


/*
============================================================================

RENDER BACK END THREAD FUNCTIONS

============================================================================
*/

/*
================
RB_SetGL2D

================
*/
void    RB_SetGL2D( void ) {
	backEnd.projection2D = qtrue;

	// set time for 2D shaders
	backEnd.refdef.time = ri.Milliseconds();
	backEnd.refdef.floatTime = (double)backEnd.refdef.time * 0.001;

	float w = glConfig.vidWidth;
	float h = glConfig.vidHeight;
	
	
	float projectionMatrix[16] = {
		2.0f/w, 0.0f, 0.0f, 0.0f,
		0.0f, 2.0f/h, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		-1.0f, -1.0f, 0.0f, 1.0f
	};
	
	vec4_t zeroPlane = {0};

	RB_EndRenderPass();
	RB_UploadSceneView(projectionMatrix, zeroPlane);

	float modelViewMatrix[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	memcpy(backEnd.or.modelMatrix, modelViewMatrix, sizeof(backEnd.or.modelMatrix));

	const qbool prevFullscreen3D = backEnd.fullscreen3D;
	backEnd.fullscreen3D = qfalse;
	RB_FinishFullscreen3D(prevFullscreen3D);

	RHI_CmdBeginBarrier();
	RHI_CmdTextureBarrier(backEnd.colorBuffer, RHI_ResourceState_RenderTargetBit);
	RHI_CmdEndBarrier();

	RHI_RenderPass renderPass = {};
	
	renderPass.colorLoad = backEnd.clearColor ? RHI_LoadOp_Clear : RHI_LoadOp_Load;
	renderPass.colorTexture = backEnd.colorBuffer;
	backEnd.clearColor = qfalse;

	RB_BeginRenderPass("2D", &renderPass);

	RHI_CmdSetScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
	RHI_CmdSetViewport( 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f );

}

void RB_UploadSceneView(const float *projectionMatrix, const float *clipPlane){
	SceneView sceneView = {};
	memcpy(sceneView.projectionMatrix, projectionMatrix, sizeof(sceneView.projectionMatrix));
	memcpy(sceneView.clipPlane, clipPlane, sizeof(sceneView.clipPlane));

	rhiBuffer currentScene = backEnd.sceneViewUploadBuffers[backEnd.currentFrameIndex];
	assert(backEnd.sceneViewCount < SCENEVIEW_MAX);

	byte* mappedBuffer = RHI_MapBuffer(currentScene);
	memcpy(mappedBuffer + backEnd.sceneViewCount * sizeof(sceneView), &sceneView, sizeof(sceneView));
	RHI_UnmapBuffer(currentScene);


	//Schedule the GPU copy and transition the layout
	RHI_CmdBeginBarrier();
	RHI_CmdBufferBarrier(backEnd.sceneViewGPUBuffer, RHI_ResourceState_CopyDestinationBit);
	RHI_CmdEndBarrier();

	RHI_CmdCopyBuffer(backEnd.sceneViewGPUBuffer, 0, currentScene, backEnd.sceneViewCount * sizeof(sceneView), sizeof(sceneView));
	
	RHI_CmdBeginBarrier();
	RHI_CmdBufferBarrier(backEnd.sceneViewGPUBuffer, RHI_ResourceState_ShaderInputBit);
	RHI_CmdEndBarrier();

	backEnd.sceneViewCount++;
}

/*
=============
RE_StretchRaw

FIXME: not exactly backend
Stretches a raw 32 bit power of 2 bitmap image over the given screen rectangle.
Used for cinematics.
=============
*/
void RE_StretchRaw( int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {
	//@TODO
}


void RE_UploadCinematic( int w, int h, int cols, int rows, const byte *data, int client, qboolean dirty ) {
	//@TODO
}


/*
=============
RB_SetColor

=============
*/
const void  *RB_SetColor( const void *data ) {
	const setColorCommand_t *cmd;

	cmd = (const setColorCommand_t *)data;

	backEnd.color2D[0] = cmd->color[0] * 255;
	backEnd.color2D[1] = cmd->color[1] * 255;
	backEnd.color2D[2] = cmd->color[2] * 255;
	backEnd.color2D[3] = cmd->color[3] * 255;

	return (const void *)( cmd + 1 );
}

/*
=============
RB_StretchPic
=============
*/
const void *RB_StretchPic( const void *data ) {
	const stretchPicCommand_t   *cmd;
	shader_t *shader;
	int numVerts, numIndexes;

	cmd = (const stretchPicCommand_t *)data;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	//the quad vertices (4 vertices / 3 indices per triangle 2 vertices are reused)
	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	*(uint32_t *)tess.vertexColors[ numVerts + 0 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 1 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 2 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 3 ] = *(uint32_t *)backEnd.color2D;

	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 1 ][1] = cmd->y;
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x;
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)( cmd + 1 );
}

// NERVE - SMF
/*
=============
RB_RotatedPic
=============
*/
const void *RB_RotatedPic( const void *data ) {
	const stretchPicCommand_t   *cmd;
	shader_t *shader;
	int numVerts, numIndexes;
	float angle;
	float pi2 = M_PI * 2;

	cmd = (const stretchPicCommand_t *)data;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	*(uint32_t *)tess.vertexColors[ numVerts + 0 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 1 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 2 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 3 ] = *(uint32_t *)backEnd.color2D;

	angle = cmd->angle * pi2;
	tess.xyz[ numVerts ][0] = cmd->x + ( cos( angle ) * cmd->w );
	tess.xyz[ numVerts ][1] = cmd->y + ( sin( angle ) * cmd->h );
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	angle = cmd->angle * pi2 + 0.25 * pi2;
	tess.xyz[ numVerts + 1 ][0] = cmd->x + ( cos( angle ) * cmd->w );
	tess.xyz[ numVerts + 1 ][1] = cmd->y + ( sin( angle ) * cmd->h );
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	angle = cmd->angle * pi2 + 0.50 * pi2;
	tess.xyz[ numVerts + 2 ][0] = cmd->x + ( cos( angle ) * cmd->w );
	tess.xyz[ numVerts + 2 ][1] = cmd->y + ( sin( angle ) * cmd->h );
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	angle = cmd->angle * pi2 + 0.75 * pi2;
	tess.xyz[ numVerts + 3 ][0] = cmd->x + ( cos( angle ) * cmd->w );
	tess.xyz[ numVerts + 3 ][1] = cmd->y + ( sin( angle ) * cmd->h );
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)( cmd + 1 );
}
// -NERVE - SMF

/*
==============
RB_StretchPicGradient
==============
*/
const void *RB_StretchPicGradient( const void *data ) {
	const stretchPicCommand_t   *cmd;
	shader_t *shader;
	int numVerts, numIndexes;

	cmd = (const stretchPicCommand_t *)data;

	if ( !backEnd.projection2D ) {
		RB_SetGL2D();
	}

	shader = cmd->shader;
	if ( shader != tess.shader ) {
		if ( tess.numIndexes ) {
			RB_EndSurface();
		}
		backEnd.currentEntity = &backEnd.entity2D;
		RB_BeginSurface( shader, 0 );
	}

	RB_CHECKOVERFLOW( 4, 6 );
	numVerts = tess.numVertexes;
	numIndexes = tess.numIndexes;

	tess.numVertexes += 4;
	tess.numIndexes += 6;

	tess.indexes[ numIndexes ] = numVerts + 3;
	tess.indexes[ numIndexes + 1 ] = numVerts + 0;
	tess.indexes[ numIndexes + 2 ] = numVerts + 2;
	tess.indexes[ numIndexes + 3 ] = numVerts + 2;
	tess.indexes[ numIndexes + 4 ] = numVerts + 0;
	tess.indexes[ numIndexes + 5 ] = numVerts + 1;

	*(uint32_t *)tess.vertexColors[ numVerts + 0 ] = *(uint32_t *)backEnd.color2D;
	*(uint32_t *)tess.vertexColors[ numVerts + 1 ] = *(uint32_t *)backEnd.color2D;

	*(uint32_t *)tess.vertexColors[ numVerts + 2 ] = *(uint32_t *)cmd->gradientColor;
	*(uint32_t *)tess.vertexColors[ numVerts + 3 ] = *(uint32_t *)cmd->gradientColor;


	tess.xyz[ numVerts ][0] = cmd->x;
	tess.xyz[ numVerts ][1] = cmd->y;
	tess.xyz[ numVerts ][2] = 0;

	tess.texCoords[ numVerts ][0][0] = cmd->s1;
	tess.texCoords[ numVerts ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 1 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 1 ][1] = cmd->y;
	tess.xyz[ numVerts + 1 ][2] = 0;

	tess.texCoords[ numVerts + 1 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 1 ][0][1] = cmd->t1;

	tess.xyz[ numVerts + 2 ][0] = cmd->x + cmd->w;
	tess.xyz[ numVerts + 2 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 2 ][2] = 0;

	tess.texCoords[ numVerts + 2 ][0][0] = cmd->s2;
	tess.texCoords[ numVerts + 2 ][0][1] = cmd->t2;

	tess.xyz[ numVerts + 3 ][0] = cmd->x;
	tess.xyz[ numVerts + 3 ][1] = cmd->y + cmd->h;
	tess.xyz[ numVerts + 3 ][2] = 0;

	tess.texCoords[ numVerts + 3 ][0][0] = cmd->s1;
	tess.texCoords[ numVerts + 3 ][0][1] = cmd->t2;

	return (const void *)( cmd + 1 );
}


/*
=============
RB_DrawSurfs

=============
*/
const void  *RB_DrawSurfs( const void *data ) {
	const drawSurfsCommand_t    *cmd;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	cmd = (const drawSurfsCommand_t *)data;

	backEnd.refdef = cmd->refdef;
	backEnd.viewParms = cmd->viewParms;

	int numOpaqueSurfs = 0;
	for(int i = 0; i < cmd->numDrawSurfs; i++){
		if(cmd->drawSurfs[i].shader->sort > SS_OPAQUE){
			break;
		}
		numOpaqueSurfs++;
	}
	// clear the z buffer, set the modelview, etc
	RB_BeginDrawingView();

	RHI_CmdBeginDebugLabel("Opaque");
	backEnd.pipelineLayoutDirty = qtrue;
	RB_RenderDrawSurfList( cmd->drawSurfs, 0, numOpaqueSurfs );
	RHI_CmdEndDebugLabel();
	
	RHI_CmdBeginDebugLabel("Dynamic Lights");
	backEnd.pipelineLayoutDirty = qtrue;
	for(int l = 0; l < backEnd.refdef.num_dlights; l++){
		dlight_t *dl = &backEnd.refdef.dlights[l];
		if(R_CullPointAndRadius(dl->origin, dl->radius) == CULL_OUT){
			continue;
		}
		RB_RenderLitSurfList(cmd->drawSurfs, 0, numOpaqueSurfs, dl);
	}
	RHI_CmdEndDebugLabel();

	RHI_CmdBeginDebugLabel("Transparent");
	backEnd.pipelineLayoutDirty = qtrue;
	RB_RenderDrawSurfList( cmd->drawSurfs, numOpaqueSurfs, cmd->numDrawSurfs );
	RHI_CmdEndDebugLabel();

	return (const void *)( cmd + 1 );
}


/*
=============
RB_BeginFrame

=============
*/
const void  *RB_BeginFrame( const void *data ) {
	backEnd.currentFrameIndex = (backEnd.currentFrameIndex + 1) % RHI_FRAMES_IN_FLIGHT;
	backEnd.renderPassCount[backEnd.currentFrameIndex] = 0;
	backEnd.clearColor = qtrue;

	backEnd.sceneViewCount = 0;
	backEnd.previousPipeline.h = 0;
	backEnd.currentDescriptorSet.h = 0;
	backEnd.pipelineChangeCount = 0;
	backEnd.previousVertexBufferCount = 0;
	backEnd.colorBufferIndex = 0;
	backEnd.colorBuffer = backEnd.colorBuffers[backEnd.colorBufferIndex];
	
	backEnd.vertexBuffers[backEnd.currentFrameIndex].indexCount = 0; 
	backEnd.vertexBuffers[backEnd.currentFrameIndex].indexFirst = 0; 
	backEnd.vertexBuffers[backEnd.currentFrameIndex].vertexCount = 0; 
	backEnd.vertexBuffers[backEnd.currentFrameIndex].vertexFirst = 0; 

	RHI_BeginFrame();

	const drawBufferCommand_t   *cmd;

	cmd = (const drawBufferCommand_t *)data;

	// clear screen for debugging
	if ( r_clear->integer ) {
		//@TODO
	}
	RHI_WaitOnSemaphore(backEnd.renderComplete, backEnd.renderCompleteCounter);
	RHI_AcquireNextImage(&backEnd.swapChainImageIndex, backEnd.imageAcquiredBinary);
	RHI_BindCommandBuffer(backEnd.commandBuffers[backEnd.currentFrameIndex]);
	RHI_BeginCommandBuffer();
	RHI_DurationQueryReset();
	backEnd.frameDuration[backEnd.currentFrameIndex] = RHI_CmdBeginDurationQuery();

	RHI_CmdBeginBarrier();
	RHI_CmdTextureBarrier(backEnd.colorBuffer, RHI_ResourceState_RenderTargetBit);
	RHI_CmdEndBarrier();



	// RHI_CmdBindPipeline(backEnd.pipeline);
	// RHI_CmdBindDescriptorSet(backEnd.pipeline, backEnd.descriptorSet);
	RHI_CmdBindIndexBuffer(backEnd.vertexBuffers[backEnd.currentFrameIndex].index);
	/*rhiBuffer buffers[3] = {backEnd.vertexBuffers[backEnd.currentFrameIndex].position, 
	backEnd.vertexBuffers[backEnd.currentFrameIndex].color,
	backEnd.vertexBuffers[backEnd.currentFrameIndex].textureCoord};
	RHI_CmdBindVertexBuffers(buffers, ARRAY_LEN(buffers));*/

	RB_ImGUI_BeginFrame();

	return (const void *)( cmd + 1 );
}


typedef struct renderPassHistory {
	uint32_t durationUs[64];
	uint32_t writeIndex;
	uint32_t count;
	uint32_t median;
} renderPassHistory;

static renderPassHistory s_history[MAX_RENDERPASSES];
static renderPassHistory s_fullFrameHistory;

int QDECL CompareSamples(void const *ptrA, void const *ptrB){
	const int *a = (const int*)ptrA;
	const int *b = (const int*)ptrB;
	return *a - *b;
}

void AddHistory(renderPassHistory *history, uint32_t currentHash, uint32_t previousHash, uint32_t currentDuration){
	enum {
		n = ARRAY_LEN(history->durationUs)
	};
	if(currentHash != previousHash){
		history->count = 1;
	}else{
		history->count = min(history->count + 1, n);
	}
	history->durationUs[history->writeIndex] = currentDuration;
	history->writeIndex = (history->writeIndex + 1) % n;
	
	uint32_t startIndex = (history->writeIndex - history->count + n) % n;
	uint32_t samples[n];
	for(int i = 0; i < history->count; i++){
		samples[i] = history->durationUs[(startIndex + i) % n];
	}
	qsort(samples, history->count, sizeof(uint32_t), CompareSamples);

	history->median = samples[history->count / 2];
}
/*
=============
RB_EndFrame

=============
*/

void DrawGUI_ShaderTrace(void){
	static bool breakdownActive = false;
	ToggleBooleanWithShortcut((qbool*)&breakdownActive, ImGuiKey_T, ImGUI_ShortcutOptions_Global);
	GUI_AddMainMenuItem(ImGUI_MainMenu_Perf, "Shader Trace", "Ctrl+Shift+T", (qbool*)&breakdownActive, qtrue);
	
	if(breakdownActive){
		if(igBegin("Shader Trace", &breakdownActive, 0)){
			
			uint32_t* buffer = (uint32_t*)RHI_MapBuffer(backEnd.shaderIndexReadbackBuffer);
			uint32_t shaderIndex = buffer[backEnd.currentFrameIndex ^ 1];
			igText("%d %d", (int)buffer[0], (int)buffer[1]);
			RHI_UnmapBuffer(backEnd.shaderIndexReadbackBuffer);

			
			if(shaderIndex < tr.numShaders){
				shader_t *sh = tr.shaders[shaderIndex];
				
				igText(sh->name);
				igNewLine();
				igText("Images:");
				for(int i = 0; i < MAX_SHADER_STAGES; i++){
					shaderStage_t *stage = sh->stages[i];
					if(stage == NULL || !stage->active){
						break;
					}
					for(int b = 0; b < 2; b++){
						image_t *img = stage->bundle[b].image[0];
						if(img != NULL){
							igSelectable_Bool(img->imgName, 0, 0, (ImVec2){0.0f, 0.0f});
							if(igIsItemHovered(ImGuiHoveredFlags_DelayShort)){
								igText("Source: %d x %d", img->width, img->height);
								igText("Upload: %d x %d", img->uploadWidth, img->uploadHeight);
								igText("Mipmap: %d", (int)img->mipmap);
								igText("Picmip: %d", (int)img->allowPicmip);
								igText("Lightmap: %d", (int)img->lightMap);
								if(igBeginTooltip()){
									igImage(img->descriptorIndex, (ImVec2){img->width, img->height},(ImVec2){0.0f, 0.0f},(ImVec2){1.0f, 1.0f},(ImVec4){1.0f, 1.0f, 1.0f, 1.0f}, (ImVec4){0.0f, 0.0f, 0.0f, 1.0f});
								
									igEndTooltip();
								}
							}
						}
					}
					
				}
				
			}
			igNewLine();
			

			
		}
		igEnd();
		
	}
	
}


void DrawGUI_Performance(void){
	int f = (backEnd.currentFrameIndex - 1 + RHI_FRAMES_IN_FLIGHT) % RHI_FRAMES_IN_FLIGHT;
	for(int i = 0; i < backEnd.renderPassCount[f]; i++){
		renderPass *currentRenderPass = &backEnd.renderPasses[f][i];
		renderPass *previousRenderPass = &backEnd.renderPasses[f][i];
		currentRenderPass->durationUs = RHI_GetDurationUs(currentRenderPass->query);
		AddHistory(&s_history[i], currentRenderPass->nameHash, previousRenderPass->nameHash, currentRenderPass->durationUs);
	}
	
	uint32_t duration = RHI_GetDurationUs(backEnd.frameDuration[(backEnd.currentFrameIndex + 1) % RHI_FRAMES_IN_FLIGHT]);
	AddHistory(&s_fullFrameHistory, 0, 0, duration);

	static bool breakdownActive = false;
	ToggleBooleanWithShortcut((qbool*)&breakdownActive, ImGuiKey_F, ImGUI_ShortcutOptions_Global);
	GUI_AddMainMenuItem(ImGUI_MainMenu_Perf, "Frame breakdown", "Ctrl+Shift+F", (qbool*)&breakdownActive, qtrue);
	if(breakdownActive){
		if(igBegin("Frame breakdown", &breakdownActive, 0)){
			igNewLine();
			int32_t renderPassDuration = 0;
			if(igBeginTable("Status",2,ImGuiTableFlags_RowBg,(ImVec2){0,0},0.0f)){
				TableHeader(2, "Renderpass", "Duration");
				TableRowInt("Entire Frame", (int)s_fullFrameHistory.median);
				for(int i = 0; i < backEnd.renderPassCount[f]; i++){
					renderPass *currentRenderPass = &backEnd.renderPasses[f][i];
					TableRowInt(currentRenderPass->name, (int)s_history[i].median);
					renderPassDuration += currentRenderPass->durationUs;
				}
				TableRowInt("Overhead", (int)duration - (int)renderPassDuration);
				igEndTable();
			}

			igText("PSO changes: %d", (int)backEnd.pipelineChangeCount);
			igText("Textures loaded: %d", (int)tr.numImages);
			

			static int previousSceneCount;
			static int previousViewCount;
			igText("Scenes: %d", tr.sceneCount - previousSceneCount);
			igText("Views: %d", tr.viewCount - previousViewCount);
			previousSceneCount = tr.sceneCount;
			previousViewCount = tr.viewCount;


			int64_t currentTime = Sys_Microseconds();
			static int64_t previousTime = INT64_MIN;
			const int64_t us = currentTime - previousTime;
			previousTime = currentTime;
			static float previousDurations[64];
			const int n = ARRAY_LEN(previousDurations);
			static int durationIndex;
			int minValue = INT_MAX;
			int maxValue = INT_MIN;
			previousDurations[durationIndex] = (float)(us);
			for(int i = 0; i < n; i++){
				minValue = min(minValue, previousDurations[i]);
				maxValue = max(maxValue, previousDurations[i]);
			}

			igText("Frame delta: %d", us);
			igText(" min: %d\n max: %d", minValue, maxValue);
			durationIndex = (durationIndex + 1) % n;
			
			igText("FPS: %d", (int)(1000000 / (us)));
			ImVec2 graphSize = {1000, 500};
			igPlotLines_FloatPtr("FPS", previousDurations,n,durationIndex,"durations", 4000 , 10000, graphSize, sizeof(float) );
			
		}
		igEnd();
	}
	
}

const void  *RB_EndFrame( const void *data ) {
	const swapBuffersCommand_t  *cmd;

	// finish any 2D drawing if needed
	if ( tess.numIndexes ) {
		RB_EndSurface();
	}

	const qbool prevFullscreen3D = backEnd.fullscreen3D;
	backEnd.fullscreen3D = qfalse;
	RB_FinishFullscreen3D(prevFullscreen3D);


	cmd = (const swapBuffersCommand_t *)data;

	GUI_DrawMainMenu();
	DrawGUI_Performance();
	DrawGUI_ShaderTrace();
	CL_ProfilerFrame();
	ri.CL_ImGUI_Update();
	DrawGUI_RHI();
	ri.CL_CG_ImGUI_Update();

	RB_ImGUI_Draw(backEnd.colorBuffer);

	rhiSampler blitSampler = backEnd.sampler[RB_GetSamplerIndex(qtrue,qfalse)];

	RB_DrawBlit(backEnd.colorBuffer, blitSampler, backEnd.swapChainTextures[backEnd.swapChainImageIndex]);
	RB_EndRenderPass();

	RHI_CmdBeginBarrier();
	RHI_CmdTextureBarrier(backEnd.swapChainTextures[backEnd.swapChainImageIndex], RHI_ResourceState_PresentBit);
	RHI_CmdEndBarrier();

	RHI_CmdCopyBuffer(backEnd.shaderIndexReadbackBuffer, 0, backEnd.shaderIndexBuffer, 0, 8);
	RHI_CmdEndDurationQuery(backEnd.frameDuration[backEnd.currentFrameIndex]);

	RHI_EndCommandBuffer();

	backEnd.renderCompleteCounter++;
	rhiSubmitGraphicsDesc graphicsDesc = {};
	RHI_SubmitGraphicsDesc_Signal(&graphicsDesc, backEnd.renderCompleteBinary, 0);
	RHI_SubmitGraphicsDesc_Signal(&graphicsDesc, backEnd.renderComplete, backEnd.renderCompleteCounter);
	RHI_SubmitGraphicsDesc_Wait(&graphicsDesc, backEnd.imageAcquiredBinary);
	RHI_SubmitGraphicsDesc_Wait_Timeline(&graphicsDesc, RHI_GetUploadSemaphore(), RHI_GetUploadSemaphoreValue());
	RHI_SubmitGraphics(&graphicsDesc);
	RHI_SubmitPresent(backEnd.renderCompleteBinary, backEnd.swapChainImageIndex);
	
	RHI_EndFrame();

	backEnd.projection2D = qfalse;

	return (const void *)( cmd + 1 );
}

/*
====================
RB_ExecuteRenderCommands

This function will be called synchronously if running without
smp extensions, or asynchronously by another thread.
====================
*/
void RB_ExecuteRenderCommands( const void *data ) {
	if(Sys_IsMinimized()){
		if(ri.IsRecordingVideo()){
			ri.Printf(PRINT_WARNING, "Recording video while minimized is not supported\n");
		}
		return;
	}
	int t1, t2;

	t1 = ri.Milliseconds();

	static qbool begun = qfalse;


	while ( 1 ) {
		switch ( *(const int *)data ) {
		case RC_SET_COLOR:
			data = RB_SetColor( data );
			break;
		case RC_STRETCH_PIC:
			#if defined(_DEBUG)
			if (!begun) {
				Sys_DebugBreak();
			}
			#endif
			data = RB_StretchPic( data );
			break;
		case RC_ROTATED_PIC:
			data = RB_RotatedPic( data );
			break;
		case RC_STRETCH_PIC_GRADIENT:
			data = RB_StretchPicGradient( data );
			break;
		case RC_DRAW_SURFS:
			data = RB_DrawSurfs( data );
			break;
		case RC_BEGIN_FRAME:
			#if defined(_DEBUG)
			if (begun) {
				Sys_DebugBreak();
			}
			#endif
			begun = qtrue;
			data = RB_BeginFrame( data );
			//wait for swap chain acquire
			//start recording command buffer
			//N frames in flight n command buffers 

			break;
		case RC_END_FRAME:
			begun = qfalse;
			data = RB_EndFrame( data );
			//stop recording to command buffer
			//submit to graphics queue
			//submit to present queue
			break;

		case RC_END_OF_LIST:
		default:
			// stop rendering on this thread
			t2 = ri.Milliseconds();
			backEnd.pc.msec = t2 - t1;
			return;
		}
	}

}

#include "shaders/generic_ps.h"
#include "shaders/generic_ps_at.h"
#include "shaders/generic_ps_at_a2c.h"
#include "shaders/generic_vs.h"
#include "shaders/generic2s_ps.h"
#include "shaders/generic2s_vs.h"

typedef struct cachedPipeline {
	rhiGraphicsPipelineDesc desc;
	rhiPipeline pipeline;
	uint32_t hash;
	struct cachedPipeline *next;
} cachedPipeline;

cachedPipeline pipelineCache[4096];
cachedPipeline *pipelineHash[256];
int pipelineCount = 0;

uint32_t RB_HashPipeline(rhiGraphicsPipelineDesc *desc){
	uint32_t crc = 0;
	CRC32_Begin(&crc);
	CRC32_ProcessBlock(&crc, desc, sizeof(rhiGraphicsPipelineDesc));
	CRC32_End(&crc);
	return crc;
}

qboolean RB_GetCachedPipeline(uint32_t hash, rhiPipeline *pipeline, rhiGraphicsPipelineDesc *desc){
	cachedPipeline *head = pipelineHash[hash & (ARRAY_LEN(pipelineHash)-1)];
	for(; head; head = head->next){
		if(head->hash == hash && (memcmp(desc, &head->desc, sizeof(rhiGraphicsPipelineDesc))==0)){
			*pipeline = head->pipeline;
			return qtrue;
		}
	}
	return qfalse;
}

void RB_AddCachedPipeline(uint32_t hash, cachedPipeline *cache){
	if(pipelineCount >= ARRAY_LEN(pipelineCache)){
		assert(!"Pipeline cache is full");
		return;
	}
	cachedPipeline *currentPipeline = &pipelineCache[pipelineCount++];
	cachedPipeline *head = pipelineHash[hash & (ARRAY_LEN(pipelineHash)-1)];
	*currentPipeline = *cache;
	currentPipeline->next = head;
	pipelineHash[hash & (ARRAY_LEN(pipelineHash)-1)] = currentPipeline;

}

void RB_ClearPipelineCache(void){
	pipelineCount = 0;
	memset(pipelineHash, 0, sizeof(pipelineHash));
}



void RB_CreateGraphicsPipeline(shader_t *newShader){

	qbool isMT = newShader->isMultitextured; //newShader->optimalStageIteratorFunc == RB_StageIteratorLightmappedMultitexture;
	
	uint32_t pcBytes = max(sizeof(pixelShaderPushConstants2), sizeof(pixelShaderPushConstants));
	
	
	for(int i = 0; i < MAX_SHADER_STAGES; i++){
		
		shaderStage_t *stage = newShader->stages[i];
		if (stage == NULL || !stage->active) {
			continue;
		}

		rhiGraphicsPipelineDesc graphicsDesc;
		memset(&graphicsDesc, 0, sizeof(rhiGraphicsPipelineDesc));
		// graphicsDesc.name = newShader->name;
		graphicsDesc.descLayout = backEnd.descriptorSetLayout;
		graphicsDesc.pushConstants.vsBytes = 64;


		

		if(isMT){
			graphicsDesc.pushConstants.psBytes = pcBytes;
			graphicsDesc.vertexShader.data = generic2s_vs;
			graphicsDesc.vertexShader.byteCount = sizeof(generic2s_vs);
			graphicsDesc.pixelShader.data = generic2s_ps;
			graphicsDesc.pixelShader.byteCount = sizeof(generic2s_ps);

			rhiVertexAttributeDesc *a;
			a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
			a->elementCount = 4;
			a->elementFormat = RHI_VertexFormat_Float32;
			a->bufferBinding = 0;

			a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
			a->elementCount = 2;
			a->elementFormat = RHI_VertexFormat_Float32;
			a->bufferBinding = 1;

			a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
			a->elementCount = 2;
			a->elementFormat = RHI_VertexFormat_Float32;
			a->bufferBinding = 1;
			a->offset = 2 * sizeof(float);
			
			graphicsDesc.vertexBufferCount = 2;
			graphicsDesc.vertexBuffers[0].stride = 4 * sizeof(float);
			graphicsDesc.vertexBuffers[1].stride = 4 * sizeof(float);

			
		}else{
			graphicsDesc.pushConstants.psBytes = pcBytes;
			graphicsDesc.vertexShader.data = generic_vs;
			graphicsDesc.vertexShader.byteCount = sizeof(generic_vs);

			if(stage->stateBits & GLS_ATEST_BITS){
				graphicsDesc.pixelShader.data = generic_ps_at;
				graphicsDesc.pixelShader.byteCount = sizeof(generic_ps_at);
			}else{
				graphicsDesc.pixelShader.data = generic_ps;
				graphicsDesc.pixelShader.byteCount = sizeof(generic_ps);
			}

			rhiVertexAttributeDesc *a;
			a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
			a->elementCount = 4;
			a->elementFormat = RHI_VertexFormat_Float32;
			a->bufferBinding = 0;

			a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
			a->elementCount = 4;
			a->elementFormat = RHI_VertexFormat_UNorm8;
			a->bufferBinding = 1;

			a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
			a->elementCount = 2;
			a->elementFormat = RHI_VertexFormat_Float32;
			a->bufferBinding = 2;
			
			graphicsDesc.vertexBufferCount = 3;
			graphicsDesc.vertexBuffers[0].stride = 4 * sizeof(float);
			graphicsDesc.vertexBuffers[1].stride = 4 * sizeof(byte);
			graphicsDesc.vertexBuffers[2].stride = 2 * sizeof(float);
		}


		
		graphicsDesc.cullType = newShader->cullType;
		graphicsDesc.polygonOffset = newShader->polygonOffset;
		graphicsDesc.srcBlend = stage->stateBits & GLS_SRCBLEND_BITS;
		graphicsDesc.dstBlend = stage->stateBits & GLS_DSTBLEND_BITS;
		graphicsDesc.depthTest = (stage->stateBits & GLS_DEPTHTEST_DISABLE) == 0;
		graphicsDesc.depthWrite = (stage->stateBits & GLS_DEPTHMASK_TRUE) != 0;
		graphicsDesc.depthTestEqual = (stage->stateBits & GLS_DEPTHFUNC_EQUAL) != 0;
		graphicsDesc.wireframe = (stage->stateBits & GLS_POLYMODE_LINE) != 0;
		graphicsDesc.colorFormat = R8G8B8A8_UNorm;


		uint32_t hash = RB_HashPipeline(&graphicsDesc);
		cachedPipeline cached = {};

		for(int i = 0; i < 2; i++){
			if(i == 1 && RB_GetMSAASampleCount() >= 2){
				graphicsDesc.sampleCount = RB_GetMSAASampleCount();
				if(stage->stateBits & GLS_ATEST_BITS){
					graphicsDesc.alphaToCoverage = qtrue;
					graphicsDesc.pixelShader.data = generic_ps_at_a2c;
					graphicsDesc.pixelShader.byteCount = sizeof(generic_ps_at_a2c);
				}
			}else{
				graphicsDesc.sampleCount = 1;
			}
			
			if(!RB_GetCachedPipeline(hash, &cached.pipeline, &graphicsDesc)){
				graphicsDesc.name = newShader->name;
				cached.pipeline = RHI_CreateGraphicsPipeline(&graphicsDesc);
				
				graphicsDesc.name = NULL;
				cached.desc = graphicsDesc;
				cached.hash = hash;
				RB_AddCachedPipeline(hash, &cached);
				totalPipelines++;
			}
			assert(cached.pipeline.h != 0);
			stage->pipeline[i] = cached.pipeline;
		}	
	}
	
}
int RB_GetDynamicLightPipelineIndex(int cull, int polygonOffset, int msaa){
	return polygonOffset * CT_COUNT + cull + msaa * CT_COUNT * 2;
}

#include "shaders/dynamiclight_ps.h"
#include "shaders/dynamiclight_vs.h"

void RB_CreateDynamicLightPipelines(void){
	for(int m = 0; m < 2; m++){
		for(int c = 0; c < CT_COUNT; c++){
			for(int p = 0; p < 2; p++){
				rhiGraphicsPipelineDesc graphicsDesc = {};
				graphicsDesc.name = va("Dynamic Light C: %d, P: %d, M: %d", c, p, m);
				graphicsDesc.descLayout = backEnd.descriptorSetLayout;
				graphicsDesc.pushConstants.vsBytes = 64;

				graphicsDesc.pushConstants.psBytes = sizeof(dynamicLightPushConstants);
				graphicsDesc.vertexShader.data = dynamiclight_vs;
				graphicsDesc.vertexShader.byteCount = sizeof(dynamiclight_vs);
				graphicsDesc.pixelShader.data = dynamiclight_ps;
				graphicsDesc.pixelShader.byteCount = sizeof(dynamiclight_ps);

				rhiVertexAttributeDesc *a;
				a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
				a->elementCount = 4; //position
				a->elementFormat = RHI_VertexFormat_Float32;
				a->bufferBinding = 0;

				a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
				a->elementCount = 2; //tc
				a->elementFormat = RHI_VertexFormat_Float32;
				a->bufferBinding = 1;

				a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
				a->elementCount = 4; //color
				a->elementFormat = RHI_VertexFormat_UNorm8;
				a->bufferBinding = 2;
				

				a = &graphicsDesc.attributes[graphicsDesc.attributeCount++];
				a->elementCount = 4; //normal
				a->elementFormat = RHI_VertexFormat_Float32;
				a->bufferBinding = 3;
		
				
				graphicsDesc.vertexBufferCount = 4;
				graphicsDesc.vertexBuffers[0].stride = 4 * sizeof(float);
				graphicsDesc.vertexBuffers[1].stride = 2 * sizeof(float);
				graphicsDesc.vertexBuffers[2].stride = 4 * sizeof(byte);
				graphicsDesc.vertexBuffers[3].stride = 4 * sizeof(float);

				graphicsDesc.cullType = c;
				graphicsDesc.polygonOffset = p;
				graphicsDesc.srcBlend = GLS_SRCBLEND_ONE;
				graphicsDesc.dstBlend = GLS_DSTBLEND_ONE;
				graphicsDesc.depthTest = qtrue;
				graphicsDesc.depthWrite = qfalse;
				graphicsDesc.depthTestEqual = qtrue;
				graphicsDesc.wireframe = qfalse;
				graphicsDesc.colorFormat = R8G8B8A8_UNorm;
				
				if(m == 0){
					graphicsDesc.sampleCount = 1;
				}else{
					graphicsDesc.sampleCount = RB_GetMSAASampleCount();
				}
				

				backEnd.dynamicLightPipelines[RB_GetDynamicLightPipelineIndex(c, p, m)] = RHI_CreateGraphicsPipeline(&graphicsDesc);
			}
		}
	}
	
}


void RB_BeginRenderPass(const char* name, const RHI_RenderPass* rp){
	if(RHI_IsRenderingActive()){
		RB_EndRenderPass();
	}
	RHI_CmdBeginDebugLabel(name);
	RHI_BeginRendering(rp);
	
	if(backEnd.renderPassCount[backEnd.currentFrameIndex] < MAX_RENDERPASSES){
		uint32_t i = backEnd.renderPassCount[backEnd.currentFrameIndex]++;
		renderPass *currentPass = &backEnd.renderPasses[backEnd.currentFrameIndex][i];
		currentPass->query = RHI_CmdBeginDurationQuery();
		Q_strncpyz(currentPass->name, name, sizeof(currentPass->name));
		uint32_t nameHash = 0;
		CRC32_Begin(&nameHash);
		CRC32_ProcessBlock(&nameHash, name, strlen(name));
		CRC32_End(&nameHash);
		currentPass->nameHash = nameHash;
	}

	

}

void RB_EndRenderPass(void){
	
	if(RHI_IsRenderingActive()){
		assert(backEnd.renderPassCount[backEnd.currentFrameIndex] > 0);
		uint32_t i = backEnd.renderPassCount[backEnd.currentFrameIndex] - 1;
		renderPass *currentPass = &backEnd.renderPasses[backEnd.currentFrameIndex][i];
		RHI_CmdEndDurationQuery(currentPass->query);
		RHI_EndRendering();
		RHI_CmdEndDebugLabel();
	}
}

int RB_GetSamplerIndex(qbool clamp, qbool anisotropy){
	return (anisotropy * 2) + clamp;
}

qbool RB_IsMSAARequested(void){
	return RB_GetMSAASampleCount() >= 2;
}

uint32_t RB_GetMSAASampleCount(void){
	if(r_msaa->integer >= 8){
		return 8;
	}else if(r_msaa->integer >= 4){
		return 4;
	}else if(r_msaa->integer >= 2){
		return 2;
	}
	return 1;
}

qbool RB_IsViewportFullscreen(const viewParms_t *vp){
	return vp->viewportHeight == glConfig.vidHeight 
			&& vp->viewportWidth == glConfig.vidWidth 
			&& vp->viewportX == 0 
			&& vp->viewportY == 0;
}

void RB_FinishFullscreen3D(qbool prevFullscreen3D){
	if(prevFullscreen3D && !backEnd.fullscreen3D){
		if(backEnd.msaaActive){
			backEnd.msaaActive = qfalse;
			RB_MSAA_Resolve(backEnd.colorBufferMS, backEnd.colorBuffer);
		} else {
			rhiSampler gammaSampler = backEnd.sampler[RB_GetSamplerIndex(qtrue,qfalse)];
			RB_DrawGamma(backEnd.colorBuffer, gammaSampler, backEnd.colorBuffers[backEnd.colorBufferIndex ^ 1]); 
			backEnd.colorBufferIndex ^= 1;
			backEnd.colorBuffer = backEnd.colorBuffers[backEnd.colorBufferIndex];
			
		}
		backEnd.pipelineLayoutDirty = qtrue;
	}
	
	
}

void RB_BeginComputePass(const char* name){
	RHI_CmdBeginDebugLabel(name);
	
	if(backEnd.renderPassCount[backEnd.currentFrameIndex] < MAX_RENDERPASSES){
		uint32_t i = backEnd.renderPassCount[backEnd.currentFrameIndex]++;
		renderPass *currentPass = &backEnd.renderPasses[backEnd.currentFrameIndex][i];
		currentPass->query = RHI_CmdBeginDurationQuery();
		Q_strncpyz(currentPass->name, name, sizeof(currentPass->name));
		uint32_t nameHash = 0;
		CRC32_Begin(&nameHash);
		CRC32_ProcessBlock(&nameHash, name, strlen(name));
		CRC32_End(&nameHash);
		currentPass->nameHash = nameHash;
	}
}

void RB_EndComputePass(void){
	assert(backEnd.renderPassCount[backEnd.currentFrameIndex] > 0);
	uint32_t i = backEnd.renderPassCount[backEnd.currentFrameIndex] - 1;
	renderPass *currentPass = &backEnd.renderPasses[backEnd.currentFrameIndex][i];
	RHI_CmdEndDurationQuery(currentPass->query);
	RHI_CmdEndDebugLabel();
}
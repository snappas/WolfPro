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

// tr_shade.c

#include "tr_local.h"

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t tess;


static image_t* R_GetAnimatedImage( textureBundle_t *bundle ) {
	int index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic( bundle->videoMapHandle );
		ri.CIN_UploadCinematic( bundle->videoMapHandle );
		return NULL;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		if ( bundle->isLightmap && ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) ) {
			return tr.whiteImage;
		}
		return bundle->image[0];
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = myftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;  // may happen with shader time offsets
	}
	index %= bundle->numImageAnimations;

	if ( bundle->isLightmap && ( backEnd.refdef.rdflags & RDF_SNOOPERVIEW ) ) {
		return tr.whiteImage;
	}
	return bundle->image[ index ];

}

static image_t* R_GetAnimatedImageSafe( textureBundle_t *bundle ) {
	image_t *image = R_GetAnimatedImage(bundle);
	if(image == NULL){
		return tr.whiteImage;
	}
	return image;
}


/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state = ( shader->remappedShader ) ? shader->remappedShader : shader;

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.batchMode = TESS_BATCH_UNSET;
	tess.dlightBits = 0;        // will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = state->optimalStageIteratorFunc;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if ( tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime ) {
		tess.shaderTime = tess.shader->clampTime;
	}

	// done.
}

static void RB_WarnStreamOverflow( const char *streamName, uint32_t used, uint32_t capacity, uint32_t batchBytes ) {
	if ( backEnd.streamOverflowWarned ) {
		return;
	}
	backEnd.streamOverflowWarned = qtrue;
	ri.Printf( PRINT_WARNING, "WARNING: %s stream full (%u of %u bytes used), skipping %u byte batch of shader '%s'\n",
			   streamName, used, capacity, batchBytes, tess.shader->name );
}

/*
==============
RB_StreamVertices

Appends to this frame's vertex stream. Nothing is written on failure.
==============
*/
qbool RB_StreamVertices( const void *data, uint32_t byteCount, uint32_t *outOffset ) {
	VertexBuffers *vb = &backEnd.vertexBuffers[backEnd.currentFrameIndex];
	const uint32_t offset = ALIGN_UP( vb->vertexUsed, 16 );

	if ( byteCount > STREAM_VERTEX_BYTES - offset ) {
		RB_WarnStreamOverflow( "vertex", vb->vertexUsed, STREAM_VERTEX_BYTES, byteCount );
		return qfalse;
	}

	memcpy( vb->vertexData + offset, data, byteCount );
	vb->vertexUsed = offset + byteCount;
	*outOffset = offset;
	return qtrue;
}

/*
==============
RB_StreamIndices

Appends to this frame's index stream. Nothing is written on failure.
==============
*/
qbool RB_StreamIndices( const void *data, uint32_t indexCount, uint32_t *outFirstIndex ) {
	VertexBuffers *vb = &backEnd.vertexBuffers[backEnd.currentFrameIndex];
	const uint32_t byteCount = indexCount * (uint32_t)sizeof( glIndex_t );

	if ( byteCount > STREAM_INDEX_BYTES - vb->indexUsed ) {
		RB_WarnStreamOverflow( "index", vb->indexUsed, STREAM_INDEX_BYTES, byteCount );
		return qfalse;
	}

	memcpy( vb->indexData + vb->indexUsed, data, byteCount );
	*outFirstIndex = vb->indexUsed / (uint32_t)sizeof( glIndex_t );
	vb->indexUsed += byteCount;
	return qtrue;
}

// tess.indexes are batch-relative, each draw reaches its vertices by bind offset
static qbool RB_StreamBatchGeometry( uint32_t *outFirstIndex, uint32_t *outPositionOffset ) {
	return RB_StreamIndices( tess.indexes, tess.numIndexes, outFirstIndex )
		&& RB_StreamVertices( tess.xyz, (uint32_t)( tess.numVertexes * sizeof( tess.xyz[0] ) ), outPositionOffset );
}



/*
===============
ComputeColors
===============
*/
static void ComputeColors( shaderStage_t *pStage ) {
	int i;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
	case CGEN_IDENTITY:
		memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
		break;
	default:
	case CGEN_IDENTITY_LIGHTING:
		if(tess.shader->lightmapIndex == LIGHTMAP_2D){
			memset( tess.svars.colors, 255, tess.numVertexes * 4 );	
		}else{
			memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
		}
		break;
	case CGEN_LIGHTING_DIFFUSE:
		RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
		break;
	case CGEN_EXACT_VERTEX:
		memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
		break;
	case CGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			*(int *)tess.svars.colors[i] = *(int *)pStage->constantColor;
		}
		break;
	case CGEN_VERTEX:
		if ( tr.identityLight == 1 || tess.shader->lightmapIndex == LIGHTMAP_2D) {
			memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
		} else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
				tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
				tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case CGEN_ONE_MINUS_VERTEX:
		if ( tr.identityLight == 1 || tess.shader->lightmapIndex == LIGHTMAP_2D) {
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
				tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
				tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
			}
		} else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
				tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
				tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
			}
		}
		break;
	case CGEN_FOG:
	{
		fog_t       *fog;

		fog = tr.world->fogs + tess.fogNum;

		for ( i = 0; i < tess.numVertexes; i++ ) {
			*( unsigned int * )&tess.svars.colors[i] = fog->colorInt;
		}
	}
	break;
	case CGEN_WAVEFORM:
		RB_CalcWaveColor( &pStage->rgbWave, ( unsigned char * ) tess.svars.colors );
		break;
	case CGEN_ENTITY:
		RB_CalcColorFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case CGEN_ONE_MINUS_ENTITY:
		RB_CalcColorFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( pStage->rgbGen != CGEN_IDENTITY ) {
			if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 pStage->rgbGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( pStage->rgbGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
		// Ridah
	case AGEN_NORMALZFADE:
	{
		float alpha, range, lowest, highest, dot;
		vec3_t worldUp;
		qboolean zombieEffect = qfalse;

		if ( VectorCompare( backEnd.currentEntity->e.fireRiseDir, vec3_origin ) ) {
			VectorSet( backEnd.currentEntity->e.fireRiseDir, 0, 0, 1 );
		}

		if ( backEnd.currentEntity->e.hModel ) {    // world surfaces dont have an axis
			VectorRotate( backEnd.currentEntity->e.fireRiseDir, backEnd.currentEntity->e.axis, worldUp );
		} else {
			VectorCopy( backEnd.currentEntity->e.fireRiseDir, worldUp );
		}

		lowest = pStage->zFadeBounds[0];
		if ( lowest == -1000 ) {    // use entity alpha
			lowest = backEnd.currentEntity->e.shaderTime;
			zombieEffect = qtrue;
		}
		highest = pStage->zFadeBounds[1];
		if ( highest == -1000 ) {   // use entity alpha
			highest = backEnd.currentEntity->e.shaderTime;
			zombieEffect = qtrue;
		}
		range = highest - lowest;
		for ( i = 0; i < tess.numVertexes; i++ ) {
			dot = DotProduct( tess.normal[i], worldUp );

			// special handling for Zombie fade effect
			if ( zombieEffect ) {
				alpha = (float)backEnd.currentEntity->e.shaderRGBA[3] * ( dot + 1.0 ) / 2.0;
				alpha += ( 2.0 * (float)backEnd.currentEntity->e.shaderRGBA[3] ) * ( 1.0 - ( dot + 1.0 ) / 2.0 );
				if ( alpha > 255.0 ) {
					alpha = 255.0;
				} else if ( alpha < 0.0 ) {
					alpha = 0.0;
				}
				tess.svars.colors[i][3] = (byte)( alpha );
				continue;
			}

			if ( dot < highest ) {
				if ( dot > lowest ) {
					if ( dot < lowest + range / 2 ) {
						alpha = ( (float)pStage->constantColor[3] * ( ( dot - lowest ) / ( range / 2 ) ) );
					} else {
						alpha = ( (float)pStage->constantColor[3] * ( 1.0 - ( ( dot - lowest - range / 2 ) / ( range / 2 ) ) ) );
					}
					if ( alpha > 255.0 ) {
						alpha = 255.0;
					} else if ( alpha < 0.0 ) {
						alpha = 0.0;
					}

					// finally, scale according to the entity's alpha
					if ( backEnd.currentEntity->e.hModel ) {
						alpha *= (float)backEnd.currentEntity->e.shaderRGBA[3] / 255.0;
					}

					tess.svars.colors[i][3] = (byte)( alpha );
				} else {
					tess.svars.colors[i][3] = 0;
				}
			} else {
				tess.svars.colors[i][3] = 0;
			}
		}
	}
	break;
		// done.
	case AGEN_VERTEX:
		if ( pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
		}
		break;
	case AGEN_PORTAL:
	{
		unsigned char alpha;

		for ( i = 0; i < tess.numVertexes; i++ )
		{
			float len;
			vec3_t v;

			VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
			len = VectorLength( v );

			len /= tess.shader->portalRange;

			if ( len < 0 ) {
				alpha = 0;
			} else if ( len > 1 )   {
				alpha = 0xff;
			} else
			{
				alpha = len * 0xff;
			}

			tess.svars.colors[i][3] = alpha;
		}
	}
	break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum ) {
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
}

/*
===============
ComputeTexCoords
===============
*/
static void ComputeTexCoords( shaderStage_t *pStage ) {
	int i;
	int b;

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
		int tm;

		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
		case TCGEN_IDENTITY:
			memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
			break;
		case TCGEN_TEXTURE:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][0][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][0][1];
			}
			break;
		case TCGEN_LIGHTMAP:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][1][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][1][1];
			}
			break;
		case TCGEN_VECTOR:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
				tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
			}
			break;
		case TCGEN_FOG:
			RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_ENVIRONMENT_MAPPED:
			RB_CalcEnvironmentTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_FIRERISEENV_MAPPED:
			RB_CalcFireRiseEnvTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_BAD:
			return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ ) {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;        // break out of for loop
				break;

			case TMOD_SWAP:
				RB_CalcSwapTexCoords( ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave,
										   ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord,
										( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].scroll,
										( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].scale,
									   ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave,
										 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm],
										   ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].rotateSpeed,
										( float * ) tess.svars.texcoords[b] );
				break;

			default:
				ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'\n", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}



extern void R_Fog( glfog_t *curfog );

/*
==============
SetIteratorFog
	set the fog parameters for this pass
==============
*/
void SetIteratorFog( void ) {
	// changed for problem when you start the game with r_fastsky set to '1'
//	if(r_fastsky->integer || backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
	if ( backEnd.refdef.rdflags & RDF_NOWORLDMODEL ) {
		R_FogOff();
		return;
	}

	if ( backEnd.refdef.rdflags & RDF_DRAWINGSKY ) {
		if ( glfogsettings[FOG_SKY].registered ) {
			R_Fog( &glfogsettings[FOG_SKY] );
		} else {
			R_FogOff();
		}

		return;
	}

	if ( skyboxportal && backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) {
		if ( glfogsettings[FOG_PORTALVIEW].registered ) {
			R_Fog( &glfogsettings[FOG_PORTALVIEW] );
		} else {
			R_FogOff();
		}
	} else {
		if ( glfogNum > FOG_NONE ) {
			R_Fog( &glfogsettings[FOG_CURRENT] );
		} else {
			R_FogOff();
		}
	}
}

static uint32_t AlphaTestMode(uint32_t stateBits){
	switch(stateBits & GLS_ATEST_BITS){
		case GLS_ATEST_GT_0:
			return 1;
		case GLS_ATEST_LT_80:
			return 2;
		case GLS_ATEST_GE_80:
			return 3;
		default:
			return 0;
	}

}

static void RB_IterateStagesGenericVulkan(shaderCommands_t *input ){
	
	VertexBuffers *vb = &backEnd.vertexBuffers[backEnd.currentFrameIndex];

	uint32_t firstIndex;
	uint32_t positionOffset;
	if(!RB_StreamBatchGeometry(&firstIndex, &positionOffset)){
		return;
	}

	// stream every stage before drawing any, so a full stream can't leave
	// a batch partly drawn
	uint32_t colorOffset[MAX_SHADER_STAGES];
	uint32_t texCoordOffset[MAX_SHADER_STAGES];
	int stageCount = 0;
	for(int i = 0; i < MAX_SHADER_STAGES; i++){
		shaderStage_t *pStage = tess.xstages[i];
		//if (pStage == NULL || !pStage->active) {
		if (pStage == NULL) {
			break;
		}

		ComputeColors( pStage );
		ComputeTexCoords( pStage );

		if(!RB_StreamVertices(tess.svars.colors, (uint32_t)(tess.numVertexes * sizeof(tess.svars.colors[0])), &colorOffset[i])
			|| !RB_StreamVertices(tess.svars.texcoords, (uint32_t)(tess.numVertexes * sizeof(float) * 2), &texCoordOffset[i])){
			return;
		}
		stageCount++;
	}

	for(int i = 0; i < stageCount; i++){
		shaderStage_t *pStage = tess.xstages[i];
		rhiPipeline pipeline = pStage->pipeline[backEnd.msaaActive ? 1 : 0];

		pixelShaderPushConstants pc; 
		image_t *currentImage = R_GetAnimatedImageSafe(&pStage->bundle[0]);
		qbool clamp = currentImage->wrapClampMode == GL_CLAMP;
		qbool anisotropy = RB_GetAnisotropy() > 1 && !backEnd.projection2D && !currentImage->lightMap;

		pc.packedData = 0;
		pc.packedData |= currentImage->descriptorIndex;
		pc.packedData |= RB_GetSamplerIndex(clamp, anisotropy) << 11;
		pc.packedData |= AlphaTestMode(pStage->stateBits) << 13;
		pc.packedData |= ((uint32_t)(Com_Clamp(0.0f, 1.0f, r_alphaboost->value) * 255.0f)) << 15;

		int writeShaderIndex = RB_IsViewportFullscreen(&backEnd.viewParms) && !backEnd.projection2D;
		pc.pixelCenterXY = (glConfig.vidWidth / 2) | ((glConfig.vidHeight / 2) << 16);
		pc.shaderIndex = (tess.shader->index) | ((backEnd.currentFrameIndex) << 13) | (writeShaderIndex << 14);

		
		if(backEnd.previousPipeline.h != pipeline.h || backEnd.pipelineLayoutDirty){
			RHI_CmdBindPipeline(pipeline);
			backEnd.previousPipeline = pipeline;
			backEnd.pipelineChangeCount++;
		}
		if(backEnd.currentDescriptorSet.h == 0 || backEnd.pipelineLayoutDirty){
			RHI_CmdBindDescriptorSet(pipeline, backEnd.descriptorSet);
			backEnd.currentDescriptorSet = backEnd.descriptorSet;
			backEnd.pipelineLayoutDirty = qfalse;
		}

		rhiBuffer buffers[] = {vb->vertex, vb->vertex, vb->vertex};
		uint32_t offsets[] = {positionOffset, colorOffset[i], texCoordOffset[i]};
		RHI_CmdBindVertexBuffers(buffers, offsets, ARRAY_LEN(buffers));

		RHI_CmdPushConstants(pipeline, RHI_Shader_Vertex, backEnd.or.modelMatrix,sizeof(backEnd.or.modelMatrix));
		RHI_CmdPushConstants(pipeline, RHI_Shader_Pixel, &pc, sizeof(pc));

		RHI_CmdDrawIndexed(tess.numIndexes, firstIndex, 0);
	}
}

uint32_t packColorR11G11B10(vec3_t color){
	uint32_t res = 0;
	res |= (uint32_t)(color[0] * 2047) & 0x7FF;
	res |= ((uint32_t)(color[1] * 2047) & 0x7FF) << 11;
	res |= ((uint32_t)(color[2] * 1023) & 0x3FF) << 22;
	return res;
}

static void RB_DrawDynamicLight(void){
		
	VertexBuffers *vb = &backEnd.vertexBuffers[backEnd.currentFrameIndex];

	int diffuseStageIndex = 0; //@TODO
	shaderStage_t *pStage = tess.xstages[diffuseStageIndex];
	if(pStage == NULL){
		return;
	}

	// dlights are optional, leave the rest of the stream to transparents and 2D
	if(vb->vertexUsed > DLIGHT_STREAM_LIMIT){
		return;
	}

	uint32_t firstIndex;
	uint32_t positionOffset;
	uint32_t normalOffset;
	if(!RB_StreamBatchGeometry(&firstIndex, &positionOffset)
		|| !RB_StreamVertices(tess.normal, (uint32_t)(tess.numVertexes * sizeof(tess.normal[0])), &normalOffset)){
		return;
	}

	ComputeColors( pStage );
	ComputeTexCoords( pStage );

	uint32_t texCoordOffset;
	uint32_t colorOffset;
	if(!RB_StreamVertices(tess.svars.texcoords, (uint32_t)(tess.numVertexes * sizeof(float) * 2), &texCoordOffset)
		|| !RB_StreamVertices(tess.svars.colors, (uint32_t)(tess.numVertexes * sizeof(tess.svars.colors[0])), &colorOffset)){
		return;
	}

	dynamicLightPushConstants pc = {}; 
	image_t *currentImage = R_GetAnimatedImageSafe(&pStage->bundle[0]);
	qbool clamp = currentImage->wrapClampMode == GL_CLAMP;
	qbool anisotropy = RB_GetAnisotropy() > 1 && !backEnd.projection2D && !currentImage->lightMap;

	pc.samplerIndex = RB_GetSamplerIndex(clamp, anisotropy);
	pc.textureIndex = currentImage->descriptorIndex;
	pc.lightColor = packColorR11G11B10(tess.dlight->color);
	VectorCopy(tess.dlight->transformed, pc.lightPos);
	pc.lightRadius = tess.dlight->radius;

	shader_t *shader = tess.shader;
	
	rhiPipeline pipeline = backEnd.dynamicLightPipelines[RB_GetDynamicLightPipelineIndex(shader->cullType, shader->polygonOffset, backEnd.msaaActive)];
	if(backEnd.previousPipeline.h != pipeline.h || backEnd.pipelineLayoutDirty){
		RHI_CmdBindPipeline(pipeline);
		backEnd.previousPipeline = pipeline;
		backEnd.pipelineChangeCount++;
	}
	if(backEnd.currentDescriptorSet.h == 0 || backEnd.pipelineLayoutDirty){
		RHI_CmdBindDescriptorSet(pipeline, backEnd.descriptorSet);
		backEnd.currentDescriptorSet = backEnd.descriptorSet;
		backEnd.pipelineLayoutDirty = qfalse;
	}

	rhiBuffer buffers[] = {vb->vertex, vb->vertex, vb->vertex, vb->vertex};
	uint32_t offsets[] = {positionOffset, texCoordOffset, colorOffset, normalOffset};
	RHI_CmdBindVertexBuffers(buffers, offsets, ARRAY_LEN(buffers));

	RHI_CmdPushConstants(pipeline, RHI_Shader_Vertex, backEnd.or.modelMatrix,sizeof(backEnd.or.modelMatrix));
	RHI_CmdPushConstants(pipeline, RHI_Shader_Pixel, &pc, sizeof(pc));

	RHI_CmdDrawIndexed(tess.numIndexes, firstIndex, 0);
}

/*
** RB_IterateStagesGeneric
*/
static void RB_IterateStagesGeneric( shaderCommands_t *input ) {
	
	RB_IterateStagesGenericVulkan(input);
#if 0
	int stage;
	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = tess.xstages[stage];

		if ( !pStage ) {
			break;
		}

		ComputeColors( pStage );
		ComputeTexCoords( pStage );


		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != 0 ) {
			DrawMultitextured( input, stage );
		} else
		{
			int fadeStart, fadeEnd;


			//
			// set state
			//
			if ( pStage->bundle[0].vertexLightmap && ( ( r_vertexLight->integer && !r_uiFullScreen->integer ) || glConfig.hardwareType == GLHW_PERMEDIA2 ) && r_lightmap->integer ) {
				GL_Bind( tr.whiteImage );
			} else {
				R_BindAnimatedImage( &pStage->bundle[0] );
			}

			// Ridah, per stage fogging (detail textures)
			if ( tess.shader->noFog && pStage->isFogged ) {
				R_FogOn();
			} else if ( tess.shader->noFog && !pStage->isFogged ) {
				R_FogOff(); // turn it back off
			} else {    // make sure it's on
				R_FogOn();
			}
			// done.

			//----(SA)	fading model stuff
			fadeStart = backEnd.currentEntity->e.fadeStartTime;

			if ( fadeStart ) {
				fadeEnd = backEnd.currentEntity->e.fadeEndTime;
				if ( fadeStart > tr.refdef.time ) {       // has not started to fade yet
					GL_State( pStage->stateBits );
				} else
				{
					int i;
					unsigned int tempState;
					float alphaval;

					if ( fadeEnd < tr.refdef.time ) {     // entity faded out completely
						continue;
					}

					alphaval = (float)( fadeEnd - tr.refdef.time ) / (float)( fadeEnd - fadeStart );

					tempState = pStage->stateBits;
					// remove the current blend, and don't write to Z buffer
					tempState &= ~( GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS | GLS_DEPTHMASK_TRUE );
					// set the blend to src_alpha, dst_one_minus_src_alpha
					tempState |= ( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
					GL_State( tempState );
					GL_Cull( CT_FRONT_SIDED );
					// modulate the alpha component of each vertex in the render list
					for ( i = 0; i < tess.numVertexes; i++ ) {
						tess.svars.colors[i][0] *= alphaval;
						tess.svars.colors[i][1] *= alphaval;
						tess.svars.colors[i][2] *= alphaval;
						tess.svars.colors[i][3] *= alphaval;
					}
				}
			} else {
				GL_State( pStage->stateBits );
			}
			//----(SA)	end

			//
			// draw
			//
			R_DrawElements( input->numIndexes, input->indexes );
		}
		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap || pStage->bundle[0].vertexLightmap ) ) {
			break;
		}
	}
#endif
}


/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void ) {
	shaderCommands_t *input;

	input = &tess;

	RB_DeformTessGeometry(); //animations on the gpu ready buffer data


	// set GL fog
	SetIteratorFog();


	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		 && !( tess.shader->surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) ) ) {
		//ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		//RB_FogPass();
	}

}


/*
** RB_StageIteratorVertexLitTexture
*/
void RB_StageIteratorVertexLitTexture( void ) {
	#if 0
	shaderCommands_t *input;
	shader_t        *shader;

	input = &tess;

	shader = input->shader;

	//
	// compute colors
	//
	RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );




	// set GL fog
	SetIteratorFog();

	//
	// call special shade routine
	//
	//@TODO
	// R_GetAnimatedImage( &tess.xstages[0]->bundle[0] );
	// R_DrawElements( input->numIndexes, input->indexes );

	//
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}
		#endif

}

//define	REPLACE_MODE

void RB_StageIteratorLightmappedMultitexture( void ) {
	shaderStage_t *pStage = tess.xstages[0];

	//if (pStage == NULL || !pStage->active) {
	if (pStage == NULL) {
		return;
	}

	rhiPipeline pipeline = pStage->pipeline[backEnd.msaaActive ? 1 : 0];

	VertexBuffers *vb = &backEnd.vertexBuffers[backEnd.currentFrameIndex];

	uint32_t firstIndex;
	rhiBuffer buffers[2];
	uint32_t offsets[2] = {0, 0};
	if(tess.batchMode == TESS_BATCH_STATIC){
		if(!RB_StreamIndices(tess.indexes, tess.numIndexes, &firstIndex)){
			return;
		}
		buffers[0] = backEnd.worldPositions;
		buffers[1] = backEnd.worldTexCoords;
	}else{
		if(!RB_StreamBatchGeometry(&firstIndex, &offsets[0])
			|| !RB_StreamVertices(tess.texCoords, (uint32_t)(tess.numVertexes * sizeof(float) * 4), &offsets[1])){
			return;
		}
		buffers[0] = vb->vertex;
		buffers[1] = vb->vertex;
	}

	pixelShaderPushConstants2 pc; 
	image_t *image[2];
	qbool clamp[2];
	qbool anisotropy[2];

	for(int i = 0; i < 2; i++){
		image[i] = R_GetAnimatedImageSafe(&pStage->bundle[i]);
		clamp[i] = image[i]->wrapClampMode == GL_CLAMP;
		anisotropy[i] = RB_GetAnisotropy() > 1 && !backEnd.projection2D && !image[i]->lightMap;
	}

	
	pc.packedData = 0;
	pc.packedData |= image[0]->descriptorIndex;
	pc.packedData |= RB_GetSamplerIndex(clamp[0], anisotropy[0]) << 11;
	pc.packedData |= image[1]->descriptorIndex << 13;
	pc.packedData |= RB_GetSamplerIndex(clamp[1], anisotropy[1]) << 24;
	

	int writeShaderIndex = RB_IsViewportFullscreen(&backEnd.viewParms) && !backEnd.projection2D;
	pc.pixelCenterXY = (glConfig.vidWidth / 2) | ((glConfig.vidHeight / 2) << 16);
	pc.shaderIndex = (tess.shader->index) | ((backEnd.currentFrameIndex) << 13) | (writeShaderIndex << 14);

	
	if(backEnd.previousPipeline.h != pipeline.h  || backEnd.pipelineLayoutDirty){
		RHI_CmdBindPipeline(pipeline);
		backEnd.previousPipeline = pipeline;
		backEnd.pipelineChangeCount++;
	}
	if(backEnd.currentDescriptorSet.h == 0 || backEnd.pipelineLayoutDirty){
		RHI_CmdBindDescriptorSet(pipeline, backEnd.descriptorSet);
		backEnd.currentDescriptorSet = backEnd.descriptorSet;
		backEnd.pipelineLayoutDirty = qfalse;
	}

	RHI_CmdBindVertexBuffers(buffers, offsets, ARRAY_LEN(buffers));

	RHI_CmdPushConstants(pipeline, RHI_Shader_Vertex, backEnd.or.modelMatrix,sizeof(backEnd.or.modelMatrix));
	RHI_CmdPushConstants(pipeline, RHI_Shader_Pixel, &pc, sizeof(pc));

	RHI_CmdDrawIndexed(tess.numIndexes, firstIndex, 0);
}

/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

	input = &tess;

	if ( input->numIndexes == 0 ) {
		return;
	}

	if ( input->indexes[SHADER_MAX_INDEXES - 1] != 0 ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit" );
	}
	if ( input->xyz[SHADER_MAX_VERTEXES - 1][0] != 0 ) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit" );
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	//
	// update performance counters
	//
	backEnd.pc.c_shaders++;
	backEnd.pc.c_vertexes += tess.numVertexes;
	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	//
	// call off to shader specific tess end function
	//
	if(tess.renderType == RT_GENERIC){
		tess.currentStageIteratorFunc();
	}else if(tess.renderType == RT_DYNAMICLIGHT){
		RB_DrawDynamicLight();
	}
	

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer ) {
		//@TODO 
	}
	if ( r_shownormals->integer ) {
		//@TODO
	}


	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
	tess.batchMode = TESS_BATCH_UNSET;  // 2D can append without a new RB_BeginSurface

}


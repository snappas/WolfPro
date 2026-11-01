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

/*
 * name:		tr_main.c
 *
 * desc:		main control flow for each frame
 *
*/

#include "tr_local.h"

trGlobals_t tr;

static float s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};


refimport_t ri;

// entities that will have procedurally generated surfaces will just
// point at this for their sorting surface
surfaceType_t entitySurface = SF_ENTITY;

// fog stuff
glfog_t glfogsettings[NUM_FOGS];
glfogType_t glfogNum = FOG_NONE;
qboolean fogIsOn = qfalse;


/*
=================
R_Fog (void)
=================
*/
void R_Fog( glfog_t *curfog ) {
	static glfog_t setfog;

	if ( !r_wolffog->integer ) {
		R_FogOff();
		return;
	}

	if ( !curfog->registered ) {   //----(SA)
		R_FogOff();
		return;
	}

	//----(SA) assme values of '0' for these parameters means 'use default'
	if ( !curfog->density ) {
		curfog->density = 1;
	}
	if ( !curfog->hint ) {
		curfog->hint = GL_DONT_CARE;
	}
	if ( !curfog->mode ) {
		curfog->mode = GL_LINEAR;
	}
	//----(SA)	end


	R_FogOn();

	// only send changes if necessary

	setfog.registered = qtrue;


}

// Ridah, allow disabling fog temporarily
void R_FogOff( void ) {
	if ( !fogIsOn ) {
		return;
	}
	fogIsOn = qfalse;
}

void R_FogOn( void ) {
	if ( fogIsOn ) {
		return;
	}

	if ( r_uiFullScreen->integer ) {   // don't fog in the menu
		R_FogOff();
		return;
	}

	if ( !r_wolffog->integer ) {
		return;
	}

//	if(backEnd.viewParms.isGLFogged) {
//		if(!(backEnd.viewParms.glFog.registered))
//			return;
//	}

	if ( backEnd.refdef.rdflags & RDF_SKYBOXPORTAL ) { // don't force world fog on portal sky
		if ( !( glfogsettings[FOG_PORTALVIEW].registered ) ) {
			return;
		}
	} else if ( !glfogNum )     {
		return;
	}

	fogIsOn = qtrue;
}
// done.



//----(SA)
/*
==============
R_SetFog

  if fogvar == FOG_CMD_SWITCHFOG {
	fogvar is the command
	var1 is the fog to switch to
	var2 is the time to transition
  }
  else {
	fogvar is the fog that's being set
	var1 is the near fog z value
	var2 is the far fog z value
	rgb = color
	density is density, and is used to derive the values of 'mode', 'drawsky', and 'clearscreen'
  }
==============
*/
void R_SetFog( int fogvar, int var1, int var2, float r, float g, float b, float density ) {
	if ( fogvar != FOG_CMD_SWITCHFOG ) {   // just set the parameters and return

		if ( var1 == 0 && var2 == 0 ) {    // clear this fog
			glfogsettings[fogvar].registered = qfalse;
			return;
		}

		glfogsettings[fogvar].color[0]      = r;
		glfogsettings[fogvar].color[1]      = g;
		glfogsettings[fogvar].color[2]      = b;
		glfogsettings[fogvar].color[3]      = 1;
		glfogsettings[fogvar].start         = var1;
		glfogsettings[fogvar].end           = var2;
		if ( density > 1 ) {
			glfogsettings[fogvar].mode          = GL_LINEAR;
			glfogsettings[fogvar].drawsky       = qfalse;
			glfogsettings[fogvar].clearscreen   = qtrue;
			glfogsettings[fogvar].density       = 1.0;
		} else
		{
			glfogsettings[fogvar].mode          = GL_EXP;
			glfogsettings[fogvar].drawsky       = qtrue;
			glfogsettings[fogvar].clearscreen   = qfalse;
			glfogsettings[fogvar].density       = density;
		}
		glfogsettings[fogvar].hint          = GL_DONT_CARE;
		glfogsettings[fogvar].registered    = qtrue;

		return;
	}

	// don't switch to invalid fogs
	if ( glfogsettings[var1].registered != qtrue ) {
		return;
	}

	glfogNum = var1;

	// transitioning to new fog, store the current values as the 'from'

	if ( glfogsettings[FOG_CURRENT].registered ) {
		memcpy( &glfogsettings[FOG_LAST], &glfogsettings[FOG_CURRENT], sizeof( glfog_t ) );
	} else {
		// if no current fog fall back to world fog
		// FIXME: handle transition if there is no FOG_MAP fog
		memcpy( &glfogsettings[FOG_LAST], &glfogsettings[FOG_MAP], sizeof( glfog_t ) );
	}

	memcpy( &glfogsettings[FOG_TARGET], &glfogsettings[glfogNum], sizeof( glfog_t ) );

	// setup transition times
	glfogsettings[FOG_TARGET].startTime = tr.refdef.time;
	glfogsettings[FOG_TARGET].finishTime = tr.refdef.time + var2;
}

//----(SA) end

/*
=================
R_CullLocalBox

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
int R_CullLocalBox( vec3_t bounds[2] ) {
	int i, j;
	vec3_t transformed[8];
	float dists[8];
	vec3_t v;
	cplane_t    *frust;
	int anyBack;
	int front, back;

	if ( r_nocull->integer ) {
		return CULL_CLIP;
	}

	// transform into world space
	for ( i = 0 ; i < 8 ; i++ ) {
		v[0] = bounds[i & 1][0];
		v[1] = bounds[( i >> 1 ) & 1][1];
		v[2] = bounds[( i >> 2 ) & 1][2];

		VectorCopy( tr.or.origin, transformed[i] );
		VectorMA( transformed[i], v[0], tr.or.axis[0], transformed[i] );
		VectorMA( transformed[i], v[1], tr.or.axis[1], transformed[i] );
		VectorMA( transformed[i], v[2], tr.or.axis[2], transformed[i] );
	}

	// check against frustum planes
	anyBack = 0;
	for ( i = 0 ; i < 4 ; i++ ) {
		frust = &tr.viewParms.frustum[i];

		front = back = 0;
		for ( j = 0 ; j < 8 ; j++ ) {
			dists[j] = DotProduct( transformed[j], frust->normal );
			if ( dists[j] > frust->dist ) {
				front = 1;
				if ( back ) {
					break;      // a point is in front
				}
			} else {
				back = 1;
			}
		}
		if ( !front ) {
			// all points were behind one of the planes
			return CULL_OUT;
		}
		anyBack |= back;
	}

	if ( !anyBack ) {
		return CULL_IN;     // completely inside frustum
	}

	return CULL_CLIP;       // partially clipped
}

/*
** R_CullLocalPointAndRadius
*/
int R_CullLocalPointAndRadius( vec3_t pt, float radius ) {
	vec3_t transformed;

	R_LocalPointToWorld( pt, transformed );

	return R_CullPointAndRadius( transformed, radius );
}

/*
** R_CullPointAndRadius
*/
int R_CullPointAndRadius( vec3_t pt, float radius ) {
	int i;
	float dist;
	cplane_t    *frust;
	qboolean mightBeClipped = qfalse;

	if ( r_nocull->integer ) {
		return CULL_CLIP;
	}

	// check against frustum planes
	for ( i = 0 ; i < 4 ; i++ )
	{
		frust = &tr.viewParms.frustum[i];

		dist = DotProduct( pt, frust->normal ) - frust->dist;
		if ( dist < -radius ) {
			return CULL_OUT;
		} else if ( dist <= radius )   {
			mightBeClipped = qtrue;
		}
	}

	if ( mightBeClipped ) {
		return CULL_CLIP;
	}

	return CULL_IN;     // completely inside frustum
}


/*
=================
R_LocalNormalToWorld

=================
*/
void R_LocalNormalToWorld( vec3_t local, vec3_t world ) {
	world[0] = local[0] * tr.or.axis[0][0] + local[1] * tr.or.axis[1][0] + local[2] * tr.or.axis[2][0];
	world[1] = local[0] * tr.or.axis[0][1] + local[1] * tr.or.axis[1][1] + local[2] * tr.or.axis[2][1];
	world[2] = local[0] * tr.or.axis[0][2] + local[1] * tr.or.axis[1][2] + local[2] * tr.or.axis[2][2];
}

/*
=================
R_LocalPointToWorld

=================
*/
void R_LocalPointToWorld( vec3_t local, vec3_t world ) {
	world[0] = local[0] * tr.or.axis[0][0] + local[1] * tr.or.axis[1][0] + local[2] * tr.or.axis[2][0] + tr.or.origin[0];
	world[1] = local[0] * tr.or.axis[0][1] + local[1] * tr.or.axis[1][1] + local[2] * tr.or.axis[2][1] + tr.or.origin[1];
	world[2] = local[0] * tr.or.axis[0][2] + local[1] * tr.or.axis[1][2] + local[2] * tr.or.axis[2][2] + tr.or.origin[2];
}

/*
=================
R_WorldToLocal

=================
*/
void R_WorldToLocal( vec3_t world, vec3_t local ) {
	local[0] = DotProduct( world, tr.or.axis[0] );
	local[1] = DotProduct( world, tr.or.axis[1] );
	local[2] = DotProduct( world, tr.or.axis[2] );
}

/*
==========================
R_TransformModelToClip

==========================
*/
void R_TransformModelToClip( const vec3_t src, const float *modelMatrix, const float *projectionMatrix,
							 vec4_t eye, vec4_t dst ) {
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		eye[i] =
			src[0] * modelMatrix[ i + 0 * 4 ] +
			src[1] * modelMatrix[ i + 1 * 4 ] +
			src[2] * modelMatrix[ i + 2 * 4 ] +
			1 * modelMatrix[ i + 3 * 4 ];
	}

	for ( i = 0 ; i < 4 ; i++ ) {
		dst[i] =
			eye[0] * projectionMatrix[ i + 0 * 4 ] +
			eye[1] * projectionMatrix[ i + 1 * 4 ] +
			eye[2] * projectionMatrix[ i + 2 * 4 ] +
			eye[3] * projectionMatrix[ i + 3 * 4 ];
	}
}

/*
==========================
R_TransformClipToWindow

==========================
*/
void R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window ) {
	normalized[0] = clip[0] / clip[3];
	normalized[1] = clip[1] / clip[3];
	normalized[2] = ( clip[2] + clip[3] ) / ( 2 * clip[3] );

	window[0] = 0.5f * ( 1.0f + normalized[0] ) * view->viewportWidth;
	window[1] = 0.5f * ( 1.0f + normalized[1] ) * view->viewportHeight;
	window[2] = normalized[2];

	window[0] = (int) ( window[0] + 0.5 );
	window[1] = (int) ( window[1] + 0.5 );
}


/*
==========================
R_MultMatrix

==========================
*/
static void R_MultMatrix( const float *a, const float *b, float *out ) {
	int i, j;

	for ( i = 0 ; i < 4 ; i++ ) {
		for ( j = 0 ; j < 4 ; j++ ) {
			out[ i * 4 + j ] =
				a [ i * 4 + 0 ] * b [ 0 * 4 + j ]
				+ a [ i * 4 + 1 ] * b [ 1 * 4 + j ]
				+ a [ i * 4 + 2 ] * b [ 2 * 4 + j ]
				+ a [ i * 4 + 3 ] * b [ 3 * 4 + j ];
		}
	}
}

/*
=================
R_RotateForEntity

Generates an orientation for an entity and viewParms
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms,
						orientationr_t *or ) {
	float glMatrix[16];
	vec3_t delta;
	float axisLength;

	if ( ent->e.reType != RT_MODEL ) {
		*or = viewParms->world;
		return;
	}

	VectorCopy( ent->e.origin, or->origin );

	VectorCopy( ent->e.axis[0], or->axis[0] );
	VectorCopy( ent->e.axis[1], or->axis[1] );
	VectorCopy( ent->e.axis[2], or->axis[2] );

	glMatrix[0] = or->axis[0][0];
	glMatrix[4] = or->axis[1][0];
	glMatrix[8] = or->axis[2][0];
	glMatrix[12] = or->origin[0];

	glMatrix[1] = or->axis[0][1];
	glMatrix[5] = or->axis[1][1];
	glMatrix[9] = or->axis[2][1];
	glMatrix[13] = or->origin[1];

	glMatrix[2] = or->axis[0][2];
	glMatrix[6] = or->axis[1][2];
	glMatrix[10] = or->axis[2][2];
	glMatrix[14] = or->origin[2];

	glMatrix[3] = 0;
	glMatrix[7] = 0;
	glMatrix[11] = 0;
	glMatrix[15] = 1;

	R_MultMatrix( glMatrix, viewParms->world.modelMatrix, or->modelMatrix );

	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( viewParms->or.origin, or->origin, delta );

	// compensate for scale in the axes if necessary
	if ( ent->e.nonNormalizedAxes ) {
		axisLength = VectorLength( ent->e.axis[0] );
		if ( !axisLength ) {
			axisLength = 0;
		} else {
			axisLength = 1.0f / axisLength;
		}
	} else {
		axisLength = 1.0f;
	}

	or->viewOrigin[0] = DotProduct( delta, or->axis[0] ) * axisLength;
	or->viewOrigin[1] = DotProduct( delta, or->axis[1] ) * axisLength;
	or->viewOrigin[2] = DotProduct( delta, or->axis[2] ) * axisLength;
}

/*
=================
R_RotateForViewer

Sets up the modelview matrix for a given viewParm
=================
*/
void R_RotateForViewer( void ) {
	float viewerMatrix[16];
	vec3_t origin;

	memset( &tr.or, 0, sizeof( tr.or ) );
	tr.or.axis[0][0] = 1;
	tr.or.axis[1][1] = 1;
	tr.or.axis[2][2] = 1;
	VectorCopy( tr.viewParms.or.origin, tr.or.viewOrigin );

	// transform by the camera placement
	VectorCopy( tr.viewParms.or.origin, origin );

	viewerMatrix[0] = tr.viewParms.or.axis[0][0];
	viewerMatrix[4] = tr.viewParms.or.axis[0][1];
	viewerMatrix[8] = tr.viewParms.or.axis[0][2];
	viewerMatrix[12] = -origin[0] * viewerMatrix[0] + - origin[1] * viewerMatrix[4] + - origin[2] * viewerMatrix[8];

	viewerMatrix[1] = tr.viewParms.or.axis[1][0];
	viewerMatrix[5] = tr.viewParms.or.axis[1][1];
	viewerMatrix[9] = tr.viewParms.or.axis[1][2];
	viewerMatrix[13] = -origin[0] * viewerMatrix[1] + - origin[1] * viewerMatrix[5] + - origin[2] * viewerMatrix[9];

	viewerMatrix[2] = tr.viewParms.or.axis[2][0];
	viewerMatrix[6] = tr.viewParms.or.axis[2][1];
	viewerMatrix[10] = tr.viewParms.or.axis[2][2];
	viewerMatrix[14] = -origin[0] * viewerMatrix[2] + - origin[1] * viewerMatrix[6] + - origin[2] * viewerMatrix[10];

	viewerMatrix[3] = 0;
	viewerMatrix[7] = 0;
	viewerMatrix[11] = 0;
	viewerMatrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	R_MultMatrix( viewerMatrix, s_flipMatrix, tr.or.modelMatrix );

	tr.viewParms.world = tr.or;

}



/*
==============
R_SetFrameFog
==============
*/
void R_SetFrameFog( void ) {

	if ( r_speeds->integer == 5 ) {
		if ( !glfogsettings[FOG_TARGET].registered ) {
			ri.Printf( PRINT_ALL, "no fog - calc zFar: %0.1f\n", tr.viewParms.zFar );
			return;
		}
	}

	// DHM - Nerve :: If fog is not valid, don't use it
	if ( !glfogsettings[FOG_TARGET].registered ) {
		return;
	}

	// still fading
	if ( glfogsettings[FOG_TARGET].finishTime && glfogsettings[FOG_TARGET].finishTime >= tr.refdef.time ) {
		float lerpPos;
		int fadeTime;

		// transitioning from density to distance
		if ( glfogsettings[FOG_LAST].mode == GL_EXP && glfogsettings[FOG_TARGET].mode == GL_LINEAR ) {
			// for now just fast transition to the target when dissimilar fogs are
			memcpy( &glfogsettings[FOG_CURRENT], &glfogsettings[FOG_TARGET], sizeof( glfog_t ) );
			glfogsettings[FOG_TARGET].finishTime = 0;
		}
		// transitioning from distance to density
		else if ( glfogsettings[FOG_LAST].mode == GL_LINEAR && glfogsettings[FOG_TARGET].mode == GL_EXP ) {
			memcpy( &glfogsettings[FOG_CURRENT], &glfogsettings[FOG_TARGET], sizeof( glfog_t ) );
			glfogsettings[FOG_TARGET].finishTime = 0;
		}
		// transitioning like fog modes
		else {

			fadeTime = glfogsettings[FOG_TARGET].finishTime - glfogsettings[FOG_TARGET].startTime;
			if ( fadeTime <= 0 ) {
				fadeTime = 1;   // avoid divide by zero


			}
			lerpPos = (float)( tr.refdef.time - glfogsettings[FOG_TARGET].startTime ) / (float)fadeTime;
			if ( lerpPos > 1 ) {
				lerpPos = 1;
			}

			// lerp near/far
			glfogsettings[FOG_CURRENT].start        = glfogsettings[FOG_LAST].start + ( ( glfogsettings[FOG_TARGET].start - glfogsettings[FOG_LAST].start ) * lerpPos );
			glfogsettings[FOG_CURRENT].end          = glfogsettings[FOG_LAST].end + ( ( glfogsettings[FOG_TARGET].end - glfogsettings[FOG_LAST].end ) * lerpPos );

			// lerp color
			glfogsettings[FOG_CURRENT].color[0]     = glfogsettings[FOG_LAST].color[0] + ( ( glfogsettings[FOG_TARGET].color[0] - glfogsettings[FOG_LAST].color[0] ) * lerpPos );
			glfogsettings[FOG_CURRENT].color[1]     = glfogsettings[FOG_LAST].color[1] + ( ( glfogsettings[FOG_TARGET].color[1] - glfogsettings[FOG_LAST].color[1] ) * lerpPos );
			glfogsettings[FOG_CURRENT].color[2]     = glfogsettings[FOG_LAST].color[2] + ( ( glfogsettings[FOG_TARGET].color[2] - glfogsettings[FOG_LAST].color[2] ) * lerpPos );

			glfogsettings[FOG_CURRENT].density      = glfogsettings[FOG_TARGET].density;
			glfogsettings[FOG_CURRENT].mode         = glfogsettings[FOG_TARGET].mode;
			glfogsettings[FOG_CURRENT].registered   = qtrue;

			// if either fog in the transition clears the screen, clear the background this frame to avoid hall of mirrors
			glfogsettings[FOG_CURRENT].clearscreen  = ( glfogsettings[FOG_TARGET].clearscreen || glfogsettings[FOG_LAST].clearscreen );
		}
	} else {
		// probably usually not necessary to copy the whole thing.
		// potential FIXME: since this is the most common occurance, diff first and only set changes
		memcpy( &glfogsettings[FOG_CURRENT], &glfogsettings[FOG_TARGET], sizeof( glfog_t ) );
	}


	// shorten the far clip if the fog opaque distance is closer than the procedural farcip dist

	if ( glfogsettings[FOG_CURRENT].mode == GL_LINEAR ) {
		if ( glfogsettings[FOG_CURRENT].end < tr.viewParms.zFar ) {
			tr.viewParms.zFar = glfogsettings[FOG_CURRENT].end;
		}
	}
//	else
//		glfogsettings[FOG_CURRENT].end = 5;


	if ( r_speeds->integer == 5 ) {
		if ( glfogsettings[FOG_CURRENT].mode == GL_LINEAR ) {
			ri.Printf( PRINT_ALL, "farclip fog - den: %0.1f  calc zFar: %0.1f  fog zfar: %0.1f\n", glfogsettings[FOG_CURRENT].density, tr.viewParms.zFar, glfogsettings[FOG_CURRENT].end );
		} else {
			ri.Printf( PRINT_ALL, "density fog - den: %0.4f  calc zFar: %0.1f  fog zFar: %0.1f\n", glfogsettings[FOG_CURRENT].density, tr.viewParms.zFar, glfogsettings[FOG_CURRENT].end );
		}
	}
}


/*
==============
SetFarClip
==============
*/
static void SetFarClip( void ) {
	float farthestCornerDistance = 0;
	int i;

	// if not rendering the world (icons, menus, etc)
	// set a 2k far clip plane
	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		tr.viewParms.zFar = 2048;
		return;
	}

	//----(SA)	this lets you use r_zfar from the command line to experiment with different
	//			distances, but setting it back to 0 uses the map (or procedurally generated) default
	if ( r_zfar->value ) {

		tr.viewParms.zFar = r_zfar->integer;
		R_SetFrameFog();

		if ( r_speeds->integer == 5 ) {
			ri.Printf( PRINT_ALL, "r_zfar value forcing farclip at: %f\n", tr.viewParms.zFar );
		}

		return;
	}

	//
	// set far clipping planes dynamically
	//
	farthestCornerDistance = 0;
	for ( i = 0; i < 8; i++ )
	{
		vec3_t v;
		vec3_t vecTo;
		float distance;

		if ( i & 1 ) {
			v[0] = tr.viewParms.visBounds[0][0];
		} else
		{
			v[0] = tr.viewParms.visBounds[1][0];
		}

		if ( i & 2 ) {
			v[1] = tr.viewParms.visBounds[0][1];
		} else
		{
			v[1] = tr.viewParms.visBounds[1][1];
		}

		if ( i & 4 ) {
			v[2] = tr.viewParms.visBounds[0][2];
		} else
		{
			v[2] = tr.viewParms.visBounds[1][2];
		}

		VectorSubtract( v, tr.viewParms.or.origin, vecTo );

		distance = vecTo[0] * vecTo[0] + vecTo[1] * vecTo[1] + vecTo[2] * vecTo[2];

		if ( distance > farthestCornerDistance ) {
			farthestCornerDistance = distance;
		}
	}

	tr.viewParms.zFar = sqrt( farthestCornerDistance );
	R_SetFrameFog();
}


/*
===============
R_SetupProjection
===============
*/
void R_SetupProjection( void ) {
	float xmin, xmax, ymin, ymax;
	float width, height, depth;
	float zNear, zFar;

	// dynamically compute far clip plane distance
	SetFarClip();

	//
	// set up projection matrix
	//
	zNear   = r_znear->value;
	if ( r_zfar->value ) {
		zFar = r_zfar->value;   // (SA) allow override for helping level designers test fog distances
	} else {
		zFar    = tr.viewParms.zFar;
	}

	ymax = zNear * tan( tr.refdef.fov_y * M_PI / 360.0f );
	ymin = -ymax;

	xmax = zNear * tan( tr.refdef.fov_x * M_PI / 360.0f );
	xmin = -xmax;

	width = xmax - xmin;
	height = ymax - ymin;
	depth = zFar - zNear;

	tr.viewParms.projectionMatrix[0] = 2 * zNear / width;
	tr.viewParms.projectionMatrix[4] = 0;
	tr.viewParms.projectionMatrix[8] = ( xmax + xmin ) / width; // normally 0
	tr.viewParms.projectionMatrix[12] = 0;

	tr.viewParms.projectionMatrix[1] = 0;
	tr.viewParms.projectionMatrix[5] = 2 * zNear / height;
	tr.viewParms.projectionMatrix[9] = ( ymax + ymin ) / height;    // normally 0
	tr.viewParms.projectionMatrix[13] = 0;

	tr.viewParms.projectionMatrix[2] = 0;
	tr.viewParms.projectionMatrix[6] = 0;
	tr.viewParms.projectionMatrix[10] = -( zFar + zNear ) / depth;
	tr.viewParms.projectionMatrix[14] = -2 * zFar * zNear / depth;

	tr.viewParms.projectionMatrix[3] = 0;
	tr.viewParms.projectionMatrix[7] = 0;
	tr.viewParms.projectionMatrix[11] = -1;
	tr.viewParms.projectionMatrix[15] = 0;

	// correction matrix for Vulkan clip space (flipped Y, half-range Z)
	float c[16] = {};
    c[0] = 1.0f;
    c[5] = -1.0f;
    c[10] = 0.5f;
    c[14] = 0.5f;
    c[15] = 1.0f;

	R_MultMatrix(tr.viewParms.projectionMatrix, c, tr.viewParms.vulkanProjectionMatrix);

}

/*
=================
R_SetupFrustum

Setup that culling frustum planes for the current view
=================
*/
void R_SetupFrustum( void ) {
	int i;
	float xs, xc;
	float ang;

	ang = tr.viewParms.fovX / 180 * M_PI * 0.5f;
	xs = sin( ang );
	xc = cos( ang );

	VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[0].normal );
	VectorMA( tr.viewParms.frustum[0].normal, xc, tr.viewParms.or.axis[1], tr.viewParms.frustum[0].normal );

	VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[1].normal );
	VectorMA( tr.viewParms.frustum[1].normal, -xc, tr.viewParms.or.axis[1], tr.viewParms.frustum[1].normal );

	ang = tr.viewParms.fovY / 180 * M_PI * 0.5f;
	xs = sin( ang );
	xc = cos( ang );

	VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[2].normal );
	VectorMA( tr.viewParms.frustum[2].normal, xc, tr.viewParms.or.axis[2], tr.viewParms.frustum[2].normal );

	VectorScale( tr.viewParms.or.axis[0], xs, tr.viewParms.frustum[3].normal );
	VectorMA( tr.viewParms.frustum[3].normal, -xc, tr.viewParms.or.axis[2], tr.viewParms.frustum[3].normal );

	for ( i = 0 ; i < 4 ; i++ ) {
		tr.viewParms.frustum[i].type = PLANE_NON_AXIAL;
		tr.viewParms.frustum[i].dist = DotProduct( tr.viewParms.or.origin, tr.viewParms.frustum[i].normal );
		SetPlaneSignbits( &tr.viewParms.frustum[i] );
	}
}


/*
=================
R_MirrorPoint
=================
*/
void R_MirrorPoint( vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out ) {
	int i;
	vec3_t local;
	vec3_t transformed;
	float d;

	VectorSubtract( in, surface->origin, local );

	VectorClear( transformed );
	for ( i = 0 ; i < 3 ; i++ ) {
		d = DotProduct( local, surface->axis[i] );
		VectorMA( transformed, d, camera->axis[i], transformed );
	}

	VectorAdd( transformed, camera->origin, out );
}

void R_MirrorVector( vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out ) {
	int i;
	float d;

	VectorClear( out );
	for ( i = 0 ; i < 3 ; i++ ) {
		d = DotProduct( in, surface->axis[i] );
		VectorMA( out, d, camera->axis[i], out );
	}
}


/*
=============
R_PlaneForSurface
=============
*/
void R_PlaneForSurface( surfaceType_t *surfType, cplane_t *plane ) {
	srfTriangles_t  *tri;
	srfPoly_t       *poly;
	drawVert_t      *v1, *v2, *v3;
	vec4_t plane4;

	if ( !surfType ) {
		memset( plane, 0, sizeof( *plane ) );
		plane->normal[0] = 1;
		return;
	}
	switch ( *surfType ) {
	case SF_FACE:
		*plane = ( (srfSurfaceFace_t *)surfType )->plane;
		return;
	case SF_TRIANGLES:
		tri = (srfTriangles_t *)surfType;
		v1 = tri->verts + tri->indexes[0];
		v2 = tri->verts + tri->indexes[1];
		v3 = tri->verts + tri->indexes[2];
		PlaneFromPoints( plane4, v1->xyz, v2->xyz, v3->xyz );
		VectorCopy( plane4, plane->normal );
		plane->dist = plane4[3];
		return;
	case SF_POLY:
		poly = (srfPoly_t *)surfType;
		PlaneFromPoints( plane4, poly->verts[0].xyz, poly->verts[1].xyz, poly->verts[2].xyz );
		VectorCopy( plane4, plane->normal );
		plane->dist = plane4[3];
		return;
	default:
		memset( plane, 0, sizeof( *plane ) );
		plane->normal[0] = 1;
		return;
	}
}

/*
=================
R_GetPortalOrientation

entityNum is the entity that the portal surface is a part of, which may
be moving and rotating.

Returns qtrue if it should be mirrored
=================
*/
qboolean R_GetPortalOrientations( drawSurf_t *drawSurf, int entityNum,
								  orientation_t *surface, orientation_t *camera,
								  vec3_t pvsOrigin, qboolean *mirror ) {
	int i;
	cplane_t originalPlane, plane;
	trRefEntity_t   *e;
	float d;
	vec3_t transformed;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( entityNum != ENTITYNUM_WORLD ) {
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.or.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.or.origin );
	} else {
		plane = originalPlane;
	}

	VectorCopy( plane.normal, surface->axis[0] );
	PerpendicularVector( surface->axis[1], surface->axis[0] );
	CrossProduct( surface->axis[0], surface->axis[1], surface->axis[2] );

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) {
		e = &tr.refdef.entities[i];
		if ( e->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
		if ( d > 64 || d < -64 ) {
			continue;
		}

		// get the pvsOrigin from the entity
		VectorCopy( e->e.oldorigin, pvsOrigin );

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[0] == e->e.origin[0] &&
			 e->e.oldorigin[1] == e->e.origin[1] &&
			 e->e.oldorigin[2] == e->e.origin[2] ) {
			VectorScale( plane.normal, plane.dist, surface->origin );
			VectorCopy( surface->origin, camera->origin );
			VectorSubtract( vec3_origin, surface->axis[0], camera->axis[0] );
			VectorCopy( surface->axis[1], camera->axis[1] );
			VectorCopy( surface->axis[2], camera->axis[2] );

			*mirror = qtrue;
			return qtrue;
		}

		// project the origin onto the surface plane to get
		// an origin point we can rotate around
		d = DotProduct( e->e.origin, plane.normal ) - plane.dist;
		VectorMA( e->e.origin, -d, surface->axis[0], surface->origin );

		// now get the camera origin and orientation
		VectorCopy( e->e.oldorigin, camera->origin );
		AxisCopy( e->e.axis, camera->axis );
		VectorSubtract( vec3_origin, camera->axis[0], camera->axis[0] );
		VectorSubtract( vec3_origin, camera->axis[1], camera->axis[1] );

		// optionally rotate
		if ( e->e.oldframe ) {
			// if a speed is specified
			if ( e->e.frame ) {
				// continuous rotate
				d = ( (double)tr.refdef.time / 1000.0 ) * e->e.frame;
				VectorCopy( camera->axis[1], transformed );
				RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
				CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
			} else {
				// bobbing rotate, with skinNum being the rotation offset
				d = sin( (double)tr.refdef.time * 0.003 );
				d = e->e.skinNum + d * 4;
				VectorCopy( camera->axis[1], transformed );
				RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
				CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
			}
		} else if ( e->e.skinNum )   {
			d = e->e.skinNum;
			VectorCopy( camera->axis[1], transformed );
			RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
			CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
		}
		*mirror = qfalse;
		return qtrue;
	}

	// if we didn't locate a portal entity, don't render anything.
	// We don't want to just treat it as a mirror, because without a
	// portal entity the server won't have communicated a proper entity set
	// in the snapshot

	// unfortunately, with local movement prediction it is easily possible
	// to see a surface before the server has communicated the matching
	// portal surface entity, so we don't want to print anything here...

	//ri.Printf( PRINT_ALL, "Portal surface without a portal entity\n" );

	return qfalse;
}

static qboolean IsMirror( const drawSurf_t *drawSurf, int entityNum ) {
	int i;
	cplane_t originalPlane, plane;
	trRefEntity_t   *e;
	float d;

	// create plane axis for the portal we are seeing
	R_PlaneForSurface( drawSurf->surface, &originalPlane );

	// rotate the plane if necessary
	if ( entityNum != ENTITYNUM_WORLD ) {
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		// get the orientation of the entity
		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

		// rotate the plane, but keep the non-rotated version for matching
		// against the portalSurface entities
		R_LocalNormalToWorld( originalPlane.normal, plane.normal );
		plane.dist = originalPlane.dist + DotProduct( plane.normal, tr.or.origin );

		// translate the original plane
		originalPlane.dist = originalPlane.dist + DotProduct( originalPlane.normal, tr.or.origin );
	} else
	{
		plane = originalPlane;
	}

	// locate the portal entity closest to this plane.
	// origin will be the origin of the portal, origin2 will be
	// the origin of the camera
	for ( i = 0 ; i < tr.refdef.num_entities ; i++ )
	{
		e = &tr.refdef.entities[i];
		if ( e->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( e->e.origin, originalPlane.normal ) - originalPlane.dist;
		if ( d > 64 || d < -64 ) {
			continue;
		}

		// if the entity is just a mirror, don't use as a camera point
		if ( e->e.oldorigin[0] == e->e.origin[0] &&
			 e->e.oldorigin[1] == e->e.origin[1] &&
			 e->e.oldorigin[2] == e->e.origin[2] ) {
			return qtrue;
		}

		return qfalse;
	}
	return qfalse;
}

/*
** SurfIsOffscreen
**
** Determines if a surface is completely offscreen.
*/
static qboolean SurfIsOffscreen( const drawSurf_t *drawSurf, vec4_t clipDest[128] ) {
	float shortest = 100000000;
	int entityNum;
	int numTriangles;
	shader_t *shader;
	int fogNum;
	int dlighted;
	vec4_t clip, eye;
	int i;
	unsigned int pointAnd = (unsigned int)~0;

	R_RotateForViewer();

	R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
	RB_BeginSurface( shader, fogNum );
	rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );

	assert( tess.numVertexes < 128 );

	for ( i = 0; i < tess.numVertexes; i++ )
	{
		int j;
		unsigned int pointFlags = 0;

		R_TransformModelToClip( tess.xyz[i], tr.or.modelMatrix, tr.viewParms.projectionMatrix, eye, clip );

		for ( j = 0; j < 3; j++ )
		{
			if ( clip[j] >= clip[3] ) {
				pointFlags |= ( 1 << ( j * 2 ) );
			} else if ( clip[j] <= -clip[3] )   {
				pointFlags |= ( 1 << ( j * 2 + 1 ) );
			}
		}
		pointAnd &= pointFlags;
	}

	// trivially reject
	if ( pointAnd ) {
		return qtrue;
	}

	// determine if this surface is backfaced and also determine the distance
	// to the nearest vertex so we can cull based on portal range.  Culling
	// based on vertex distance isn't 100% correct (we should be checking for
	// range to the surface), but it's good enough for the types of portals
	// we have in the game right now.
	numTriangles = tess.numIndexes / 3;

	for ( i = 0; i < tess.numIndexes; i += 3 )
	{
		vec3_t normal;
		float dot;
		float len;

		VectorSubtract( tess.xyz[tess.indexes[i]], tr.viewParms.or.origin, normal );

		len = VectorLengthSquared( normal );            // lose the sqrt
		if ( len < shortest ) {
			shortest = len;
		}

		if ( ( dot = DotProduct( normal, tess.normal[tess.indexes[i]] ) ) >= 0 ) {
			numTriangles--;
		}
	}
	if ( !numTriangles ) {
		return qtrue;
	}

	// mirrors can early out at this point, since we don't do a fade over distance
	// with them (although we could)
	if ( IsMirror( drawSurf, entityNum ) ) {
		return qfalse;
	}

	if ( shortest > ( tess.shader->portalRange * tess.shader->portalRange ) ) {
		return qtrue;
	}

	return qfalse;
}

/*
========================
R_MirrorViewBySurface

Returns qtrue if another view has been rendered
========================
*/
qboolean R_MirrorViewBySurface( drawSurf_t *drawSurf, int entityNum ) {
	vec4_t clipDest[128];
	viewParms_t newParms;
	viewParms_t oldParms;
	orientation_t surface, camera;

	// don't recursively mirror
	if ( tr.viewParms.isPortal ) {
		ri.Printf( PRINT_DEVELOPER, "WARNING: recursive mirror/portal found\n" );
		return qfalse;
	}

//	if ( r_noportals->integer || r_fastsky->integer || tr.levelGLFog) {
	if ( r_noportals->integer || r_fastsky->integer ) {
		return qfalse;
	}

	// trivially reject portal/mirror
	if ( SurfIsOffscreen( drawSurf, clipDest ) ) {
		return qfalse;
	}

	// save old viewParms so we can return to it after the mirror view
	oldParms = tr.viewParms;

	newParms = tr.viewParms;
	newParms.isPortal = qtrue;
	if ( !R_GetPortalOrientations( drawSurf, entityNum, &surface, &camera,
								   newParms.pvsOrigin, &newParms.isMirror ) ) {
		return qfalse;      // bad portal, no portalentity
	}

	R_MirrorPoint( oldParms.or.origin, &surface, &camera, newParms.or.origin );

	VectorSubtract( vec3_origin, camera.axis[0], newParms.portalPlane.normal );
	newParms.portalPlane.dist = DotProduct( camera.origin, newParms.portalPlane.normal );

	R_MirrorVector( oldParms.or.axis[0], &surface, &camera, newParms.or.axis[0] );
	R_MirrorVector( oldParms.or.axis[1], &surface, &camera, newParms.or.axis[1] );
	R_MirrorVector( oldParms.or.axis[2], &surface, &camera, newParms.or.axis[2] );

	// OPTIMIZE: restrict the viewport on the mirrored view

	// render the mirror view
	R_RenderView( &newParms );

	tr.viewParms = oldParms;

	return qtrue;
}

/*
=================
R_SpriteFogNum

See if a sprite is inside a fog volume
=================
*/
int R_SpriteFogNum( trRefEntity_t *ent ) {
	int i, j;
	fog_t           *fog;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( ent->e.origin[j] - ent->e.radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( ent->e.origin[j] + ent->e.radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}

/*
==========================================================================================

DRAWSURF SORTING

==========================================================================================
*/

/*
=================
qsort replacement

=================
*/
/* this parameter defines the cutoff between using quick sort and
   insertion sort for arrays; arrays with lengths shorter or equal to the
   below value use insertion sort */

#define CUTOFF 8            /* testing shows that this is good value */

static void shortsort( drawSurf_t *lo, drawSurf_t *hi ) {
	drawSurf_t  *p, *max;
	drawSurf_t temp;

	while ( hi > lo ) {
		max = lo;
		for ( p = lo + 1; p <= hi; p++ ) {
			if ( p->sort > max->sort ) {
				max = p;
			}
		}
		temp = *max;
		*max = *hi;
		*hi = temp;
		hi--;
	}
}




//==========================================================================================

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf( surfaceType_t *surface, shader_t *shader,
					int fogIndex, int dlightMap ) {
	int index;

	// instead of checking for overflow, we just mask the index
	// so it wraps around
	index = tr.refdef.numDrawSurfs & DRAWSURF_MASK;
	// the sort data is packed into a single 32 bit value so it can be
	// compared quickly during the qsorting process
	tr.refdef.drawSurfs[index].sort = R_ComposeSort(shader, tr.shiftedEntityNum >> QSORT_ENTITYNUM_SHIFT, fogIndex, dlightMap);
	tr.refdef.drawSurfs[index].surface = surface;
	tr.refdef.drawSurfs[index].shader = shader;
	tr.refdef.numDrawSurfs++;
}

/*
=================
R_DecomposeSort
=================
*/
void R_DecomposeSort( int64_t sort, int *entityNum, shader_t **shader,
					  int *fogNum, int *dlightMap ) {
	*fogNum = ( sort >> QSORT_FOGNUM_SHIFT ) & QSORT_FOGNUM_MASK;
	*shader = tr.sortedShaders[ ( sort >> QSORT_SHADERNUM_SHIFT ) & QSORT_SHADERNUM_MASK ];
	*entityNum = ( sort >> QSORT_ENTITYNUM_SHIFT ) & QSORT_ENTITYNUM_MASK;   // (SA) uppded entity count for Wolf to 11 bits
	*dlightMap = (sort >> QSORT_DLIGHT_SHIFT) & QSORT_DLIGHT_MASK;
}

int64_t R_ComposeSort( shader_t *shader, int entityNum, int fogNum, int dlightMap ){
	//Q_assert(shader->stages[0]); //it's possible for stage 0 to not be active when the shader is created
	uint32_t psoIndex = shader->stages[0] ? RHI_GetIndexFromHandle(shader->stages[0]->pipeline[0].h) : 0;

	return (int64_t)( 0
	| ((int64_t)psoIndex << QSORT_PSONUM_SHIFT)
	| ((int64_t)shader->isAlphaTested << QSORT_ALPHATEST_SHIFT)
	| ((int64_t)!shader->isMultitextured << QSORT_LIGHTMAP_SHIFT)
	| ((int64_t)shader->sortedIndex << QSORT_SHADERNUM_SHIFT ) 
	| ((int64_t)entityNum << QSORT_ENTITYNUM_SHIFT) 
	| ((int64_t)fogNum << QSORT_FOGNUM_SHIFT) 
	| ((int64_t)dlightMap << QSORT_DLIGHT_SHIFT) ); 
}

// returns -1, 0 or 1
int qsort_signumf(float x)
{
    return (x > 0.0f) - (x < -0.0f);
}

// returns -1, 0 or 1
int qsort_signum(int64_t x)
{
    return (x > 0) - (x < 0);
}


int QDECL CompareDrawSurfs(void const *ptrA, void const *ptrB){
	const drawSurf_t *a = (const drawSurf_t*)ptrA;
	const drawSurf_t *b = (const drawSurf_t*)ptrB;

	float keyDifference = a->shader->sort - b->shader->sort;

	if(keyDifference != 0.0f){
		return qsort_signumf(keyDifference);
	}
	
	int64_t sortDifference = a->sort - b->sort;
	return qsort_signum(sortDifference);
}

// dlights reaching this surface, 0 for anything the additive pass doesn't cover
static int R_SurfaceDlightMask( const drawSurf_t *drawSurf ) {
	// dlightBits isn't refreshed for surfaces without the sort key's dlight bit
	if(!( ( drawSurf->sort >> QSORT_DLIGHT_SHIFT ) & QSORT_DLIGHT_MASK )){
		return 0;
	}
	if(drawSurf->shader->surfaceFlags & ( SURF_NODLIGHT | SURF_SKY )){
		return 0;
	}

	switch(*drawSurf->surface){
		case SF_GRID:
			return ((srfGridMesh_t*)drawSurf->surface)->dlightBits;
		case SF_FACE:
			return ((srfSurfaceFace_t*)drawSurf->surface)->dlightBits;
		case SF_TRIANGLES:
			return ((srfTriangles_t*)drawSurf->surface)->dlightBits;
		default:
			return 0;
	}
}

/*
=================
R_BuildLitSurfLists

Appends one list per dlight to tr.refdef.litSurfs. Each keeps the draw surface
order, so batching still works. A list past MAX_LITSURFS is truncated.
=================
*/
static void R_BuildLitSurfLists( const drawSurf_t *drawSurfs, int numDrawSurfs, int *firstLitSurf, int *numLitSurfs ) {
	const int numDlights = tr.refdef.num_dlights;
	const unsigned int dlightMask = numDlights >= MAX_DLIGHTS ? ~0u : ( 1u << numDlights ) - 1u;
	int surfCount[MAX_DLIGHTS] = { };
	int numOpaqueSurfs = 0;
	int nextLitSurf = tr.refdef.numLitSurfs;
	int i, l;

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		if(drawSurfs[i].shader->sort > SS_OPAQUE){
			break;
		}
		const unsigned int mask = (unsigned int)R_SurfaceDlightMask(&drawSurfs[i]) & dlightMask;
		for(l = 0; l < numDlights; l++){
			if(mask & (1u<<l)){
				surfCount[l]++;
			}
		}
		numOpaqueSurfs++;
	}

	for(l = 0; l < numDlights; l++){
		const int available = MAX_LITSURFS - nextLitSurf;
		firstLitSurf[l] = nextLitSurf;
		numLitSurfs[l] = surfCount[l] < available ? surfCount[l] : available;
		nextLitSurf += numLitSurfs[l];
		surfCount[l] = 0;
	}
	tr.refdef.numLitSurfs = nextLitSurf;

	for ( i = 0 ; i < numOpaqueSurfs ; i++ ) {
		const drawSurf_t *drawSurf = &drawSurfs[i];
		const unsigned int mask = (unsigned int)R_SurfaceDlightMask(drawSurf) & dlightMask;
		for(l = 0; l < numDlights; l++){
			if((mask & (1u<<l)) && surfCount[l] < numLitSurfs[l]){
				litSurf_t *litSurf = &tr.refdef.litSurfs[firstLitSurf[l] + surfCount[l]++];
				litSurf->surface = drawSurf->surface;
				litSurf->shader = drawSurf->shader;
				litSurf->sort = drawSurf->sort;
			}
		}
	}
}

/*
=================
R_SortDrawSurfs
=================
*/
void R_SortDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t        *shader;
	int fogNum;
	int entityNum;
	int dlighted;
	int i;
	int firstLitSurf[MAX_DLIGHTS] = { };
	int numLitSurfs[MAX_DLIGHTS] = { };

	// it is possible for some views to not have any surfaces
	if ( numDrawSurfs < 1 ) {
		// we still need to add it for hyperspace cases
		R_AddDrawSurfCmd( drawSurfs, numDrawSurfs, NULL, NULL );
		return;
	}

	// sort the drawsurfs by sort type, then orientation, then shader
	qsort(drawSurfs,numDrawSurfs, sizeof( drawSurf_t ), CompareDrawSurfs );

	// must come first: a mirror view below appends its own lists and overwrites
	// the dlightBits these depend on
	if(tr.refdef.num_dlights > 0){
		R_BuildLitSurfLists(drawSurfs, numDrawSurfs, firstLitSurf, numLitSurfs);
	}

	// check for any pass through drawing, which
	// may cause another view to be rendered first
	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		R_DecomposeSort( ( drawSurfs + i )->sort, &entityNum, &shader, &fogNum, &dlighted );

		if ( shader->sort > SS_PORTAL ) {
			break;
		}

		// no shader should ever have this sort type
		if ( shader->sort == SS_BAD ) {
			ri.Error( ERR_DROP, "Shader '%s'with sort == SS_BAD", shader->name );
		}

		// if the mirror was completely clipped away, we may need to check another surface
		if ( R_MirrorViewBySurface( ( drawSurfs + i ), entityNum ) ) {
			// this is a debug option to see exactly what is being mirrored
			if ( r_portalOnly->integer ) {
				return;
			}
			break;      // only one mirror view at a time
		}
	}

	R_AddDrawSurfCmd( drawSurfs, numDrawSurfs, firstLitSurf, numLitSurfs );
}

/*
=============
R_AddEntitySurfaces
=============
*/
void R_AddEntitySurfaces( void ) {
	trRefEntity_t   *ent;
	shader_t        *shader;

	if ( !r_drawentities->integer ) {
		return;
	}

	for ( tr.currentEntityNum = 0;
		  tr.currentEntityNum < tr.refdef.num_entities;
		  tr.currentEntityNum++ ) {
		ent = tr.currentEntity = &tr.refdef.entities[tr.currentEntityNum];

		ent->needDlights = qfalse;

		// preshift the value we are going to OR into the drawsurf sort
		tr.shiftedEntityNum = tr.currentEntityNum << QSORT_ENTITYNUM_SHIFT;

		//
		// the weapon model must be handled special --
		// we don't want the hacked weapon position showing in
		// mirrors, because the true body position will already be drawn
		//
		if ( ( ent->e.renderfx & RF_FIRST_PERSON ) && tr.viewParms.isPortal ) {
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch ( ent->e.reType ) {
		case RT_PORTALSURFACE:
			break;      // don't draw anything
		case RT_SPRITE:
		case RT_SPLASH:
		case RT_BEAM:
		case RT_LIGHTNING:
		case RT_RAIL_CORE:
		case RT_RAIL_CORE_TAPER:
		case RT_RAIL_RINGS:
			// self blood sprites, talk balloons, etc should not be drawn in the primary
			// view.  We can't just do this check for all entities, because md3
			// entities may still want to cast shadows from them
			if ( ( ent->e.renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal ) {
				continue;
			}
			shader = R_GetShaderByHandle( ent->e.customShader );
			R_AddDrawSurf( &entitySurface, shader, R_SpriteFogNum( ent ), 0 );
			break;

		case RT_MODEL:
			// we must set up parts of tr.or for model culling
			R_RotateForEntity( ent, &tr.viewParms, &tr.or );

			tr.currentModel = R_GetModelByHandle( ent->e.hModel );
			if ( !tr.currentModel ) {
				R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
			} else {
				switch ( tr.currentModel->type ) {
				case MOD_MESH:
					R_AddMD3Surfaces( ent );
					break;
					// Ridah
				case MOD_MDC:
					R_AddMDCSurfaces( ent );
					break;
					// done.
				case MOD_MDS:
					R_AddAnimSurfaces( ent );
					break;
				case MOD_BRUSH:
					R_AddBrushModelSurfaces( ent );
					break;
				case MOD_BAD:       // null model axis
					if ( ( ent->e.renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal ) {
						break;
					}
					shader = R_GetShaderByHandle( ent->e.customShader );
					R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
					break;
				default:
					ri.Error( ERR_DROP, "R_AddEntitySurfaces: Bad modeltype" );
					break;
				}
			}
			break;
		default:
			ri.Error( ERR_DROP, "R_AddEntitySurfaces: Bad reType" );
		}
	}

}


/*
====================
R_GenerateDrawSurfs
====================
*/
void R_GenerateDrawSurfs( void ) {
	R_AddWorldSurfaces();

	R_AddPolygonSurfaces();

	// set the projection matrix with the minimum zfar
	// now that we have the world bounded
	// this needs to be done before entities are
	// added, because they use the projection
	// matrix for lod calculation
	R_SetupProjection();

	R_AddEntitySurfaces();
}

/*
================
R_DebugPolygon
================
*/
void R_DebugPolygon( int color, int numPoints, float *points ) {
	//@TODO 
}

/*
====================
R_DebugGraphics

Visualization aid for movement clipping debugging
====================
*/
void R_DebugGraphics( void ) {
	if ( !r_debugSurface->integer ) {
		return;
	}

	R_FogOff(); // moved this in here to keep from /always/ doing the fog state change

	ri.CM_DrawDebugSurface( R_DebugPolygon );
}


/*
================
R_RenderView

A view may be either the actual camera view,
or a mirror / remote location
================
*/
void R_RenderView( viewParms_t *parms ) {
	

	if ( parms->viewportWidth <= 0 || parms->viewportHeight <= 0 ) {
		return;
	}

	tr.viewCount++;

	tr.viewParms = *parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;

	int firstDrawSurf = tr.refdef.numDrawSurfs;

	// set viewParms.world
	R_RotateForViewer();

	R_SetupFrustum();

	R_GenerateDrawSurfs();

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
	// wrapped around in the buffer and we will be missing
	// the first surfaces, not the last ones
	int numDrawSurfs = tr.refdef.numDrawSurfs;
	if ( numDrawSurfs > MAX_DRAWSURFS ) {
		numDrawSurfs = MAX_DRAWSURFS;
	}

	R_SortDrawSurfs( tr.refdef.drawSurfs + firstDrawSurf, numDrawSurfs - firstDrawSurf );

	// draw main system development information (surface outlines, etc)
	R_FogOff();
	R_DebugGraphics();
	R_FogOn();

}

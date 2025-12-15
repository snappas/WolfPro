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

/*

All bones should be an identity orientation to display the mesh exactly
as it is specified.

For all other frames, the bones represent the transformation from the
orientation of the bone in the base frame to the orientation in this
frame.

*/

//#define HIGH_PRECISION_BONES	// enable this for 32bit precision bones
//#define DBG_PROFILE_BONES

//-----------------------------------------------------------------------------
// Static Vars, ugly but easiest (and fastest) means of seperating RB_SurfaceAnim
// and R_CalcBones


static int             *triangles, *boneRefs, *pIndexes;
static int indexes;
static int baseIndex, baseVertex, oldIndexes;
static int numVerts;
static mdsVertex_t     *v;







static float           *tempVert, *tempNormal;


static int render_count;
static float lodRadius, lodScale;
static int             *collapse_map, *pCollapseMap;
static int collapse[ MDS_MAX_VERTS ], *pCollapse;
static int p0, p1, p2;



//-----------------------------------------------------------------------------

static float ProjectRadius( float r, vec3_t location ) {
	float pr;
	float dist;
	float c;
	vec3_t p;
	float projected[4];

	c = DotProduct( tr.viewParms.or.axis[0], tr.viewParms.or.origin );
	dist = DotProduct( tr.viewParms.or.axis[0], location ) - c;

	if ( dist <= 0 ) {
		return 0;
	}

	p[0] = 0;
	p[1] = fabs( r );
	p[2] = -dist;

	projected[0] = p[0] * tr.viewParms.projectionMatrix[0] +
				   p[1] * tr.viewParms.projectionMatrix[4] +
				   p[2] * tr.viewParms.projectionMatrix[8] +
				   tr.viewParms.projectionMatrix[12];

	projected[1] = p[0] * tr.viewParms.projectionMatrix[1] +
				   p[1] * tr.viewParms.projectionMatrix[5] +
				   p[2] * tr.viewParms.projectionMatrix[9] +
				   tr.viewParms.projectionMatrix[13];

	projected[2] = p[0] * tr.viewParms.projectionMatrix[2] +
				   p[1] * tr.viewParms.projectionMatrix[6] +
				   p[2] * tr.viewParms.projectionMatrix[10] +
				   tr.viewParms.projectionMatrix[14];

	projected[3] = p[0] * tr.viewParms.projectionMatrix[3] +
				   p[1] * tr.viewParms.projectionMatrix[7] +
				   p[2] * tr.viewParms.projectionMatrix[11] +
				   tr.viewParms.projectionMatrix[15];


	pr = projected[1] / projected[3];

	if ( pr > 1.0f ) {
		pr = 1.0f;
	}

	return pr;
}

/*
=============
R_CullModel
=============
*/
static int R_CullModel( mdsHeader_t *header, trRefEntity_t *ent ) {
	vec3_t bounds[2];
	mdsFrame_t  *oldFrame, *newFrame;
	int i, frameSize;

	frameSize = (int) ( sizeof( mdsFrame_t ) - sizeof( mdsBoneFrameCompressed_t ) + header->numBones * sizeof( mdsBoneFrameCompressed_t ) );

	// compute frame pointers
	newFrame = ( mdsFrame_t * )( ( byte * ) header + header->ofsFrames + ent->e.frame * frameSize );
	oldFrame = ( mdsFrame_t * )( ( byte * ) header + header->ofsFrames + ent->e.oldframe * frameSize );

	// cull bounding sphere ONLY if this is not an upscaled entity
	if ( !ent->e.nonNormalizedAxes ) {
		if ( ent->e.frame == ent->e.oldframe ) {
			switch ( R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius ) )
			{
			case CULL_OUT:
				tr.pc.c_sphere_cull_md3_out++;
				return CULL_OUT;

			case CULL_IN:
				tr.pc.c_sphere_cull_md3_in++;
				return CULL_IN;

			case CULL_CLIP:
				tr.pc.c_sphere_cull_md3_clip++;
				break;
			}
		} else
		{
			int sphereCull, sphereCullB;

			sphereCull  = R_CullLocalPointAndRadius( newFrame->localOrigin, newFrame->radius );
			if ( newFrame == oldFrame ) {
				sphereCullB = sphereCull;
			} else {
				sphereCullB = R_CullLocalPointAndRadius( oldFrame->localOrigin, oldFrame->radius );
			}

			if ( sphereCull == sphereCullB ) {
				if ( sphereCull == CULL_OUT ) {
					tr.pc.c_sphere_cull_md3_out++;
					return CULL_OUT;
				} else if ( sphereCull == CULL_IN )   {
					tr.pc.c_sphere_cull_md3_in++;
					return CULL_IN;
				} else
				{
					tr.pc.c_sphere_cull_md3_clip++;
				}
			}
		}
	}

	// calculate a bounding box in the current coordinate system
	for ( i = 0 ; i < 3 ; i++ ) {
		bounds[0][i] = oldFrame->bounds[0][i] < newFrame->bounds[0][i] ? oldFrame->bounds[0][i] : newFrame->bounds[0][i];
		bounds[1][i] = oldFrame->bounds[1][i] > newFrame->bounds[1][i] ? oldFrame->bounds[1][i] : newFrame->bounds[1][i];
	}

	switch ( R_CullLocalBox( bounds ) )
	{
	case CULL_IN:
		tr.pc.c_box_cull_md3_in++;
		return CULL_IN;
	case CULL_CLIP:
		tr.pc.c_box_cull_md3_clip++;
		return CULL_CLIP;
	case CULL_OUT:
	default:
		tr.pc.c_box_cull_md3_out++;
		return CULL_OUT;
	}
}

/*
=================
R_CalcMDSLod

=================
*/
float R_CalcMDSLod( refEntity_t *refent, vec3_t origin, float radius, float modelBias, float modelScale ) {
	float flod, lodScale;
	float projectedRadius;

	// compute projected bounding sphere and use that as a criteria for selecting LOD

	projectedRadius = ProjectRadius( radius, origin );
	if ( projectedRadius != 0 ) {

//		ri.Printf (PRINT_ALL, "projected radius: %f\n", projectedRadius);

		lodScale = r_lodscale->value;   // fudge factor since MDS uses a much smoother method of LOD
		flod = projectedRadius * lodScale * modelScale;
	} else
	{
		// object intersects near view plane, e.g. view weapon
		flod = 1.0f;
	}

	if ( refent->reFlags & REFLAG_FORCE_LOD ) {
		flod *= 0.5;
	}
//----(SA)	like reflag_force_lod, but separate for the moment
	if ( refent->reFlags & REFLAG_DEAD_LOD ) {
		flod *= 0.8;
	}

	flod -= 0.25 * ( r_lodbias->value ) + modelBias;

	if ( flod < 0.0 ) {
		flod = 0.0;
	} else if ( flod > 1.0f ) {
		flod = 1.0f;
	}

	return flod;
}

/*
=================
R_ComputeFogNum

=================
*/
static int R_ComputeFogNum( mdsHeader_t *header, trRefEntity_t *ent ) {
	int i, j;
	fog_t           *fog;
	mdsFrame_t      *mdsFrame;
	vec3_t localOrigin;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	// FIXME: non-normalized axis issues
	mdsFrame = ( mdsFrame_t * )( ( byte * ) header + header->ofsFrames + ( sizeof( mdsFrame_t ) + sizeof( mdsBoneFrameCompressed_t ) * ( header->numBones - 1 ) ) * ent->e.frame );
	VectorAdd( ent->e.origin, mdsFrame->localOrigin, localOrigin );
	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( localOrigin[j] - mdsFrame->radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( localOrigin[j] + mdsFrame->radius <= fog->bounds[0][j] ) {
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
==============
R_AddAnimSurfaces
==============
*/
void R_AddAnimSurfaces( trRefEntity_t *ent ) {
	mdsHeader_t     *header;
	mdsSurface_t    *surface;
	shader_t        *shader = 0;
	int i, fogNum, cull;
	qboolean personalModel;

	// don't add third_person objects if not in a portal
	personalModel = ( ent->e.renderfx & RF_THIRD_PERSON ) && !tr.viewParms.isPortal;

	header = tr.currentModel->mds;

	//
	// cull the entire model if merged bounding box of both frames
	// is outside the view frustum.
	//
	cull = R_CullModel( header, ent );
	if ( cull == CULL_OUT ) {
		return;
	}

	//
	// set up lighting now that we know we aren't culled
	//
	if ( !personalModel || r_shadows->integer > 1 ) {
		R_SetupEntityLighting( &tr.refdef, ent );
	}

	//
	// see if we are in a fog volume
	//
	fogNum = R_ComputeFogNum( header, ent );

	surface = ( mdsSurface_t * )( (byte *)header + header->ofsSurfaces );
	for ( i = 0 ; i < header->numSurfaces ; i++ ) {

		if ( ent->e.customShader ) {
			shader = R_GetShaderByHandle( ent->e.customShader );
		} else if ( ent->e.customSkin > 0 && ent->e.customSkin < tr.numSkins ) {
			skin_t *skin;
			int j;

			skin = R_GetSkinByHandle( ent->e.customSkin );

			// match the surface name to something in the skin file
			shader = tr.defaultShader;

			if ( ent->e.renderfx & RF_BLINK ) {
				const char *s = va( "%s_b", surface->name );   // append '_b' for 'blink'
				for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
					if ( !strcmp( skin->surfaces[j]->name, s ) ) {
						shader = skin->surfaces[j]->shader;
						break;
					}
				}
			}

			if ( shader == tr.defaultShader ) {    // blink reference in skin was not found
				for ( j = 0 ; j < skin->numSurfaces ; j++ ) {
					// the names have both been lowercased

					if ( !strcmp( skin->surfaces[j]->name, surface->name ) ) {
						shader = skin->surfaces[j]->shader;
						break;
					}
				}
			}

			if ( shader == tr.defaultShader ) {
				ri.Printf( PRINT_DEVELOPER, "WARNING: no shader for surface %s in skin %s\n", surface->name, skin->name );
			} else if ( shader->defaultShader )     {
				ri.Printf( PRINT_DEVELOPER, "WARNING: shader %s in skin %s not found\n", shader->name, skin->name );
			}
		} else {
			shader = R_GetShaderByHandle( surface->shaderIndex );
		}

		// don't add third_person objects if not viewing through a portal
		if ( !personalModel ) {
			R_AddDrawSurf( (void *)surface, shader, fogNum, qfalse );
		}

		surface = ( mdsSurface_t * )( (byte *)surface + surface->ofsEnd );
	}
}

static void LocalMatrixTransformVector( vec3_t in, vec3_t mat[ 3 ], vec3_t out ) {
	out[ 0 ] = in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ];
	out[ 1 ] = in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ];
	out[ 2 ] = in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ];
}

static void LocalMatrixTransformVectorTranslate( vec3_t in, vec3_t mat[ 3 ], vec3_t tr, vec3_t out ) {
	out[ 0 ] = in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ] + tr[ 0 ];
	out[ 1 ] = in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ] + tr[ 1 ];
	out[ 2 ] = in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ] + tr[ 2 ];
}



static void LocalScaledMatrixTransformVectorTranslate( vec3_t in, float s, vec3_t mat[ 3 ], vec3_t tr, vec3_t out ) {
	out[ 0 ] = ( 1.0f - s ) * in[ 0 ] + s * ( in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ] + tr[ 0 ] );
	out[ 1 ] = ( 1.0f - s ) * in[ 1 ] + s * ( in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ] + tr[ 1 ] );
	out[ 2 ] = ( 1.0f - s ) * in[ 2 ] + s * ( in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ] + tr[ 2 ] );
}

static void LocalScaledMatrixTransformVectorFullTranslate( vec3_t in, float s, vec3_t mat[ 3 ], vec3_t tr, vec3_t out ) {
	out[ 0 ] = ( 1.0f - s ) * in[ 0 ] + s * ( in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ] ) + tr[ 0 ];
	out[ 1 ] = ( 1.0f - s ) * in[ 1 ] + s * ( in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ] ) + tr[ 1 ];
	out[ 2 ] = ( 1.0f - s ) * in[ 2 ] + s * ( in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ] ) + tr[ 2 ];
}

static void LocalAddScaledMatrixTransformVectorFullTranslate( vec3_t in, float s, vec3_t mat[ 3 ], vec3_t tr, vec3_t out ) {
	out[ 0 ] += s * ( in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ] ) + tr[ 0 ];
	out[ 1 ] += s * ( in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ] ) + tr[ 1 ];
	out[ 2 ] += s * ( in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ] ) + tr[ 2 ];
}

static void LocalAddScaledMatrixTransformVectorTranslate( vec3_t in, float s, vec3_t mat[ 3 ], vec3_t tr, vec3_t out ) {
	out[ 0 ] += s * ( in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ] + tr[ 0 ] );
	out[ 1 ] += s * ( in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ] + tr[ 1 ] );
	out[ 2 ] += s * ( in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ] + tr[ 2 ] );
}

static void LocalAddScaledMatrixTransformVector( vec3_t in, float s, vec3_t mat[ 3 ], vec3_t out ) {
	out[ 0 ] += s * ( in[ 0 ] * mat[ 0 ][ 0 ] + in[ 1 ] * mat[ 0 ][ 1 ] + in[ 2 ] * mat[ 0 ][ 2 ] );
	out[ 1 ] += s * ( in[ 0 ] * mat[ 1 ][ 0 ] + in[ 1 ] * mat[ 1 ][ 1 ] + in[ 2 ] * mat[ 1 ][ 2 ] );
	out[ 2 ] += s * ( in[ 0 ] * mat[ 2 ][ 0 ] + in[ 1 ] * mat[ 2 ][ 1 ] + in[ 2 ] * mat[ 2 ][ 2 ] );
}





/*
===============================================================================

4x4 Matrices

===============================================================================
*/

static void Matrix4Multiply( const vec4_t a[4], const vec4_t b[4], vec4_t dst[4] ) {
	dst[0][0] = a[0][0] * b[0][0] + a[0][1] * b[1][0] + a[0][2] * b[2][0] + a[0][3] * b[3][0];
	dst[0][1] = a[0][0] * b[0][1] + a[0][1] * b[1][1] + a[0][2] * b[2][1] + a[0][3] * b[3][1];
	dst[0][2] = a[0][0] * b[0][2] + a[0][1] * b[1][2] + a[0][2] * b[2][2] + a[0][3] * b[3][2];
	dst[0][3] = a[0][0] * b[0][3] + a[0][1] * b[1][3] + a[0][2] * b[2][3] + a[0][3] * b[3][3];

	dst[1][0] = a[1][0] * b[0][0] + a[1][1] * b[1][0] + a[1][2] * b[2][0] + a[1][3] * b[3][0];
	dst[1][1] = a[1][0] * b[0][1] + a[1][1] * b[1][1] + a[1][2] * b[2][1] + a[1][3] * b[3][1];
	dst[1][2] = a[1][0] * b[0][2] + a[1][1] * b[1][2] + a[1][2] * b[2][2] + a[1][3] * b[3][2];
	dst[1][3] = a[1][0] * b[0][3] + a[1][1] * b[1][3] + a[1][2] * b[2][3] + a[1][3] * b[3][3];

	dst[2][0] = a[2][0] * b[0][0] + a[2][1] * b[1][0] + a[2][2] * b[2][0] + a[2][3] * b[3][0];
	dst[2][1] = a[2][0] * b[0][1] + a[2][1] * b[1][1] + a[2][2] * b[2][1] + a[2][3] * b[3][1];
	dst[2][2] = a[2][0] * b[0][2] + a[2][1] * b[1][2] + a[2][2] * b[2][2] + a[2][3] * b[3][2];
	dst[2][3] = a[2][0] * b[0][3] + a[2][1] * b[1][3] + a[2][2] * b[2][3] + a[2][3] * b[3][3];

	dst[3][0] = a[3][0] * b[0][0] + a[3][1] * b[1][0] + a[3][2] * b[2][0] + a[3][3] * b[3][0];
	dst[3][1] = a[3][0] * b[0][1] + a[3][1] * b[1][1] + a[3][2] * b[2][1] + a[3][3] * b[3][1];
	dst[3][2] = a[3][0] * b[0][2] + a[3][1] * b[1][2] + a[3][2] * b[2][2] + a[3][3] * b[3][2];
	dst[3][3] = a[3][0] * b[0][3] + a[3][1] * b[1][3] + a[3][2] * b[2][3] + a[3][3] * b[3][3];
}



static void Matrix4Transpose( const vec4_t matrix[4], vec4_t transpose[4] ) {
	int i, j;
	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			transpose[i][j] = matrix[j][i];
		}
	}
}

static void Matrix4FromAxis( const vec3_t axis[3], vec4_t dst[4] ) {
	int i, j;
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			dst[i][j] = axis[i][j];
		}
		dst[3][i] = 0;
		dst[i][3] = 0;
	}
	dst[3][3] = 1;
}

static void Matrix4FromScaledAxis( const vec3_t axis[3], const float scale, vec4_t dst[4] ) {
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			dst[i][j] = scale * axis[i][j];
			if ( i == j ) {
				dst[i][j] += 1.0f - scale;
			}
		}
		dst[3][i] = 0;
		dst[i][3] = 0;
	}
	dst[3][3] = 1;
}

static void Matrix4FromTranslation( const vec3_t t, vec4_t dst[4] ) {
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			if ( i == j ) {
				dst[i][j] = 1;
			} else {
				dst[i][j] = 0;
			}
		}
		dst[i][3] = t[i];
		dst[3][i] = 0;
	}
	dst[3][3] = 1;
}

// can put an axis rotation followed by a translation directly into one matrix
// TTimo: const usage would require an explicit cast, non ANSI C
// see unix/const-arg.c


// can put a scaled axis rotation followed by a translation directly into one matrix
// TTimo: const usage would require an explicit cast, non ANSI C
// see unix/const-arg.c


static void Matrix4FromScale( const float scale, vec4_t dst[4] ) {
	int i, j;

	for ( i = 0; i < 4; i++ ) {
		for ( j = 0; j < 4; j++ ) {
			if ( i == j ) {
				dst[i][j] = scale;
			} else {
				dst[i][j] = 0;
			}
		}
	}
	dst[3][3] = 1;
}

static void Matrix4TransformVector( const vec4_t m[4], const vec3_t src, vec3_t dst ) {
	dst[0] = m[0][0] * src[0] + m[0][1] * src[1] + m[0][2] * src[2] + m[0][3];
	dst[1] = m[1][0] * src[0] + m[1][1] * src[1] + m[1][2] * src[2] + m[1][3];
	dst[2] = m[2][0] * src[0] + m[2][1] * src[1] + m[2][2] * src[2] + m[2][3];
}

/*
===============================================================================

3x3 Matrices

===============================================================================
*/







#ifdef DBG_PROFILE_BONES
#define DBG_SHOWTIME    Com_Printf( "%i: %i, ", di++, ( dt = ri.Milliseconds() ) - ldt ); ldt = dt;
#else
#define DBG_SHOWTIME    ;
#endif

/*
==============
RB_SurfaceAnim
==============
*/
void RB_SurfaceAnim( mdsSurface_t *surface ) {
	int i, j, k;
	refEntity_t *refent;
	int             *boneList;
	mdsHeader_t     *header;
	

#ifdef DBG_PROFILE_BONES
	int di = 0, dt, ldt;

	dt = ri.Milliseconds();
	ldt = dt;
#endif

	refent = &backEnd.currentEntity->e;
	boneList = ( int * )( (byte *)surface + surface->ofsBoneReferences );
	header = ( mdsHeader_t * )( (byte *)surface + surface->ofsHeader );

	MDL_CalcBones( header, (const refEntity_t *)refent, boneList, surface->numBoneReferences);

	DBG_SHOWTIME

	//
	// calculate LOD
	//
	// TODO: lerp the radius and origin
	VectorAdd( refent->origin, frame->localOrigin, bonesVec);
	lodRadius = frame->radius;
	lodScale = R_CalcMDSLod( refent, bonesVec, lodRadius, header->lodBias, header->lodScale );


//DBG_SHOWTIME

//----(SA)	modification to allow dead skeletal bodies to go below minlod (experiment)
	if ( refent->reFlags & REFLAG_DEAD_LOD ) {
		if ( lodScale < 0.35 ) {   // allow dead to lod down to 35% (even if below surf->minLod) (%35 is arbitrary and probably not good generally.  worked for the blackguard/infantry as a test though)
			lodScale = 0.35;
		}
		render_count = (int)( (float) surface->numVerts * lodScale );

	} else {
		render_count = (int)( (float) surface->numVerts * lodScale );
		if ( render_count < surface->minLod ) {
			if ( !( refent->reFlags & REFLAG_DEAD_LOD ) ) {
				render_count = surface->minLod;
			}
		}
	}
//----(SA)	end


	if ( render_count > surface->numVerts ) {
		render_count = surface->numVerts;
	}

	RB_CheckOverflow( render_count, surface->numTriangles );

//DBG_SHOWTIME

	//
	// setup triangle list
	//
	RB_CheckOverflow( surface->numVerts, surface->numTriangles * 3 );

//DBG_SHOWTIME

	collapse_map   = ( int * )( ( byte * )surface + surface->ofsCollapseMap );
	triangles = ( int * )( (byte *)surface + surface->ofsTriangles );
	indexes = surface->numTriangles * 3;
	baseIndex = tess.numIndexes;
	baseVertex = tess.numVertexes;
	oldIndexes = baseIndex;

	tess.numVertexes += render_count;

	pIndexes = (int*)&tess.indexes[baseIndex];

//DBG_SHOWTIME

	if ( render_count == surface->numVerts ) {
		memcpy( pIndexes, triangles, sizeof( triangles[0] ) * indexes );
		if ( baseVertex ) {
			int *indexesEnd;
			for ( indexesEnd = pIndexes + indexes ; pIndexes < indexesEnd ; pIndexes++ ) {
				*pIndexes += baseVertex;
			}
		}
		tess.numIndexes += indexes;
	} else
	{
		int *collapseEnd;

		pCollapse = collapse;
		for ( j = 0; j < render_count; pCollapse++, j++ )
		{
			*pCollapse = j;
		}

		pCollapseMap = &collapse_map[render_count];
		for ( collapseEnd = collapse + surface->numVerts ; pCollapse < collapseEnd; pCollapse++, pCollapseMap++ )
		{
			*pCollapse = collapse[ *pCollapseMap ];
		}

		for ( j = 0 ; j < indexes ; j += 3 )
		{
			p0 = collapse[ *( triangles++ ) ];
			p1 = collapse[ *( triangles++ ) ];
			p2 = collapse[ *( triangles++ ) ];

			// FIXME
			// note:  serious optimization opportunity here,
			//  by sorting the triangles the following "continue"
			//  could have been made into a "break" statement.
			if ( p0 == p1 || p1 == p2 || p2 == p0 ) {
				continue;
			}

			*( pIndexes++ ) = baseVertex + p0;
			*( pIndexes++ ) = baseVertex + p1;
			*( pIndexes++ ) = baseVertex + p2;
			tess.numIndexes += 3;
		}

		baseIndex = tess.numIndexes;
	}

//DBG_SHOWTIME

	//
	// deform the vertexes by the lerped bones
	//
	numVerts = surface->numVerts;
	v = ( mdsVertex_t * )( (byte *)surface + surface->ofsVerts );
	tempVert = ( float * )( tess.xyz + baseVertex );
	tempNormal = ( float * )( tess.normal + baseVertex );
	for ( j = 0; j < render_count; j++, tempVert += 4, tempNormal += 4 ) {
		mdsWeight_t *w;

		VectorClear( tempVert );

		w = v->weights;
		for ( k = 0 ; k < v->numWeights ; k++, w++ ) {
			mdsBoneFrame_t  *bone = &bones[w->boneIndex];
			LocalAddScaledMatrixTransformVectorTranslate( w->offset, w->boneWeight, bone->matrix, bone->translation, tempVert );
		}

		LocalMatrixTransformVector( v->normal, bones[v->weights[0].boneIndex].matrix, tempNormal );

		tess.texCoords[baseVertex + j][0][0] = v->texCoords[0];
		tess.texCoords[baseVertex + j][0][1] = v->texCoords[1];

		v = (mdsVertex_t *)&v->weights[v->numWeights];
	}

	DBG_SHOWTIME
	/*
	if ( r_bonesDebug->integer ) {
		
		if ( r_bonesDebug->integer < 3 ) {
			// DEBUG: show the bones as a stick figure with axis at each bone
			boneRefs = ( int * )( (byte *)surface + surface->ofsBoneReferences );
			for ( i = 0; i < surface->numBoneReferences; i++, boneRefs++ ) {
				bonePtr = &bones[*boneRefs];
				//@TODO GL
			}
		}

		if ( r_bonesDebug->integer == 3 || r_bonesDebug->integer == 4 ) {
			int render_indexes = ( tess.numIndexes - oldIndexes );

			// show mesh edges
			tempVert = ( float * )( tess.xyz + baseVertex );
			tempNormal = ( float * )( tess.normal + baseVertex );
			//@TODO GL

//----(SA)	track debug stats
			if ( r_bonesDebug->integer == 4 ) {
				totalrv += render_count;
				totalrt += render_indexes / 3;
				totalv += surface->numVerts;
				totalt += surface->numTriangles;
			}
//----(SA)	end

			if ( r_bonesDebug->integer == 3 ) {
				ri.Printf( PRINT_ALL, "Lod %.2f  verts %4d/%4d  tris %4d/%4d  (%.2f%%)\n", lodScale, render_count, surface->numVerts, render_indexes / 3, surface->numTriangles,
						   ( float )( 100.0 * render_indexes / 3 ) / (float) surface->numTriangles );
			}
		}
	}

	if ( r_bonesDebug->integer > 1 ) {
		// dont draw the actual surface
		tess.numIndexes = oldIndexes;
		tess.numVertexes = baseVertex;
		return;
	}
	*/

#ifdef DBG_PROFILE_BONES
	Com_Printf( "\n" );
#endif

}

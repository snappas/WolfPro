#include "../game/q_shared.h"
#include "../qcommon/qfiles.h"
#include "../cgame/tr_types.h"
#include "../qcommon/qcommon.h"

static refEntity_t lastBoneEntity;
//static mdsBoneFrame_t bones[MDS_MAX_BONES], rawBones[MDS_MAX_BONES], oldBones[MDS_MAX_BONES];
mdsBoneFrame_t rawBones[MDS_MAX_BONES], oldBones[MDS_MAX_BONES];
static mdsBoneInfo_t   *boneInfo, *thisBoneInfo, *parentBoneInfo;
static mdsBoneFrame_t  *bonePtr, *bone, *parentBone;
static mdsBoneFrameCompressed_t    *cBonePtr, *cTBonePtr, *cOldBonePtr, *cOldTBonePtr, *cBoneList, *cOldBoneList, *cBoneListTorso, *cOldBoneListTorso;
static char validBones[MDS_MAX_BONES];
static char newBones[ MDS_MAX_BONES ];
static qboolean isTorso, fullTorso;
static short           *sh, *sh2;
static float           *pf;
static vec3_t angles, tangles, torsoParentOffset, torsoAxis[3], tmpAxis[3];
static float diff, a1, a2;
//static vec3_t vec, v2, dir;
static vec3_t v2, dir;
//static mdsFrame_t      *frame, *torsoFrame;
static mdsFrame_t *torsoFrame;
static mdsFrame_t      *oldFrame, *oldTorsoFrame;
static int frameSize;
static float torsoFrontlerp, torsoBacklerp;
static float frontlerp, backlerp;
static vec4_t m1[4], m2[4];
// static  vec4_t m3[4], m4[4]; // TTimo unused
// static  vec4_t tmp1[4], tmp2[4]; // TTimo unused
static vec3_t t;
mdsFrame_t *frame;
mdsBoneFrame_t  bones[MDS_MAX_BONES];
vec3_t bonesVec;

static int totalrv, totalrt, totalv, totalt;    //----(SA)

int cl_numModels;
model_t *cl_models[2048];
int sv_numModels;
model_t* sv_models[2048];

#define LL( x ) x = LittleLong( x )

/*
** R_GetModelByHandle
*/
model_t *MDL_GetModelByHandle( qhandle_t index, vmType_t vmType ) {
	model_t     *mod;

	if(vmType == VM_CGAME || vmType == VM_UI){
		// out of range gets the defualt model
		if ( index < 1 || index >= cl_numModels ) {
			return cl_models[0];
		}

		mod = cl_models[index];
	}else{
		if ( index < 1 || index >= sv_numModels ) {
			return sv_models[0];
		}

		mod = sv_models[index];
	}

	return mod;
}

/*
** R_AllocModel
*/
model_t *MDL_AllocModel( vmType_t vmType ) {
	model_t     *mod;

	int *numModels;
	model_t **models;
	if(vmType == VM_CGAME || vmType == VM_UI){
		numModels = &cl_numModels;
		models = cl_models;
	}else{
		numModels = &sv_numModels;
		models = sv_models;
	}

	if ( *numModels == 2048 ) {
		return NULL;
	}

	mod = Hunk_Alloc( sizeof( *models[*numModels] ), h_low );
	mod->index = *numModels;
	models[*numModels] = mod;
	(*numModels)++;

	return mod;
}


/*
====================
RE_RegisterModel

Loads in a model for the given name

Zero will be returned if the model fails to load.
An entry will be retained for failed models as an
optimization to prevent disk rescanning if they are
asked for again.
====================
*/
qhandle_t MDL_RegisterModel( const char *name, vmType_t vmType ) {
	model_t     *mod;
	unsigned    *buf;
	int lod;
	int ident = 0;         // TTimo: init
	qboolean loaded;
	qhandle_t hModel;
	int numLoaded;

	if ( !name || !name[0] ) {
		// Ridah, disabled this, we can see models that can't be found because they won't be there
		//ri.Printf( PRINT_ALL, "RE_RegisterModel: NULL name\n" );
		return 0;
	}

	if ( strlen( name ) >= MAX_QPATH ) {
		Com_Printf( "Model name exceeds MAX_QPATH\n" );
		return 0;
	}

	int *numModels;
	model_t **models;
	if(vmType == VM_CGAME || vmType == VM_UI){
		numModels = &cl_numModels;
		models = cl_models;
	}else{
		numModels = &sv_numModels;
		models = sv_models;
	}

	//
	// search the currently loaded models
	//
	for ( hModel = 1 ; hModel < *numModels; hModel++ ) {
		mod = models[hModel];
		if ( !Q_stricmp( mod->name, name ) ) {
			if ( mod->type == MOD_BAD ) {
				return 0;
			}
			return hModel;
		}
	}

	// allocate a new model_t

	if ( ( mod = MDL_AllocModel(vmType) ) == NULL ) {
		Com_Printf( S_COLOR_YELLOW"RE_RegisterModel: R_AllocModel() failed for '%s'\n", name );
		return 0;
	}

	// only set the name after the model has been successfully loaded
	Q_strncpyz( mod->name, name, sizeof( mod->name ) );

	mod->numLods = 0;

	//
	// load the files
	//
	numLoaded = 0;

	if ( strstr( name, ".mds" ) ) {  // try loading skeletal file
		loaded = qfalse;
		FS_ReadFile( name, (void **)&buf );
		if ( buf ) {
			ident = LittleLong( *(unsigned *)buf );
			if ( ident == MDS_IDENT ) {
				loaded = R_LoadMDS( mod, buf, name, vmType );
			}

			FS_FreeFile( buf );
		}

		if ( loaded ) {
			return mod->index;
		}
	}

	for ( lod = MD3_MAX_LODS - 1 ; lod >= 0 ; lod-- ) {
		char filename[1024];

		strcpy( filename, name );

		if ( lod != 0 ) {
			char namebuf[80];

			if ( strrchr( filename, '.' ) ) {
				*strrchr( filename, '.' ) = 0;
			}
			sprintf( namebuf, "_%d.md3", lod );
			strcat( filename, namebuf );
		}


		filename[strlen( filename ) - 1] = 'c';  // try MDC first

		FS_ReadFile( filename, (void **)&buf );

		if ( !buf ) {
			
			filename[strlen( filename ) - 1] = '3';  // try MD3 second
			
			FS_ReadFile( filename, (void **)&buf );
			if ( !buf ) {
				continue;
			}
		}

		ident = LittleLong( *(unsigned *)buf );
		// Ridah, mesh compression
		if ( ident != MD3_IDENT && ident != MDC_IDENT ) {
			Com_Printf( S_COLOR_YELLOW"RE_RegisterModel: unknown fileid for %s\n", name );
			goto fail;
		}

		if ( ident == MD3_IDENT ) {
			loaded = R_LoadMD3( mod, lod, buf, name, vmType );
		} else {
			loaded = R_LoadMDC( mod, lod, buf, name, vmType );
		}
		// done.

		FS_FreeFile( buf );

		if ( !loaded ) {
			if ( lod == 0 ) {
				goto fail;
			} else {
				break;
			}
		} else {
			mod->numLods++;
			numLoaded++;
			// if we have a valid model and are biased
			// so that we won't see any higher detail ones,
			// stop loading them
			// if ( lod <= r_lodbias->integer ) {
			// 	break;
			// }
		}
	}


	if ( numLoaded ) {
		// duplicate into higher lod spots that weren't
		// loaded, in case the user changes r_lodbias on the fly
		for ( lod-- ; lod >= 0 ; lod-- ) {
			mod->numLods++;
			// Ridah, mesh compression
			//	this check for mod->md3[0] could leave mod->md3[0] == 0x0000000 if r_lodbias is set to anything except '0'
			//	which causes trouble in tr_mesh.c in R_AddMD3Surfaces() and other locations since it checks md3[0]
			//	for various things.
			if ( ident == MD3_IDENT ) { //----(SA)	modified
//			if (mod->md3[0])		//----(SA)	end
				mod->md3[lod] = mod->md3[lod + 1];
			} else {
				mod->mdc[lod] = mod->mdc[lod + 1];
			}
			// done.
		}

		return mod->index;
	}

fail:
	// we still keep the model_t around, so if the model name is asked for
	// again, we won't bother scanning the filesystem
	mod->type = MOD_BAD;
	return 0;
}


/*
================
R_GetTag
================
*/
static int MDL_GetTag( byte *mod, int frame, const char *tagName, int startTagIndex, md3Tag_t **outTag ) {
	md3Tag_t        *tag;
	int i;
	md3Header_t     *md3;

	md3 = (md3Header_t *) mod;

	if ( frame >= md3->numFrames ) {
		// it is possible to have a bad frame while changing models, so don't error
		frame = md3->numFrames - 1;
	}

	if ( startTagIndex > md3->numTags ) {
		*outTag = NULL;
		return -1;
	}

	tag = ( md3Tag_t * )( (byte *)mod + md3->ofsTags ) + frame * md3->numTags;
	for ( i = 0 ; i < md3->numTags ; i++, tag++ ) {
		if ( ( i >= startTagIndex ) && !strcmp( tag->name, tagName ) ) {

			// if we are looking for an indexed tag, wait until we find the correct number of matches
			//if (startTagIndex) {
			//	startTagIndex--;
			//	continue;
			//}

			*outTag = tag;
			return i;   // found it
		}
	}

	*outTag = NULL;
	return -1;
}



/*
===============
MDL_RecursiveBoneListAdd
===============
*/
void MDL_RecursiveBoneListAdd( int bi, int *boneList, int *numBones, mdsBoneInfo_t *boneInfoList ) {

	if ( boneInfoList[ bi ].parent >= 0 ) {

		MDL_RecursiveBoneListAdd( boneInfoList[ bi ].parent, boneList, numBones, boneInfoList );

	}

	boneList[ ( *numBones )++ ] = bi;

}


void LocalAngleVector(vec3_t angles, vec3_t forward) {
	float LAVangle;
	float sp, sy, cp, cy;

	LAVangle = angles[YAW] * (M_PI * 2 / 360);
	sy = sin(LAVangle);
	cy = cos(LAVangle);
	LAVangle = angles[PITCH] * (M_PI * 2 / 360);
	sp = sin(LAVangle);
	cp = cos(LAVangle);

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
}

static void LocalVectorMA(vec3_t org, float dist, vec3_t vec, vec3_t out) {
	out[0] = org[0] + dist * vec[0];
	out[1] = org[1] + dist * vec[1];
	out[2] = org[2] + dist * vec[2];
}


#define ANGLES_SHORT_TO_FLOAT( pf, sh )     { *( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = SHORT2ANGLE( *( sh++ ) ); }

static void SLerp_Normal( vec3_t from, vec3_t to, float tt, vec3_t out ) {
	float ft = 1.0 - tt;

	out[0] = from[0] * ft + to[0] * tt;
	out[1] = from[1] * ft + to[1] * tt;
	out[2] = from[2] * ft + to[2] * tt;

	VectorNormalize( out );
}



/*
==============
R_CalcBone
==============
*/
void MDL_CalcBone( mdsHeader_t *header, const refEntity_t *refent, int boneNum, vec3_t vec) {
	int j;

	thisBoneInfo = &boneInfo[boneNum];
	if ( thisBoneInfo->torsoWeight ) {
		cTBonePtr = &cBoneListTorso[boneNum];
		isTorso = qtrue;
		if ( thisBoneInfo->torsoWeight == 1.0f ) {
			fullTorso = qtrue;
		}
	} else {
		isTorso = qfalse;
		fullTorso = qfalse;
	}
	cBonePtr = &cBoneList[boneNum];

	bonePtr = &bones[ boneNum ];

	// we can assume the parent has already been uncompressed for this frame + lerp
	if ( thisBoneInfo->parent >= 0 ) {
		parentBone = &bones[ thisBoneInfo->parent ];
		parentBoneInfo = &boneInfo[ thisBoneInfo->parent ];
	} else {
		parentBone = NULL;
		parentBoneInfo = NULL;
	}

#ifdef HIGH_PRECISION_BONES
	// rotation
	if ( fullTorso ) {
		VectorCopy( cTBonePtr->angles, angles );
	} else {
		VectorCopy( cBonePtr->angles, angles );
		if ( isTorso ) {
			VectorCopy( cTBonePtr->angles, tangles );
			// blend the angles together
			for ( j = 0; j < 3; j++ ) {
				diff = tangles[j] - angles[j];
				if ( fabs( diff ) > 180 ) {
					diff = AngleNormalize180( diff );
				}
				angles[j] = angles[j] + thisBoneInfo->torsoWeight * diff;
			}
		}
	}
#else
	// rotation
	if ( fullTorso ) {
		sh = (short *)cTBonePtr->angles;
		pf = angles;
		ANGLES_SHORT_TO_FLOAT( pf, sh );
	} else {
		sh = (short *)cBonePtr->angles;
		pf = angles;
		ANGLES_SHORT_TO_FLOAT( pf, sh );
		if ( isTorso ) {
			sh = (short *)cTBonePtr->angles;
			pf = tangles;
			ANGLES_SHORT_TO_FLOAT( pf, sh );
			// blend the angles together
			for ( j = 0; j < 3; j++ ) {
				diff = tangles[j] - angles[j];
				if ( fabs( diff ) > 180 ) {
					diff = AngleNormalize180( diff );
				}
				angles[j] = angles[j] + thisBoneInfo->torsoWeight * diff;
			}
		}
	}
#endif
	AnglesToAxis( angles, bonePtr->matrix );

	// translation
	if ( parentBone ) {

#ifdef HIGH_PRECISION_BONES
		if ( fullTorso ) {
			angles[0] = cTBonePtr->ofsAngles[0];
			angles[1] = cTBonePtr->ofsAngles[1];
			angles[2] = 0;
			LocalAngleVector( angles, vec );
			LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, vec, bonePtr->translation );
		} else {

			angles[0] = cBonePtr->ofsAngles[0];
			angles[1] = cBonePtr->ofsAngles[1];
			angles[2] = 0;
			LocalAngleVector( angles, vec );

			if ( isTorso ) {
				tangles[0] = cTBonePtr->ofsAngles[0];
				tangles[1] = cTBonePtr->ofsAngles[1];
				tangles[2] = 0;
				LocalAngleVector( tangles, v2 );

				// blend the angles together
				SLerp_Normal( vec, v2, thisBoneInfo->torsoWeight, vec );
				LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, vec, bonePtr->translation );

			} else {    // legs bone
				LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, vec, bonePtr->translation );
			}
		}
#else
		if ( fullTorso ) {
			sh = (short *)cTBonePtr->ofsAngles; pf = angles;
			*( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = 0;
			LocalAngleVector( angles, vec );
			LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, vec, bonePtr->translation );
		} else {

			sh = (short *)cBonePtr->ofsAngles; pf = angles;
			*( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = 0;
			LocalAngleVector( angles, vec );

			if ( isTorso ) {
				sh = (short *)cTBonePtr->ofsAngles;
				pf = tangles;
				*( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = SHORT2ANGLE( *( sh++ ) ); *( pf++ ) = 0;
				LocalAngleVector( tangles, v2 );

				// blend the angles together
				SLerp_Normal( vec, v2, thisBoneInfo->torsoWeight, vec );
				LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, vec, bonePtr->translation );

			} else {    // legs bone
				LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, vec, bonePtr->translation );
			}
		}
#endif
	} else {    // just use the frame position
		bonePtr->translation[0] = frame->parentOffset[0];
		bonePtr->translation[1] = frame->parentOffset[1];
		bonePtr->translation[2] = frame->parentOffset[2];
	}
	//
	if ( boneNum == header->torsoParent ) { // this is the torsoParent
		VectorCopy( bonePtr->translation, torsoParentOffset );
	}
	//
	validBones[boneNum] = 1;
	//
	rawBones[boneNum] = *bonePtr;
	newBones[boneNum] = 1;

}

/*
==============
MDL_CalcBoneLerp
==============
*/
void MDL_CalcBoneLerp(mdsHeader_t *header, const refEntity_t *refent, int boneNum, vec3_t vec ) {
	int j;

	if ( !refent || !header || boneNum < 0 || boneNum >= MDS_MAX_BONES ) {
		return;
	}


	thisBoneInfo = &boneInfo[boneNum];

	if ( !thisBoneInfo ) {
		return;
	}

	if ( thisBoneInfo->parent >= 0 ) {
		parentBone = &bones[ thisBoneInfo->parent ];
		parentBoneInfo = &boneInfo[ thisBoneInfo->parent ];
	} else {
		parentBone = NULL;
		parentBoneInfo = NULL;
	}

	if ( thisBoneInfo->torsoWeight ) {
		cTBonePtr = &cBoneListTorso[boneNum];
		cOldTBonePtr = &cOldBoneListTorso[boneNum];
		isTorso = qtrue;
		if ( thisBoneInfo->torsoWeight == 1.0f ) {
			fullTorso = qtrue;
		}
	} else {
		isTorso = qfalse;
		fullTorso = qfalse;
	}
	cBonePtr = &cBoneList[boneNum];
	cOldBonePtr = &cOldBoneList[boneNum];

	bonePtr = &bones[boneNum];

	newBones[ boneNum ] = 1;

	// rotation (take into account 170 to -170 lerps, which need to take the shortest route)
	if ( fullTorso ) {

		sh = (short *)cTBonePtr->angles;
		sh2 = (short *)cOldTBonePtr->angles;
		pf = angles;

		a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
		*( pf++ ) = a1 - torsoBacklerp * diff;
		a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
		*( pf++ ) = a1 - torsoBacklerp * diff;
		a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
		*( pf++ ) = a1 - torsoBacklerp * diff;

	} else {

		sh = (short *)cBonePtr->angles;
		sh2 = (short *)cOldBonePtr->angles;
		pf = angles;

		a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
		*( pf++ ) = a1 - backlerp * diff;
		a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
		*( pf++ ) = a1 - backlerp * diff;
		a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
		*( pf++ ) = a1 - backlerp * diff;

		if ( isTorso ) {

			sh = (short *)cTBonePtr->angles;
			sh2 = (short *)cOldTBonePtr->angles;
			pf = tangles;

			a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
			*( pf++ ) = a1 - torsoBacklerp * diff;
			a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
			*( pf++ ) = a1 - torsoBacklerp * diff;
			a1 = SHORT2ANGLE( *( sh++ ) ); a2 = SHORT2ANGLE( *( sh2++ ) ); diff = AngleNormalize180( a1 - a2 );
			*( pf++ ) = a1 - torsoBacklerp * diff;

			// blend the angles together
			for ( j = 0; j < 3; j++ ) {
				diff = tangles[j] - angles[j];
				if ( fabs( diff ) > 180 ) {
					diff = AngleNormalize180( diff );
				}
				angles[j] = angles[j] + thisBoneInfo->torsoWeight * diff;
			}

		}

	}
	AnglesToAxis( angles, bonePtr->matrix );

	if ( parentBone ) {

		if ( fullTorso ) {
			sh = (short *)cTBonePtr->ofsAngles;
			sh2 = (short *)cOldTBonePtr->ofsAngles;
		} else {
			sh = (short *)cBonePtr->ofsAngles;
			sh2 = (short *)cOldBonePtr->ofsAngles;
		}

		pf = angles;
		*( pf++ ) = SHORT2ANGLE( *( sh++ ) );
		*( pf++ ) = SHORT2ANGLE( *( sh++ ) );
		*( pf++ ) = 0;
		LocalAngleVector( angles, v2 );     // new

		pf = angles;
		*( pf++ ) = SHORT2ANGLE( *( sh2++ ) );
		*( pf++ ) = SHORT2ANGLE( *( sh2++ ) );
		*( pf++ ) = 0;
		LocalAngleVector( angles, vec );    // old

		// blend the angles together
		if ( fullTorso ) {
			SLerp_Normal( vec, v2, torsoFrontlerp, dir );
		} else {
			SLerp_Normal( vec, v2, frontlerp, dir );
		}

		// translation
		if ( !fullTorso && isTorso ) {    // partial legs/torso, need to lerp according to torsoWeight

			// calc the torso frame
			sh = (short *)cTBonePtr->ofsAngles;
			sh2 = (short *)cOldTBonePtr->ofsAngles;

			pf = angles;
			*( pf++ ) = SHORT2ANGLE( *( sh++ ) );
			*( pf++ ) = SHORT2ANGLE( *( sh++ ) );
			*( pf++ ) = 0;
			LocalAngleVector( angles, v2 );     // new

			pf = angles;
			*( pf++ ) = SHORT2ANGLE( *( sh2++ ) );
			*( pf++ ) = SHORT2ANGLE( *( sh2++ ) );
			*( pf++ ) = 0;
			LocalAngleVector( angles, vec );    // old

			// blend the angles together
			SLerp_Normal( vec, v2, torsoFrontlerp, v2 );

			// blend the torso/legs together
			SLerp_Normal( dir, v2, thisBoneInfo->torsoWeight, dir );

		}

		LocalVectorMA( parentBone->translation, thisBoneInfo->parentDist, dir, bonePtr->translation );

	} else {    // just interpolate the frame positions

		bonePtr->translation[0] = frontlerp * frame->parentOffset[0] + backlerp * oldFrame->parentOffset[0];
		bonePtr->translation[1] = frontlerp * frame->parentOffset[1] + backlerp * oldFrame->parentOffset[1];
		bonePtr->translation[2] = frontlerp * frame->parentOffset[2] + backlerp * oldFrame->parentOffset[2];

	}
	//
	if ( boneNum == header->torsoParent ) { // this is the torsoParent
		VectorCopy( bonePtr->translation, torsoParentOffset );
	}
	validBones[boneNum] = 1;
	//
	rawBones[boneNum] = *bonePtr;
	newBones[boneNum] = 1;

}

/*
==============
R_CalcBones

	The list of bones[] should only be built and modified from within here
==============
*/

void MDL_CalcBones(mdsHeader_t* header, const refEntity_t* refent, int* boneList, int numBones) {

	int i;
	int* boneRefs;
	float torsoWeight;
	
	//
	// if the entity has changed since the last time the bones were built, reset them
	//
	if (memcmp(&lastBoneEntity, refent, sizeof(refEntity_t))) {
		// different, cached bones are not valid
		memset(validBones, 0, header->numBones);
		lastBoneEntity = *refent;

		// (SA) also reset these counter statics
//----(SA)	print stats for the complete model (not per-surface)
		// if (r_bonesDebug->integer == 4 && totalrt) {
		// 	ri.Printf(PRINT_ALL, "Lod %.2f  verts %4d/%4d  tris %4d/%4d  (%.2f%%)\n",
		// 		lodScale,
		// 		totalrv,
		// 		totalv,
		// 		totalrt,
		// 		totalt,
		// 		(float)(100.0 * totalrt) / (float)totalt);
		// }
		//----(SA)	end
		totalrv = totalrt = totalv = totalt = 0;

	}

	memset(newBones, 0, header->numBones);

	if (refent->oldframe == refent->frame) {
		backlerp = 0;
		frontlerp = 1;
	}
	else {
		backlerp = refent->backlerp;
		frontlerp = 1.0f - backlerp;
	}

	if (refent->oldTorsoFrame == refent->torsoFrame) {
		torsoBacklerp = 0;
		torsoFrontlerp = 1;
	}
	else {
		torsoBacklerp = refent->torsoBacklerp;
		torsoFrontlerp = 1.0f - torsoBacklerp;
	}

	frameSize = (int)(sizeof(mdsFrame_t) + (header->numBones - 1) * sizeof(mdsBoneFrameCompressed_t));

	frame = (mdsFrame_t*)((byte*)header + header->ofsFrames +
		refent->frame * frameSize);
	torsoFrame = (mdsFrame_t*)((byte*)header + header->ofsFrames +
		refent->torsoFrame * frameSize);
	oldFrame = (mdsFrame_t*)((byte*)header + header->ofsFrames +
		refent->oldframe * frameSize);
	oldTorsoFrame = (mdsFrame_t*)((byte*)header + header->ofsFrames +
		refent->oldTorsoFrame * frameSize);

	//
	// lerp all the needed bones (torsoParent is always the first bone in the list)
	//
	cBoneList = frame->bones;
	cBoneListTorso = torsoFrame->bones;

	boneInfo = (mdsBoneInfo_t*)((byte*)header + header->ofsBones);
	boneRefs = boneList;
	//
	Matrix3Transpose(refent->torsoAxis, torsoAxis);

#ifdef HIGH_PRECISION_BONES
	if (qtrue) {
#else
	if (!backlerp && !torsoBacklerp) {
#endif

		for (i = 0; i < numBones; i++, boneRefs++) {

			if (validBones[*boneRefs]) {
				// this bone is still in the cache
				bones[*boneRefs] = rawBones[*boneRefs];
				continue;
			}

			// find our parent, and make sure it has been calculated
			if ((boneInfo[*boneRefs].parent >= 0) && (!validBones[boneInfo[*boneRefs].parent] && !newBones[boneInfo[*boneRefs].parent])) {
				MDL_CalcBone(header, refent, boneInfo[*boneRefs].parent, bonesVec);
			}

			MDL_CalcBone(header, refent, *boneRefs, bonesVec);

		}

	}
	else {    // interpolated

		cOldBoneList = oldFrame->bones;
		cOldBoneListTorso = oldTorsoFrame->bones;

		for (i = 0; i < numBones; i++, boneRefs++) {

			if (validBones[*boneRefs]) {
				// this bone is still in the cache
				bones[*boneRefs] = rawBones[*boneRefs];
				continue;
			}

			// find our parent, and make sure it has been calculated
			if ((boneInfo[*boneRefs].parent >= 0) && (!validBones[boneInfo[*boneRefs].parent] && !newBones[boneInfo[*boneRefs].parent])) {
				MDL_CalcBoneLerp(header, refent, boneInfo[*boneRefs].parent, bonesVec);
			}

			MDL_CalcBoneLerp(header, refent, *boneRefs, bonesVec);

		}

	}

	// adjust for torso rotations
	torsoWeight = 0;
	boneRefs = boneList;
	for (i = 0; i < numBones; i++, boneRefs++) {

		thisBoneInfo = &boneInfo[*boneRefs];
		bonePtr = &bones[*boneRefs];
		// add torso rotation
		if (thisBoneInfo->torsoWeight > 0) {

			if (!newBones[*boneRefs]) {
				// just copy it back from the previous calc
				bones[*boneRefs] = oldBones[*boneRefs];
				continue;
			}

			if (!(thisBoneInfo->flags & BONEFLAG_TAG)) {

				// 1st multiply with the bone->matrix
				// 2nd translation for rotation relative to bone around torso parent offset
				VectorSubtract(bonePtr->translation, torsoParentOffset, t);
				Matrix4FromAxisPlusTranslation(bonePtr->matrix, t, m1);
				// 3rd scaled rotation
				// 4th translate back to torso parent offset
				// use previously created matrix if available for the same weight
				if (torsoWeight != thisBoneInfo->torsoWeight) {
					Matrix4FromScaledAxisPlusTranslation(torsoAxis, thisBoneInfo->torsoWeight, torsoParentOffset, m2);
					torsoWeight = thisBoneInfo->torsoWeight;
				}
				// multiply matrices to create one matrix to do all calculations
				Matrix4MultiplyInto3x3AndTranslation(m2, m1, bonePtr->matrix, bonePtr->translation);

			}
			else {    // tag's require special handling

				// rotate each of the axis by the torsoAngles
				LocalScaledMatrixTransformVector(bonePtr->matrix[0], thisBoneInfo->torsoWeight, torsoAxis, tmpAxis[0]);
				LocalScaledMatrixTransformVector(bonePtr->matrix[1], thisBoneInfo->torsoWeight, torsoAxis, tmpAxis[1]);
				LocalScaledMatrixTransformVector(bonePtr->matrix[2], thisBoneInfo->torsoWeight, torsoAxis, tmpAxis[2]);
				memcpy(bonePtr->matrix, tmpAxis, sizeof(tmpAxis));

				// rotate the translation around the torsoParent
				VectorSubtract(bonePtr->translation, torsoParentOffset, t);
				LocalScaledMatrixTransformVector(t, thisBoneInfo->torsoWeight, torsoAxis, bonePtr->translation);
				VectorAdd(bonePtr->translation, torsoParentOffset, bonePtr->translation);

			}
		}
	}

	// backup the final bones
	memcpy(oldBones, bones, sizeof(bones[0]) * header->numBones);
	}

/*
===============
R_GetBoneTag
===============
*/
int R_GetBoneTag( orientation_t *outTag, mdsHeader_t *mds, int startTagIndex, const refEntity_t *refent, const char *tagName ) {

	int i;
	mdsTag_t    *pTag;
	mdsBoneInfo_t *boneInfoList;
	int boneList[ MDS_MAX_BONES ];
	int numBones;
	

	if ( startTagIndex > mds->numTags ) {
		memset( outTag, 0, sizeof( *outTag ) );
		return -1;
	}

	// find the correct tag

	pTag = ( mdsTag_t * )( (byte *)mds + mds->ofsTags );

	pTag += startTagIndex;

	for ( i = startTagIndex; i < mds->numTags; i++, pTag++ ) {
		if ( !strcmp( pTag->name, tagName ) ) {
			break;
		}
	}

	if ( i >= mds->numTags ) {
		memset( outTag, 0, sizeof( *outTag ) );
		return -1;
	}

	// now build the list of bones we need to calc to get this tag's bone information

	boneInfoList = ( mdsBoneInfo_t * )( (byte *)mds + mds->ofsBones );
	numBones = 0;

	MDL_RecursiveBoneListAdd( pTag->boneIndex, boneList, &numBones, boneInfoList );

	// calc the bones
	MDL_CalcBones( (mdsHeader_t *)mds, refent, boneList, numBones);

	// now extract the orientation for the bone that represents our tag

	memcpy( outTag->axis, bones[ pTag->boneIndex ].matrix, sizeof( outTag->axis ) );
	VectorCopy( bones[ pTag->boneIndex ].translation, outTag->origin );


	return i;
}


/*
================
R_GetMDCTag
================
*/
static int R_GetMDCTag( byte *mod, int frame, const char *tagName, int startTagIndex, mdcTag_t **outTag ) {
	mdcTag_t        *tag;
	mdcTagName_t    *pTagName;
	int i;
	mdcHeader_t     *mdc;

	mdc = (mdcHeader_t *) mod;

	if ( frame >= mdc->numFrames ) {
		// it is possible to have a bad frame while changing models, so don't error
		frame = mdc->numFrames - 1;
	}

	if ( startTagIndex > mdc->numTags ) {
		*outTag = NULL;
		return -1;
	}

	pTagName = ( mdcTagName_t * )( (byte *)mod + mdc->ofsTagNames );
	for ( i = 0 ; i < mdc->numTags ; i++, pTagName++ ) {
		if ( ( i >= startTagIndex ) && !strcmp( pTagName->name, tagName ) ) {
			break;  // found it
		}
	}

	if ( i >= mdc->numTags ) {
		*outTag = NULL;
		return -1;
	}

	tag = ( mdcTag_t * )( (byte *)mod + mdc->ofsTags ) + frame * mdc->numTags + i;
	*outTag = tag;
	return i;
}

/*
================
R_LerpTag

  returns the index of the tag it found, for cycling through tags with the same name
================
*/


int MDL_LerpTagExt( orientation_t *tag, lerpInfo_t *lerpInfo, const char *tagNameIn, int startIndex, vmType_t vmType ){
	refEntity_t re;
	re.hModel = lerpInfo->modelHandle;
	re.oldframe = lerpInfo->oldFrame;
	re.frame = lerpInfo->frame;
	re.backlerp = lerpInfo->backlerp;
	re.torsoFrame = lerpInfo->torsoFrame;
	re.oldTorsoFrame = lerpInfo->oldTorsoFrame;
	re.torsoBacklerp = lerpInfo->torsoBacklerp;
	for(int i = 0; i < 3; i++){
		VectorCopy(lerpInfo->torsoAxis[i], re.torsoAxis[i]);
		VectorCopy(lerpInfo->legsAxis[i], re.axis[i]);
	}
	

	return MDL_LerpTag(tag, &re, tagNameIn, startIndex, vmType);
}

int MDL_LerpTag( orientation_t *tag, const refEntity_t *refent, const char *tagNameIn, int startIndex, vmType_t vmType ) {
	md3Tag_t    *start, *end;
	md3Tag_t ustart, uend;
	int i;
	float frontLerp, backLerp;
	model_t     *model;
	vec3_t sangles, eangles;
	char tagName[MAX_QPATH];       //, *ch;
	int retval;
	qhandle_t handle;
	int startFrame, endFrame;
	float frac;

	handle = refent->hModel;
	startFrame = refent->oldframe;
	endFrame = refent->frame;
	frac = 1.0 - refent->backlerp;

	Q_strncpyz( tagName, tagNameIn, MAX_QPATH );
/*
	// if the tagName has a space in it, then it is passing through the starting tag number
	if (ch = strrchr(tagName, ' ')) {
		*ch = 0;
		ch++;
		startIndex = atoi(ch);
	}
*/
	model = MDL_GetModelByHandle( handle, vmType );
	if ( !model->md3[0] && !model->mdc[0] && !model->mds ) {
		AxisClear( tag->axis );
		VectorClear( tag->origin );
		return -1;
	}

	frontLerp = frac;
	backLerp = 1.0 - frac;

	if ( model->type == MOD_MESH ) {
		// old MD3 style
		retval = MDL_GetTag( (byte *)model->md3[0], startFrame, tagName, startIndex, &start );
		retval = MDL_GetTag( (byte *)model->md3[0], endFrame, tagName, startIndex, &end );

	} else if ( model->type == MOD_MDS ) {    // use bone lerping

		retval = R_GetBoneTag( tag, model->mds, startIndex, refent, tagNameIn );

		if ( retval >= 0 ) {
			return retval;
		}

		// failed
		return -1;

	} else {
		// psuedo-compressed MDC tags
		mdcTag_t    *cstart, *cend;

		retval = R_GetMDCTag( (byte *)model->mdc[0], startFrame, tagName, startIndex, &cstart );
		retval = R_GetMDCTag( (byte *)model->mdc[0], endFrame, tagName, startIndex, &cend );

		// uncompress the MDC tags into MD3 style tags
		if ( cstart && cend ) {
			for ( i = 0; i < 3; i++ ) {
				ustart.origin[i] = (float)cstart->xyz[i] * MD3_XYZ_SCALE;
				uend.origin[i] = (float)cend->xyz[i] * MD3_XYZ_SCALE;
				sangles[i] = (float)cstart->angles[i] * MDC_TAG_ANGLE_SCALE;
				eangles[i] = (float)cend->angles[i] * MDC_TAG_ANGLE_SCALE;
			}

			AnglesToAxis( sangles, ustart.axis );
			AnglesToAxis( eangles, uend.axis );

			start = &ustart;
			end = &uend;
		} else {
			start = NULL;
			end = NULL;
		}

	}

	if ( !start || !end ) {
		AxisClear( tag->axis );
		VectorClear( tag->origin );
		return -1;
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		tag->origin[i] = start->origin[i] * backLerp +  end->origin[i] * frontLerp;
		tag->axis[0][i] = start->axis[0][i] * backLerp +  end->axis[0][i] * frontLerp;
		tag->axis[1][i] = start->axis[1][i] * backLerp +  end->axis[1][i] * frontLerp;
		tag->axis[2][i] = start->axis[2][i] * backLerp +  end->axis[2][i] * frontLerp;
	}

	VectorNormalize( tag->axis[0] );
	VectorNormalize( tag->axis[1] );
	VectorNormalize( tag->axis[2] );

	return retval;
}


/*
=================
R_LoadMDC
=================
*/
qboolean R_LoadMDC( model_t *mod, int lod, void *buffer, const char *mod_name, vmType_t vmType ) {
	int i, j;
	mdcHeader_t         *pinmodel;
	md3Frame_t          *frame;
	mdcSurface_t        *surf;
	md3Shader_t         *shader;
	md3Triangle_t       *tri;
	md3St_t             *st;
	md3XyzNormal_t      *xyz;
	mdcXyzCompressed_t  *xyzComp;
	mdcTag_t            *tag;
	short               *ps;
	int version;
	int size;

	pinmodel = (mdcHeader_t *)buffer;

	version = LittleLong( pinmodel->version );
	if ( version != MDC_VERSION ) {
		Com_Printf( S_COLOR_YELLOW"R_LoadMDC: %s has wrong version (%i should be %i)\n",
				   mod_name, version, MDC_VERSION );
		return qfalse;
	}

	mod->type = MOD_MDC;
	size = LittleLong( pinmodel->ofsEnd );
	mod->dataSize += size;
	mod->mdc[lod] = Hunk_Alloc( size, h_low );

	memcpy( mod->mdc[lod], buffer, LittleLong( pinmodel->ofsEnd ) );

	LL( mod->mdc[lod]->ident );
	LL( mod->mdc[lod]->version );
	LL( mod->mdc[lod]->numFrames );
	LL( mod->mdc[lod]->numTags );
	LL( mod->mdc[lod]->numSurfaces );
	LL( mod->mdc[lod]->ofsFrames );
	LL( mod->mdc[lod]->ofsTagNames );
	LL( mod->mdc[lod]->ofsTags );
	LL( mod->mdc[lod]->ofsSurfaces );
	LL( mod->mdc[lod]->ofsEnd );
	LL( mod->mdc[lod]->flags );
	LL( mod->mdc[lod]->numSkins );


	if ( mod->mdc[lod]->numFrames < 1 ) {
		Com_Printf( S_COLOR_YELLOW"R_LoadMDC: %s has no frames\n", mod_name );
		return qfalse;
	}

	// swap all the frames
	frame = ( md3Frame_t * )( (byte *)mod->mdc[lod] + mod->mdc[lod]->ofsFrames );
	for ( i = 0 ; i < mod->mdc[lod]->numFrames ; i++, frame++ ) {
		frame->radius = LittleFloat( frame->radius );
		if ( strstr( mod->name,"sherman" ) || strstr( mod->name, "mg42" ) ) {
			frame->radius = 256;
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = 128;
				frame->bounds[1][j] = -128;
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		} else
		{
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
				frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		}
	}

	// swap all the tags
	tag = ( mdcTag_t * )( (byte *)mod->mdc[lod] + mod->mdc[lod]->ofsTags );
	if ( LittleLong( 1 ) != 1 ) {
		for ( i = 0 ; i < mod->mdc[lod]->numTags * mod->mdc[lod]->numFrames ; i++, tag++ ) {
			for ( j = 0 ; j < 3 ; j++ ) {
				tag->xyz[j] = LittleShort( tag->xyz[j] );
				tag->angles[j] = LittleShort( tag->angles[j] );
			}
		}
	}

	// swap all the surfaces
	surf = ( mdcSurface_t * )( (byte *)mod->mdc[lod] + mod->mdc[lod]->ofsSurfaces );
	for ( i = 0 ; i < mod->mdc[lod]->numSurfaces ; i++ ) {

		LL( surf->ident );
		LL( surf->flags );
		LL( surf->numBaseFrames );
		LL( surf->numCompFrames );
		LL( surf->numShaders );
		LL( surf->numTriangles );
		LL( surf->ofsTriangles );
		LL( surf->numVerts );
		LL( surf->ofsShaders );
		LL( surf->ofsSt );
		LL( surf->ofsXyzNormals );
		LL( surf->ofsXyzCompressed );
		LL( surf->ofsFrameBaseFrames );
		LL( surf->ofsFrameCompFrames );
		LL( surf->ofsEnd );

		if ( surf->numVerts > SHADER_MAX_VERTEXES ) {
			Com_Error( ERR_DROP, "R_LoadMDC: %s has more than %i verts on a surface (%i)",
					  mod_name, SHADER_MAX_VERTEXES, surf->numVerts );
		}
		if ( surf->numTriangles * 3 > SHADER_MAX_INDEXES ) {
			Com_Error( ERR_DROP, "R_LoadMDC: %s has more than %i triangles on a surface (%i)",
					  mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles );
		}

		// change to surface identifier
		surf->ident = SF_MDC;

		// lowercase the surface name so skin compares are faster
		Q_strlwr( surf->name );

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen( surf->name );
		if ( j > 2 && surf->name[j - 2] == '_' ) {
			surf->name[j - 2] = 0;
		}

		// register the shaders
		shader = ( md3Shader_t * )( (byte *)surf + surf->ofsShaders );
		for ( j = 0 ; j < surf->numShaders ; j++, shader++ ) {
		#ifndef DEDICATED
			shader->shaderIndex = R_LookupShaderIndexFromName(shader->name, vmType);
		#else
			shader->shaderIndex = 0;
		#endif
		}

		// Ridah, optimization, only do the swapping if we really need to
		if ( LittleShort( 1 ) != 1 ) {

			// swap all the triangles
			tri = ( md3Triangle_t * )( (byte *)surf + surf->ofsTriangles );
			for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
				LL( tri->indexes[0] );
				LL( tri->indexes[1] );
				LL( tri->indexes[2] );
			}

			// swap all the ST
			st = ( md3St_t * )( (byte *)surf + surf->ofsSt );
			for ( j = 0 ; j < surf->numVerts ; j++, st++ ) {
				st->st[0] = LittleFloat( st->st[0] );
				st->st[1] = LittleFloat( st->st[1] );
			}

			// swap all the XyzNormals
			xyz = ( md3XyzNormal_t * )( (byte *)surf + surf->ofsXyzNormals );
			for ( j = 0 ; j < surf->numVerts * surf->numBaseFrames ; j++, xyz++ )
			{
				xyz->xyz[0] = LittleShort( xyz->xyz[0] );
				xyz->xyz[1] = LittleShort( xyz->xyz[1] );
				xyz->xyz[2] = LittleShort( xyz->xyz[2] );

				xyz->normal = LittleShort( xyz->normal );
			}

			// swap all the XyzCompressed
			xyzComp = ( mdcXyzCompressed_t * )( (byte *)surf + surf->ofsXyzCompressed );
			for ( j = 0 ; j < surf->numVerts * surf->numCompFrames ; j++, xyzComp++ )
			{
				LL( xyzComp->ofsVec );
			}

			// swap the frameBaseFrames
			ps = ( short * )( (byte *)surf + surf->ofsFrameBaseFrames );
			for ( j = 0; j < mod->mdc[lod]->numFrames; j++, ps++ )
			{
				*ps = LittleShort( *ps );
			}

			// swap the frameCompFrames
			ps = ( short * )( (byte *)surf + surf->ofsFrameCompFrames );
			for ( j = 0; j < mod->mdc[lod]->numFrames; j++, ps++ )
			{
				*ps = LittleShort( *ps );
			}
		}
		// done.

		// find the next surface
		surf = ( mdcSurface_t * )( (byte *)surf + surf->ofsEnd );
	}

	return qtrue;
}

/*
=================
R_LoadMD3
=================
*/
qboolean R_LoadMD3( model_t *mod, int lod, void *buffer, const char *mod_name, vmType_t vmType ) {
	int i, j;
	md3Header_t         *pinmodel;
	md3Frame_t          *frame;
	md3Surface_t        *surf;
	md3Shader_t         *shader;
	md3Triangle_t       *tri;
	md3St_t             *st;
	md3XyzNormal_t      *xyz;
	md3Tag_t            *tag;
	int version;
	int size;
	qboolean fixRadius = qfalse;

	pinmodel = (md3Header_t *)buffer;

	version = LittleLong( pinmodel->version );
	if ( version != MD3_VERSION ) {
		Com_Printf( S_COLOR_YELLOW"R_LoadMD3: %s has wrong version (%i should be %i)\n",
				   mod_name, version, MD3_VERSION );
		return qfalse;
	}

	mod->type = MOD_MESH;
	size = LittleLong( pinmodel->ofsEnd );
	mod->dataSize += size;
	// Ridah, convert to compressed format
	mod->md3[lod] = Hunk_Alloc( size, h_low );
	
	// done.

	memcpy( mod->md3[lod], buffer, LittleLong( pinmodel->ofsEnd ) );

	LL( mod->md3[lod]->ident );
	LL( mod->md3[lod]->version );
	LL( mod->md3[lod]->numFrames );
	LL( mod->md3[lod]->numTags );
	LL( mod->md3[lod]->numSurfaces );
	LL( mod->md3[lod]->ofsFrames );
	LL( mod->md3[lod]->ofsTags );
	LL( mod->md3[lod]->ofsSurfaces );
	LL( mod->md3[lod]->ofsEnd );

	if ( mod->md3[lod]->numFrames < 1 ) {
		Com_Printf( S_COLOR_YELLOW"R_LoadMD3: %s has no frames\n", mod_name );
		return qfalse;
	}

	if ( strstr( mod->name,"sherman" ) || strstr( mod->name, "mg42" ) ) {
		fixRadius = qtrue;
	}

	// swap all the frames
	frame = ( md3Frame_t * )( (byte *)mod->md3[lod] + mod->md3[lod]->ofsFrames );
	for ( i = 0 ; i < mod->md3[lod]->numFrames ; i++, frame++ ) {
		frame->radius = LittleFloat( frame->radius );
		if ( fixRadius ) {
			frame->radius = 256;
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = 128;
				frame->bounds[1][j] = -128;
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		}
		// Hack for Bug using plugin generated model
		else if ( frame->radius == 1 ) {
			frame->radius = 256;
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = 128;
				frame->bounds[1][j] = -128;
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		} else
		{
			for ( j = 0 ; j < 3 ; j++ ) {
				frame->bounds[0][j] = LittleFloat( frame->bounds[0][j] );
				frame->bounds[1][j] = LittleFloat( frame->bounds[1][j] );
				frame->localOrigin[j] = LittleFloat( frame->localOrigin[j] );
			}
		}
	}

	// swap all the tags
	tag = ( md3Tag_t * )( (byte *)mod->md3[lod] + mod->md3[lod]->ofsTags );
	for ( i = 0 ; i < mod->md3[lod]->numTags * mod->md3[lod]->numFrames ; i++, tag++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			tag->origin[j] = LittleFloat( tag->origin[j] );
			tag->axis[0][j] = LittleFloat( tag->axis[0][j] );
			tag->axis[1][j] = LittleFloat( tag->axis[1][j] );
			tag->axis[2][j] = LittleFloat( tag->axis[2][j] );
		}
	}

	// swap all the surfaces
	surf = ( md3Surface_t * )( (byte *)mod->md3[lod] + mod->md3[lod]->ofsSurfaces );
	for ( i = 0 ; i < mod->md3[lod]->numSurfaces ; i++ ) {

		LL( surf->ident );
		LL( surf->flags );
		LL( surf->numFrames );
		LL( surf->numShaders );
		LL( surf->numTriangles );
		LL( surf->ofsTriangles );
		LL( surf->numVerts );
		LL( surf->ofsShaders );
		LL( surf->ofsSt );
		LL( surf->ofsXyzNormals );
		LL( surf->ofsEnd );

		if ( surf->numVerts > SHADER_MAX_VERTEXES ) {
			Com_Error( ERR_DROP, "R_LoadMD3: %s has more than %i verts on a surface (%i)",
					  mod_name, SHADER_MAX_VERTEXES, surf->numVerts );
		}
		if ( surf->numTriangles * 3 > SHADER_MAX_INDEXES ) {
			Com_Error( ERR_DROP, "R_LoadMD3: %s has more than %i triangles on a surface (%i)",
					  mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles );
		}

		// change to surface identifier
		surf->ident = SF_MD3;

		// lowercase the surface name so skin compares are faster
		Q_strlwr( surf->name );

		// strip off a trailing _1 or _2
		// this is a crutch for q3data being a mess
		j = strlen( surf->name );
		if ( j > 2 && surf->name[j - 2] == '_' ) {
			surf->name[j - 2] = 0;
		}

		// register the shaders
		shader = ( md3Shader_t * )( (byte *)surf + surf->ofsShaders );
		for ( j = 0 ; j < surf->numShaders ; j++, shader++ ) {
		#ifndef DEDICATED
			shader->shaderIndex = R_LookupShaderIndexFromName(shader->name, vmType);
		#else
			shader->shaderIndex = 0;
		#endif
		}

		// Ridah, optimization, only do the swapping if we really need to
		if ( LittleShort( 1 ) != 1 ) {

			// swap all the triangles
			tri = ( md3Triangle_t * )( (byte *)surf + surf->ofsTriangles );
			for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
				LL( tri->indexes[0] );
				LL( tri->indexes[1] );
				LL( tri->indexes[2] );
			}

			// swap all the ST
			st = ( md3St_t * )( (byte *)surf + surf->ofsSt );
			for ( j = 0 ; j < surf->numVerts ; j++, st++ ) {
				st->st[0] = LittleFloat( st->st[0] );
				st->st[1] = LittleFloat( st->st[1] );
			}

			// swap all the XyzNormals
			xyz = ( md3XyzNormal_t * )( (byte *)surf + surf->ofsXyzNormals );
			for ( j = 0 ; j < surf->numVerts * surf->numFrames ; j++, xyz++ )
			{
				xyz->xyz[0] = LittleShort( xyz->xyz[0] );
				xyz->xyz[1] = LittleShort( xyz->xyz[1] );
				xyz->xyz[2] = LittleShort( xyz->xyz[2] );

				xyz->normal = LittleShort( xyz->normal );
			}

		}
		// done.

		// find the next surface
		surf = ( md3Surface_t * )( (byte *)surf + surf->ofsEnd );
	}

	return qtrue;
}



/*
=================
R_LoadMDS
=================
*/
qboolean R_LoadMDS( model_t *mod, void *buffer, const char *mod_name, vmType_t vmType ) {
	int i, j, k;
	mdsHeader_t         *pinmodel, *mds;
	mdsFrame_t          *mdsFrame;
	mdsSurface_t        *surf;
	mdsTriangle_t       *tri;
	mdsVertex_t         *v;
	mdsBoneInfo_t       *bi;
	mdsTag_t            *tag;
	int version;
	int size;
	int frameSize;
	int                 *collapseMap, *boneref;

	pinmodel = (mdsHeader_t *)buffer;

	version = LittleLong( pinmodel->version );
	if ( version != MDS_VERSION ) {
		Com_Printf( S_COLOR_YELLOW"R_LoadMDS: %s has wrong version (%i should be %i)\n",
				   mod_name, version, MDS_VERSION );
		return qfalse;
	}

	mod->type = MOD_MDS;
	size = LittleLong( pinmodel->ofsEnd );
	mod->dataSize += size;
	mds = mod->mds = Hunk_Alloc( size, h_low );

	memcpy( mds, buffer, LittleLong( pinmodel->ofsEnd ) );

	LL( mds->ident );
	LL( mds->version );
	LL( mds->numFrames );
	LL( mds->numBones );
	LL( mds->numTags );
	LL( mds->numSurfaces );
	LL( mds->ofsFrames );
	LL( mds->ofsBones );
	LL( mds->ofsTags );
	LL( mds->ofsEnd );
	LL( mds->ofsSurfaces );
	mds->lodBias = LittleFloat( mds->lodBias );
	mds->lodScale = LittleFloat( mds->lodScale );
	LL( mds->torsoParent );

	if ( mds->numFrames < 1 ) {
		Com_Printf( S_COLOR_YELLOW"R_LoadMDS: %s has no frames\n", mod_name );
		return qfalse;
	}

	if ( LittleLong( 1 ) != 1 ) {
		// swap all the frames
		//frameSize = (int)( &((mdsFrame_t *)0)->bones[ mds->numBones ] );
		frameSize = (int) ( sizeof( mdsFrame_t ) - sizeof( mdsBoneFrameCompressed_t ) + mds->numBones * sizeof( mdsBoneFrameCompressed_t ) );
		for ( i = 0 ; i < mds->numFrames ; i++, mdsFrame++ ) {
			mdsFrame = ( mdsFrame_t * )( (byte *)mds + mds->ofsFrames + i * frameSize );
			mdsFrame->radius = LittleFloat( mdsFrame->radius );
			for ( j = 0 ; j < 3 ; j++ ) {
				mdsFrame->bounds[0][j] = LittleFloat( mdsFrame->bounds[0][j] );
				mdsFrame->bounds[1][j] = LittleFloat( mdsFrame->bounds[1][j] );
				mdsFrame->localOrigin[j] = LittleFloat( mdsFrame->localOrigin[j] );
				mdsFrame->parentOffset[j] = LittleFloat( mdsFrame->parentOffset[j] );
			}
			for ( j = 0 ; j < mds->numBones * sizeof( mdsBoneFrameCompressed_t ) / sizeof( short ) ; j++ ) {
				( (short *)mdsFrame->bones )[j] = LittleShort( ( (short *)mdsFrame->bones )[j] );
			}
		}

		// swap all the tags
		tag = ( mdsTag_t * )( (byte *)mds + mds->ofsTags );
		for ( i = 0 ; i < mds->numTags ; i++, tag++ ) {
			LL( tag->boneIndex );
			tag->torsoWeight = LittleFloat( tag->torsoWeight );
		}

		// swap all the bones
		for ( i = 0 ; i < mds->numBones ; i++, bi++ ) {
			bi = ( mdsBoneInfo_t * )( (byte *)mds + mds->ofsBones + i * sizeof( mdsBoneInfo_t ) );
			LL( bi->parent );
			bi->torsoWeight = LittleFloat( bi->torsoWeight );
			bi->parentDist = LittleFloat( bi->parentDist );
			LL( bi->flags );
		}
	}

	// swap all the surfaces
	surf = ( mdsSurface_t * )( (byte *)mds + mds->ofsSurfaces );
	for ( i = 0 ; i < mds->numSurfaces ; i++ ) {
		if ( LittleLong( 1 ) != 1 ) {
			LL( surf->ident );
			LL( surf->shaderIndex );
			LL( surf->minLod );
			LL( surf->ofsHeader );
			LL( surf->ofsCollapseMap );
			LL( surf->numTriangles );
			LL( surf->ofsTriangles );
			LL( surf->numVerts );
			LL( surf->ofsVerts );
			LL( surf->numBoneReferences );
			LL( surf->ofsBoneReferences );
			LL( surf->ofsEnd );
		}

		if ( surf->numVerts > SHADER_MAX_VERTEXES ) {
			Com_Error( ERR_DROP, "R_LoadMDS: %s has more than %i verts on a surface (%i)",
					  mod_name, SHADER_MAX_VERTEXES, surf->numVerts );
		}
		if ( surf->numTriangles * 3 > SHADER_MAX_INDEXES ) {
			Com_Error( ERR_DROP, "R_LoadMDS: %s has more than %i triangles on a surface (%i)",
					  mod_name, SHADER_MAX_INDEXES / 3, surf->numTriangles );
		}

		// register the shaders
		if ( surf->shader[0] ) {
		#ifndef DEDICATED
			surf->shaderIndex = R_LookupShaderIndexFromName(surf->shader, vmType);
		#else
			surf->shaderIndex = 0;
		#endif
		} else {
			surf->shaderIndex = 0;
		}

		if ( LittleLong( 1 ) != 1 ) {
			// swap all the triangles
			tri = ( mdsTriangle_t * )( (byte *)surf + surf->ofsTriangles );
			for ( j = 0 ; j < surf->numTriangles ; j++, tri++ ) {
				LL( tri->indexes[0] );
				LL( tri->indexes[1] );
				LL( tri->indexes[2] );
			}

			// swap all the vertexes
			v = ( mdsVertex_t * )( (byte *)surf + surf->ofsVerts );
			for ( j = 0 ; j < surf->numVerts ; j++ ) {
				v->normal[0] = LittleFloat( v->normal[0] );
				v->normal[1] = LittleFloat( v->normal[1] );
				v->normal[2] = LittleFloat( v->normal[2] );

				v->texCoords[0] = LittleFloat( v->texCoords[0] );
				v->texCoords[1] = LittleFloat( v->texCoords[1] );

				v->numWeights = LittleLong( v->numWeights );

				for ( k = 0 ; k < v->numWeights ; k++ ) {
					v->weights[k].boneIndex = LittleLong( v->weights[k].boneIndex );
					v->weights[k].boneWeight = LittleFloat( v->weights[k].boneWeight );
					v->weights[k].offset[0] = LittleFloat( v->weights[k].offset[0] );
					v->weights[k].offset[1] = LittleFloat( v->weights[k].offset[1] );
					v->weights[k].offset[2] = LittleFloat( v->weights[k].offset[2] );
				}

				// find the fixedParent for this vert (if exists)
				v->fixedParent = -1;
				if ( v->numWeights == 2 ) {
					// find the closest parent
					if ( VectorLength( v->weights[0].offset ) < VectorLength( v->weights[1].offset ) ) {
						v->fixedParent = 0;
					} else {
						v->fixedParent = 1;
					}
					v->fixedDist = VectorLength( v->weights[v->fixedParent].offset );
				}

				v = (mdsVertex_t *)&v->weights[v->numWeights];
			}

			// swap the collapse map
			collapseMap = ( int * )( (byte *)surf + surf->ofsCollapseMap );
			for ( j = 0; j < surf->numVerts; j++, collapseMap++ ) {
				*collapseMap = LittleLong( *collapseMap );
			}

			// swap the bone references
			boneref = ( int * )( ( byte *)surf + surf->ofsBoneReferences );
			for ( j = 0; j < surf->numBoneReferences; j++, boneref++ ) {
				*boneref = LittleLong( *boneref );
			}
		}

		// find the next surface
		surf = ( mdsSurface_t * )( (byte *)surf + surf->ofsEnd );
	}

	return qtrue;
}


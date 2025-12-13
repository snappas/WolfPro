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

// tr_models.c -- model loading and caching

#include "tr_local.h"


#define LL( x ) x = LittleLong( x )



model_t *loadmodel;

extern cvar_t *r_exportCompressedModels;
extern cvar_t *r_buildScript;

/*
** R_GetModelByHandle
*/
model_t *R_GetModelByHandle( qhandle_t index ) {
	model_t     *mod;

	// out of range gets the defualt model
	if ( index < 1 || index >= cl_numModels ) {
		return cl_models[0];
	}

	mod = cl_models[index];

	return mod;
}

//===============================================================================

/*
** R_AllocModel
*/
model_t *R_AllocModel( void ) {
	model_t     *mod;

	if ( cl_numModels == MAX_MOD_KNOWN ) {
		return NULL;
	}

	mod = ri.Hunk_Alloc( sizeof( *cl_models[cl_numModels] ), h_low );
	mod->index = cl_numModels;
	cl_models[cl_numModels] = mod;
	cl_numModels++;

	return mod;
}






// done.
//-------------------------------------------------------------------------------




//=============================================================================



/*
===============
R_ModelInit
===============
*/
void R_ModelInit( void ) {
	model_t     *mod;

	// leave a space for NULL model
	cl_numModels = 0;

	mod = R_AllocModel();
	mod->type = MOD_BAD;

}

int R_LookupShaderIndexFromName(char *name, vmType_t vmType){
	if(vmType == VM_QAGAME){
		return 0;
	}
	shader_t *sh;
	sh = R_FindShader( name, LIGHTMAP_NONE, qtrue );
	if ( sh->defaultShader ) {
		return 0;
	} else {
		return sh->index;
	}
}


/*
================
R_Modellist_f
================
*/

void R_Modellist_f( void ) {
	int i, j;
	model_t *mod;
	int total;
	int lods;
	char *filter = Cmd_Argv(1);

	total = 0;
	for ( i = 1 ; i < cl_numModels; i++ ) {
		mod = cl_models[i];

		if(filter[0] != '\0' && !Com_Filter(filter, mod->name, qfalse)){
			continue;
		}
		
		lods = 1;
		for ( j = 1 ; j < MD3_MAX_LODS ; j++ ) {
			if ( mod->md3[j] && mod->md3[j] != mod->md3[j - 1] ) {
				lods++;
			}
		}
		ri.Printf( PRINT_ALL, "%8i : (%i) %s\n",mod->dataSize, lods, mod->name );
		total += mod->dataSize;
	}
	ri.Printf( PRINT_ALL, "%8i : Total models\n", total );

#if 0       // not working right with new hunk
	if ( tr.world ) {
		ri.Printf( PRINT_ALL, "\n%8i : %s\n", tr.world->dataSize, tr.world->name );
	}
#endif
}


//=============================================================================









/*
====================
R_ModelBounds
====================
*/
void R_ModelBounds( qhandle_t handle, vec3_t mins, vec3_t maxs ) {
	model_t     *model;
	md3Header_t *header;
	md3Frame_t  *md3Frame;

	model = R_GetModelByHandle( handle );

	if ( model->bmodel ) {
		VectorCopy( model->bmodel->bounds[0], mins );
		VectorCopy( model->bmodel->bounds[1], maxs );
		return;
	}

	// Ridah
	if ( model->md3[0] ) {
		header = model->md3[0];

		md3Frame = ( md3Frame_t * )( (byte *)header + header->ofsFrames );

		VectorCopy( md3Frame->bounds[0], mins );
		VectorCopy( md3Frame->bounds[1], maxs );
		return;
	} else if ( model->mdc[0] ) {
		md3Frame = ( md3Frame_t * )( (byte *)model->mdc[0] + model->mdc[0]->ofsFrames );

		VectorCopy( md3Frame->bounds[0], mins );
		VectorCopy( md3Frame->bounds[1], maxs );
		return;
	}

	VectorClear( mins );
	VectorClear( maxs );
	// done.
}




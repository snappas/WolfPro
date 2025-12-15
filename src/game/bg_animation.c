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


//===========================================================================
// bg_animation.c
//
//	Incorporates several elements related to the new flexible animation system.
//
//	This includes scripting elements, support routines for new animation set
//	reference system and other bits and pieces.
//
//===========================================================================

#include "q_shared.h"
#include "bg_public.h"

// JPW NERVE -- added because I need to check single/multiplayer instances and branch accordingly
#ifdef CGAMEDLL
extern vmCvar_t cg_gameType;
#endif
#ifdef GAMEDLL
extern vmCvar_t g_gametype;
#endif

// debug defines, to prevent doing costly string cvar lookups
//#define	DBGANIMS
//#define	DBGANIMEVENTS

// this is used globally within this file to reduce redundant params
static animScriptData_t *globalScriptData = NULL;

#define MAX_ANIM_DEFINES    16

static char *globalFilename;    // to prevent redundant params
static int parseClient;

// these are used globally during script parsing
static int numDefines[NUM_ANIM_CONDITIONS];
static char defineStrings[10000];       // stores the actual strings
static int defineStringsOffset;
static animStringItem_t defineStr[NUM_ANIM_CONDITIONS][MAX_ANIM_DEFINES];
static int defineBits[NUM_ANIM_CONDITIONS][MAX_ANIM_DEFINES][2];

static scriptAnimMoveTypes_t parseMovetype;
static int parseEvent;

animStringItem_t weaponStrings[WP_NUM_WEAPONS];
qboolean weaponStringsInited = qfalse;

animStringItem_t animStateStr[] =
{
	{"RELAXED", -1},
	{"QUERY", -1},
	{"ALERT", -1},
	{"COMBAT", -1},

	{NULL, -1},
};

static animStringItem_t animMoveTypesStr[] =
{
	{"** UNUSED **", -1},
	{"IDLE", -1},
	{"IDLECR", -1},
	{"WALK", -1},
	{"WALKBK", -1},
	{"WALKCR", -1},
	{"WALKCRBK", -1},
	{"RUN", -1},
	{"RUNBK", -1},
	{"SWIM", -1},
	{"SWIMBK", -1},
	{"STRAFERIGHT", -1},
	{"STRAFELEFT", -1},
	{"TURNRIGHT", -1},
	{"TURNLEFT", -1},
	{"CLIMBUP", -1},
	{"CLIMBDOWN", -1},
	{"FALLEN", -1},                  // DHM - Nerve :: dead, before limbo

	{NULL, -1},
};

static animStringItem_t animEventTypesStr[] =
{
	{"PAIN", -1},
	{"DEATH", -1},
	{"FIREWEAPON", -1},
	{"JUMP", -1},
	{"JUMPBK", -1},
	{"LAND", -1},
	{"DROPWEAPON", -1},
	{"RAISEWEAPON", -1},
	{"CLIMBMOUNT", -1},
	{"CLIMBDISMOUNT", -1},
	{"RELOAD", -1},
	{"PICKUPGRENADE", -1},
	{"KICKGRENADE", -1},
	{"QUERY", -1},
	{"INFORM_FRIENDLY_OF_ENEMY", -1},
	{"KICK", -1},
	{"REVIVE", -1},
	{"FIRSTSIGHT", -1},
	{"ROLL", -1},
	{"FLIP", -1},
	{"DIVE", -1},
	{"PRONE_TO_CROUCH", -1},
	{"BULLETIMPACT", -1},
	{"INSPECTSOUND", -1},

	{NULL, -1},
};

animStringItem_t animBodyPartsStr[] =
{
	{"** UNUSED **", -1},
	{"LEGS", -1},
	{"TORSO", -1},
	{"BOTH", -1},

	{NULL, -1},
};

//------------------------------------------------------------
// conditions

static animStringItem_t animConditionPositionsStr[] =
{
	{"** UNUSED **", -1},
	{"BEHIND", -1},
	{"INFRONT", -1},
	{"RIGHT", -1},
	{"LEFT", -1},

	{NULL, -1},
};

static animStringItem_t animConditionMountedStr[] =
{
	{"** UNUSED **", -1},
	{"MG42", -1},

	{NULL, -1},
};

static animStringItem_t animConditionLeaningStr[] =
{
	{"** UNUSED **", -1},
	{"RIGHT", -1},
	{"LEFT", -1},

	{NULL, -1},
};

// !!! NOTE: this must be kept in sync with the tag names in ai_cast_characters.c
static animStringItem_t animConditionImpactPointsStr[] =
{
	{"** UNUSED **", -1},
	{"HEAD", -1},
	{"CHEST", -1},
	{"GUT", -1},
	{"GROIN", -1},
	{"SHOULDER_RIGHT", -1},
	{"SHOULDER_LEFT", -1},
	{"KNEE_RIGHT", -1},
	{"KNEE_LEFT", -1},

	{NULL, -1},
};

// !!! NOTE: this must be kept in sync with the teams in ai_cast.h
static animStringItem_t animEnemyTeamsStr[] =
{
	{"NAZI", -1},
	{"ALLIES", -1},
	{"MONSTER", -1},
	{"SPARE1", -1},
	{"SPARE2", -1},
	{"SPARE3", -1},
	{"SPARE4", -1},
	{"NEUTRAL", -1}
};

static animStringItem_t animHealthLevelStr[] =
{
	{"1", -1},
	{"2", -1},
	{"3", -1},
};

typedef enum
{
	ANIM_CONDTYPE_BITFLAGS,
	ANIM_CONDTYPE_VALUE,

	NUM_ANIM_CONDTYPES
} animScriptConditionTypes_t;

typedef struct
{
	animScriptConditionTypes_t type;
	animStringItem_t            *values;
} animConditionTable_t;

static animStringItem_t animConditionsStr[] =
{
	{"WEAPONS", -1},
	{"ENEMY_POSITION", -1},
	{"ENEMY_WEAPON", -1},
	{"UNDERWATER", -1},
	{"MOUNTED", -1},
	{"MOVETYPE", -1},
	{"UNDERHAND", -1},
	{"LEANING", -1},
	{"IMPACT_POINT", -1},
	{"CROUCHING", -1},
	{"STUNNED", -1},
	{"FIRING", -1},
	{"SHORT_REACTION", -1},
	{"ENEMY_TEAM", -1},
	{"PARACHUTE", -1},
	{"CHARGING", -1},
	{"SECONDLIFE", -1},
	{"HEALTH_LEVEL", -1},

	{NULL, -1},
};

static animConditionTable_t animConditionsTable[NUM_ANIM_CONDITIONS] =
{
	{ANIM_CONDTYPE_BITFLAGS,        weaponStrings},
	{ANIM_CONDTYPE_BITFLAGS,        animConditionPositionsStr},
	{ANIM_CONDTYPE_BITFLAGS,        weaponStrings},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           animConditionMountedStr},
	{ANIM_CONDTYPE_BITFLAGS,        animMoveTypesStr},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           animConditionLeaningStr},
	{ANIM_CONDTYPE_VALUE,           animConditionImpactPointsStr},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           animEnemyTeamsStr},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           NULL},
	{ANIM_CONDTYPE_VALUE,           animHealthLevelStr},
};

//------------------------------------------------------------

/*
================
return a hash value for the given string
================
*/
static long BG_StringHashValue( const char *fname ) {
	int i;
	long hash;
	char letter;

	hash = 0;
	i = 0;
	while ( fname[i] != '\0' ) {
		letter = tolower( fname[i] );
		hash += (long)( letter ) * ( i + 119 );
		i++;
	}
	if ( hash == -1 ) {
		hash = 0;   // never return -1
	}
	return hash;
}

/*
=================
BG_AnimParseError
=================
*/
void QDECL BG_AnimParseError( const char *msg, ... ) {
	va_list argptr;
	char text[1024];

	va_start( argptr, msg );
	Q_vsnprintf( text, sizeof( text ), msg, argptr );
	va_end( argptr );

	if ( globalFilename ) {
		Com_Error( ERR_DROP,  "%s: (%s, line %i)", text, globalFilename, COM_GetCurrentParseLine() + 1 );
	} else {
		Com_Error( ERR_DROP,  "%s", text );
	}
}

/*
=================
BG_ModelInfoForClient
=================
*/
animModelInfo_t *BG_ModelInfoForClient( int client ) {
	if ( !globalScriptData ) {
		BG_AnimParseError( "BG_ModelInfoForClient: NULL globalScriptData" );
	}
	//
	if ( !globalScriptData->clientModels[client] ) {
		BG_AnimParseError( "BG_ModelInfoForClient: client %i has no modelinfo", client );
	}
	//
	return &globalScriptData->modelInfo[globalScriptData->clientModels[client] - 1];
}

/*
=================
BG_ModelInfoForModelname
=================
*/
animModelInfo_t *BG_ModelInfoForModelname( char *modelname ) {
	int i;
	animModelInfo_t *modelInfo;
	//
	if ( !globalScriptData ) {
		BG_AnimParseError( "BG_ModelInfoForModelname: NULL globalScriptData" );
	}
	//
	for ( i = 0, modelInfo = globalScriptData->modelInfo; i < MAX_ANIMSCRIPT_MODELS; i++, modelInfo++ ) {
		if ( !modelInfo->modelname[0] ) {
			continue;
		}
		if ( !Q_stricmp( modelname, modelInfo->modelname ) ) {
			return modelInfo;
		}
	}
	//
	return NULL;
}

/*
=================
BG_AnimationIndexForString
=================
*/
int BG_AnimationIndexForString( char *string, int client ) {
	int i, hash;
	animation_t *anim;
	animModelInfo_t *modelInfo;

	modelInfo = BG_ModelInfoForClient( client );

	hash = BG_StringHashValue( string );

	for ( i = 0, anim = modelInfo->animations; i < modelInfo->numAnimations; i++, anim++ ) {
		if ( ( hash == anim->nameHash ) && !Q_stricmp( string, anim->name ) ) {
			// found a match
			return i;
		}
	}
	// no match found
	BG_AnimParseError( "BG_AnimationIndexForString: unknown index '%s' for model '%s'", string, modelInfo->modelname );
	return -1;  // shutup compiler
}

/*
=================
BG_AnimationForString
=================
*/
animation_t *BG_AnimationForString( char *string, animModelInfo_t *modelInfo ) {
	int i, hash;
	animation_t *anim;

	hash = BG_StringHashValue( string );

	for ( i = 0, anim = modelInfo->animations; i < modelInfo->numAnimations; i++, anim++ ) {
		if ( ( hash == anim->nameHash ) && !Q_stricmp( string, anim->name ) ) {
			// found a match
			return anim;
		}
	}
	// no match found
	Com_Error( ERR_DROP, "BG_AnimationForString: unknown animation '%s' for model '%s'", string, modelInfo->modelname );
	return NULL;    // shutup compiler
}

/*
=================
BG_IndexForString

  errors out if no match found
=================
*/
int BG_IndexForString( char *token, animStringItem_t *strings, qboolean allowFail ) {
	int i, hash;
	animStringItem_t *strav;

	hash = BG_StringHashValue( token );

	for ( i = 0, strav = strings; strav->string; strav++, i++ ) {
		if ( strav->hash == -1 ) {
			strav->hash = BG_StringHashValue( strav->string );
		}
		if ( ( hash == strav->hash ) && !Q_stricmp( token, strav->string ) ) {
			// found a match
			return i;
		}
	}
	// no match found
	if ( !allowFail ) {
		BG_AnimParseError( "BG_IndexForString: unknown token '%s'", token );
	}
	//
	return -1;
}

/*
===============
BG_CopyStringIntoBuffer
===============
*/
char *BG_CopyStringIntoBuffer( char *string, char *buffer, int bufSize, int *offset ) {
	char *pch;

	// check for overloaded buffer
	if ( *offset + strlen( string ) + 1 >= bufSize ) {
		BG_AnimParseError( "BG_CopyStringIntoBuffer: out of buffer space" );
	}

	pch = &buffer[*offset];

	// safe to do a strcpy since we've already checked for overrun
	strcpy( pch, string );

	// move the offset along
	*offset += strlen( string ) + 1;

	return pch;
}

/*
============
BG_InitWeaponStrings

  Builds the list of weapon names from the item list. This is done here rather
  than hardcoded to ease the process of modifying the weapons.
============
*/
void BG_InitWeaponStrings( void ) {
	int i;
	gitem_t *item;

	memset( weaponStrings, 0, sizeof( weaponStrings ) );

	for ( i = 0; i < WP_NUM_WEAPONS; i++ ) {
		// find this weapon in the itemslist, and extract the name
		for ( item = bg_itemlist + 1; item->classname; item++ ) {
			if ( item->giType == IT_WEAPON && item->giTag == i ) {
				// found a match
				weaponStrings[i].string = item->pickup_name;
				weaponStrings[i].hash = BG_StringHashValue( weaponStrings[i].string );
				break;
			}
		}

		if ( !item->classname ) {
			weaponStrings[i].string = "(unknown)";
			weaponStrings[i].hash = BG_StringHashValue( weaponStrings[i].string );
		}
	}

	weaponStringsInited = qtrue;
}

/*
============
BG_AnimParseAnimConfig

  returns qfalse if error, qtrue otherwise
============
*/
qboolean BG_AnimParseAnimConfig( animModelInfo_t *animModelInfo, const char *filename, const char *input ) {
	char    *text_p, *token;
	animation_t *animations;
	headAnimation_t *headAnims;
	int i, fps, skip = -1;

	if ( !weaponStringsInited ) {
		BG_InitWeaponStrings();
	}

	globalFilename = (char *)filename;

	animations = animModelInfo->animations;
	animModelInfo->numAnimations = 0;
	headAnims = animModelInfo->headAnims;

	text_p = (char *)input;
	COM_BeginParseSession( "BG_AnimParseAnimConfig" );

	animModelInfo->footsteps = FOOTSTEP_NORMAL;
	VectorClear( animModelInfo->headOffset );
	animModelInfo->gender = GENDER_MALE;
	animModelInfo->isSkeletal = qfalse;
	animModelInfo->version = 0;

	// read optional parameters
	while ( 1 ) {
		token = COM_Parse( &text_p );
		if ( !token ) {
			break;
		}
		if ( !Q_stricmp( token, "footsteps" ) ) {
			token = COM_Parse( &text_p );
			if ( !token ) {
				break;
			}
			if ( !Q_stricmp( token, "default" ) || !Q_stricmp( token, "normal" ) ) {
				animModelInfo->footsteps = FOOTSTEP_NORMAL;
			} else if ( !Q_stricmp( token, "boot" ) ) {
				animModelInfo->footsteps = FOOTSTEP_BOOT;
			} else if ( !Q_stricmp( token, "flesh" ) ) {
				animModelInfo->footsteps = FOOTSTEP_FLESH;
			} else if ( !Q_stricmp( token, "mech" ) ) {
				animModelInfo->footsteps = FOOTSTEP_MECH;
			} else if ( !Q_stricmp( token, "energy" ) ) {
				animModelInfo->footsteps = FOOTSTEP_ENERGY;
			} else {
				BG_AnimParseError( "Bad footsteps parm '%s'\n", token );
			}
			continue;
		} else if ( !Q_stricmp( token, "headoffset" ) ) {
			for ( i = 0 ; i < 3 ; i++ ) {
				token = COM_Parse( &text_p );
				if ( !token ) {
					break;
				}
				animModelInfo->headOffset[i] = atof( token );
			}
			continue;
		} else if ( !Q_stricmp( token, "sex" ) ) {
			token = COM_Parse( &text_p );
			if ( !token ) {
				break;
			}
			if ( token[0] == 'f' || token[0] == 'F' ) {
				animModelInfo->gender = GENDER_FEMALE;
			} else if ( token[0] == 'n' || token[0] == 'N' ) {
				animModelInfo->gender = GENDER_NEUTER;
			} else {
				animModelInfo->gender = GENDER_MALE;
			}
			continue;
		} else if ( !Q_stricmp( token, "version" ) ) {
			token = COM_Parse( &text_p );
			if ( !token ) {
				break;
			}
			animModelInfo->version = atoi( token );
			continue;
		} else if ( !Q_stricmp( token, "skeletal" ) ) {
			animModelInfo->isSkeletal = qtrue;
			continue;
		}

		if ( animModelInfo->version < 2 ) {
			// if it is a number, start parsing animations
			if ( token[0] >= '0' && token[0] <= '9' ) {
				text_p -= strlen( token );    // unget the token
				break;
			}
		}

		// STARTANIMS marks the start of the animations
		if ( !Q_stricmp( token, "STARTANIMS" ) ) {
			break;
		}
		BG_AnimParseError( "unknown token '%s'", token );
	}

	// read information for each frame
	for ( i = 0 ; ( animModelInfo->version > 1 ) || ( i < MAX_ANIMATIONS ) ; i++ ) {

		token = COM_Parse( &text_p );
		if ( !token ) {
			break;
		}

		if ( animModelInfo->version > 1 ) {   // includes animation names at start of each line

			if ( !Q_stricmp( token, "ENDANIMS" ) ) {   // end of animations
				break;
			}

			Q_strncpyz( animations[i].name, token, sizeof( animations[i].name ) );
			// convert to all lower case
			Q_strlwr( animations[i].name );
			//
			token = COM_ParseExt( &text_p, qfalse );
			if ( !token || !token[0] ) {
				BG_AnimParseError( "end of file without ENDANIMS" );
			}
		} else {
			// just set it to the equivalent animStrings[]
			Q_strncpyz( animations[i].name, animStrings[i], sizeof( animations[i].name ) );
			// convert to all lower case
			Q_strlwr( animations[i].name );
		}

		animations[i].firstFrame = atoi( token );

		if ( !animModelInfo->isSkeletal ) { // skeletal models dont require adjustment

			// leg only frames are adjusted to not count the upper body only frames
			if ( i == LEGS_WALKCR ) {
				skip = animations[LEGS_WALKCR].firstFrame - animations[TORSO_GESTURE].firstFrame;
			}
			if ( i >= LEGS_WALKCR ) {
				animations[i].firstFrame -= skip;
			}

		}

		token = COM_ParseExt( &text_p, qfalse );
		if ( !token || !token[0] ) {
			BG_AnimParseError( "end of file without ENDANIMS" );
		}
		animations[i].numFrames = atoi( token );

		token = COM_ParseExt( &text_p, qfalse );
		if ( !token || !token[0] ) {
			BG_AnimParseError( "end of file without ENDANIMS: line %i" );
		}
		animations[i].loopFrames = atoi( token );

		token = COM_ParseExt( &text_p, qfalse );
		if ( !token || !token[0] ) {
			BG_AnimParseError( "end of file without ENDANIMS: line %i" );
		}
		fps = atof( token );
		if ( fps == 0 ) {
			fps = 1;
		}
		animations[i].frameLerp = 1000 / fps;
		animations[i].initialLerp = 1000 / fps;

		// movespeed
		token = COM_ParseExt( &text_p, qfalse );
		if ( !token || !token[0] ) {
			BG_AnimParseError( "end of file without ENDANIMS" );
		}
		animations[i].moveSpeed = atoi( token );

		// animation blending
		token = COM_ParseExt( &text_p, qfalse );    // must be on same line
		if ( !token ) {
			animations[i].animBlend = 0;
		} else {
			animations[i].animBlend = atoi( token );
		}

		// calculate the duration
		animations[i].duration = animations[i].initialLerp
								 + animations[i].frameLerp * animations[i].numFrames
								 + animations[i].animBlend;

		// get the nameHash
		animations[i].nameHash = BG_StringHashValue( animations[i].name );

		if ( !Q_strncmp( animations[i].name, "climb", 5 ) ) {
			animations[i].flags |= ANIMFL_LADDERANIM;
		}
		if ( strstr( animations[i].name, "firing" ) ) {
			animations[i].flags |= ANIMFL_FIRINGANIM;
			animations[i].initialLerp = 40;
		}

	}

	animModelInfo->numAnimations = i;

	if ( animModelInfo->version < 2 && i != MAX_ANIMATIONS ) {
		BG_AnimParseError( "Incorrect number of animations" );
		return qfalse;
	}

	// check for head anims
	token = COM_Parse( &text_p );
	if ( token && token[0] ) {
		if ( animModelInfo->version < 2 || !Q_stricmp( token, "HEADFRAMES" ) ) {

			// read information for each head frame
			for ( i = 0 ; i < MAX_HEAD_ANIMS ; i++ ) {

				token = COM_Parse( &text_p );
				if ( !token || !token[0] ) {
					break;
				}

				if ( animModelInfo->version > 1 ) {   // includes animation names at start of each line
					// just throw this information away, not required for head
					token = COM_ParseExt( &text_p, qfalse );
					if ( !token || !token[0] ) {
						break;
					}
				}

				if ( !i ) {
					skip = atoi( token );
				}

				headAnims[i].firstFrame = atoi( token );
				// modify according to last frame of the main animations, since the head is totally seperate
				headAnims[i].firstFrame -= animations[MAX_ANIMATIONS - 1].firstFrame + animations[MAX_ANIMATIONS - 1].numFrames + skip;

				token = COM_ParseExt( &text_p, qfalse );
				if ( !token || !token[0] ) {
					break;
				}
				headAnims[i].numFrames = atoi( token );

				// skip the movespeed
				token = COM_ParseExt( &text_p, qfalse );
			}

			animModelInfo->numHeadAnims = i;

			if ( i != MAX_HEAD_ANIMS ) {
				BG_AnimParseError( "Incorrect number of head frames" );
				return qfalse;
			}

		}
	}

	return qtrue;
}

/*
=================
BG_ParseConditionBits

  convert the string into a single int containing bit flags, stopping at a ',' or end of line
=================
*/
void BG_ParseConditionBits( char **text_pp, animStringItem_t *stringTable, int condIndex, int result[2] ) {
	qboolean endFlag = qfalse;
	int indexFound;
	int /*indexBits,*/ tempBits[2];
	char currentString[MAX_QPATH];
	qboolean minus = qfalse;
	char *token;

	//indexBits = 0;
	currentString[0] = '\0';
	memset( result, 0, sizeof( int ) * 2 );
	memset( tempBits, 0, sizeof( int ) * 2 );

	while ( !endFlag ) {

		token = COM_ParseExt( text_pp, qfalse );
		if ( !token || !token[0] ) {
			COM_RestoreParseSession( text_pp ); // go back to the previous token
			endFlag = qtrue;    // done parsing indexes
			if ( !strlen( currentString ) ) {
				break;
			}
		}

		if ( !Q_stricmp( token, "," ) ) {
			endFlag = qtrue;    // end of indexes
		}

		if ( !Q_stricmp( token, "none" ) ) { // first bit is always the "unused" bit
			COM_BitSet( result, 0 );
			continue;
		}

		if ( !Q_stricmp( token, "none," ) ) {    // first bit is always the "unused" bit
			COM_BitSet( result, 0 );
			endFlag = qtrue;    // end of indexes
			continue;
		}

		if ( !Q_stricmp( token, "NOT" ) ) {
			token = "MINUS"; // NOT is equivalent to MINUS
		}

		if ( !endFlag && Q_stricmp( token, "AND" ) && Q_stricmp( token, "MINUS" ) ) { // must be a index
			// check for a comma (end of indexes)
			if ( token[strlen( token ) - 1] == ',' ) {
				endFlag = qtrue;
				token[strlen( token ) - 1] = '\0';
			}
			// append this to the currentString
			if ( strlen( currentString ) ) {
				Q_strcat( currentString, sizeof( currentString ), " " );
			}
			Q_strcat( currentString, sizeof( currentString ), token );
		}

		if ( !Q_stricmp( token, "AND" ) || !Q_stricmp( token, "MINUS" ) || endFlag ) {
			// process the currentString
			if ( !strlen( currentString ) ) {
				if ( endFlag ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected end of condition" );
				} else {
					// check for minus indexes to follow
					if ( !Q_stricmp( token, "MINUS" ) ) {
						minus = qtrue;
						continue;
					}
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );
				}
			}
			if ( !Q_stricmp( currentString, "all" ) ) {
				tempBits[0] = ~0x0;
				tempBits[1] = ~0x0;
			} else {
				// first check this string with our defines
				indexFound = BG_IndexForString( currentString, defineStr[condIndex], qtrue );
				if ( indexFound >= 0 ) {
					// we have precalculated the bitflags for the defines
					tempBits[0] = defineBits[condIndex][indexFound][0];
					tempBits[1] = defineBits[condIndex][indexFound][1];
				} else {
					// convert the string into an index
					indexFound = BG_IndexForString( currentString, stringTable, qfalse );
					// convert the index into a bitflag
					COM_BitSet( tempBits, indexFound );
				}
			}
			// perform operation
			if ( minus ) {    // subtract
				result[0] &= ~tempBits[0];
				result[1] &= ~tempBits[1];
			} else {        // add
				result[0] |= tempBits[0];
				result[1] |= tempBits[1];
			}
			// clear the currentString
			currentString[0] = '\0';
			// check for minus indexes to follow
			if ( !Q_stricmp( token, "MINUS" ) ) {
				minus = qtrue;
			}
		}

	}
}

/*
=================
BG_ParseConditions

  returns qtrue if everything went ok, error drops otherwise
=================
*/
qboolean BG_ParseConditions( char **text_pp, animScriptItem_t *scriptItem ) {
	int conditionIndex, conditionValue[2];
	char    *token;

	conditionValue[0] = 0;
	conditionValue[1] = 0;

	while ( 1 ) {

		token = COM_ParseExt( text_pp, qfalse );
		if ( !token || !token[0] ) {
			break;
		}

		// special case, "default" has no conditions
		if ( !Q_stricmp( token, "default" ) ) {
			return qtrue;
		}

		conditionIndex = BG_IndexForString( token, animConditionsStr, qfalse );

		switch ( animConditionsTable[conditionIndex].type ) {
		case ANIM_CONDTYPE_BITFLAGS:
			BG_ParseConditionBits( text_pp, animConditionsTable[conditionIndex].values, conditionIndex, conditionValue );
			break;
		case ANIM_CONDTYPE_VALUE:
			if ( animConditionsTable[conditionIndex].values ) {
				token = COM_ParseExt( text_pp, qfalse );
				if ( !token || !token[0] ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected condition value, found end of line" );  // RF modification
				}
				// check for a comma (condition divider)
				if ( token[strlen( token ) - 1] == ',' ) {
					token[strlen( token ) - 1] = '\0';
				}
				conditionValue[0] = BG_IndexForString( token, animConditionsTable[conditionIndex].values, qfalse );
			} else {
				conditionValue[0] = 1;  // not used, just check for a positive condition
			}
			break;
		default: // TTimo gcc: NUM_ANIM_CONDTYPES not handled in switch
			break;
		}

		// now append this condition to the item
		scriptItem->conditions[scriptItem->numConditions].index = conditionIndex;
		scriptItem->conditions[scriptItem->numConditions].value[0] = conditionValue[0];
		scriptItem->conditions[scriptItem->numConditions].value[1] = conditionValue[1];
		scriptItem->numConditions++;
	}

	if ( scriptItem->numConditions == 0 ) {
		BG_AnimParseError( "BG_ParseConditions: no conditions found" );  // RF mod
	}

	return qtrue;
}

/*
=================
BG_ParseCommands
=================
*/
void BG_ParseCommands( char **input, animScriptItem_t *scriptItem, animModelInfo_t *modelInfo, animScriptData_t *scriptData ) {
	char    *token;
	// TTimo gcc: might be used uninitialized
	animScriptCommand_t *command = NULL;
	int partIndex = 0;

	while ( 1 ) {

		// parse the body part
		token = COM_ParseExt( input, ( partIndex < 1 ) );
		if ( !token || !token[0] ) {
			break;
		}
		if ( !Q_stricmp( token, "}" ) ) {
			// unget the bracket and get out of here
			*input -= strlen( token );
			break;
		}

		// new command?
		if ( partIndex == 0 ) {
			// have we exceeded the maximum number of commands?
			if ( scriptItem->numCommands >= MAX_ANIMSCRIPT_ANIMCOMMANDS ) {
				BG_AnimParseError( "BG_ParseCommands: exceeded maximum number of animations (%i)", MAX_ANIMSCRIPT_ANIMCOMMANDS );
			}
			command = &scriptItem->commands[scriptItem->numCommands++];
			memset( command, 0, sizeof( animScriptCommand_t ) );
		}

		command->bodyPart[partIndex] = BG_IndexForString( token, animBodyPartsStr, qtrue );
		if ( command->bodyPart[partIndex] > 0 ) {
			// parse the animation
			token = COM_ParseExt( input, qfalse );
			if ( !token || !token[0] ) {
				BG_AnimParseError( "BG_ParseCommands: expected animation" );
			}
			command->animIndex[partIndex] = BG_AnimationIndexForString( token, parseClient );
			command->animDuration[partIndex] = modelInfo->animations[command->animIndex[partIndex]].duration;
			// if this is a locomotion, set the movetype of the animation so we can reverse engineer the movetype from the animation, on the client
			if ( parseMovetype != ANIM_MT_UNUSED && command->bodyPart[partIndex] != ANIM_BP_TORSO ) {
				modelInfo->animations[command->animIndex[partIndex]].movetype |= ( 1 << parseMovetype );
			}
			// if this is a fireweapon event, then this is a firing animation
			if ( parseEvent == ANIM_ET_FIREWEAPON ) {
				modelInfo->animations[command->animIndex[partIndex]].flags |= ANIMFL_FIRINGANIM;
				modelInfo->animations[command->animIndex[partIndex]].initialLerp = 40;
			}
			// check for a duration for this animation instance
			token = COM_ParseExt( input, qfalse );
			if ( token && token[0] ) {
				if ( !Q_stricmp( token, "duration" ) ) {
					// read the duration
					token = COM_ParseExt( input, qfalse );
					if ( !token || !token[0] ) {
						BG_AnimParseError( "BG_ParseCommands: expected duration value" );
					}
					command->animDuration[partIndex] = atoi( token );
				} else {    // unget the token
					COM_RestoreParseSession( input );
				}
			} else {
				COM_RestoreParseSession( input );
			}

			if ( command->bodyPart[partIndex] != ANIM_BP_BOTH && partIndex++ < 1 ) {
				continue;   // allow parsing of another bodypart
			}
		} else {
			// unget the token
			*input -= strlen( token );
		}

		// parse optional parameters (sounds, etc)
		while ( 1 ) {
			token = COM_ParseExt( input, qfalse );
			if ( !token || !token[0] ) {
				break;
			}

			if ( !Q_stricmp( token, "sound" ) ) {

				token = COM_ParseExt( input, qfalse );
				if ( !token || !token[0] ) {
					BG_AnimParseError( "BG_ParseCommands: expected sound" );
				}
				// NOTE: only sound script are supported at this stage
				if ( strstr( token, ".wav" ) ) {
					BG_AnimParseError( "BG_ParseCommands: wav files not supported, only sound scripts" );    // RF mod
				}
				command->soundIndex = globalScriptData->soundIndex( token );

			} else {
				// unknown??
				BG_AnimParseError( "BG_ParseCommands: unknown parameter '%s'", token );
			}
		}

		partIndex = 0;
	}
}

/*
=================
BG_AnimParseAnimScript

  Parse the animation script for this model, converting it into run-time structures
=================
*/

typedef enum
{
	PARSEMODE_DEFINES,
	PARSEMODE_ANIMATION,
	PARSEMODE_CANNED_ANIMATIONS,
	PARSEMODE_STATECHANGES,
	PARSEMODE_EVENTS
} animScriptParseMode_t;

static animStringItem_t animParseModesStr[] =
{
	{"defines", -1},
	{"animations", -1},
	{"canned_animations", -1},
	{"statechanges", -1},
	{"events", -1},

	{NULL, -1},
};

void BG_AnimParseAnimScript( animModelInfo_t *modelInfo, animScriptData_t *scriptData, int client, char *filename, char *input ) {
	#define MAX_INDENT_LEVELS   3

	char    *text_p, *token;
	animScriptParseMode_t parseMode;
	animScript_t        *currentScript;
	animScriptItem_t tempScriptItem;
	// TTimo gcc: might be used unitialized
	animScriptItem_t *currentScriptItem = NULL;
	int indexes[MAX_INDENT_LEVELS], indentLevel, oldState, newParseMode;
	int i, defineType;

	// the scriptData passed into here must be the one this binary is using
	globalScriptData = scriptData;

	// current client being parsed
	parseClient = client;

	// start at the defines
	parseMode = PARSEMODE_DEFINES;

	// record which modelInfo this client is using
	scriptData->clientModels[client] = 1 + (int)( modelInfo - scriptData->modelInfo );

	// init the global defines
	globalFilename = filename;
	memset( defineStr, 0, sizeof( defineStr ) );
	memset( defineStrings, 0, sizeof( defineStrings ) );
	memset( numDefines, 0, sizeof( numDefines ) );
	defineStringsOffset = 0;

	for ( i = 0; i < MAX_INDENT_LEVELS; i++ )
		indexes[i] = -1;
	indentLevel = 0;
	currentScript = NULL;

	text_p = input;
	COM_BeginParseSession( "BG_AnimParseAnimScript" );

	// read in the weapon defines
	while ( 1 ) {

		token = COM_Parse( &text_p );
		if ( !token || !token[0] ) {
			if ( indentLevel ) {
				BG_AnimParseError( "BG_AnimParseAnimScript: unexpected end of file: %s" );
			}
			break;
		}

		// check for a new section
		newParseMode = BG_IndexForString( token, animParseModesStr, qtrue );
		if ( newParseMode >= 0 ) {
			if ( indentLevel ) {
				BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
			}

			parseMode = newParseMode;
			parseMovetype = ANIM_MT_UNUSED;
			parseEvent = -1;
			continue;
		}

		switch ( parseMode ) {

		case PARSEMODE_DEFINES:

			if ( !Q_stricmp( token, "set" ) ) {

				// read in the define type
				token = COM_ParseExt( &text_p, qfalse );
				if ( !token || !token[0] ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected condition type string" );   // RF mod
				}
				defineType = BG_IndexForString( token, animConditionsStr, qfalse );

				// read in the define
				token = COM_ParseExt( &text_p, qfalse );
				if ( !token || !token[0] ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected condition define string" ); // RF mod
				}

				// copy the define to the strings list
				defineStr[defineType][numDefines[defineType]].string = BG_CopyStringIntoBuffer( token, defineStrings, sizeof( defineStrings ), &defineStringsOffset );
				defineStr[defineType][numDefines[defineType]].hash = BG_StringHashValue( defineStr[defineType][numDefines[defineType]].string );
				// expecting an =
				token = COM_ParseExt( &text_p, qfalse );
				if ( !token ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected '=', found end of line" );  // RF mod
				}
				if ( Q_stricmp( token, "=" ) ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected '=', found '%s'", token );  // RF mod
				}

				// parse the bits
				BG_ParseConditionBits( &text_p, animConditionsTable[defineType].values, defineType, defineBits[defineType][numDefines[defineType]] );
				numDefines[defineType]++;

				// copy the weapon defines over to the enemy_weapon defines
				memcpy( &defineStr[ANIM_COND_ENEMY_WEAPON][0], &defineStr[ANIM_COND_WEAPON][0], sizeof( animStringItem_t ) * MAX_ANIM_DEFINES );
				memcpy( &defineBits[ANIM_COND_ENEMY_WEAPON][0], &defineBits[ANIM_COND_WEAPON][0], sizeof( int ) * 2 * MAX_ANIM_DEFINES );
				numDefines[ANIM_COND_ENEMY_WEAPON] = numDefines[ANIM_COND_WEAPON];

			}

			break;

		case PARSEMODE_ANIMATION:
		case PARSEMODE_CANNED_ANIMATIONS:

			if ( !Q_stricmp( token, "{" ) ) {

				// about to increment indent level, check that we have enough information to do this
				if ( indentLevel >= MAX_INDENT_LEVELS ) { // too many indentations
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
				}
				if ( indexes[indentLevel] < 0 ) {     // we havent found out what this new group is yet
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
				}
				//
				indentLevel++;

			} else if ( !Q_stricmp( token, "}" ) ) {

				// reduce the indentLevel
				indentLevel--;
				if ( indentLevel < 0 ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
				}
				if ( indentLevel == 1 ) {
					currentScript = NULL;
				}
				// make sure we read a new index before next indent
				indexes[indentLevel] = -1;

			} else if ( indentLevel == 0 && ( indexes[indentLevel] < 0 ) ) {

				if ( Q_stricmp( token, "state" ) ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected 'state'" ); // RF mod
				}

				// read in the state type
				token = COM_ParseExt( &text_p, qfalse );
				if ( !token ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected state type" );  // RF mod
				}
				indexes[indentLevel] = BG_IndexForString( token, animStateStr, qfalse );

//----(SA) // RF mod
				// check for the open bracket
				token = COM_ParseExt( &text_p, qtrue );
				if ( !token || Q_stricmp( token, "{" ) ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: expected '{'" );
				}
				indentLevel++;
//----(SA) // RF mod
			} else if ( ( indentLevel == 1 ) && ( indexes[indentLevel] < 0 ) ) {

				// we are expecting a movement type
				indexes[indentLevel] = BG_IndexForString( token, animMoveTypesStr, qfalse );
				if ( parseMode == PARSEMODE_ANIMATION ) {
					currentScript = &modelInfo->scriptAnims[indexes[0]][indexes[1]];
					parseMovetype = indexes[1];
				} else if ( parseMode == PARSEMODE_CANNED_ANIMATIONS ) {
					currentScript = &modelInfo->scriptCannedAnims[indexes[0]][indexes[1]];
				}
				memset( currentScript, 0, sizeof( *currentScript ) );

			} else if ( ( indentLevel == 2 ) && ( indexes[indentLevel] < 0 ) ) {

				// we are expecting a condition specifier
				// move the text_p backwards so we can read in the last token again
				text_p -= strlen( token );
				// sanity check that
				if ( Q_strncmp( text_p, token, strlen( token ) ) ) {
					// this should never happen, just here to check that this operation is correct before code goes live
					BG_AnimParseError( "BG_AnimParseAnimScript: internal error" );
				}
				//
				memset( &tempScriptItem, 0, sizeof( tempScriptItem ) );
				indexes[indentLevel] = BG_ParseConditions( &text_p, &tempScriptItem );
				// do we have enough room in this script for another item?
				if ( currentScript->numItems >= MAX_ANIMSCRIPT_ITEMS ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: exceeded maximum items per script (%i)", MAX_ANIMSCRIPT_ITEMS ); // RF mod
				}
				// are there enough items left in the global list?
				if ( modelInfo->numScriptItems >= MAX_ANIMSCRIPT_ITEMS_PER_MODEL ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: exceeded maximum global items (%i)", MAX_ANIMSCRIPT_ITEMS_PER_MODEL );   // RF mod
				}
				// it was parsed ok, so grab an item from the global list to use
				currentScript->items[currentScript->numItems] = &modelInfo->scriptItems[ modelInfo->numScriptItems++ ];
				currentScriptItem = currentScript->items[currentScript->numItems];
				currentScript->numItems++;
				// copy the data across from the temp script item
				*currentScriptItem = tempScriptItem;

			} else if ( indentLevel == 3 ) {

				// we are reading the commands, so parse this line as if it were a command
				// move the text_p backwards so we can read in the last token again
				text_p -= strlen( token );
				// sanity check that
				if ( Q_strncmp( text_p, token, strlen( token ) ) ) {
					// this should never happen, just here to check that this operation is correct before code goes live
					BG_AnimParseError( "BG_AnimParseAnimScript: internal error" );
				}
				//
				BG_ParseCommands( &text_p, currentScriptItem, modelInfo, scriptData );

			} else {

				// huh ??
				BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod

			}

			break;

		case PARSEMODE_STATECHANGES:
		case PARSEMODE_EVENTS:

			if ( !Q_stricmp( token, "{" ) ) {

				// about to increment indent level, check that we have enough information to do this
				if ( indentLevel >= MAX_INDENT_LEVELS ) { // too many indentations
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
				}
				if ( indexes[indentLevel] < 0 ) {     // we havent found out what this new group is yet
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
				}
				//
				indentLevel++;

			} else if ( !Q_stricmp( token, "}" ) ) {

				// reduce the indentLevel
				indentLevel--;
				if ( indentLevel < 0 ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod
				}
				if ( indentLevel == 0 ) {
					currentScript = NULL;
				}
				// make sure we read a new index before next indent
				indexes[indentLevel] = -1;

			} else if ( indentLevel == 0 && ( indexes[indentLevel] < 0 ) ) {

				if ( parseMode == PARSEMODE_STATECHANGES ) {

					if ( Q_stricmp( token, "statechange" ) ) {
						BG_AnimParseError( "BG_AnimParseAnimScript: expected 'statechange', got '%s'", token );  // RF mod
					}

					// read in the old state type
					token = COM_ParseExt( &text_p, qfalse );
					if ( !token ) {
						BG_AnimParseError( "BG_AnimParseAnimScript: expected <state type>" );    // RF mod
					}
					oldState = BG_IndexForString( token, animStateStr, qfalse );

					// read in the new state type
					token = COM_ParseExt( &text_p, qfalse );
					if ( !token ) {
						BG_AnimParseError( "BG_AnimParseAnimScript: expected <state type>" );    // RF mod
					}
					indexes[indentLevel] = BG_IndexForString( token, animStateStr, qfalse );

					currentScript = &modelInfo->scriptStateChange[oldState][indexes[indentLevel]];

//----(SA)		// RF mod
					// check for the open bracket
					token = COM_ParseExt( &text_p, qtrue );
					if ( !token || Q_stricmp( token, "{" ) ) {
						BG_AnimParseError( "BG_AnimParseAnimScript: expected '{'" );
					}
					indentLevel++;
//----(SA)		// RF mod
				} else {

					// read in the event type
					indexes[indentLevel] = BG_IndexForString( token, animEventTypesStr, qfalse );
					currentScript = &modelInfo->scriptEvents[indexes[0]];

					parseEvent = indexes[indentLevel];

				}

				memset( currentScript, 0, sizeof( *currentScript ) );

			} else if ( ( indentLevel == 1 ) && ( indexes[indentLevel] < 0 ) ) {

				// we are expecting a condition specifier
				// move the text_p backwards so we can read in the last token again
				text_p -= strlen( token );
				// sanity check that
				if ( Q_strncmp( text_p, token, strlen( token ) ) ) {
					// this should never happen, just here to check that this operation is correct before code goes live
					BG_AnimParseError( "BG_AnimParseAnimScript: internal error" );
				}
				//
				memset( &tempScriptItem, 0, sizeof( tempScriptItem ) );
				indexes[indentLevel] = BG_ParseConditions( &text_p, &tempScriptItem );
				// do we have enough room in this script for another item?
				if ( currentScript->numItems >= MAX_ANIMSCRIPT_ITEMS ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: exceeded maximum items per script (%i)", MAX_ANIMSCRIPT_ITEMS ); // RF mod
				}
				// are there enough items left in the global list?
				if ( modelInfo->numScriptItems >= MAX_ANIMSCRIPT_ITEMS_PER_MODEL ) {
					BG_AnimParseError( "BG_AnimParseAnimScript: exceeded maximum global items (%i)", MAX_ANIMSCRIPT_ITEMS_PER_MODEL );   // RF mod
				}
				// it was parsed ok, so grab an item from the global list to use
				currentScript->items[currentScript->numItems] = &modelInfo->scriptItems[ modelInfo->numScriptItems++ ];
				currentScriptItem = currentScript->items[currentScript->numItems];
				currentScript->numItems++;
				// copy the data across from the temp script item
				*currentScriptItem = tempScriptItem;

			} else if ( indentLevel == 2 ) {

				// we are reading the commands, so parse this line as if it were a command
				// move the text_p backwards so we can read in the last token again
				text_p -= strlen( token );
				// sanity check that
				if ( Q_strncmp( text_p, token, strlen( token ) ) ) {
					// this should never happen, just here to check that this operation is correct before code goes live
					BG_AnimParseError( "BG_AnimParseAnimScript: internal error" );
				}
				//
				BG_ParseCommands( &text_p, currentScriptItem, modelInfo, scriptData );

			} else {

				// huh ??
				BG_AnimParseError( "BG_AnimParseAnimScript: unexpected '%s'", token );   // RF mod

			}

		}

	}

	globalFilename = NULL;

}

//------------------------------------------------------------------------
//
// run-time gameplay functions, these are called during gameplay, so they must be
// cpu efficient.
//

/*
===============
BG_EvaluateConditions

  returns qfalse if the set of conditions fails, qtrue otherwise
===============
*/
qboolean BG_EvaluateConditions( int client, animScriptItem_t *scriptItem ) {
	int i;
	animScriptCondition_t *cond;

	for ( i = 0, cond = scriptItem->conditions; i < scriptItem->numConditions; i++, cond++ )
	{
		switch ( animConditionsTable[cond->index].type ) {
		case ANIM_CONDTYPE_BITFLAGS:
			if ( !( globalScriptData->clientConditions[client][cond->index][0] & cond->value[0] ) &&
				 !( globalScriptData->clientConditions[client][cond->index][1] & cond->value[1] ) ) {
				return qfalse;
			}
			break;
		case ANIM_CONDTYPE_VALUE:
			if ( !( globalScriptData->clientConditions[client][cond->index][0] == cond->value[0] ) ) {
				return qfalse;
			}
			break;
		default: // TTimo NUM_ANIM_CONDTYPES not handled
			break;
		}
	}
	//
	// all conditions must have passed
	return qtrue;
}

/*
===============
BG_FirstValidItem

  scroll through the script items, returning the first script found to pass all conditions

  returns NULL if no match found
===============
*/
animScriptItem_t *BG_FirstValidItem( int client, animScript_t *script ) {
	animScriptItem_t **ppScriptItem;

	int i;

	for ( i = 0, ppScriptItem = script->items; i < script->numItems; i++, ppScriptItem++ )
	{
		if ( BG_EvaluateConditions( client, *ppScriptItem ) ) {
			return *ppScriptItem;
		}
	}
	//
	return NULL;
}

/*
===============
BG_PlayAnim
===============
*/
int BG_PlayAnim( playerState_t *ps, int animNum, animBodyPart_t bodyPart, int forceDuration, qboolean setTimer, qboolean isContinue, qboolean force ) {
	int duration;
	qboolean wasSet = qfalse;
	animModelInfo_t *modelInfo;

	modelInfo = BG_ModelInfoForClient( ps->clientNum );

	if ( forceDuration ) {
		duration = forceDuration;
	} else {
		duration = modelInfo->animations[animNum].duration + 50;    // account for lerping between anims
	}

	switch ( bodyPart ) {
	case ANIM_BP_BOTH:
	case ANIM_BP_LEGS:

		if ( ( ps->legsTimer < 50 ) || force ) {
			if ( !isContinue || !( ( ps->legsAnim & ~ANIM_TOGGLEBIT ) == animNum ) ) {
				wasSet = qtrue;
				ps->legsAnim = ( ( ps->legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT ) | animNum;
				if ( setTimer ) {
					ps->legsTimer = duration;
				}
			} else if ( setTimer && modelInfo->animations[animNum].loopFrames ) {
				ps->legsTimer = duration;
			}
		}

		if ( bodyPart == ANIM_BP_LEGS ) {
			break;
		}

	case ANIM_BP_TORSO:

		if ( ( ps->torsoTimer < 50 ) || force ) {
			if ( !isContinue || !( ( ps->torsoAnim & ~ANIM_TOGGLEBIT ) == animNum ) ) {
				ps->torsoAnim = ( ( ps->torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT ) | animNum;
				if ( setTimer ) {
					ps->torsoTimer = duration;
				}
			} else if ( setTimer && modelInfo->animations[animNum].loopFrames ) {
				ps->torsoTimer = duration;
			}
		}

		break;
	default: // TTimo default ANIM_BP_UNUSED NUM_ANIM_BODYPARTS not handled
		break;
	}

	if ( !wasSet ) {
		return -1;
	}

	return duration;
}

/*
===============
BG_PlayAnimName
===============
*/
int BG_PlayAnimName( playerState_t *ps, char *animName, animBodyPart_t bodyPart, qboolean setTimer, qboolean isContinue, qboolean force ) {
	return BG_PlayAnim( ps, BG_AnimationIndexForString( animName, ps->clientNum ), bodyPart, 0, setTimer, isContinue, force );
}

/*
===============
BG_ExecuteCommand

  returns the duration of the animation, -1 if no anim was set
===============
*/
int BG_ExecuteCommand( playerState_t *ps, animScriptCommand_t *scriptCommand, qboolean setTimer, qboolean isContinue, qboolean force ) {
	int duration = -1;
	qboolean playedLegsAnim = qfalse;

	if ( scriptCommand->bodyPart[0] ) {
		duration = scriptCommand->animDuration[0] + 50;
		// FIXME: how to sync torso/legs anims accounting for transition blends, etc
		if ( scriptCommand->bodyPart[0] == ANIM_BP_BOTH || scriptCommand->bodyPart[0] == ANIM_BP_LEGS ) {
			playedLegsAnim = ( BG_PlayAnim( ps, scriptCommand->animIndex[0], scriptCommand->bodyPart[0], duration, setTimer, isContinue, force ) > -1 );
		} else {
			BG_PlayAnim( ps, scriptCommand->animIndex[0], scriptCommand->bodyPart[0], duration, setTimer, isContinue, force );
		}
	}
	if ( scriptCommand->bodyPart[1] ) {
		duration = scriptCommand->animDuration[0] + 50;
		// FIXME: how to sync torso/legs anims accounting for transition blends, etc
		// just play the animation for the torso
		if ( scriptCommand->bodyPart[1] == ANIM_BP_BOTH || scriptCommand->bodyPart[1] == ANIM_BP_LEGS ) {
			playedLegsAnim = ( BG_PlayAnim( ps, scriptCommand->animIndex[1], scriptCommand->bodyPart[1], duration, setTimer, isContinue, force ) > -1 );
		} else {
			BG_PlayAnim( ps, scriptCommand->animIndex[1], scriptCommand->bodyPart[1], duration, setTimer, isContinue, force );
		}
	}

	if ( scriptCommand->soundIndex ) {
		globalScriptData->playSound( scriptCommand->soundIndex, ps->origin, ps->clientNum );
	}

	if ( !playedLegsAnim ) {
		return -1;
	}

	return duration;
}

/*
================
BG_AnimScriptAnimation

  runs the normal locomotive animations

  returns 1 if an animation was set, -1 if no animation was found, 0 otherwise
================
*/
int BG_AnimScriptAnimation( playerState_t *ps, aistateEnum_t state, scriptAnimMoveTypes_t movetype, qboolean isContinue ) {
	animModelInfo_t     *modelInfo = NULL;
	animScript_t        *script = NULL;
	animScriptItem_t    *scriptItem = NULL;
	animScriptCommand_t *scriptCommand = NULL;

	// DHM - Nerve :: Allow fallen movetype while dead
	if ( ps->eFlags & EF_DEAD && movetype != ANIM_MT_FALLEN ) {
		return -1;
	}

	modelInfo = BG_ModelInfoForClient( ps->clientNum );

#ifdef DBGANIMS
	if ( ps->clientNum ) {
		Com_Printf( "script anim: cl %i, mt %s, ", ps->clientNum, animMoveTypesStr[movetype] );
	}
#endif

	// try finding a match in all states below the given state
	while ( !scriptItem && state >= 0 ) {
		script = &modelInfo->scriptAnims[ state ][ movetype ];
		if ( !script->numItems ) {
			state--;
			continue;
		}
		// find the first script item, that passes all the conditions for this event
		scriptItem = BG_FirstValidItem( ps->clientNum, script );
		if ( !scriptItem ) {
			state--;
			continue;
		}
	}
	//
	if ( !scriptItem ) {
#ifdef DBGANIMS
		if ( ps->clientNum ) {
			Com_Printf( "no valid conditions\n" );
		}
#endif
		return -1;
	}
	// save this as our current movetype
	BG_UpdateConditionValue( ps->clientNum, ANIM_COND_MOVETYPE, movetype, qtrue );
	// pick the correct animation for this character (animations must be constant for each character, otherwise they'll constantly change)
	scriptCommand = &scriptItem->commands[ ps->clientNum % scriptItem->numCommands ];

#ifdef DBGANIMS
	if ( scriptCommand->bodyPart[0] ) {
		if ( ps->clientNum ) {
			Com_Printf( "anim0 (%s): %s", animBodyPartsStr[scriptCommand->bodyPart[0]].string, modelInfo->animations[scriptCommand->animIndex[0]].name );
		}
	}
	if ( scriptCommand->bodyPart[1] ) {
		if ( ps->clientNum ) {
			Com_Printf( "anim1 (%s): %s", animBodyPartsStr[scriptCommand->bodyPart[1]].string, modelInfo->animations[scriptCommand->animIndex[1]].name );
		}
	}
	if ( ps->clientNum ) {
		Com_Printf( "\n" );
	}
#endif

	// run it
	return ( BG_ExecuteCommand( ps, scriptCommand, qfalse, isContinue, qfalse ) != -1 );
}

/*
================
BG_AnimScriptCannedAnimation

  uses the current movetype for this client to play a canned animation

  returns the duration in milliseconds that this model should be paused. -1 if no anim found
================
*/
int BG_AnimScriptCannedAnimation( playerState_t *ps, aistateEnum_t state ) {
	animModelInfo_t     *modelInfo;
	animScript_t        *script;
	animScriptItem_t    *scriptItem;
	animScriptCommand_t *scriptCommand;
	scriptAnimMoveTypes_t movetype;

	if ( ps->eFlags & EF_DEAD ) {
		return -1;
	}

	movetype = globalScriptData->clientConditions[ ps->clientNum ][ ANIM_COND_MOVETYPE ][0];
	if ( !movetype ) {    // no valid movetype yet for this client
		return -1;
	}
	//
	modelInfo = BG_ModelInfoForClient( ps->clientNum );
	script = &modelInfo->scriptCannedAnims[ state ][ movetype ];
	if ( !script->numItems ) {
		return -1;
	}
	// find the first script item, that passes all the conditions for this event
	scriptItem = BG_FirstValidItem( ps->clientNum, script );
	if ( !scriptItem ) {
		return -1;
	}
	// pick a random command
	scriptCommand = &scriptItem->commands[ rand() % scriptItem->numCommands ];
	// run it
	return BG_ExecuteCommand( ps, scriptCommand, qtrue, qfalse, qfalse );
}

/*
================
BG_AnimScriptStateChange

  returns the duration in milliseconds that this model should be paused. -1 if no anim found
================
*/
int BG_AnimScriptStateChange( playerState_t *ps, aistateEnum_t newState, aistateEnum_t oldState ) {
	animModelInfo_t     *modelInfo;
	animScript_t        *script;
	animScriptItem_t    *scriptItem;
	animScriptCommand_t *scriptCommand;

	if ( ps->eFlags & EF_DEAD ) {
		return -1;
	}

	modelInfo = BG_ModelInfoForClient( ps->clientNum );
	script = &modelInfo->scriptStateChange[ oldState ][ newState ];
	if ( !script->numItems ) {
		return -1;
	}
	// find the first script item, that passes all the conditions for this event
	scriptItem = BG_FirstValidItem( ps->clientNum, script );
	if ( !scriptItem ) {
		return -1;
	}
	// pick a random command
	scriptCommand = &scriptItem->commands[ rand() % scriptItem->numCommands ];
	// run it
	return BG_ExecuteCommand( ps, scriptCommand, qtrue, qfalse, qfalse );
}

/*
================
BG_AnimScriptEvent

  returns the duration in milliseconds that this model should be paused. -1 if no event found
================
*/
int BG_AnimScriptEvent( playerState_t *ps, scriptAnimEventTypes_t event, qboolean isContinue, qboolean force ) {
	animModelInfo_t     *modelInfo;
	animScript_t        *script;
	animScriptItem_t    *scriptItem;
	animScriptCommand_t *scriptCommand;

	if ( event != ANIM_ET_DEATH && ps->eFlags & EF_DEAD ) {
		return -1;
	}

#ifdef DBGANIMEVENTS
	Com_Printf( "script event: cl %i, ev %s, ", ps->clientNum, animEventTypesStr[event] );
#endif

	modelInfo = BG_ModelInfoForClient( ps->clientNum );
	script = &modelInfo->scriptEvents[ event ];
	if ( !script->numItems ) {
#ifdef DBGANIMEVENTS
		Com_Printf( "no entry\n" );
#endif
		return -1;
	}
	// find the first script item, that passes all the conditions for this event
	scriptItem = BG_FirstValidItem( ps->clientNum, script );
	if ( !scriptItem ) {
#ifdef DBGANIMEVENTS
		Com_Printf( "no valid conditions\n" );
#endif
		return -1;
	}
	// pick a random command
	scriptCommand = &scriptItem->commands[ rand() % scriptItem->numCommands ];

#ifdef DBGANIMEVENTS
	if ( scriptCommand->bodyPart[0] ) {
		Com_Printf( "anim0 (%s): %s", animBodyPartsStr[scriptCommand->bodyPart[0]].string, modelInfo->animations[scriptCommand->animIndex[0]].name );
	}
	if ( scriptCommand->bodyPart[1] ) {
		Com_Printf( "anim1 (%s): %s", animBodyPartsStr[scriptCommand->bodyPart[1]].string, modelInfo->animations[scriptCommand->animIndex[1]].name );
	}
	Com_Printf( "\n" );
#endif

	// run it
	return BG_ExecuteCommand( ps, scriptCommand, qtrue, isContinue, force );
}

/*
===============
BG_ValidAnimScript

  returns qtrue if the given client has animation scripts
===============
*/
qboolean BG_ValidAnimScript( int clientNum ) {
	if ( !globalScriptData->clientModels[clientNum] ) {
		return qfalse;
	}
	//
	if ( !globalScriptData->modelInfo[ globalScriptData->clientModels[clientNum] ].numScriptItems ) {
		return qfalse;
	}
	//
	return qtrue;
}

/*
===============
BG_GetAnimString
===============
*/
char *BG_GetAnimString( int client, int anim ) {
	animModelInfo_t *modelinfo = BG_ModelInfoForClient( client );
	//
	if ( anim >= modelinfo->numAnimations ) {
		BG_AnimParseError( "BG_GetAnimString: anim index is out of range" );
	}
	//
	return modelinfo->animations[anim].name;
}

/*
==============
BG_UpdateConditionValue
==============
*/
void BG_UpdateConditionValue( int client, int condition, int value, qboolean checkConversion ) {
	if ( checkConversion ) {
		// we may need to convert to bitflags
		if ( animConditionsTable[condition].type == ANIM_CONDTYPE_BITFLAGS ) {

			// DHM - Nerve :: We want to set the ScriptData to the explicit value passed in.
			//				COM_BitSet will OR values on top of each other, so clear it first.
			globalScriptData->clientConditions[client][condition][0] = 0;
			globalScriptData->clientConditions[client][condition][1] = 0;
			// dhm - end

			COM_BitSet( globalScriptData->clientConditions[client][condition], value );
			return;
		}
	}
	globalScriptData->clientConditions[client][condition][0] = value;
}

/*
==============
BG_GetConditionValue
==============
*/
int BG_GetConditionValue( int client, int condition, qboolean checkConversion ) {
	int value, i;

	
	value = globalScriptData->clientConditions[client][condition][0]; //this used to return the address of [0]
	

	if ( checkConversion ) {
		// we may need to convert to a value
		if ( animConditionsTable[condition].type == ANIM_CONDTYPE_BITFLAGS ) {
			//if (!value)
			//	return 0;
			for ( i = 0; i < 8 * sizeof( globalScriptData->clientConditions[0][0] ); i++ ) {
				if ( COM_BitCheck( globalScriptData->clientConditions[client][condition], i ) ) {
					return i;
				}
			}
			// nothing found
			return 0;
			//BG_AnimParseError( "BG_GetConditionValue: internal error" );
		}
	}
		
	return value;
}

/*
================
BG_GetAnimScriptAnimation

  returns the locomotion animation index, -1 if no animation was found, 0 otherwise
================
*/
int BG_GetAnimScriptAnimation( int client, aistateEnum_t state, scriptAnimMoveTypes_t movetype ) {
	animModelInfo_t     *modelInfo;
	animScript_t        *script;
	animScriptItem_t    *scriptItem = NULL;
	animScriptCommand_t *scriptCommand;

	modelInfo = BG_ModelInfoForClient( client );

	// try finding a match in all states below the given state
	while ( !scriptItem && state >= 0 ) {
		script = &modelInfo->scriptAnims[ state ][ movetype ];
		if ( !script->numItems ) {
			state--;
			continue;
		}
		// find the first script item, that passes all the conditions for this event
		scriptItem = BG_FirstValidItem( client, script );
		if ( !scriptItem ) {
			state--;
			continue;
		}
	}
	//
	if ( !scriptItem ) {
		return -1;
	}
	// pick the correct animation for this character (animations must be constant for each character, otherwise they'll constantly change)
	scriptCommand = &scriptItem->commands[ client % scriptItem->numCommands ];
	if ( !scriptCommand->bodyPart[0] ) {
		return -1;
	}
	// return the animation
	return scriptCommand->animIndex[0];
}

/*
================
BG_GetAnimScriptEvent

  returns the animation index for this event
================
*/
int BG_GetAnimScriptEvent( playerState_t *ps, scriptAnimEventTypes_t event ) {
	animModelInfo_t     *modelInfo;
	animScript_t        *script;
	animScriptItem_t    *scriptItem;
	animScriptCommand_t *scriptCommand;

	if ( event != ANIM_ET_DEATH && ps->eFlags & EF_DEAD ) {
		return -1;
	}

	modelInfo = BG_ModelInfoForClient( ps->clientNum );
	script = &modelInfo->scriptEvents[ event ];
	if ( !script->numItems ) {
		return -1;
	}
	// find the first script item, that passes all the conditions for this event
	scriptItem = BG_FirstValidItem( ps->clientNum, script );
	if ( !scriptItem ) {
		return -1;
	}
	// pick a random command
	scriptCommand = &scriptItem->commands[ rand() % scriptItem->numCommands ];

	// return the animation
	return scriptCommand->animIndex[0];
}

/*
===============
BG_GetAnimationForIndex

  returns the animation_t for the given index
===============
*/
animation_t *BG_GetAnimationForIndex( int client, int index ) {
	animModelInfo_t     *modelInfo;

	modelInfo = BG_ModelInfoForClient( client );
	if ( index < 0 || index >= modelInfo->numAnimations ) {
		Com_Error( ERR_DROP, "BG_GetAnimationForIndex: index out of bounds" );
	}

	return &modelInfo->animations[index];
}

/*
=================
BG_AnimUpdatePlayerStateConditions
=================
*/
void BG_AnimUpdatePlayerStateConditions( pmove_t *pmove ) {
	playerState_t *ps = pmove->ps;

	// WEAPON
	if ( ps->eFlags & EF_ZOOMING ) {
		BG_UpdateConditionValue( ps->clientNum, ANIM_COND_WEAPON, WP_BINOCULARS, qtrue );
	} else {
		BG_UpdateConditionValue( ps->clientNum, ANIM_COND_WEAPON, ps->weapon, qtrue );
	}

	// MOUNTED
	if ( ps->eFlags & EF_MG42_ACTIVE ) {
		BG_UpdateConditionValue( ps->clientNum, ANIM_COND_MOUNTED, MOUNTED_MG42, qtrue );
	} else {
		BG_UpdateConditionValue( ps->clientNum, ANIM_COND_MOUNTED, MOUNTED_UNUSED, qtrue );
	}

	// UNDERHAND
	BG_UpdateConditionValue( ps->clientNum, ANIM_COND_UNDERHAND, ps->viewangles[0] > 0, qtrue );

	if ( ps->viewheight == ps->crouchViewHeight ) {
		ps->eFlags |= EF_CROUCHING;
	} else {
		ps->eFlags &= ~EF_CROUCHING;
	}

	if ( pmove->cmd.buttons & BUTTON_ATTACK ) {
		BG_UpdateConditionValue( ps->clientNum, ANIM_COND_FIRING, qtrue, qtrue );
	} else {
		BG_UpdateConditionValue( ps->clientNum, ANIM_COND_FIRING, qfalse, qtrue );
	}
}

/*
================
BG_IsCrouchingAnim
================
*/
qboolean BG_IsCrouchingAnim( int clientNum, int animNum ) {
	animation_t *anim;
	if(clientNum < 0){
		return qfalse;
	}

	// FIXME: make compatible with new scripting
	animNum &= ~ANIM_TOGGLEBIT;
	//
	anim = BG_GetAnimationForIndex( clientNum, animNum );
	//
	if ( anim->movetype & ( ( 1 << ANIM_MT_IDLECR ) | ( 1 << ANIM_MT_WALKCR ) | ( 1 << ANIM_MT_WALKCRBK ) ) ) {
		return qtrue;
	}
	//
	return qfalse;
}

void BG_LerpCrouchingAnimation(int clientNum, lerpFrame_t *lf, lerpFrame_t *torsoLerpframe, lerpFrame_t *legsLerpFrame, int newAnimation, int oldAnimNum, animation_t *oldanim){
	int transitionMin = -1;
	if ( !( lf->animation->flags & ANIMFL_FIRINGANIM ) || ( lf != torsoLerpframe ) ) {
		if ( ( lf == legsLerpFrame ) && ( BG_IsCrouchingAnim( clientNum, newAnimation ) != BG_IsCrouchingAnim( clientNum, oldAnimNum ) ) ) {
			if ( lf->animation->moveSpeed || ( lf->animation->movetype & ( ( 1 << ANIM_MT_TURNLEFT ) | ( 1 << ANIM_MT_TURNRIGHT ) ) ) ) { // if unknown movetype, go there faster
				transitionMin = lf->frameTime + 200;    // slowly raise/drop
			} else {
				transitionMin = lf->frameTime + 350;    // slowly raise/drop
			}
		} else if ( lf->animation->moveSpeed ) {
			transitionMin = lf->frameTime + 120;    // always do some lerping (?)
		} else { // not moving, so take your time
			transitionMin = lf->frameTime + 170;    // always do some lerping (?)

		}
		if ( oldanim && oldanim->animBlend ) { //transitionMin < lf->frameTime + oldanim->animBlend) {
			transitionMin = lf->frameTime + oldanim->animBlend;
			lf->animationTime = transitionMin;
		} else {
			// slow down transitions according to speed
			if ( lf->animation->moveSpeed && lf->animSpeedScale < 1.0 ) {
				lf->animationTime += lf->animation->initialLerp;
			}

			if ( lf->animationTime < transitionMin ) {
				lf->animationTime = transitionMin;
			}
		}
	}
}


void BG_SetLerpFrameAnimation(int time, int clientNum, animModelInfo_t *modelInfo, lerpFrame_t *lf, int newAnimation, lerpFrame_t *torsoLerpFrame, lerpFrame_t *legsLerpFrame){
	if(!modelInfo){
		return;
	}

	qboolean firstAnim = qfalse;

	animation_t *oldanim = lf->animation;
	int oldAnimNum = lf->animationNumber;

	if ( !lf->animation ) {
		firstAnim = qtrue;
	}

	lf->animationNumber = newAnimation;
	newAnimation &= ~ANIM_TOGGLEBIT;

	

	if ( newAnimation < 0 || newAnimation >= modelInfo->numAnimations ) {
		Com_Error(ERR_DROP, "Bad animation number (CG_SLFA): %i", newAnimation );
	}

	animation_t *anim = &modelInfo->animations[ newAnimation ];

	lf->animation = anim;
	lf->animationTime = lf->frameTime + anim->initialLerp;

	if(clientNum < 0){
		return;
	}

	BG_LerpCrouchingAnimation(clientNum, lf, torsoLerpFrame, legsLerpFrame, newAnimation, oldAnimNum, oldanim);

	// if first anim, go immediately
	if ( firstAnim ) {
		lf->frameTime = time - 1;
		lf->animationTime = time - 1;
		lf->frame = anim->firstFrame;
	}

}

#define LF_DEBUG 0
void BG_RunLerpFrame(int clientNum, animModelInfo_t *modelInfo, lerpFrame_t *lf, int time, int newAnimation, lerpFrame_t *torsoLerpFrame, lerpFrame_t *legsLerpFrame){
	// see if the animation sequence is switching
	if (modelInfo && ( newAnimation != lf->animationNumber || !lf->animation ) ) {  //----(SA)	modified
		BG_SetLerpFrameAnimation(time, clientNum, modelInfo, lf, newAnimation, torsoLerpFrame, legsLerpFrame);
	}

	int f;
	animation_t *anim;

	// if we have passed the current frame, move it to
	// oldFrame and calculate a new frame
	if ( time >= lf->frameTime ) {
		lf->oldFrame = lf->frame;
		lf->oldFrameTime = lf->frameTime;

		// get the next frame based on the animation
		anim = lf->animation;
		if ( !anim->frameLerp ) {
			return;     // shouldn't happen
		}
		if ( time < lf->animationTime ) {
			lf->frameTime = lf->animationTime;      // initial lerp
		} else {
			lf->frameTime = lf->oldFrameTime + anim->frameLerp;
		}
		f = ( lf->frameTime - lf->animationTime ) / anim->frameLerp;
		if ( f >= anim->numFrames ) {
			f -= anim->numFrames;
			if ( anim->loopFrames ) {
				f %= anim->loopFrames;
				f += anim->numFrames - anim->loopFrames;
			} else {
				f = anim->numFrames - 1;
				// the animation is stuck at the end, so it
				// can immediately transition to another sequence
				lf->frameTime = time;
			}
		}
		lf->frame = anim->firstFrame + f;
		if ( time > lf->frameTime ) {
			lf->frameTime = time;
			if ( LF_DEBUG ) {
				Com_Printf( "Clamp lf->frameTime\n" );
			}
		}
	}

	if ( lf->frameTime > time + 200 ) {
		lf->frameTime = time;
	}

	if ( lf->oldFrameTime > time ) {
		lf->oldFrameTime = time;
	}

	// calculate current lerp value
	if ( lf->frameTime == lf->oldFrameTime ) {
		lf->backlerp = 0;
	} else {
		lf->backlerp = 1.0 - (float)( time - lf->oldFrameTime ) / ( lf->frameTime - lf->oldFrameTime );
	}
}

void BG_RunLerpFrameRate(int snapshotTime, int time, int clientNum, animModelInfo_t *modelInfo, lerpFrame_t *lf,
						 int newAnimation, lerpFrame_t *torsoLerpFrame, lerpFrame_t *legsLerpFrame,
						 vec3_t currentOrigin, float manualAnimSpeed, int recursion){
		int f;
	animation_t *anim, *oldAnim;
	animation_t *otherAnim = NULL;
	qboolean isLadderAnim;

#define ANIM_SCALEMAX_LOW   1.1
#define ANIM_SCALEMAX_HIGH  1.6

#define ANIM_SPEEDMAX_LOW   100
#define ANIM_SPEEDMAX_HIGH  20

	// debugging tool to get no animations
	// if ( cg_animSpeed.integer == 0 ) {
	// 	lf->oldFrame = lf->frame = lf->backlerp = 0;
	// 	return;
	// }

	isLadderAnim = lf->animation && ( lf->animation->flags & ANIMFL_LADDERANIM );

	oldAnim = lf->animation;

	// see if the animation sequence is switching
	if ( newAnimation != lf->animationNumber || !lf->animation ) {
		BG_SetLerpFrameAnimation(time, clientNum, modelInfo, lf, newAnimation, torsoLerpFrame, legsLerpFrame );
	}

	// Ridah, make sure the animation speed is updated when possible
	anim = lf->animation;
	if ( anim->moveSpeed && lf->oldFrameSnapshotTime ) {
		float moveSpeed;

		// calculate the speed at which we moved over the last frame
		if ( snapshotTime != lf->oldFrameSnapshotTime) {
			// if ( clientNum == cg.snap->ps.clientNum ) {
			// 	if ( isLadderAnim ) { // only use Z axis for speed
			// 		if ( cent->currentState.aiChar != AICHAR_FEMZOMBIE ) {    // femzombie has sideways climbing
			// 			lf->oldFramePos[0] = cent->lerpOrigin[0];
			// 			lf->oldFramePos[1] = cent->lerpOrigin[1];
			// 		}
			// 	} else {    // only use x/y axis
			// 		lf->oldFramePos[2] = cent->lerpOrigin[2];
			// 	}
			// 	moveSpeed = Distance( cent->lerpOrigin, lf->oldFramePos ) / ( (float)( cg.time - lf->oldFrameTime ) / 1000.0 );
			// } else {
				if ( isLadderAnim ) { // only use Z axis for speed
					lf->oldFramePos[0] = currentOrigin[0];
					lf->oldFramePos[1] = currentOrigin[1];
				}

				moveSpeed = Distance( currentOrigin, lf->oldFramePos ) / ( (float)( time - lf->oldFrameTime ) / 1000.0 );
				
			// }
			//
			// convert it to a factor of this animation's movespeed
			lf->animSpeedScale = moveSpeed / (float)anim->moveSpeed;
			lf->oldFrameSnapshotTime = snapshotTime;
		}
	} else {
		// move at normal speed
		lf->animSpeedScale = 1.0;
		lf->oldFrameSnapshotTime = snapshotTime;
	}
	// adjust with manual setting (pain anims)
	lf->animSpeedScale *= manualAnimSpeed;

	// if we have passed the current frame, move it to
	// oldFrame and calculate a new frame
	if ( time >= lf->frameTime ) {

		lf->oldFrame = lf->frame;
		lf->oldFrameTime = lf->frameTime;
		VectorCopy( currentOrigin, lf->oldFramePos );

		// restrict the speed range
		if ( lf->animSpeedScale < 0.25 ) {    // if it's too slow, then a really slow spped, combined with a sudden take-off, can leave them playing a really slow frame while they a moving really fast
			if ( lf->animSpeedScale < 0.01 && isLadderAnim ) {
				lf->animSpeedScale = 0.0;
			} else {
				lf->animSpeedScale = 0.25;
			}
		} else if ( lf->animSpeedScale > ANIM_SCALEMAX_LOW ) {

			if ( !( anim->flags & ANIMFL_LADDERANIM ) ) {
				// allow slower anims to speed up more than faster anims
				if ( anim->moveSpeed > ANIM_SPEEDMAX_LOW ) {
					lf->animSpeedScale = ANIM_SCALEMAX_LOW;
				} else if ( anim->moveSpeed < ANIM_SPEEDMAX_HIGH ) {
					if ( lf->animSpeedScale > ANIM_SCALEMAX_HIGH ) {
						lf->animSpeedScale = ANIM_SCALEMAX_HIGH;
					}
				} else {
					lf->animSpeedScale = ANIM_SCALEMAX_HIGH - ( ANIM_SCALEMAX_HIGH - ANIM_SCALEMAX_LOW ) * (float)( anim->moveSpeed - ANIM_SPEEDMAX_HIGH ) / (float)( ANIM_SPEEDMAX_LOW - ANIM_SPEEDMAX_HIGH );
				}
			} else if ( lf->animSpeedScale > 4.0 ) {
				lf->animSpeedScale = 4.0;
			}

		}

		if ( lf == legsLerpFrame ) {
			otherAnim = torsoLerpFrame->animation;
		} else if ( lf == torsoLerpFrame ) {
			otherAnim = legsLerpFrame->animation;
		}

		// get the next frame based on the animation
		if ( !lf->animSpeedScale ) {
			// stopped on the ladder, so stay on the same frame
			f = lf->frame - anim->firstFrame;
			lf->frameTime += anim->frameLerp;       // don't wait too long before starting to move again
		} else if ( lf->oldAnimationNumber != lf->animationNumber &&
					( !anim->moveSpeed || lf->oldFrame < anim->firstFrame || lf->oldFrame >= anim->firstFrame + anim->numFrames ) ) { // Ridah, added this so walking frames don't always get reset to 0, which can happen in the middle of a walking anim, which looks wierd
			lf->frameTime = lf->animationTime;      // initial lerp
			if ( oldAnim && anim->moveSpeed ) {   // keep locomotions going continuously
				f = ( lf->frame - oldAnim->firstFrame ) + 1;
				while ( f < 0 ) {
					f += anim->numFrames;
				}
			} else {
				f = 0;
			}
		} else if ( ( lf == legsLerpFrame ) && otherAnim && !( anim->flags & ANIMFL_FIRINGANIM ) && ( ( lf->animationNumber & ~ANIM_TOGGLEBIT ) == ( torsoLerpFrame->animationNumber & ~ANIM_TOGGLEBIT ) ) && ( !anim->moveSpeed ) ) {
			// legs should synch with torso
			f = torsoLerpFrame->frame - otherAnim->firstFrame;
			if ( f >= anim->numFrames || f < 0 ) {
				f = 0;  // wait at the start for the legs to catch up (assuming they are still in an old anim)
			}
			lf->frameTime = torsoLerpFrame->frameTime;
		} else if ( ( lf == torsoLerpFrame ) && otherAnim && !( anim->flags & ANIMFL_FIRINGANIM ) && ( ( lf->animationNumber & ~ANIM_TOGGLEBIT ) == ( legsLerpFrame->animationNumber & ~ANIM_TOGGLEBIT ) ) && ( otherAnim->moveSpeed ) ) {
			// torso needs to sync with legs
			f = legsLerpFrame->frame - otherAnim->firstFrame;
			if ( f >= anim->numFrames || f < 0 ) {
				f = 0;  // wait at the start for the legs to catch up (assuming they are still in an old anim)
			}
			lf->frameTime = legsLerpFrame->frameTime;
		} else {
			lf->frameTime = lf->oldFrameTime + (int)( (float)anim->frameLerp * ( 1.0 / lf->animSpeedScale ) );
			if ( lf->frameTime < time ) {
				lf->frameTime = time;
			}

			// check for skipping frames (eg. death anims play in slo-mo if low framerate)
			if ( time > lf->frameTime && !anim->moveSpeed ) {
				f = ( lf->frame - anim->firstFrame ) + 1 + ( time - lf->frameTime ) / anim->frameLerp;
			} else {
				f = ( lf->frame - anim->firstFrame ) + 1;
			}

			if ( f < 0 ) {
				f = 0;
			}
		}
		//f = ( lf->frameTime - lf->animationTime ) / anim->frameLerp;
		if ( f >= anim->numFrames ) {
			f -= anim->numFrames;
			if ( anim->loopFrames ) {
				f %= anim->loopFrames;
				f += anim->numFrames - anim->loopFrames;
			} else {
				f = anim->numFrames - 1;
				// the animation is stuck at the end, so it
				// can immediately transition to another sequence
				lf->frameTime = time;
			}
		}
		lf->frame = anim->firstFrame + f;
		if ( time > lf->frameTime ) {

			// Ridah, run the frame again until we move ahead of the current time, fixes walking speeds for zombie
			if ( /*!anim->moveSpeed ||*/ recursion > 4 ) {
				lf->frameTime = time;
			} else {
				BG_RunLerpFrameRate(snapshotTime, time, clientNum, modelInfo, lf, newAnimation, torsoLerpFrame, legsLerpFrame, currentOrigin, manualAnimSpeed, recursion + 1 );
			}

			if ( 0 ) {
				Com_Printf( "Clamp lf->frameTime\n" );
			}
		}
		lf->oldAnimationNumber = lf->animationNumber;
	}

	if ( lf->oldFrameTime > time ) {
		lf->oldFrameTime = time;
	}
	// calculate current lerp value
	if ( lf->frameTime == lf->oldFrameTime ) {
		lf->backlerp = 0;
	} else {
		lf->backlerp = 1.0 - (float)( time - lf->oldFrameTime ) / ( lf->frameTime - lf->oldFrameTime );
	}
}



void BG_SwingAngles( int frameTime, float destination, float swingTolerance, float clampTolerance,
							float speed, float *angle, qboolean *swinging ) {
	float swing;
	float move;
	float scale;

	if ( !*swinging ) {
		// see if a swing should be started
		swing = AngleSubtract( *angle, destination );
		if ( swing > swingTolerance || swing < -swingTolerance ) {
			*swinging = qtrue;
		}
	}

	if ( !*swinging ) {
		return;
	}

	// modify the speed depending on the delta
	// so it doesn't seem so linear
	swing = AngleSubtract( destination, *angle );
	scale = fabs( swing );
	scale *= 0.05;
	if ( scale < 0.5 ) {
		scale = 0.5;
	}

	// swing towards the destination angle
	if ( swing >= 0 ) {
		move = frameTime * scale * speed;
		if ( move >= swing ) {
			move = swing;
			*swinging = qfalse;
		} else {
			*swinging = SWING_LEFT;     // left
		}
		*angle = AngleMod( *angle + move );
	} else if ( swing < 0 ) {
		move = frameTime * scale * -speed;
		if ( move <= swing ) {
			move = swing;
			*swinging = qfalse;
		} else {
			*swinging = SWING_RIGHT;    // right
		}
		*angle = AngleMod( *angle + move );
	}

	// clamp to no more than tolerance
	swing = AngleSubtract( destination, *angle );
	if ( swing > clampTolerance ) {
		*angle = AngleMod( destination - ( clampTolerance - 1 ) );
	} else if ( swing < -clampTolerance ) {
		*angle = AngleMod( destination + ( clampTolerance - 1 ) );
	}
}


/*
===============
BG_PlayerAngles

Handles seperate torso motion

  legs pivot based on direction of movement

  head always looks exactly at cent->lerpAngles

  if motion < 20 degrees, show in head only
  if < 45 degrees, also show in torso
===============
*/

#define SWING_SPEED 0.1f
void BG_PlayerAngles(int entityNum, entityState_t *es, int frameTime, lerpFrame_t *torsoFrame, lerpFrame_t *legsFrame,  vec3_t lerpAngles, 
					vec3_t legsAngles, vec3_t torsoAngles, vec3_t headAngles, vec3_t legsAxis[3], vec3_t torsoAxis[3], vec3_t headAxis[3] ){
	
	float dest;
//	static	int	movementOffsets[8] = { 0, 22, 45, -22, 0, 22, -45, -22 }; // TTimo: unused
	vec3_t velocity;
	float speed;
	float clampTolerance;
	int legsSet;
	
	legsSet = es->legsAnim & ~ANIM_TOGGLEBIT;

	VectorCopy( lerpAngles, headAngles );
	headAngles[YAW] = AngleMod( headAngles[YAW] );
	VectorClear( legsAngles );
	VectorClear( torsoAngles );

	// --------- yaw -------------

	// allow yaw to drift a bit, unless these conditions don't allow them
	if (    !( BG_GetConditionValue( es->number, ANIM_COND_MOVETYPE, qfalse ) & ( ( 1 << ANIM_MT_IDLE ) | ( 1 << ANIM_MT_IDLECR ) ) )/*
		||	 (BG_GetConditionValue( cent->currentState.number, ANIM_COND_MOVETYPE, qfalse ) & ((1<<ANIM_MT_STRAFELEFT) | (1<<ANIM_MT_STRAFERIGHT)) )*/) {

		// always point all in the same direction
		torsoFrame->yawing = qtrue;  // always center
		torsoFrame->pitching = qtrue;    // always center
		legsFrame->yawing = qtrue;   // always center

		// if firing, make sure torso and head are always aligned
	} else if ( BG_GetConditionValue( es->number, ANIM_COND_FIRING, qtrue ) ) {

		torsoFrame->yawing = qtrue;  // always center
		torsoFrame->pitching = qtrue;    // always center

	}

	// adjust legs for movement dir
	if ( es->eFlags & EF_DEAD ) {
		// don't let dead bodies twitch
		legsAngles[YAW] = headAngles[YAW];
		torsoAngles[YAW] = headAngles[YAW];
	} else {
		legsAngles[YAW] = headAngles[YAW] + es->angles2[YAW];

		if ( es->eFlags & EF_NOSWINGANGLES ) {
			legsAngles[YAW] = torsoAngles[YAW] = headAngles[YAW];   // always face firing direction
			clampTolerance = 60;
		} else if ( !( es->eFlags & EF_FIRING ) ) {
			torsoAngles[YAW] = headAngles[YAW] + 0.35 * es->angles2[YAW];
			clampTolerance = 90;
		} else {    // must be firing
			torsoAngles[YAW] = headAngles[YAW]; // always face firing direction
			//if (fabs(cent->currentState.angles2[YAW]) > 30)
			//	legsAngles[YAW] = headAngles[YAW];
			clampTolerance = 60;
		}

		// torso
		BG_SwingAngles( frameTime, torsoAngles[YAW], 25, clampTolerance, SWING_SPEED, &torsoFrame->yawAngle, &torsoFrame->yawing );

		// if the legs are yawing (facing heading direction), allow them to rotate a bit, so we don't keep calling
		// the legs_turn animation while an AI is firing, and therefore his angles will be randomizing according to their accuracy

		clampTolerance = 150;

		if  ( BG_GetConditionValue( entityNum, ANIM_COND_MOVETYPE, qfalse ) & ( 1 << ANIM_MT_IDLE ) ) {
			legsFrame->yawing = qfalse; // set it if they really need to swing
			BG_SwingAngles( frameTime, legsAngles[YAW], 20, clampTolerance, 0.5f * SWING_SPEED, &legsFrame->yawAngle, &legsFrame->yawing );
		} else
		//if	( BG_GetConditionValue( ci->clientNum, ANIM_COND_MOVETYPE, qfalse ) & ((1<<ANIM_MT_STRAFERIGHT)|(1<<ANIM_MT_STRAFELEFT)) )
		if  ( strstr( BG_GetAnimString( entityNum, legsSet ), "strafe" ) ) {
			legsFrame->yawing = qfalse; // set it if they really need to swing
			legsAngles[YAW] = headAngles[YAW];
			BG_SwingAngles( frameTime, legsAngles[YAW], 0, clampTolerance, SWING_SPEED, &legsFrame->yawAngle, &legsFrame->yawing );
		} else
		if ( legsFrame->yawing ) {
			BG_SwingAngles( frameTime, legsAngles[YAW], 0, clampTolerance, SWING_SPEED, &legsFrame->yawAngle, &legsFrame->yawing );
		} else
		{
			BG_SwingAngles( frameTime, legsAngles[YAW], 40, clampTolerance, SWING_SPEED, &legsFrame->yawAngle, &legsFrame->yawing );
		}

		torsoAngles[YAW] = torsoFrame->yawAngle;
		legsAngles[YAW] = legsFrame->yawAngle;
	}

	// --------- pitch -------------

	// only show a fraction of the pitch angle in the torso
	if ( headAngles[PITCH] > 180 ) {
		dest = ( -360 + headAngles[PITCH] ) * 0.75;
	} else {
		dest = headAngles[PITCH] * 0.75;
	}
	BG_SwingAngles( frameTime, dest, 15, 30, 0.1f, &torsoFrame->pitchAngle, &torsoFrame->pitching );
	torsoAngles[PITCH] = torsoFrame->pitchAngle;

	// --------- roll -------------


	// lean towards the direction of travel
	VectorCopy( es->pos.trDelta, velocity );
	speed = VectorNormalize( velocity );
	if ( speed ) {
		vec3_t axis[3];
		float side;

		speed *= 0.05;

		AnglesToAxis( legsAngles, axis );
		side = speed * DotProduct( velocity, axis[1] );
		legsAngles[ROLL] -= side;

		side = speed * DotProduct( velocity, axis[0] );
		legsAngles[PITCH] += side;
	}

}

void BG_PlayerAnglesToAxis(vec3_t legsAngles, vec3_t torsoAngles, vec3_t headAngles, vec3_t legsAxis[3], vec3_t torsoAxis[3], vec3_t headAxis[3] ){
	// pull the angles back out of the hierarchial chain
	AnglesSubtract( headAngles, torsoAngles, headAngles );
	AnglesSubtract( torsoAngles, legsAngles, torsoAngles );
	AnglesToAxis( legsAngles, legsAxis );
	AnglesToAxis( torsoAngles, torsoAxis );
	AnglesToAxis( headAngles, headAxis );

}

void BG_PositionRotatedEntityOnTag(vec3_t entityOrigin, vec3_t entityAxis[3], vec3_t parentOrigin, vec3_t parentAxis[3], orientation_t *orientation){
	vec3_t tempAxis[3];
	// FIXME: allow origin offsets along tag?
	VectorCopy( parentOrigin, entityOrigin );
	for ( int i = 0 ; i < 3 ; i++ ) {
		VectorMA( entityOrigin, orientation->origin[i], parentAxis[i], entityOrigin );
	}

	// had to cast away the const to avoid compiler problems...
	MatrixMultiply( entityAxis, orientation->axis, tempAxis );
	MatrixMultiply( tempAxis, parentAxis, entityAxis );
}
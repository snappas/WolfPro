#include "cg_local.h"
#include "../client/cl_imgui.h"
#include <float.h>

qbool BeginTimeline(const char* str_id);
qbool TimelineEvent(const char* str_id, float* values, int numVals);
void EndTimeline(void);

void CG_ImGUI_Share(void *ctx, void *alloc, void *free, void **user){
	cgs.igContext = ctx;
	cgs.igAlloc = alloc;
	cgs.igFree = free;
	cgs.igUserData = user;
}

char* GetStringTimestamp(int seconds){
	int hour = seconds / 3600;
	int minute = (seconds / 60) % 60;
	int second = seconds % 60;
	if(hour > 0){
		return va("%02d:%02d:%02d", hour, minute, second);
	}
	return va("%02d:%02d", minute, second);
}

static const float TIMELINE_RADIUS = 8.0f;
static const float delta = 3.0f;
static const float TIMELINE_HEIGHT = 8.0f * 2.0f + 3.0f * 2;

static void ImGUIDemoWindow(void){
	static qbool demoWindowActive = qfalse;
	ToggleBooleanWithShortcut(&demoWindowActive, ImGuiKey_C, ImGUI_ShortcutOptions_Global);
	trap_CL_AddGuiMenu(ImGUI_MainMenu_Info, "ImGUI Demo", "", &demoWindowActive, qtrue);
   
	if(demoWindowActive){
		if(igBegin("ImGUI Demo", (bool*)&demoWindowActive, 0)){
			igShowDemoWindow((bool*)&demoWindowActive);
        }
        igEnd();
    }
}

static void DemoPlaybackTimeline(void){
	static qbool demoTimelineActive = qfalse;
	if (cg.demoPlayback) {
		demoTimelineActive = qtrue;
	}

	ToggleBooleanWithShortcut(&demoTimelineActive, ImGuiKey_D, ImGUI_ShortcutOptions_Global);
	trap_CL_AddGuiMenu(ImGUI_MainMenu_Info, "Demo Tools", "", &demoTimelineActive, cg.demoPlayback);
   
	if(demoTimelineActive){
		
		if(igBegin("Timeline", (bool*)&demoTimelineActive, 0)) {
			const char *event_types[] = {"Frags", "Obj Taken", "Obj Dropped"};
			static qbool selected[] = { qtrue, qtrue, qfalse };
			if(igTreeNode_Str("Events")){
				igBeginChild_Str("##EventsAvailable", (ImVec2){0.0f, 0.0f,}, ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY, 0);
				for(int i = 0; i < ARRAY_LEN(event_types); i++){
					igCheckbox(event_types[i], (bool*)& selected[i]);
				}
				
				igEndChild();
				igTreePop();
			}
			
			
			int demoDuration = (m_lastServerTime - m_firstServerTime);
			int currentOffset = (m_currServerTime - m_firstServerTime);
			igPushItemWidth(igGetWindowWidth() - 16.0f);
			
			//igSliderFloat("##timeline", &percent, 0.0f, 100.0f, "", ImGuiSliderFlags_None);
			if (BeginTimeline("Timeline3")) {
				float killAtPercent[1024];
				float docsPickupAtPercent[1024];
				float docsDropAtPercent[1024];
				
				for(int k = 0; k < ndp_docPickupSize; k++){
					docsPickupAtPercent[k] =  (float)(ndp_docPickupTime[k] - m_firstServerTime) / demoDuration;
				}
				for(int k = 0; k < ndp_docDropSize; k++){
					docsDropAtPercent[k] =  (float)(ndp_docDropTime[k] - m_firstServerTime) / demoDuration;
				}
		
				for(int i = 0; i < ARRAY_LEN(selected); i++){

					if(selected[i]){
						switch (i) {
						case 0:
						{
							for(int k = 0; k < ndp_myKillsSize; k++){
								killAtPercent[k] =  (float)(ndp_myKills[k] - m_firstServerTime) / demoDuration;
							}
							trap_IgImage(cgs.media.skullIcon, TIMELINE_HEIGHT, TIMELINE_HEIGHT);
							igSameLine(0.0f, 2.0f);
							TimelineEvent(event_types[i], killAtPercent, ndp_myKillsSize );
							break;
						}
						case 1:
							trap_IgImageEx(cgs.media.exclamationIcon, TIMELINE_HEIGHT, TIMELINE_HEIGHT, 0.23f, 0.23f, 0.77f, 0.77f);
							igSameLine(0.0f, 2.0f);
							TimelineEvent(event_types[i], docsPickupAtPercent, ndp_docPickupSize);
							break;
						}
						
					}
				}
				EndTimeline();
			}
			
			
			igPopItemWidth();
			
			igText("%s / %s", GetStringTimestamp(currentOffset / 1000), GetStringTimestamp(demoDuration / 1000));

			
			
			igSameLine(0.0f, 16.0f);
			if(igButton("Next Frag", (ImVec2){0.0f, 0.0f})){
				CG_NDP_GoToNextFrag(qtrue);
			}
			igSameLine(0.0f, 16.0f);

if (igButton("Prev Frag", (ImVec2) { 0.0f, 0.0f })) {
	CG_NDP_GoToNextFrag(qfalse);
}
igSameLine(0.0f, 16.0f);
static qbool paused = qfalse;
if (!paused) {
	if (igButton("Pause", (ImVec2) { 0.0f, 0.0f })) {
		trap_Cvar_Set("timescale", "0");
		paused = qtrue;
	}
}
else {
	if (igButton("Play", (ImVec2) { 0.0f, 0.0f })) {
		trap_Cvar_Set("timescale", "1");
		paused = qfalse;
	}
}
		}
		igEnd();
	}
}

typedef enum cvarType_e {
	CVT_BOOL,
	CVT_INT,
	CVT_FLOAT,
	CVT_STRING,
	CVT_COLOR
} cvarType;

typedef struct cvarGui_s {
	char name[MAX_CVAR_VALUE_STRING];
	vmCvar_t* cvar;
	cvarType type;
	int minIntValue;
	int maxIntValue;
	float minFloatValue;
	float maxFloatValue;
} cvarGui_t;




cvarGui_t hudCvars[] = {
	{"cg_customCrosshair", &cg_customCrosshair, CVT_BOOL, 0, 1, 0.0f, 0.0f },
	{"cg_customCrosshairHeight", &cg_customCrosshairHeight, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairThickness", &cg_customCrosshairThickness, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairThicknessAlt", &cg_customCrosshairThicknessAlt, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairWidth", &cg_customCrosshairWidth, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairXOffset", &cg_customCrosshairXOffset, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairYOffset", &cg_customCrosshairYOffset, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairXGap", &cg_customCrosshairXGap, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairYGap", &cg_customCrosshairYGap, CVT_FLOAT, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairColor", &cg_customCrosshairColor, CVT_COLOR, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairColorAlt", &cg_customCrosshairColorAlt, CVT_COLOR, 0, 0, 0.0f, 50.0f },
	{"cg_customCrosshairVMirror", &cg_customCrosshairVMirror, CVT_BOOL, 0, 0, 0.0f, 50.0f },

	{"cg_drawCrosshair", &cg_drawCrosshair, CVT_INT, 0, 25, 0.0f, 50.0f },
	{"cg_crosshairHealth", &cg_crosshairHealth, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_crosshairSize", &cg_crosshairSize, CVT_FLOAT, 0, 100, 0.0f, 150.0f },
	{"cg_crosshairX", &cg_crosshairX, CVT_INT, -10, 10, 0.0f, 50.0f },
	{"cg_crosshairY", &cg_crosshairY, CVT_INT, -10, 10, 0.0f, 50.0f },
	{"cg_crosshairPulse", &cg_crosshairPulse, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_crosshairAlpha", &cg_crosshairAlpha, CVT_FLOAT, 0, 0, 0.0f, 1.0f },
	{"cg_crosshairAlphaAlt", &cg_crosshairAlphaAlt, CVT_FLOAT, 0, 0, 0.0f, 1.0f },
	{"cg_crosshairColor", &cg_crosshairColor, CVT_COLOR, 0, 0, 0.0f, 50.0f },
	{"cg_crosshairColorAlt", &cg_crosshairColorAlt, CVT_COLOR, 0, 0, 0.0f, 50.0f },

	{"cg_registeredPlayers", &cg_registeredPlayers, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_gun_z", &cg_gun_z, CVT_FLOAT, 0, 0, -50.0f, 50.0f },
	{"cg_gun_y", &cg_gun_y, CVT_FLOAT, 0, 0, -50.0f, 50.0f },
	{"cg_gun_x", &cg_gun_x, CVT_FLOAT, 0, 0, -25.0f, 25.0f },
	{"cg_drawGun", &cg_drawGun, CVT_BOOL, 0, 0, 0.0f, 50.0f },

	{"cg_drawFPS", &cg_drawFPS, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_drawTimer", &cg_drawTimer, CVT_BOOL, 0, 0, 0.0f, 50.0f },

	{"cg_hudAlpha", &cg_hudAlpha, CVT_FLOAT, 0, 0, 0.0f, 1.0f },
	{"cg_fov", &cg_fov, CVT_FLOAT, 0, 0, 85.0f, 120.0f },

	{"cg_teamChatHeight", &cg_teamChatHeight, CVT_INT, 0, 32, 0.0f, 50.0f },
	{"cg_chatX", &cg_chatX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_chatY", &cg_chatY, CVT_INT, 0, 480, 0.0f, 50.0f },
	{"cg_chatAlpha", &cg_chatAlpha, CVT_FLOAT, 0, 480, 0.0f, 1.0f },
	{"cg_chatBackgroundColor", &cg_chatBackgroundColor, CVT_COLOR, 0, 480, 0.0f, 1.0f },

	{"cg_fragsWidth", &cg_fragsWidth, CVT_INT, 8, 48, 0.0f, 50.0f },
	{"cg_fragsY", &cg_fragsY, CVT_INT, 0, 480, 0.0f, 50.0f },

	{"con_notifytime", &cg_drawNotifyText, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_drawNotifyText", &cg_drawNotifyText, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_notifyTextX", &cg_notifyTextX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_notifyTextY", &cg_notifyTextY, CVT_INT, 0, 480, 0.0f, 50.0f },
	{"cg_notifyTextShadow", &cg_notifyTextShadow, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_notifyTextWidth", &cg_notifyTextWidth, CVT_INT, 0, 48, 0.0f, 50.0f },
	{"cg_notifyTextHeight", &cg_notifyTextHeight, CVT_INT, 0, 48, 0.0f, 50.0f },
	{"cg_notifyTextLines", &cg_notifyTextLines, CVT_INT, 0, 16, 0.0f, 50.0f },

	{"cg_lagometer", &cg_lagometer, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_lagometerX", &cg_lagometerX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_lagometerY", &cg_lagometerY, CVT_INT, 0, 480, 0.0f, 50.0f },

	{"cg_drawCompass", &cg_drawCompass, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_compassX", &cg_compassX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_compassY", &cg_compassY, CVT_INT, 0, 480, 0.0f, 50.0f },

	{"cg_drawSpeed", &cg_drawSpeed, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_speedX", &cg_speedX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_speedY", &cg_speedY, CVT_INT, 0, 480, 0.0f, 50.0f },

	

	{"cg_teamOverlayX", &cg_teamOverlayX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_teamOverlayY", &cg_teamOverlayY, CVT_INT, 0, 480, 0.0f, 50.0f },

	{"cg_showPriorityText", &cg_showPriorityText, CVT_BOOL, 0, 0, 0.0f, 50.0f },
	{"cg_priorityTextX", &cg_priorityTextX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_priorityTextY", &cg_priorityTextY, CVT_INT, 0, 480, 0.0f, 50.0f },

	{"cg_drawReinforcementTime", &cg_drawReinforcementTime, CVT_INT, 0, 3, 0.0f, 50.0f },
	{"cg_drawEnemyTimer", &cg_drawEnemyTimer, CVT_INT, 0, 3, 0.0f, 50.0f },
	{"cg_enemyTimerColor", &cg_enemyTimerColor, CVT_COLOR, 0, 0, 0.0f, 50.0f },
	{"cg_reinforcementTimeColor", &cg_reinforcementTimeColor, CVT_COLOR, 0, 0, 0.0f, 50.0f },
	{"cg_enemyTimerX", &cg_enemyTimerX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_enemyTimerY", &cg_enemyTimerY, CVT_INT, 0, 480, 0.0f, 50.0f },
	{"cg_enemyTimerProX", &cg_enemyTimerProX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_enemyTimerProY", &cg_enemyTimerProY, CVT_INT, 0, 480, 0.0f, 50.0f },
	{"cg_reinforcementTimeX", &cg_reinforcementTimeX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_reinforcementTimeY", &cg_reinforcementTimeY, CVT_INT, 0, 480, 0.0f, 50.0f },
	{"cg_reinforcementTimeProX", &cg_reinforcementTimeProX, CVT_INT, 0, 640, 0.0f, 50.0f },
	{"cg_reinforcementTimeProY", &cg_reinforcementTimeProY, CVT_INT, 0, 480, 0.0f, 50.0f },


};

static void HudEditor(void){
	static qbool hudEditorActive = qfalse;
	if (cg.hudeditor) {
		hudEditorActive = qtrue;
	}

	ToggleBooleanWithShortcut(&hudEditorActive, ImGuiKey_H, ImGUI_ShortcutOptions_Global);
	trap_CL_AddGuiMenu(ImGUI_MainMenu_Info, "HUD Editor", "", &hudEditorActive, cg.hudeditor);

	if (hudEditorActive) {
		if (igBegin("HUD Editor", (bool*)&hudEditorActive, 0)) {
			
			static ImGuiTextFilter filter = {};
			igText("Filter:");
			igSameLine(0.0f, 0.0f);
			ImGuiTextFilter_Draw(&filter, "##filter", 0.0f);
			if(igBeginTable("CVars",2,ImGuiTableFlags_RowBg,(ImVec2){0,0},0.0f)){
				igTableSetupColumn("Name", 0, 0, 0);
				igTableSetupColumn("Value", 0, 0, 0);
				igTableHeadersRow();

				for(int i = 0; i < ARRAY_LEN(hudCvars); i++){
					if(ImGuiTextFilter_PassFilter(&filter, hudCvars[i].name, NULL)){
						igTableNextRow(0, 0.0f);
						igTableSetColumnIndex(0);
						igText(hudCvars[i].name);
						igTableSetColumnIndex(1);
						switch(hudCvars[i].type){
							case CVT_BOOL:
								{
									int orig = hudCvars[i].cvar->integer;
									igCheckbox(va("##%s", hudCvars[i].name), (bool*)&hudCvars[i].cvar->integer);
									if(orig != hudCvars[i].cvar->integer){
										trap_Cvar_Set(hudCvars[i].name, va("%d", hudCvars[i].cvar->integer));
									}
									break;
								}
							case CVT_INT:
								{
									int orig = hudCvars[i].cvar->integer;
									igSliderInt(va("##%s", hudCvars[i].name), &hudCvars[i].cvar->integer, hudCvars[i].minIntValue, hudCvars[i].maxIntValue, "%d", ImGuiSliderFlags_None);
									if(orig != hudCvars[i].cvar->integer){
										trap_Cvar_Set(hudCvars[i].name, va("%d", hudCvars[i].cvar->integer));
									}
									break;
								}
							case CVT_FLOAT:
								{
									float orig = hudCvars[i].cvar->value;
									igSliderFloat(va("##%s", hudCvars[i].name), &hudCvars[i].cvar->value, hudCvars[i].minFloatValue, hudCvars[i].maxFloatValue, "%.02f", ImGuiSliderFlags_None);
									if(orig != hudCvars[i].cvar->value){
										trap_Cvar_Set(hudCvars[i].name, va("%.04f", hudCvars[i].cvar->value));
									}
									break;
								}
							case CVT_STRING:
								{
									char orig[MAX_CVAR_VALUE_STRING];
									Q_strncpyz(orig, hudCvars[i].cvar->string, sizeof(orig));
									igInputText(va("##%s", hudCvars[i].name), hudCvars[i].cvar->string, sizeof(hudCvars[i].cvar->string), 0, NULL, NULL);
									if(Q_stricmp(orig, hudCvars[i].cvar->string)){
										trap_Cvar_Set(hudCvars[i].name, va("%s", hudCvars[i].cvar->string));
									}
									break;
								}
							case CVT_COLOR:
								{
									vec4_t color, orig;
									Com_ParseHexColor(color, hudCvars[i].cvar->string, qtrue);
									Vector4Copy(color, orig);
									igColorEdit4(va("##%s", hudCvars[i].name), color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
									snprintf(hudCvars[i].cvar->string, sizeof(hudCvars[i].cvar->string), "%02X%02X%02X%02X", (int)(color[0] * 255.0f), (int)(color[1] * 255.0f), (int)(color[2] * 255.0f), (int)(color[3] * 255.0f));
									if(Vector4Compare(color, orig) == 0){
									 	trap_Cvar_Set(hudCvars[i].name, va("%s", hudCvars[i].cvar->string));
									}
									break;
								}
							default:
								break;
						}
					}
				}
				igEndTable();
			}
		
		}
		igEnd();
	}
	

}


void CG_ImGUI_Update(void) {
	if (cgs.igContext == NULL) {
		return;
	}
	igSetCurrentContext((ImGuiContext*)cgs.igContext);
	igSetAllocatorFunctions((ImGuiMemAllocFunc)cgs.igAlloc, (ImGuiMemFreeFunc)cgs.igFree, cgs.igUserData);


	ImGUIDemoWindow();
	DemoPlaybackTimeline();
	HudEditor();

}

static ImVec2 minBgRect, maxBgRect;
static ImVec2 minRect, maxRect;

qbool BeginTimeline(const char* str_id)
{
	minBgRect.y = FLT_MAX;
	minBgRect.x = FLT_MAX;
	maxBgRect.y = -FLT_MAX;
	maxBgRect.x = -FLT_MAX;
	minRect.y = FLT_MAX;
	minRect.x = FLT_MAX;
	maxRect.y = -FLT_MAX;
	maxRect.x = -FLT_MAX;
	return (qbool)igBeginChild_Str(str_id, (ImVec2){0.0f, 0.0f}, ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_AutoResizeY, 0);
	
}




qbool TimelineEvent(const char* str_id, float* values, int numVals)
{
	ImGuiWindow* win = igGetCurrentWindow();
	ImGuiContext *ctx = igGetCurrentContext();
	
	const ImU32 inactive_color = igColorConvertFloat4ToU32(ctx->Style.Colors[ImGuiCol_Button]);
	const ImU32 active_color = igColorConvertFloat4ToU32(ctx->Style.Colors[ImGuiCol_ButtonHovered]);
	const ImU32 background = 0xFF26382A;
	qbool changed = qfalse;
	ImVec2 localMin = win->DC.CursorPos;
	
	ImVec2 freeSpace;
	igGetContentRegionAvail(&freeSpace);
	//ImVec2 localMax = (ImVec2){ win->DC.CursorPos.x + freeSpace.x - 2 * TIMELINE_RADIUS - (2*delta),  win->DC.CursorPos.y + (2 * TIMELINE_RADIUS) + delta };
	ImVec2 localMax = (ImVec2){ win->DC.CursorPos.x + freeSpace.x,  win->DC.CursorPos.y + (2 * TIMELINE_RADIUS) + 2 * delta };
	ImDrawList_AddRectFilled(igGetWindowDrawList(), localMin, localMax, background, 0.0f, 0);

	for (int i = 0; i < numVals; i++)
	{
		ImVec2 pos = localMin;
		pos.x += TIMELINE_RADIUS + delta + (freeSpace.x - 2 * TIMELINE_RADIUS - (2*delta)) * values[i];
		pos.y += TIMELINE_RADIUS + delta;

		igSetCursorScreenPos((ImVec2){pos.x - TIMELINE_RADIUS, pos.y - TIMELINE_RADIUS - delta});
		igPushID_Int(i); 
		if (igInvisibleButton(str_id, (ImVec2) { 2 * TIMELINE_RADIUS, 2 * TIMELINE_RADIUS + 2 * delta }, 0)) {
			CG_NDP_SeekAbsolute(max(values[i] * (m_lastServerTime - m_firstServerTime) + m_firstServerTime - 2000, m_firstServerTime));
		}
		if (igIsItemActive() || igIsItemHovered(0))
		{
			int currentOffset = values[i] * (m_lastServerTime - m_firstServerTime);
			igSetTooltip("%s", GetStringTimestamp(currentOffset / 1000)); 
		}
		
		igPopID();
		ImDrawList_AddCircleFilled(igGetWindowDrawList(), pos, TIMELINE_RADIUS, igIsItemActive() || igIsItemHovered(0) ? active_color : inactive_color, 0);
		
	}

	minBgRect.x = min(minBgRect.x, localMin.x);
	minBgRect.y = min(minBgRect.y, localMin.y);
	maxBgRect.x = max(maxBgRect.x, localMax.x);
	maxBgRect.y = max(maxBgRect.y, localMax.y);


	localMin.x += TIMELINE_RADIUS + delta;
	localMin.y += delta;
	localMax.x -= TIMELINE_RADIUS + delta;
	localMax.y -= delta;

	minRect.x = min(minRect.x, localMin.x);
	minRect.y = min(minRect.y, localMin.y);
	maxRect.x = max(maxRect.x, localMax.x);
	maxRect.y = max(maxRect.y, localMax.y);

	return changed;
}


void EndTimeline(void)
{
	ImGuiWindow* win = igGetCurrentWindow();
	int demoDuration = (m_lastServerTime - m_firstServerTime);
	int currentOffset = (m_currServerTime - m_firstServerTime);
	float pc = (float)currentOffset / (float)demoDuration;
	const ImU32 color = 0xFF0000FF;
	float barWidth = 2.0f;
	ImVec2 localMin, localMax;
	localMin.x = minRect.x;
	localMin.y = win->DC.CursorPos.y;
	localMax.x = localMin.x +  pc * (maxRect.x - minRect.x);
	localMax.y = win->DC.CursorPos.y + TIMELINE_RADIUS;

	//igSetCursorScreenPos((ImVec2){localMin.x - TIMELINE_RADIUS, localMin.y - TIMELINE_RADIUS - delta});
	//igDummy((ImVec2) { 2 * TIMELINE_RADIUS, 2 * TIMELINE_RADIUS + 2 * delta });
	igDummy((ImVec2) { maxBgRect.x - minBgRect.x, TIMELINE_RADIUS});
	ImDrawList_AddRectFilled(igGetWindowDrawList(), localMin, localMax, color, 0.0f, 0);

	localMin.x = minRect.x + pc * (maxRect.x - minRect.x) - 0.5f * barWidth;
	localMin.y = minBgRect.y ;
	localMax.x = localMin.x + 0.5f * barWidth;
	localMax.y = maxBgRect.y + 2 * delta;
	
	ImDrawList_AddRectFilled(igGetWindowDrawList(),localMin, localMax, color, 0.0f, 0);
	igEndChild();
}
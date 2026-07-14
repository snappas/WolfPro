#include "../client/cl_imgui.h"
#include "rhi.h"
#include "tr_local.h"

#include "shaders/imgui_ps.h"
#include "shaders/imgui_vs.h"

#define MAX_IMGUI_VERTS (1<<18)
#define MAX_IMGUI_INDICES (MAX_IMGUI_VERTS * 3)


rhiPipeline ImGUIpipeline;
rhiBuffer imGUIvertexBuffers[RHI_FRAMES_IN_FLIGHT];
rhiBuffer imGUIindexBuffers[RHI_FRAMES_IN_FLIGHT];
int imGUIfontAtlasIndex;

#pragma pack(push,1)
typedef struct vertexPC {
    vec2_t scale;
    vec2_t bias;
} vertexPC;

typedef struct pixelPC {
    uint32_t texIndex;
    uint32_t samplerIndex;
} pixelPC;
#pragma pack(pop)

void RB_ImGUI_Init(void){
    assert(sizeof(ImDrawIdx) == 4);
    ImGuiIO* io = igGetIO();
    io->DisplaySize.x = glConfig.vidWidth;
    io->DisplaySize.y = glConfig.vidHeight;
    io->BackendRendererName = "Wolfenstein";
    io->BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    
    rhiGraphicsPipelineDesc desc = {};
    desc.name = "ImGUI";
    desc.attributeCount = 3;
    desc.attributes[0].bufferBinding = 0;
    desc.attributes[1].bufferBinding = 0;
    desc.attributes[2].bufferBinding = 0;
    desc.attributes[0].elementCount = 2;
    desc.attributes[1].elementCount = 2;
    desc.attributes[2].elementCount = 4;
    desc.attributes[0].elementFormat = RHI_VertexFormat_Float32;
    desc.attributes[1].elementFormat = RHI_VertexFormat_Float32;
    desc.attributes[2].elementFormat = RHI_VertexFormat_UNorm8;
    desc.attributes[0].offset = offsetof(ImDrawVert, pos);
    desc.attributes[1].offset = offsetof(ImDrawVert, uv);
    desc.attributes[2].offset = offsetof(ImDrawVert, col);
    desc.vertexBufferCount = 1;
    desc.vertexBuffers[0].stride = sizeof(ImDrawVert);

    desc.colorFormat = R8G8B8A8_UNorm;
    desc.cullType = CT_TWO_SIDED;

    desc.dstBlend = GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
    desc.srcBlend = GLS_SRCBLEND_SRC_ALPHA;

    desc.pixelShader.data = imgui_ps;
    desc.pixelShader.byteCount = sizeof(imgui_ps);
    desc.vertexShader.data = imgui_vs;
    desc.vertexShader.byteCount = sizeof(imgui_vs);

    desc.pushConstants.vsBytes = sizeof(vertexPC);
    desc.pushConstants.psBytes = sizeof(pixelPC);

    desc.descLayout = backEnd.descriptorSetLayout;

    ImGUIpipeline = RHI_CreateGraphicsPipeline(&desc);

    for(int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++){
        rhiBufferDesc vertexBufferDesc = {};
        vertexBufferDesc.allowedStates = RHI_ResourceState_VertexBufferBit;
        vertexBufferDesc.byteCount = MAX_IMGUI_VERTS * sizeof(ImDrawVert);
        vertexBufferDesc.initialState = RHI_ResourceState_VertexBufferBit;
        vertexBufferDesc.memoryUsage = RHI_MemoryUsage_Upload;
        vertexBufferDesc.name = va("ImGUI vertex %d", i);
        imGUIvertexBuffers[i] = RHI_CreateBuffer(&vertexBufferDesc);

        rhiBufferDesc indexBufferDesc = {};
        indexBufferDesc.allowedStates = RHI_ResourceState_IndexBufferBit;
        indexBufferDesc.byteCount = MAX_IMGUI_INDICES * sizeof(ImDrawIdx);
        indexBufferDesc.initialState = RHI_ResourceState_IndexBufferBit;
        indexBufferDesc.memoryUsage = RHI_MemoryUsage_Upload;
        indexBufferDesc.name = va("ImGUI index %d", i);
        imGUIindexBuffers[i] = RHI_CreateBuffer(&indexBufferDesc);
    }
    unsigned char *pixels;
    int width, height;
    ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &width, &height, NULL);

    rhiTextureDesc textureDesc = {};
    textureDesc.allowedStates = RHI_ResourceState_CopyDestinationBit | RHI_ResourceState_ShaderInputBit;
    textureDesc.initialState = RHI_ResourceState_CopyDestinationBit;
    textureDesc.format = R8G8B8A8_UNorm;
    textureDesc.height = height;
    textureDesc.width = width;
    textureDesc.mipCount = 1;
    textureDesc.name = "Font Atlas";
    textureDesc.sampleCount = 1;

    rhiTexture imGUIfontAtlas = RHI_CreateTexture(&textureDesc);
    
    
    rhiTextureUpload textureUpload;
    rhiTextureUploadDesc uploadDesc = {};
    uploadDesc.handle = imGUIfontAtlas;
    RHI_BeginTextureUpload(&textureUpload, &uploadDesc);
    for(int i = 0; i < height; i++){
        memcpy(textureUpload.data + textureUpload.rowPitch * i, pixels + width * 4 * i, width * 4);
    }
    RHI_EndTextureUpload();
    imGUIfontAtlasIndex = tr.textureDescriptorCount++;
    RHI_UpdateDescriptorSet(backEnd.descriptorSet, 0, RHI_DescriptorType_ReadOnlyTexture, imGUIfontAtlasIndex, 1, &imGUIfontAtlas, 0);

    ImFontAtlas_SetTexID(io->Fonts, (ImTextureID)imGUIfontAtlasIndex);

}

void RB_ImGUI_BeginFrame(void){
    igNewFrame();
}


void RB_ImGUI_Draw(rhiTexture renderTarget){
    if(r_debugUI->integer == 0){
        igEndFrame();
        return;
    }
    ImGuiIO* io = igGetIO();
    io->DisplaySize.x = glConfig.vidWidth;
    io->DisplaySize.y = glConfig.vidHeight;
   
    igEndFrame();
    igRender();
    ImDrawData *drawData = igGetDrawData();
    if(drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f || drawData->CmdListsCount <= 0 ){
        return;
    }

    PROF_BEGIN( "RB_ImGUI_Draw" );

    rhiBuffer currentVB = imGUIvertexBuffers[backEnd.currentFrameIndex];
    rhiBuffer currentIB = imGUIindexBuffers[backEnd.currentFrameIndex];
    ImDrawVert* vertices = (ImDrawVert*)RHI_MapBuffer(currentVB);
    ImDrawIdx* indices = (ImDrawIdx*)RHI_MapBuffer(currentIB);
#ifdef _DEBUG
    ImDrawVert* verticesEnd = vertices + MAX_IMGUI_VERTS;
    ImDrawIdx* indicesEnd = indices + MAX_IMGUI_INDICES;
#endif

    for(int i = 0; i < drawData->CmdListsCount; i++){
        ImDrawList *draw = drawData->CmdLists.Data[i];
        assert(vertices + draw->VtxBuffer.Size <= verticesEnd);
        assert(indices  + draw->IdxBuffer.Size <= indicesEnd);
        memcpy(vertices, draw->VtxBuffer.Data, draw->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(indices,  draw->IdxBuffer.Data, draw->IdxBuffer.Size * sizeof(ImDrawIdx));
        vertices += draw->VtxBuffer.Size;
        indices += draw->IdxBuffer.Size;
    }
    RHI_UnmapBuffer(currentVB);
    RHI_UnmapBuffer(currentIB);

    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(renderTarget, RHI_ResourceState_RenderTargetBit);
    RHI_CmdEndBarrier();

    RHI_RenderPass renderPass = {};
    renderPass.colorLoad = RHI_LoadOp_Load;
    renderPass.colorTexture = renderTarget;
    RB_BeginRenderPass("ImGUI", &renderPass);

    RHI_CmdBindPipeline(ImGUIpipeline);
    RHI_CmdBindDescriptorSet(ImGUIpipeline, backEnd.descriptorSet);
    RHI_CmdBindVertexBuffers(&currentVB, 1);
    RHI_CmdBindIndexBuffer(currentIB);
    RHI_CmdSetViewport(0,0,glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f);

    vertexPC vPC;
    vPC.scale[0] = 2.0f / drawData->DisplaySize.x;
    vPC.scale[1] = 2.0f / drawData->DisplaySize.y;


    vPC.bias[0] = -1.0f - drawData->DisplayPos.x * vPC.scale[0];
    vPC.bias[1] = -1.0f - drawData->DisplayPos.y * vPC.scale[1];

    RHI_CmdPushConstants(ImGUIpipeline, RHI_Shader_Vertex, &vPC, sizeof(vertexPC));

    int globalVtxOffset = 0;
    int globalIdxOffset = 0;
    ImVec2 clipOff = drawData->DisplayPos;

    for(int i = 0; i < drawData->CmdListsCount; i++){

        ImDrawList *draw = drawData->CmdLists.Data[i];
        for(int c = 0; c < draw->CmdBuffer.Size; c++){
            ImDrawCmd *cmd = &draw->CmdBuffer.Data[c];
            
            ImVec2 clip_min = { cmd->ClipRect.x - clipOff.x, cmd->ClipRect.y - clipOff.y };
            ImVec2 clip_max = { cmd->ClipRect.z - clipOff.x, cmd->ClipRect.w - clipOff.y };
            if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
            {
                continue;
            }
            RHI_CmdSetScissor(clip_min.x, clip_min.y, clip_max.x - clip_min.x, clip_max.y - clip_min.y);
            pixelPC pPC;
            pPC.samplerIndex = RB_GetSamplerIndex(qfalse, qfalse);
            pPC.texIndex = (uint32_t)cmd->TextureId;
            RHI_CmdPushConstants(ImGUIpipeline, RHI_Shader_Pixel, &pPC, sizeof(pPC));
            RHI_CmdDrawIndexed(cmd->ElemCount, cmd->IdxOffset + globalIdxOffset, cmd->VtxOffset + globalVtxOffset);
        }
        globalIdxOffset += draw->IdxBuffer.Size;
        globalVtxOffset += draw->VtxBuffer.Size;

    }
    RB_EndRenderPass();

    PROF_END();
}

void RE_DrawMainMenuBarInfo(void)
{
	static uint32_t frameTimes[16];
	static uint32_t frameCount = 0;
	static int displayedFPS = 0;

	// frame times can be 0 in some cases
	if(rhie.presentToPresentUS > 0)
	{
		frameTimes[frameCount++] = rhie.presentToPresentUS;
	}
	else
	{
		frameCount = 0;
	}
	if(frameCount == ARRAY_LEN(frameTimes))
	{
		uint32_t sum = 0;
		for(uint32_t i = 0; i < ARRAY_LEN(frameTimes); ++i)
		{
			sum += frameTimes[i];
		}
		sum /= ARRAY_LEN(frameTimes);
		displayedFPS = (int)((1000000 + (sum >> 1)) / sum);
		frameCount = 0;
	}

		const char* const info = va(
		"%s | %3d FPS",
		RHI_GetDeviceName(), displayedFPS);
	//const float offset = igGetWindowWidth() - igCalcTextSize("___").x - igCalcTextSize(info).x; igCalcTextSize()
	//igSameLine(offset);
	igText(info);
}


void RE_GUI_Image(qhandle_t handle, float x, float y){
    shader_t *result = R_GetShaderByHandle(handle);
    if(result->stages[0] != NULL && result->stages[0]->active){
        image_t *image = result->stages[0]->bundle[0].image[0];
        if(image != NULL){
            igImage((ImTextureID)image->descriptorIndex, (ImVec2){x, y}, (ImVec2){0.0f, 0.0f},
                     (ImVec2){1.0f, 1.0f}, (ImVec4){1.0f, 1.0f, 1.0f, 1.0f}, (ImVec4){1.0f, 1.0f, 1.0f, 0.0f});
        }
    }
}

void RE_GUI_Image_Ex(qhandle_t handle, float x, float y, float s1, float t1, float s2, float t2){
    shader_t *result = R_GetShaderByHandle(handle);
    if(result->stages[0] != NULL && result->stages[0]->active){
        image_t *image = result->stages[0]->bundle[0].image[0];
        if(image != NULL){
            igImage((ImTextureID)image->descriptorIndex, (ImVec2){x, y}, (ImVec2){s1, t1},
                     (ImVec2){s2, t2}, (ImVec4){1.0f, 1.0f, 1.0f, 1.0f}, (ImVec4){1.0f, 1.0f, 1.0f, 0.0f});
        }
    }
}
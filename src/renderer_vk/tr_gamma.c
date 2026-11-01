#include "tr_local.h"
#include "rhi.h"
#include "shaders/gamma_ps.h"
#include "shaders/gamma_vs.h"


#pragma pack(push,1)

typedef struct GammaPC {
    float invGamma;
    float brightness;
} GammaPC;

#pragma pack(pop)

rhiDescriptorSetLayout descSetLayout;
rhiPipeline gammaPipeline;
rhiDescriptorSet gammaDescSet[2];

void RB_InitGamma(void){
    rhiDescriptorSetLayoutDesc descriptorSetLayoutDesc = {};
    descriptorSetLayoutDesc.name = "Gamma Desc Set Layout";
    descriptorSetLayoutDesc.bindingCount = 2;
    descriptorSetLayoutDesc.bindings[0].descriptorCount = 1;
    descriptorSetLayoutDesc.bindings[0].descriptorType = RHI_DescriptorType_ReadOnlyTexture;
    descriptorSetLayoutDesc.bindings[0].stageFlags = RHI_PipelineStage_PixelBit;

    descriptorSetLayoutDesc.bindings[1].descriptorCount = 1;
    descriptorSetLayoutDesc.bindings[1].descriptorType = RHI_DescriptorType_Sampler;
    descriptorSetLayoutDesc.bindings[1].stageFlags = RHI_PipelineStage_PixelBit;

    descSetLayout = RHI_CreateDescriptorSetLayout(&descriptorSetLayoutDesc);

    rhiGraphicsPipelineDesc desc = {};
    desc.name = "Gamma";
    desc.colorFormat = R8G8B8A8_UNorm;
    desc.cullType = CT_TWO_SIDED;
    desc.noDepthAttachment = qtrue;
    desc.descLayout = descSetLayout;

    desc.dstBlend = GLS_DSTBLEND_ZERO;
    desc.srcBlend = GLS_SRCBLEND_ONE;
    desc.pixelShader.data = gamma_ps;
    desc.pixelShader.byteCount = sizeof(gamma_ps);

    desc.vertexShader.data = gamma_vs;
    desc.vertexShader.byteCount = sizeof(gamma_vs);

    desc.pushConstants.psBytes = sizeof(GammaPC);

    gammaPipeline = RHI_CreateGraphicsPipeline(&desc);
    gammaDescSet[0] = RHI_CreateDescriptorSet("Gamma 1", descSetLayout, qfalse);
    gammaDescSet[1] = RHI_CreateDescriptorSet("Gamma 2", descSetLayout, qfalse);

    
}

void RB_DrawGamma(rhiTexture texture, rhiSampler sampler, rhiTexture renderTarget){
    RB_EndRenderPass();

    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(renderTarget, RHI_ResourceState_RenderTargetBit);
    RHI_CmdTextureBarrier(texture, RHI_ResourceState_ShaderInputBit);
    RHI_CmdEndBarrier();

    RHI_RenderPass renderPass = {};
    renderPass.colorLoad = RHI_LoadOp_Discard;
    renderPass.colorTexture = renderTarget;
    RB_BeginRenderPass("Gamma", &renderPass);

    GammaPC gammaPC;
    gammaPC.brightness = 1.0f;
    gammaPC.invGamma = 1.0f / r_gamma->value;

    RHI_CmdPushConstants(gammaPipeline, RHI_Shader_Pixel, &gammaPC, sizeof(GammaPC));

    RHI_CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f);
    RHI_CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
    RHI_CmdBindPipeline(gammaPipeline);

    rhiDescriptorSet set = gammaDescSet[backEnd.currentFrameIndex];
    RHI_UpdateDescriptorSet(set, 0, RHI_DescriptorType_ReadOnlyTexture, 0, 1, &texture, 0);
    RHI_UpdateDescriptorSet(set, 1, RHI_DescriptorType_Sampler, 0, 1, &sampler, 0);

    RHI_CmdBindDescriptorSet(gammaPipeline, set);
    RHI_CmdDraw(3, 0);

    RB_EndRenderPass();
}
#include "tr_local.h"
#include "rhi.h"
#include "shaders/blit_ps.h"
#include "shaders/blit_vs.h"

rhiDescriptorSetLayout blitDescSetLayout;
rhiPipeline blitPipeline;
rhiDescriptorSet blitDescSet[2];

void RB_InitBlit(void){
    rhiDescriptorSetLayoutDesc descriptorSetLayoutDesc = {};
    descriptorSetLayoutDesc.name = "Blit Desc Set Layout";
    descriptorSetLayoutDesc.bindingCount = 2;
    descriptorSetLayoutDesc.bindings[0].descriptorCount = 1;
    descriptorSetLayoutDesc.bindings[0].descriptorType = RHI_DescriptorType_ReadOnlyTexture;
    descriptorSetLayoutDesc.bindings[0].stageFlags = RHI_PipelineStage_PixelBit;

    descriptorSetLayoutDesc.bindings[1].descriptorCount = 1;
    descriptorSetLayoutDesc.bindings[1].descriptorType = RHI_DescriptorType_Sampler;
    descriptorSetLayoutDesc.bindings[1].stageFlags = RHI_PipelineStage_PixelBit;

    blitDescSetLayout = RHI_CreateDescriptorSetLayout(&descriptorSetLayoutDesc);

    rhiGraphicsPipelineDesc desc = {};
    desc.name = "Blit";
    desc.colorFormat = RHI_GetSwapChainFormat();
    desc.cullType = CT_TWO_SIDED;
    desc.descLayout = blitDescSetLayout;

    desc.dstBlend = GLS_DSTBLEND_ZERO;
    desc.srcBlend = GLS_SRCBLEND_ONE;
    desc.pixelShader.data = blit_ps;
    desc.pixelShader.byteCount = sizeof(blit_ps);

    desc.vertexShader.data = blit_vs;
    desc.vertexShader.byteCount = sizeof(blit_vs);

    blitPipeline = RHI_CreateGraphicsPipeline(&desc);
    blitDescSet[0] = RHI_CreateDescriptorSet("Blit", blitDescSetLayout, qfalse);
    blitDescSet[1] = RHI_CreateDescriptorSet("Blit", blitDescSetLayout, qfalse);

    
}

void RB_DrawBlit(rhiTexture texture, rhiSampler sampler, rhiTexture swapChainImage){
    RB_EndRenderPass();

    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(swapChainImage, RHI_ResourceState_RenderTargetBit);
    RHI_CmdTextureBarrier(texture, RHI_ResourceState_ShaderInputBit);
    RHI_CmdEndBarrier();

    qbool isBlitted = r_fullscreen->integer && !r_fullscreenDesktop->integer;
    RHI_RenderPass renderPass = {};
    renderPass.colorLoad = isBlitted ? RHI_LoadOp_Clear : RHI_LoadOp_Discard ;
    renderPass.colorTexture = swapChainImage;
    Vector4Set(renderPass.color, 0, 0, 0, 1);
    renderPass.width = glInfo.winWidth;
    renderPass.height = glInfo.winHeight;
    RB_BeginRenderPass("Blit", &renderPass);

    if(isBlitted){
        if (r_fullscreenStretch->integer) {
            RHI_CmdSetViewport(0, 0, glInfo.winWidth, glInfo.winHeight, 0.0f, 1.0f);
            RHI_CmdSetScissor(0, 0, glInfo.winWidth, glInfo.winHeight);
        }
        else {
            uint32_t x = (glInfo.winWidth - glConfig.vidWidth) / 2;
            uint32_t y = (glInfo.winHeight - glConfig.vidHeight) / 2;
            RHI_CmdSetViewport(x, y, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f);
            RHI_CmdSetScissor(x, y, glConfig.vidWidth, glConfig.vidHeight);
        }
    }else{
        RHI_CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight, 0.0f, 1.0f);
        RHI_CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
    }

    rhiDescriptorSet set = blitDescSet[backEnd.currentFrameIndex];
    RHI_UpdateDescriptorSet(set, 0, RHI_DescriptorType_ReadOnlyTexture, 0, 1, &texture, 0);
    RHI_UpdateDescriptorSet(set, 1, RHI_DescriptorType_Sampler, 0, 1, &sampler, 0);

    RHI_CmdBindPipeline(blitPipeline);
    RHI_CmdBindDescriptorSet(blitPipeline, set);
    RHI_CmdDraw(3, 0);

    RB_EndRenderPass();
}
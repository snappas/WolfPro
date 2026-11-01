#include "rhi.h"
#include "tr_vulkan.h"
#include "../client/cl_imgui.h"

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger)
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if(func != NULL){
        func(instance, messenger, NULL);
    }
}

#define MAX_CACHED_RENDER_PASSES 32
#define MAX_CACHED_FRAMEBUFFERS 64

typedef struct CachedRenderPass {
    VkRenderPass pass;
    VkFormat color;
    VkFormat depth;
    VkSampleCountFlagBits samples;
    VkAttachmentLoadOp colorLoad;
    VkAttachmentLoadOp depthLoad;
} CachedRenderPass;

typedef struct CachedFramebuffer {
    VkFramebuffer framebuffer;
    VkRenderPass pass;
    VkImageView color;
    VkImageView depth;
    uint32_t width;
    uint32_t height;
} CachedFramebuffer;

static CachedRenderPass s_renderPasses[MAX_CACHED_RENDER_PASSES];
static int s_renderPassCount = 0;
static CachedFramebuffer s_framebuffers[MAX_CACHED_FRAMEBUFFERS];
static int s_framebufferCount = 0;

// Attachments stay in the layouts the barriers set, and the barrier before
// each pass orders it against earlier work, so no subpass dependency is needed.
static VkRenderPass GetRenderPass( VkFormat color, VkFormat depth, VkSampleCountFlagBits samples, VkAttachmentLoadOp colorLoad, VkAttachmentLoadOp depthLoad )
{
    if(depth == VK_FORMAT_UNDEFINED){
        depthLoad = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    for(int i = 0; i < s_renderPassCount; i++){
        const CachedRenderPass *c = &s_renderPasses[i];
        if(c->color == color && c->depth == depth && c->samples == samples && c->colorLoad == colorLoad && c->depthLoad == depthLoad){
            return c->pass;
        }
    }
    if(s_renderPassCount >= MAX_CACHED_RENDER_PASSES){
        ri.Error(ERR_FATAL, "Render pass cache is full (%d of %d, color format %d, depth format %d, %d samples)\n",
            s_renderPassCount, MAX_CACHED_RENDER_PASSES, (int)color, (int)depth, (int)samples);
    }

    VkAttachmentDescription attachments[2] = {};
    attachments[0].format = color;
    attachments[0].samples = samples;
    attachments[0].loadOp = colorLoad;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = depth;
    attachments[1].samples = samples;
    attachments[1].loadOp = depthLoad;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthRef = {};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = depth != VK_FORMAT_UNDEFINED ? &depthRef : NULL;

    VkRenderPassCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = depth != VK_FORMAT_UNDEFINED ? 2 : 1;
    createInfo.pAttachments = attachments;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;

    CachedRenderPass *entry = &s_renderPasses[s_renderPassCount];
    VK(vkCreateRenderPass(vk.device, &createInfo, NULL, &entry->pass));
    entry->color = color;
    entry->depth = depth;
    entry->samples = samples;
    entry->colorLoad = colorLoad;
    entry->depthLoad = depthLoad;
    s_renderPassCount++;
    return entry->pass;
}

static VkFramebuffer GetFramebuffer( VkRenderPass pass, VkImageView color, VkImageView depth, uint32_t width, uint32_t height )
{
    for(int i = 0; i < s_framebufferCount; i++){
        const CachedFramebuffer *c = &s_framebuffers[i];
        if(c->pass == pass && c->color == color && c->depth == depth && c->width == width && c->height == height){
            return c->framebuffer;
        }
    }
    if(s_framebufferCount >= MAX_CACHED_FRAMEBUFFERS){
        ri.Error(ERR_FATAL, "Framebuffer cache is full (%d of %d, %d x %d)\n",
            s_framebufferCount, MAX_CACHED_FRAMEBUFFERS, (int)width, (int)height);
    }

    const VkImageView attachments[2] = { color, depth };
    VkFramebufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    createInfo.renderPass = pass;
    createInfo.attachmentCount = depth != VK_NULL_HANDLE ? 2 : 1;
    createInfo.pAttachments = attachments;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.layers = 1;

    CachedFramebuffer *entry = &s_framebuffers[s_framebufferCount];
    VK(vkCreateFramebuffer(vk.device, &createInfo, NULL, &entry->framebuffer));
    entry->pass = pass;
    entry->color = color;
    entry->depth = depth;
    entry->width = width;
    entry->height = height;
    s_framebufferCount++;
    return entry->framebuffer;
}

static void DestroyRenderPassCache( void )
{
    for(int i = 0; i < s_renderPassCount; i++){
        vkDestroyRenderPass(vk.device, s_renderPasses[i].pass, NULL);
    }
    s_renderPassCount = 0;
}

// The caller waits for the device first. A view handle can be reused after
// its destruction, so this must run whenever an attachment view is destroyed.
void DestroyFramebufferCache( void )
{
    for(int i = 0; i < s_framebufferCount; i++){
        vkDestroyFramebuffer(vk.device, s_framebuffers[i].framebuffer, NULL);
    }
    s_framebufferCount = 0;
}

// The swap chain extent can differ from the window size at any time, not
// only during a resize, so the area is clamped to the render target.
static VkRect2D GetRenderArea( const RHI_RenderPass *renderPass, const Texture *colorTexture )
{
    uint32_t width = glConfig.vidWidth;
    uint32_t height = glConfig.vidHeight;
    if(renderPass->width != 0 && renderPass->height != 0){
        width = renderPass->width;
        height = renderPass->height;
    }

    VkRect2D area;
    area.offset.x = renderPass->x;
    area.offset.y = renderPass->y;
    area.extent.width = min(width, colorTexture->desc.width - renderPass->x);
    area.extent.height = min(height, colorTexture->desc.height - renderPass->y);
    return area;
}

static void BeginClassicRenderPass( const RHI_RenderPass *renderPass )
{
    Texture *colorTexture = GET_TEXTURE(renderPass->colorTexture);
    Texture *depthTexture = renderPass->depthTexture.h != 0 ? GET_TEXTURE(renderPass->depthTexture) : NULL;
    assert(colorTexture->currentLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    assert(depthTexture == NULL || depthTexture->currentLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    const VkRenderPass pass = GetRenderPass(colorTexture->format,
        depthTexture != NULL ? depthTexture->format : VK_FORMAT_UNDEFINED,
        GetVkSampleCount(colorTexture->desc.sampleCount),
        GetVkAttachmentLoadOp(renderPass->colorLoad),
        GetVkAttachmentLoadOp(renderPass->depthLoad));

    VkClearValue clearValues[2] = {};
    memcpy(clearValues[0].color.float32, renderPass->color, sizeof(float) * 4);
    clearValues[1].depthStencil.depth = renderPass->depth;

    VkRenderPassBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = pass;
    beginInfo.framebuffer = GetFramebuffer(pass, colorTexture->view, depthTexture != NULL ? depthTexture->view : VK_NULL_HANDLE, colorTexture->desc.width, colorTexture->desc.height);
    beginInfo.clearValueCount = depthTexture != NULL ? 2 : 1;
    beginInfo.pClearValues = clearValues;
    beginInfo.renderArea = GetRenderArea(renderPass, colorTexture);

    vkCmdBeginRenderPass(vk.activeCommandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

extern qbool crashing;
void RHI_Shutdown(qboolean destroyWindow)
{
    // a fatal error during RHI_Init lands here with Vulkan partly set up
    if(!vk.initialized){
        if(destroyWindow){
            Sys_Vulkan_Shutdown();
        }
        return;
    }

    vkDeviceWaitIdle(vk.device);

    DestroyFramebufferCache();
    DestroyRenderPassCache();

    //destroy user resources
    PoolIterator it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.samplerPool, &it)){
        Sampler *sampler = (Sampler*)it.value;
        vkDestroySampler(vk.device, sampler->sampler, NULL);
    }
    Pool_Clear(&vk.samplerPool);

    //

    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.bufferPool, &it)){
        Buffer *buffer = (Buffer*)it.value;
        if(!buffer->desc.longLifetime || destroyWindow){
            
            vmaDestroyBuffer(vk.allocator, buffer->buffer, buffer->allocation);
            
            Pool_Remove(&vk.bufferPool, it.handle);
             
        }
    }
    
    Pool_ClearUnused(&vk.bufferPool);


    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.semaphorePool, &it)){
        Semaphore *semaphore = (Semaphore*)it.value;
        if(!semaphore->longLifetime || destroyWindow){
            vkDestroySemaphore(vk.device, semaphore->semaphore, NULL);
            Pool_Remove(&vk.semaphorePool, it.handle);
        }
    }
    Pool_ClearUnused(&vk.semaphorePool);
    
    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.descriptorSetLayoutPool, &it)){
        DescriptorSetLayout *descSetLayout = (DescriptorSetLayout*)it.value;
        if(!descSetLayout->desc.longLifetime || destroyWindow){
            vkDestroyDescriptorSetLayout(vk.device,descSetLayout->layout, NULL);
            Pool_Remove(&vk.descriptorSetLayoutPool, it.handle);
        }
    }
    Pool_ClearUnused(&vk.descriptorSetLayoutPool);

    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.descriptorSetPool, &it)){
        DescriptorSet *descSet = (DescriptorSet*)it.value;
        if(!descSet->longLifetime || destroyWindow){
            vkFreeDescriptorSets(vk.device, vk.descriptorPool, 1, &descSet->set);
            Pool_Remove(&vk.descriptorSetPool, it.handle);
        }
    }
    Pool_ClearUnused(&vk.descriptorSetPool);
    
    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.pipelinePool, &it)){
        Pipeline *pipeline = (Pipeline*)it.value;
        qbool longLifetime = pipeline->compute ? pipeline->computeDesc.longLifetime : pipeline->graphicsDesc.longLifetime;
        if(!longLifetime || destroyWindow){
            vkDestroyPipelineLayout(vk.device, pipeline->layout.pipelineLayout, NULL);
            vkDestroyPipeline(vk.device,pipeline->pipeline, NULL);
            Pool_Remove(&vk.pipelinePool, it.handle);
        }
    }
    Pool_ClearUnused(&vk.pipelinePool);

    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.commandBufferPool, &it)){
        CommandBuffer *commandBuffer = (CommandBuffer*)it.value;
        if(!commandBuffer->longLifetime || destroyWindow){
            vkFreeCommandBuffers(vk.device, commandBuffer->commandPool, 1, &commandBuffer->commandBuffer);
            Pool_Remove(&vk.commandBufferPool, it.handle);
        }
    }
    Pool_ClearUnused(&vk.commandBufferPool);

    it = Pool_BeginIteration();
    while(Pool_Iterate(&vk.texturePool, &it)){
        Texture *texture = (Texture*)it.value;
        
        if(!texture->desc.longLifetime || destroyWindow){
            if(!texture->desc.nativeImage){
                vmaDestroyImage(vk.allocator, texture->image, texture->allocation);
            }
            vkDestroyImageView(vk.device, texture->view, NULL);
            for(int i = 0; i < texture->desc.mipCount; i++){
                vkDestroyImageView(vk.device, texture->mipViews[i], NULL); 
            }
            Pool_Remove(&vk.texturePool, it.handle);
        }
    }
    Pool_ClearUnused(&vk.texturePool);

    
    
    if(destroyWindow){
        //destroy private resources
        for(int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++){
            vkDestroyQueryPool(vk.device, vk.queryPool[i], NULL);
        }
        vkDestroyPipelineCache(vk.device, vk.pipelineCache, NULL);
        vk.pipelineCache = VK_NULL_HANDLE;
        vkDestroyDescriptorPool(vk.device, vk.descriptorPool, NULL);
        vkDestroyCommandPool(vk.device, vk.commandPool, NULL);
        vkDestroySwapchainKHR(vk.device, vk.swapChain, NULL);
        vmaDestroyAllocator(vk.allocator);
        vkDestroyDevice(vk.device, NULL);
        vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
        if (vk.ext.EXT_debug_utils){
            DestroyDebugUtilsMessengerEXT(vk.instance, vk.ext.debugMessenger);
        }
        vkDestroyInstance(vk.instance, NULL);
        Sys_Vulkan_Shutdown();
        vk.initialized = qfalse;
    }

}


void RHI_WaitUntilDeviceIdle()
{
}

rhiSemaphore RHI_CreateBinarySemaphore( void )
{
    VkSemaphore semaphore;
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK(vkCreateSemaphore(vk.device, &createInfo, NULL, &semaphore));
    SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore, "binary semaphore");

    Semaphore binarySemaphore = {};
    binarySemaphore.signaled = qfalse;
    binarySemaphore.binary = qtrue;
    binarySemaphore.semaphore = semaphore;
    return (rhiSemaphore) { Pool_Add(&vk.semaphorePool, &binarySemaphore) };
}

rhiSemaphore RHI_CreateTimelineSemaphore( qboolean longLifetime )
{
    VkSemaphoreTypeCreateInfo timelineCreateInfo = {};
    timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCreateInfo.pNext = NULL;
    timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCreateInfo.initialValue = 0;


    VkSemaphore semaphore;
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.pNext = &timelineCreateInfo;
    VK(vkCreateSemaphore(vk.device, &createInfo, NULL, &semaphore));
    SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)semaphore, "binary semaphore");

    Semaphore timelineSemaphore = {};
    timelineSemaphore.signaled = qfalse;
    timelineSemaphore.binary = qfalse;
    timelineSemaphore.semaphore = semaphore;
    timelineSemaphore.longLifetime = longLifetime;
    return (rhiSemaphore) { Pool_Add(&vk.semaphorePool, &timelineSemaphore) };
}

void RHI_WaitOnSemaphore(rhiSemaphore semaphore, uint64_t semaphoreValue)
{
    VkSemaphore sem = GET_SEMAPHORE(semaphore)->semaphore;
    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.pNext = NULL;
    waitInfo.flags = 0;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &sem;
    waitInfo.pValues = &semaphoreValue;
    VK(vkWaitSemaphores(vk.device, &waitInfo, UINT64_MAX));
}

rhiBuffer RHI_CreateBuffer(const rhiBufferDesc *desc)
{
    assert(desc);
    assert(desc->byteCount > 0);
    assert(desc->name);
    assert(desc->allowedStates != 0);
    assert(popcnt((uint32_t)desc->initialState) == 1);
    assert((desc->initialState & desc->allowedStates) != 0);
    VmaAllocationCreateInfo allocCreateInfo = {};
    allocCreateInfo.usage = GetVmaMemoryUsage(desc->memoryUsage); 
    if(desc->memoryUsage != RHI_MemoryUsage_DeviceLocal){
        allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.size = desc->byteCount;
    bufferInfo.usage = GetVkBufferUsageFlags(desc->allowedStates);

    VmaAllocation vmaAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo = {};

    VkBuffer newBuffer;
    VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &newBuffer, &vmaAlloc, &allocInfo));
    SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)newBuffer, desc->name);

    Buffer buffer = {};
    buffer.desc = *desc;
    buffer.buffer = newBuffer;
    buffer.allocation = vmaAlloc;
    buffer.mappedData = allocInfo.pMappedData;
    buffer.memoryUsage = desc->memoryUsage;
    buffer.currentLayout = desc->initialState;
    
    VkMemoryPropertyFlags memFlags;
	vmaGetMemoryTypeProperties(vk.allocator, allocInfo.memoryType, &memFlags);
	buffer.hostCoherent = (memFlags & (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) != 0;

    return (rhiBuffer) { Pool_Add(&vk.bufferPool, &buffer) };
}

byte* RHI_MapBuffer(rhiBuffer buffer)
{
    Buffer *currentBuffer = GET_BUFFER(buffer);
    assert(currentBuffer->mappedData);
    if(!(currentBuffer->hostCoherent) && currentBuffer->memoryUsage == RHI_MemoryUsage_Readback){
        vmaInvalidateAllocation(vk.allocator, currentBuffer->allocation, 0, VK_WHOLE_SIZE);
    }
    return (byte*)currentBuffer->mappedData;
}

void RHI_UnmapBuffer(rhiBuffer buffer)
{
    Buffer *currentBuffer = GET_BUFFER(buffer);
    assert(currentBuffer->mappedData);
    if(!(currentBuffer->hostCoherent) && (currentBuffer->memoryUsage == RHI_MemoryUsage_Upload || currentBuffer->memoryUsage == RHI_MemoryUsage_Staging)){
        vmaFlushAllocation(vk.allocator, currentBuffer->allocation, 0, VK_WHOLE_SIZE);
    }
}

void RHI_FlushBuffer(rhiBuffer buffer, uint32_t offset, uint32_t byteCount)
{
    Buffer *currentBuffer = GET_BUFFER(buffer);
    assert(currentBuffer->mappedData);
    if(!(currentBuffer->hostCoherent) && byteCount > 0){
        vmaFlushAllocation(vk.allocator, currentBuffer->allocation, offset, byteCount);
    }
}

rhiTexture RHI_CreateTexture(const rhiTextureDesc *desc)
{
    assert(desc);
    assert(desc->width > 0);
    assert(desc->height > 0);
    assert(desc->mipCount > 0);
    assert(desc->sampleCount > 0);
    assert(popcnt((uint32_t)desc->initialState) == 1);
    assert((desc->initialState & desc->allowedStates) != 0);

    const qbool ownsImage = (qbool)(desc->nativeImage == 0);
    

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFormat format = VK_FORMAT_UNDEFINED;

    if(!ownsImage)
    {
        image = (VkImage)desc->nativeImage;
        format = (VkFormat)desc->nativeFormat;
        layout = (VkImageLayout)desc->nativeLayout;
    }
    else
    {
        assert(desc->memoryUsage == RHI_MemoryUsage_DeviceLocal || desc->memoryUsage == RHI_MemoryUsage_Readback);
        format = GetVkFormat(desc->format);

		VmaAllocationCreateInfo allocCreateInfo = {};
		allocCreateInfo.usage = GetVmaMemoryUsage(desc->memoryUsage);

		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = desc->width;
		imageInfo.extent.height = desc->height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = desc->mipCount;
		imageInfo.arrayLayers = 1;
		imageInfo.format = format;
		imageInfo.tiling = desc->memoryUsage == RHI_MemoryUsage_Readback ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; 
        imageInfo.usage = GetVkImageUsageFlags(desc->allowedStates);
		imageInfo.samples = GetVkSampleCount(desc->sampleCount);
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK(vmaCreateImage(vk.allocator, &imageInfo, &allocCreateInfo, &image, &allocation, NULL));

		SetObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, desc->name);
    }

    
    VkImageView view;
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = GetVkImageAspectFlags(format);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK(vkCreateImageView(vk.device, &viewInfo, NULL, &view));
    SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, desc->name);

   

    Texture texture = {};
    texture.desc = *desc;
    texture.image = image;
    texture.view = view;
    texture.allocation = allocation;
    texture.ownsImage = ownsImage;
    texture.format = format;
    texture.currentLayout = layout;

    for(int i = 0; i < desc->mipCount; i++){
        VkImageView mipView;
        VkImageViewCreateInfo mipViewInfo = {};
        mipViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        mipViewInfo.image = image;
        mipViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        mipViewInfo.format = format;
        mipViewInfo.subresourceRange.aspectMask = GetVkImageAspectFlags(format);
        mipViewInfo.subresourceRange.baseMipLevel = i;
        mipViewInfo.subresourceRange.levelCount = 1;
        mipViewInfo.subresourceRange.baseArrayLayer = 0;
        mipViewInfo.subresourceRange.layerCount = 1;
        VK(vkCreateImageView(vk.device, &mipViewInfo, NULL, &mipView));
        SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)mipView, va("mip #%d: %s", i, desc->name));
        texture.mipViews[i] = mipView;
    }


    return (rhiTexture) { Pool_Add(&vk.texturePool, &texture) };
}

qbool RHI_IsIntegratedGPU( void )
{
    return vk.deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? qtrue : qfalse;
}

uint32_t RHI_GetMaxMSAASamples( void )
{
    const VkPhysicalDeviceLimits *l = &vk.deviceProperties.limits;
    const VkSampleCountFlags counts = l->framebufferColorSampleCounts & l->framebufferDepthSampleCounts
        & l->sampledImageColorSampleCounts & l->sampledImageDepthSampleCounts;
    if(counts & VK_SAMPLE_COUNT_8_BIT){
        return 8;
    }else if(counts & VK_SAMPLE_COUNT_4_BIT){
        return 4;
    }else if(counts & VK_SAMPLE_COUNT_2_BIT){
        return 2;
    }
    return 1;
}

uint32_t RHI_GetMaxAnisotropy( void )
{
    return vk.caps.maxAnisotropy >= 1.0f ? (uint32_t)vk.caps.maxAnisotropy : 1;
}

rhiSampler RHI_CreateSampler(const char* name, RHI_TextureAddressing mode, uint32_t anisotropy)
{
    VkSamplerAddressMode wrapMode = (mode == RHI_TextureAddressing_Repeat) ?  VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    VkSampler sampler;
	VkSamplerCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.addressModeU = wrapMode;
	createInfo.addressModeV = wrapMode;
	createInfo.addressModeW = wrapMode;
	createInfo.anisotropyEnable = (anisotropy > 1 && vk.caps.anisotropy) ? VK_TRUE : VK_FALSE;
	createInfo.maxAnisotropy = (float)anisotropy < vk.caps.maxAnisotropy ? (float)anisotropy : vk.caps.maxAnisotropy;
	if(!createInfo.anisotropyEnable){
		createInfo.maxAnisotropy = 1.0f;
	}
	createInfo.minFilter = VK_FILTER_LINEAR;
	createInfo.magFilter = VK_FILTER_LINEAR;
	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	createInfo.minLod = 0.0f;
	createInfo.maxLod = 666.0f;
	createInfo.mipLodBias = 0.0f;
	VK(vkCreateSampler(vk.device, &createInfo, NULL, &sampler));

	SetObjectName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, name);

    Sampler privateSampler;
    privateSampler.sampler = sampler;

    return (rhiSampler) { Pool_Add(&vk.samplerPool, &privateSampler) };
}

void RHI_CreateShader()
{
}





rhiDescriptorSetLayout RHI_CreateDescriptorSetLayout(const rhiDescriptorSetLayoutDesc *desc)
{
    VkDescriptorSetLayoutBinding bindings[ARRAY_LEN(desc->bindings)];
    VkDescriptorBindingFlags bindingFlags[ARRAY_LEN(desc->bindings)];

    for(int i = 0; i < desc->bindingCount; i++){
        assert(desc->bindings[i].stageFlags != 0);
        VkDescriptorSetLayoutBinding *binding = &bindings[i];
        *binding = (VkDescriptorSetLayoutBinding){};
        binding->binding = i;
        binding->descriptorCount = desc->bindings[i].descriptorCount;
        binding->descriptorType = GetVkDescriptorType(desc->bindings[i].descriptorType);
        binding->stageFlags = GetVkShaderStageFlags(desc->bindings[i].stageFlags);
        binding->pImmutableSamplers = VK_NULL_HANDLE;

        if(desc->bindings[i].descriptorType == RHI_DescriptorType_ReadOnlyBuffer){
            bindingFlags[i] = 0;
        }else{
            bindingFlags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
            if(desc->bindings[i].descriptorCount > 1){
                bindingFlags[i] |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
            }
        }

    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFlagsCreateInfo = {};
	descSetFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    descSetFlagsCreateInfo.pBindingFlags = bindingFlags;
    descSetFlagsCreateInfo.bindingCount = desc->bindingCount;

    
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	VkDescriptorSetLayoutCreateInfo descSetCreateInfo = {};
	descSetCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetCreateInfo.pNext = &descSetFlagsCreateInfo;
    descSetCreateInfo.pBindings = bindings;
    descSetCreateInfo.bindingCount = desc->bindingCount;
	descSetCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	VK(vkCreateDescriptorSetLayout(vk.device, &descSetCreateInfo, NULL, &layout));

	SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)layout, desc->name);

    DescriptorSetLayout descLayout = {};
    descLayout.layout = layout;
    descLayout.desc = *desc;

    return (rhiDescriptorSetLayout) { Pool_Add(&vk.descriptorSetLayoutPool, &descLayout) };
}

rhiDescriptorSet RHI_CreateDescriptorSet(const char *name, rhiDescriptorSetLayout layoutHandle, qboolean longLifetime)
{
    DescriptorSetLayout *layout = GET_LAYOUT(layoutHandle);

    VkDescriptorSet descriptorSet;

    VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = vk.descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout->layout;
    
	VK(vkAllocateDescriptorSets(vk.device, &allocInfo, &descriptorSet));
    SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSet, name);
    DescriptorSet desc = {};
    desc.set = descriptorSet;
    desc.layout = layoutHandle;
    desc.longLifetime = longLifetime;

    return (rhiDescriptorSet) { Pool_Add(&vk.descriptorSetPool, &desc) };
}

void RHI_UpdateDescriptorSet(rhiDescriptorSet descriptorHandle, uint32_t bindingIndex, RHI_DescriptorType type, uint32_t offset, uint32_t descriptorCount, const void *handles, uint32_t mipIndex){
    DescriptorSet *descSet = GET_DESCRIPTORSET(descriptorHandle);
#ifdef _DEBUG
    DescriptorSetLayout *layout = GET_LAYOUT(descSet->layout);
    assert(bindingIndex < layout->desc.bindingCount);
    assert(layout->desc.bindings[bindingIndex].descriptorType == type);
    assert(layout->desc.bindings[bindingIndex].descriptorCount >= offset + descriptorCount);
#endif

    VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = descSet->set;
	write.dstBinding = bindingIndex;
	write.descriptorCount = descriptorCount;
	write.descriptorType = GetVkDescriptorType(type);
	write.pBufferInfo = NULL;
	write.pImageInfo = NULL;
    write.dstArrayElement = offset;

    static VkDescriptorImageInfo imageInfo[MAX_DRAWIMAGES];
    static VkDescriptorBufferInfo bufferInfo[64];

    if(type == RHI_DescriptorType_ReadOnlyTexture){
        assert(descriptorCount <= ARRAY_LEN(imageInfo));
        const rhiTexture *textureHandle = (const rhiTexture*)handles;
        for(int i = 0; i < descriptorCount; i++){
            Texture *texture = GET_TEXTURE(textureHandle[i]);
            imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo[i].imageView = texture->view;
            imageInfo[i].sampler = VK_NULL_HANDLE;
        }
        write.pImageInfo = imageInfo;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    } 
    else if(type == RHI_DescriptorType_Sampler){
        assert(descriptorCount <= ARRAY_LEN(imageInfo));
        const rhiSampler *samplerHandle = (const rhiSampler*)handles;
        for(int i = 0; i < descriptorCount; i++){
            imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo[i].imageView = VK_NULL_HANDLE;
            imageInfo[i].sampler = GET_SAMPLER(samplerHandle[i])->sampler;
        }
        write.pImageInfo = imageInfo;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    }
    else if(type == RHI_DescriptorType_ReadOnlyBuffer){
        assert(descriptorCount <= ARRAY_LEN(bufferInfo));
        const rhiBuffer *bufferHandle = (const rhiBuffer*)handles;
        for(int i = 0; i < descriptorCount; i++){
            bufferInfo[i].buffer = GET_BUFFER(bufferHandle[i])->buffer;
            bufferInfo[i].offset = 0;
            bufferInfo[i].range = VK_WHOLE_SIZE;
        }
       
        write.pBufferInfo = bufferInfo;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    }else if(type == RHI_DescriptorType_ReadWriteTexture){
        assert(descriptorCount <= ARRAY_LEN(imageInfo));
        const rhiTexture *textureHandle = (const rhiTexture*)handles;
        for(int i = 0; i < descriptorCount; i++){
            Texture *texture = GET_TEXTURE(textureHandle[i]);
            imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            imageInfo[i].imageView = texture->mipViews[mipIndex];
            imageInfo[i].sampler = VK_NULL_HANDLE;
        }
        write.pImageInfo = imageInfo;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);

    }
    else if (type == RHI_DescriptorType_ReadWriteBuffer) {
        assert(descriptorCount <= ARRAY_LEN(bufferInfo));
        const rhiBuffer* bufferHandle = (const rhiBuffer*)handles;
        for (int i = 0; i < descriptorCount; i++) {
            bufferInfo[i].buffer = GET_BUFFER(bufferHandle[i])->buffer;
            bufferInfo[i].offset = 0;
            bufferInfo[i].range = VK_WHOLE_SIZE;
        }

        write.pBufferInfo = bufferInfo;
        vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    }else{
        assert(!"Unhandled descriptor type");
    }




}

VkBool32 isBlendEnabled(uint32_t srcBlend, uint32_t dstBlend){
    if(srcBlend == GLS_SRCBLEND_ONE && dstBlend == GLS_DSTBLEND_ZERO){
        return VK_FALSE;
    }
    if(srcBlend == 0 && dstBlend == 0){
        return VK_FALSE;
    }
    return VK_TRUE;
}

rhiPipeline RHI_CreateComputePipeline(const rhiComputePipelineDesc *computeDesc){
    DescriptorSetLayout *descriptorSetLayout = GET_LAYOUT(computeDesc->descLayout);

    VkPushConstantRange pcr = {};
    pcr.size = max(computeDesc->pushConstantsBytes, 4);
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout->layout;
    pipelineLayoutCreateInfo.pPushConstantRanges = &pcr;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 1;


	VK(vkCreatePipelineLayout(vk.device, &pipelineLayoutCreateInfo, NULL, &vkPipelineLayout));
	SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)vkPipelineLayout, va("%s Layout", computeDesc->name));

    VkShaderModule csModule;
    VkShaderModuleCreateInfo csCreateInfo = {};
    csCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    csCreateInfo.codeSize = computeDesc->shader.byteCount;
    csCreateInfo.pCode = (const uint32_t*)computeDesc->shader.data;

    VK(vkCreateShaderModule(vk.device, &csCreateInfo, NULL, &csModule));

    PipelineLayout layout = {};
    layout.pipelineLayout = vkPipelineLayout;

    VkComputePipelineCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    createInfo.layout = vkPipelineLayout;
    createInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    createInfo.stage.module = csModule;
    createInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    createInfo.stage.pName = "cs";

    VkPipeline vkPipeline;
    VK(vkCreateComputePipelines(vk.device, vk.pipelineCache, 1, &createInfo, NULL, &vkPipeline));

    vkDestroyShaderModule(vk.device, csModule, NULL);

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)vkPipeline, computeDesc->name);

    Pipeline pipeline = {};
    pipeline.pipeline = vkPipeline;
    pipeline.compute = qtrue;
    pipeline.layout = layout;
    pipeline.pushConstantOffsets[RHI_Shader_Compute] = 0;
    pipeline.pushConstantSize[RHI_Shader_Compute] = computeDesc->pushConstantsBytes;
    pipeline.computeDesc = *computeDesc;


    return (rhiPipeline) { Pool_Add(&vk.pipelinePool, &pipeline) };
}

rhiPipeline RHI_CreateGraphicsPipeline(const rhiGraphicsPipelineDesc *graphicsDesc)
{
    assert((graphicsDesc->srcBlend & (~GLS_SRCBLEND_BITS)) == 0);
    assert((graphicsDesc->dstBlend & (~GLS_DSTBLEND_BITS)) == 0);
    assert(graphicsDesc->colorFormat != RHI_TextureFormat_Invalid);
    assert(graphicsDesc->colorFormat < RHI_TextureFormat_Count);
    assert(graphicsDesc->colorFormat > 0);
    DescriptorSetLayout *descriptorSetLayout = GET_LAYOUT(graphicsDesc->descLayout);

    

    VkPushConstantRange pcr[2] = {};
    uint32_t pcrOffset = 0;
    uint32_t pcrCount = 0;
    if(graphicsDesc->pushConstants.vsBytes > 0){
        VkPushConstantRange *range = &pcr[pcrCount++];
        range->size = graphicsDesc->pushConstants.vsBytes;
        range->stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        range->offset = pcrOffset;
        pcrOffset += graphicsDesc->pushConstants.vsBytes;
    }
    if(graphicsDesc->pushConstants.psBytes > 0){
        VkPushConstantRange *range = &pcr[pcrCount++];
        range->size = graphicsDesc->pushConstants.psBytes;
        range->stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        range->offset = pcrOffset;
    }
    

    VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout->layout;
    pipelineLayoutCreateInfo.pPushConstantRanges = pcr;
    pipelineLayoutCreateInfo.pushConstantRangeCount = pcrCount;

	VK(vkCreatePipelineLayout(vk.device, &pipelineLayoutCreateInfo, NULL, &vkPipelineLayout));
	SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)vkPipelineLayout, va("%s Layout", graphicsDesc->name));

    PipelineLayout layout = {};
    layout.pipelineLayout = vkPipelineLayout;


    // --------------------------------------------------------------------------------------------------

    VkShaderModule vsModule;
    VkShaderModuleCreateInfo vsCreateInfo = {};
    vsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vsCreateInfo.codeSize = graphicsDesc->vertexShader.byteCount;
    vsCreateInfo.pCode = (const uint32_t*)graphicsDesc->vertexShader.data;

    VkShaderModule psModule;
    VkShaderModuleCreateInfo psCreateInfo = {};
    psCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    psCreateInfo.codeSize = graphicsDesc->pixelShader.byteCount;
    psCreateInfo.pCode = (const uint32_t*)graphicsDesc->pixelShader.data;

    VK(vkCreateShaderModule(vk.device, &vsCreateInfo,NULL,&vsModule));
    VK(vkCreateShaderModule(vk.device, &psCreateInfo,NULL,&psModule));

	int firstStage = 0;
	int stageCount = 2;
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].pName = "vs";
    stages[0].module = vsModule;
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].pName = "ps";
    stages[1].module = psModule;


	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.primitiveRestartEnable = VK_FALSE;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;


	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = (graphicsDesc->wireframe && vk.caps.fillModeNonSolid) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = GetVkCullModeFlags(graphicsDesc->cullType);
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	//rasterizer.depthClampEnable = vk.deviceFeatures.depthClamp;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.lineWidth = 1.0f;

    
    if(graphicsDesc->polygonOffset){
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = -1.0f;
        rasterizer.depthBiasSlopeFactor = -2.0f;
        rasterizer.depthClampEnable = VK_FALSE;
    }

	// VkPipelineMultisampleStateCreateInfo multiSampling {};
	// multiSampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	// multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	// multiSampling.alphaToCoverageEnable = VK_FALSE;


	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.blendEnable = isBlendEnabled(graphicsDesc->srcBlend, graphicsDesc->dstBlend);
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcColorBlendFactor = GetSourceColorBlendFactor(graphicsDesc->srcBlend);
	colorBlendAttachment.dstColorBlendFactor = GetDestinationColorBlendFactor(graphicsDesc->dstBlend);
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = GetSourceColorBlendFactor(graphicsDesc->srcBlend);
	colorBlendAttachment.dstAlphaBlendFactor = GetDestinationColorBlendFactor(graphicsDesc->dstBlend);
	colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;


	// @NOTE: VK_DYNAMIC_STATE_CULL_MODE_EXT is not widely available (VK_EXT_extended_dynamic_state)
	const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = ARRAY_LEN(dynamicStates);
	dynamicState.pDynamicStates = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;


    // Provide information for dynamic rendering
    VkPipelineRenderingCreateInfo pipeline_create = {};
    pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    pipeline_create.pNext                   = VK_NULL_HANDLE;
    pipeline_create.colorAttachmentCount    = 1;
    VkFormat colorFormat = GetVkFormat(graphicsDesc->colorFormat);
    pipeline_create.pColorAttachmentFormats = &colorFormat;
    const VkFormat depthFormat = graphicsDesc->noDepthAttachment ? VK_FORMAT_UNDEFINED : GetVkFormat(D32_SFloat);
    pipeline_create.depthAttachmentFormat   = depthFormat;
    pipeline_create.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

  
    VkVertexInputAttributeDescription vertexAttributes[ARRAY_LEN(graphicsDesc->attributes)];
    VkVertexInputBindingDescription vertexBindings[ARRAY_LEN(graphicsDesc->attributes)];
    for(int i = 0; i < graphicsDesc->attributeCount; i++){

        VkVertexInputAttributeDescription vertexAttributeInfo = {};
        //a binding can have multiple attributes (interleaved vertex format)
        vertexAttributeInfo.location = i; //shader bindings / shader input location (vk::location in HLSL) 
        vertexAttributeInfo.binding = graphicsDesc->attributes[i].bufferBinding; //buffer bindings / vertex buffer index (CmdBindVertexBuffers in C)
        vertexAttributeInfo.format = GetVkFormatFromVertexFormat(graphicsDesc->attributes[i].elementFormat, graphicsDesc->attributes[i].elementCount);
        vertexAttributeInfo.offset = graphicsDesc->attributes[i].offset; //attribute byte offset

        vertexAttributes[i] = vertexAttributeInfo;
    }

    for(int i = 0; i < graphicsDesc->vertexBufferCount; i++){
        VkVertexInputBindingDescription vertexBindingInfo = {};
        vertexBindingInfo.binding = i; //shader binding point
        vertexBindingInfo.stride = graphicsDesc->vertexBuffers[i].stride; 
        vertexBindingInfo.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        vertexBindings[i] = vertexBindingInfo;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
    vertexInputInfo.vertexAttributeDescriptionCount = graphicsDesc->attributeCount;
    vertexInputInfo.vertexBindingDescriptionCount = graphicsDesc->vertexBufferCount;

    VkPipelineMultisampleStateCreateInfo multiSampling = {};
	multiSampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multiSampling.rasterizationSamples = GetVkSampleCount(graphicsDesc->sampleCount);
	//multiSampling.alphaToCoverageEnable = GetVkSampleCount(graphicsDesc->sampleCount) != VK_SAMPLE_COUNT_1_BIT;
    multiSampling.alphaToCoverageEnable = graphicsDesc->alphaToCoverage;
    
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthWriteEnable = graphicsDesc->depthWrite;
    depthStencil.depthTestEnable = graphicsDesc->depthTest;
    depthStencil.depthCompareOp = graphicsDesc->depthTestEqual? VK_COMPARE_OP_EQUAL: VK_COMPARE_OP_LESS_OR_EQUAL;


    // Use the pNext to point to the rendering create struct
    VkGraphicsPipelineCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    if(vk.caps.dynamicRendering){
        createInfo.pNext           = &pipeline_create; // reference the new dynamic structure
    }else{
        // load ops don't affect compatibility, so this suits every pass variant
        createInfo.renderPass      = GetRenderPass(colorFormat, depthFormat, GetVkSampleCount(graphicsDesc->sampleCount), VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_LOAD);
    }
    createInfo.layout = layout.pipelineLayout;
	createInfo.subpass = 0; // we always target sub-pass 0
	createInfo.stageCount = stageCount;
	createInfo.pStages = stages + firstStage;
	createInfo.pColorBlendState = &colorBlending;
	createInfo.pDepthStencilState = &depthStencil;
	createInfo.pDynamicState = &dynamicState;
    
	createInfo.pInputAssemblyState = &inputAssembly;
	createInfo.pMultisampleState = &multiSampling;
	createInfo.pRasterizationState = &rasterizer;
	createInfo.pVertexInputState = &vertexInputInfo; //add later
	createInfo.pViewportState = &viewportState;

    VkPipeline vkPipeline;
    VK(vkCreateGraphicsPipelines(vk.device, vk.pipelineCache, 1, &createInfo, NULL, &vkPipeline));

    vkDestroyShaderModule(vk.device, psModule, NULL);
    vkDestroyShaderModule(vk.device, vsModule, NULL);

    SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)vkPipeline, graphicsDesc->name);

    Pipeline pipeline = {};
    pipeline.pipeline = vkPipeline;
    pipeline.compute = qfalse;
    pipeline.layout = layout;
    pipeline.pushConstantOffsets[RHI_Shader_Vertex] = 0;
    pipeline.pushConstantOffsets[RHI_Shader_Pixel] = graphicsDesc->pushConstants.vsBytes;
    pipeline.pushConstantSize[RHI_Shader_Vertex] = graphicsDesc->pushConstants.vsBytes;
    pipeline.pushConstantSize[RHI_Shader_Pixel] = graphicsDesc->pushConstants.psBytes;


    return (rhiPipeline) { Pool_Add(&vk.pipelinePool, &pipeline) };

}

rhiTexture* RHI_GetSwapChainImages( void )
{
    return vk.swapChainRenderTargets;
}

uint32_t RHI_GetSwapChainImageCount( void ){
    return vk.swapChainImageCount;
}

#if !defined(_WIN32)
void sdl_PollEvents( void ); // no Sys_ wrapper exists for the SDL pump
#endif

void RHI_AcquireNextImage(uint32_t* outImageIndex, rhiSemaphore signalSemaphore)
{
    Semaphore* signal = GET_SEMAPHORE(signalSemaphore);
    assert(signal->binary == qtrue);
    assert(signal->signaled == qfalse);

    // An out-of-date acquire leaves the semaphore unsignalled, so it is
    // safe to reuse on the retry against the recreated swap chain.
    const int maxAttempts = 3;
    for(int attempt = 1; attempt <= maxAttempts; attempt++){
        const VkResult r = vkAcquireNextImageKHR(vk.device, vk.swapChain, UINT64_MAX, signal->semaphore, VK_NULL_HANDLE, outImageIndex);
        if(r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR){
            if(*outImageIndex >= RHI_MAX_SWAP_CHAIN_IMAGES){
                ri.Error(ERR_FATAL, "Swap chain image index out of range (%d >= %d)\n", (int)*outImageIndex, RHI_MAX_SWAP_CHAIN_IMAGES);
            }
            signal->signaled = qtrue;
            return;
        }
        signal->signaled = qfalse;
        if(r != VK_ERROR_OUT_OF_DATE_KHR){
            Check(r, "vkAcquireNextImageKHR");
            return;
        }
        if(!RecreateSwapchain()){
            // Minimized mid-frame: there is no image to continue with, so wait
            // for an area. Only a window that isn't minimized has a time limit.
            const int waitMs = 10000;
            int start = Sys_Milliseconds();
            while(!RecreateSwapchain()){
#if !defined(_WIN32)
                sdl_PollEvents();
#endif
                if(Sys_IsMinimized()){
                    start = Sys_Milliseconds();
                }else if(Sys_Milliseconds() - start > waitMs){
                    ri.Error(ERR_FATAL, "Swap chain surface still has no area after %d ms (window %d x %d)\n", waitMs, glInfo.winWidth, glInfo.winHeight);
                }
                Sys_Sleep(20);
            }
        }
    }
    ri.Error(ERR_FATAL, "vkAcquireNextImageKHR out of date after %d attempts (window %d x %d)\n", maxAttempts, glInfo.winWidth, glInfo.winHeight);
}


void RHI_SubmitGraphics(const rhiSubmitGraphicsDesc *graphicsDesc)
{
    VkTimelineSemaphoreSubmitInfo timelineInfo;
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.pNext = NULL;
    timelineInfo.waitSemaphoreValueCount = graphicsDesc->waitSemaphoreCount;
    timelineInfo.pWaitSemaphoreValues = graphicsDesc->waitSemaphoreValues;
    timelineInfo.signalSemaphoreValueCount = graphicsDesc->signalSemaphoreCount;
    timelineInfo.pSignalSemaphoreValues = graphicsDesc->signalSemaphoreValues;

    VkSemaphore wait[ARRAY_LEN(graphicsDesc->waitSemaphores)];
    VkSemaphore signal[ARRAY_LEN(graphicsDesc->signalSemaphores)];
    VkPipelineStageFlags flags[ARRAY_LEN(graphicsDesc->waitSemaphores)];

    for(int i = 0; i < graphicsDesc->waitSemaphoreCount; i++){
        Semaphore *semaphore = GET_SEMAPHORE(graphicsDesc->waitSemaphores[i]);
        wait[i] = semaphore->semaphore;
    
        if(semaphore->binary){
            assert(semaphore->signaled == qtrue);
            semaphore->signaled = qfalse;
        }

        flags[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    for(int i = 0; i < graphicsDesc->signalSemaphoreCount; i++){
        Semaphore *semaphore = GET_SEMAPHORE(graphicsDesc->signalSemaphores[i]);
        signal[i] = semaphore->semaphore;
        if(semaphore->binary){
            assert(semaphore->signaled == qfalse);
            semaphore->signaled = qtrue;
        }
    }

    VkSubmitInfo submitInfo = {};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext                = &timelineInfo;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &vk.activeCommandBuffer;
    submitInfo.waitSemaphoreCount   = graphicsDesc->waitSemaphoreCount;
    submitInfo.pWaitSemaphores      = wait;
    submitInfo.signalSemaphoreCount = graphicsDesc->signalSemaphoreCount;
    submitInfo.pSignalSemaphores    = signal;
    submitInfo.pWaitDstStageMask    = flags;


    VK(vkQueueSubmit(vk.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE));
}



void RHI_SubmitPresent(rhiSemaphore waitSemaphore, uint32_t swapChainImageIndex)
{
    Semaphore* semaphore = GET_SEMAPHORE(waitSemaphore);
    VkSemaphore wait = semaphore->semaphore;

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &vk.swapChain;
    presentInfo.pImageIndices      = &swapChainImageIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &wait;

    assert(semaphore->binary == qtrue);
    assert(semaphore->signaled == qtrue);
    semaphore->signaled = qfalse;

    PROF_MOMENT_C( "Present", 0xC800C8FFu ); // magenta
    const VkResult r = vkQueuePresentKHR(vk.queues.present, &presentInfo);
    if(r == VK_ERROR_OUT_OF_DATE_KHR){
        RecreateSwapchain();
    }else if(r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR){
        Check(r, "vkQueuePresentKHR");
    }

}

rhiCommandBuffer RHI_CreateCommandBuffer( qboolean longLifetime)
{
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = vk.commandPool;
    allocInfo.commandBufferCount = 1;
    VK(vkAllocateCommandBuffers(vk.device, &allocInfo, &commandBuffer));

    CommandBuffer buffer = {};
    buffer.commandPool = vk.commandPool;
    buffer.commandBuffer = commandBuffer;
    buffer.longLifetime = longLifetime;
    return (rhiCommandBuffer) { Pool_Add(&vk.commandBufferPool, &buffer) };
}

void RHI_BindCommandBuffer(rhiCommandBuffer commandBuffer)
{
    CommandBuffer* cmdBuffer = (CommandBuffer*)Pool_Get(&vk.commandBufferPool, commandBuffer.h);
    vk.activeCommandBuffer = cmdBuffer->commandBuffer;
    assert(vk.activeCommandBuffer != NULL);
}


void RHI_BeginCommandBuffer( void )
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK(vkBeginCommandBuffer(vk.activeCommandBuffer, &beginInfo));
}

void RHI_EndCommandBuffer()
{
    VK(vkEndCommandBuffer(vk.activeCommandBuffer));
}

void RHI_BeginRendering(const RHI_RenderPass* renderPass)
{
    assert(vk.renderingActive == qfalse);

    if(!vk.caps.dynamicRendering){
        BeginClassicRenderPass(renderPass);
        vk.renderingActive = qtrue;
        vk.currentRenderPass = *renderPass;
        return;
    }

    Texture* colorTexture = GET_TEXTURE(renderPass->colorTexture);
    VkRenderingAttachmentInfo colorAttachmentInfo = {};
    colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachmentInfo.imageView = colorTexture->view;
    colorAttachmentInfo.imageLayout = colorTexture->currentLayout;
    colorAttachmentInfo.loadOp = GetVkAttachmentLoadOp(renderPass->colorLoad);
    memcpy(colorAttachmentInfo.clearValue.color.float32, renderPass->color, sizeof(float) * 4);
    colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachmentInfo = {};
    VkRenderingAttachmentInfo *pDepthAttachmentInfo = NULL;
    if(renderPass->depthTexture.h != 0){
        Texture* depthTexture = GET_TEXTURE(renderPass->depthTexture);
        depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachmentInfo.imageView = depthTexture->view;
        depthAttachmentInfo.imageLayout = depthTexture->currentLayout;
        depthAttachmentInfo.loadOp = GetVkAttachmentLoadOp(renderPass->depthLoad);
        depthAttachmentInfo.clearValue.depthStencil.depth = renderPass->depth;
        depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        pDepthAttachmentInfo = &depthAttachmentInfo;
    }

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachmentInfo;
    renderingInfo.pDepthAttachment = pDepthAttachmentInfo;
    renderingInfo.layerCount = 1;
    renderingInfo.renderArea = GetRenderArea(renderPass, colorTexture);

    vk.cmdBeginRendering(vk.activeCommandBuffer, &renderingInfo);
    vk.renderingActive = qtrue;
    vk.currentRenderPass = *renderPass;
    
}

qboolean RHI_IsRenderingActive(void){
    return vk.renderingActive;
}

RHI_RenderPass* RHI_CurrentRenderPass(void){
    return &vk.currentRenderPass;
}

void RHI_EndRendering(void)
{
    assert(vk.renderingActive == qtrue);
    if(vk.caps.dynamicRendering){
        vk.cmdEndRendering(vk.activeCommandBuffer);
    }else{
        vkCmdEndRenderPass(vk.activeCommandBuffer);
    }
    vk.renderingActive = qfalse;
}

void RHI_CmdBindPipeline(rhiPipeline pipeline)
{
    Pipeline* privatePipeline = GET_PIPELINE(pipeline);
    VkPipelineBindPoint bindPoint = privatePipeline->compute? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

    vkCmdBindPipeline(vk.activeCommandBuffer, bindPoint, privatePipeline->pipeline);
}

void RHI_CmdBindDescriptorSet(rhiPipeline pipeline, rhiDescriptorSet descriptorSet)
{
    Pipeline *privatePipeline = GET_PIPELINE(pipeline);
    DescriptorSet *privateDescSet = GET_DESCRIPTORSET(descriptorSet);
    VkPipelineBindPoint bindPoint = privatePipeline->compute? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

    vkCmdBindDescriptorSets(vk.activeCommandBuffer, bindPoint, privatePipeline->layout.pipelineLayout, 0, 1, &privateDescSet->set, 0, NULL);
}

void RHI_CmdBindVertexBuffers(const rhiBuffer *buffers, const uint32_t *offsets, uint32_t bufferCount)
{
    VkBuffer vkBuffers[16]; 
    VkDeviceSize vkOffsets[16];
    assert(bufferCount < ARRAY_LEN(vkBuffers));
    for(int i = 0; i < bufferCount; i++){
        Buffer *buffer = GET_BUFFER(buffers[i]);
        vkBuffers[i] = buffer->buffer;
        vkOffsets[i] = offsets[i];
    }
    vkCmdBindVertexBuffers(vk.activeCommandBuffer,0, bufferCount, vkBuffers, vkOffsets);
}

void RHI_CmdBindIndexBuffer(rhiBuffer indexBuffer)
{
    Buffer *privateBuffer = GET_BUFFER(indexBuffer);
    vkCmdBindIndexBuffer(vk.activeCommandBuffer,privateBuffer->buffer, 0, VK_INDEX_TYPE_UINT32);
}

void RHI_CmdSetViewport(int32_t x, int32_t y, uint32_t width, uint32_t height, float minDepth, float maxDepth)
{
    if(x < 0){
        width = Com_ClampInt(1, width, width + x);
        x = 0;
    }
    if(y < 0){
        height = Com_ClampInt(1, height, height + y);
        y = 0;
    }
    
    VkViewport viewport;
	viewport.x = (float)x;
	viewport.y = (float)y;
	viewport.width = (float)width;
	viewport.height = (float)height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;

    vkCmdSetViewport(vk.activeCommandBuffer, 0, 1, &viewport);
}

void RHI_CmdSetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    if(x < 0){
        width = Com_ClampInt(1, width, width + x);
        x = 0;
    }
    if(y < 0){
        height = Com_ClampInt(1, height, height + y);
        y = 0;
    }
    VkRect2D scissor;
	scissor.offset.x = x;
	scissor.offset.y = y;
	scissor.extent.width = width;
	scissor.extent.height = height;

    vkCmdSetScissor(vk.activeCommandBuffer, 0, 1, &scissor);
}

void RHI_CmdPushConstants(rhiPipeline pipeline, RHI_Shader shader, const void *constants, uint32_t byteCount)
{
    Pipeline *privatePipeline = GET_PIPELINE(pipeline);
 //   assert(byteCount == privatePipeline->pushConstantSize[shader]);
    assert((uint32_t)shader < RHI_Shader_Count);

    vkCmdPushConstants(vk.activeCommandBuffer, privatePipeline->layout.pipelineLayout, 
                       GetVkShaderStageFlagsFromShader(shader), privatePipeline->pushConstantOffsets[shader],
                       byteCount, constants);
}

void RHI_CmdDraw(uint32_t vertexCount, uint32_t firstVertex)
{
    vkCmdDraw(vk.activeCommandBuffer, vertexCount, 1, firstVertex, 0);
}

void RHI_CmdDrawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex)
{
    vkCmdDrawIndexed(vk.activeCommandBuffer, indexCount, 1, firstIndex, firstVertex, 0);
}

void RHI_CmdDispatch(uint32_t x, uint32_t y, uint32_t z)
{
    vkCmdDispatch(vk.activeCommandBuffer, x, y, z);
}

void RHI_CmdBeginBarrier()
{
    vk.textureBarrierCount = 0;    
    vk.bufferBarrierCount = 0;  
}

// Maps sync2-only stage bits onto legacy ones. NONE becomes TOP_OF_PIPE as
// a source and BOTTOM_OF_PIPE as a destination (no dependent access there).
static VkPipelineStageFlags GetLegacyStageMask(VkPipelineStageFlags2 stages, qbool isDestination)
{
    VkPipelineStageFlags2 rest = stages;
    VkPipelineStageFlags legacy = 0;

    if(stages == VK_PIPELINE_STAGE_2_NONE){
        return isDestination ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    const VkPipelineStageFlags2 transferBits = VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT |
        VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT;
    const VkPipelineStageFlags2 vertexInputBits = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
    if(rest & transferBits){
        legacy |= VK_PIPELINE_STAGE_TRANSFER_BIT;
        rest &= ~transferBits;
    }
    if(rest & vertexInputBits){
        legacy |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
        rest &= ~vertexInputBits;
    }
    // remaining bits keep their legacy values
    assert((rest >> 32) == 0);
    return legacy | (VkPipelineStageFlags)(rest & 0xffffffffu);
}

static VkAccessFlags GetLegacyAccessMask(VkAccessFlags2 access)
{
    // every access bit used here is below bit 32 and shares its legacy value
    assert((access >> 32) == 0);
    return (VkAccessFlags)(access & 0xffffffffu);
}

void RHI_CmdEndBarrier()
{
    VkBufferMemoryBarrier2 bufferBarriers[ARRAY_LEN(vk.bufferBarriers)];
    uint32_t bufferBarrierCount = 0;

    for(int i = 0; i < vk.bufferBarrierCount; i++){
        Buffer* buffer = GET_BUFFER(vk.bufferBarriers[i]);
        RHI_ResourceState newLayout = vk.bufferState[i];
        if(buffer->currentLayout == newLayout ){
            continue;
        }
        VkBufferMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        barrier.srcAccessMask = GetVkAccessFlagsFromResource(buffer->currentLayout);
        barrier.dstAccessMask = GetVkAccessFlagsFromResource(newLayout);
        barrier.srcStageMask = GetVkStageFlagsFromResource(buffer->currentLayout);
        barrier.dstStageMask = GetVkStageFlagsFromResource(newLayout);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer->buffer;

        buffer->currentLayout = newLayout;
        
        bufferBarriers[bufferBarrierCount] = barrier;
        bufferBarrierCount++;

    }


    VkImageMemoryBarrier2 textureBarriers[ARRAY_LEN(vk.textureBarriers)];
    uint32_t textureBarrierCount = 0;
    for(int i = 0; i < vk.textureBarrierCount; i++){
        Texture* texture = GET_TEXTURE(vk.textureBarriers[i]);
        VkImageLayout newLayout = GetVkImageLayout(vk.textureState[i]);
        // attachments are rewritten pass to pass in one layout, so they need the dependency
        if(texture->currentLayout == newLayout && newLayout != VK_IMAGE_LAYOUT_GENERAL
            && newLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            && newLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL){
            continue;
        }
        

        VkImageMemoryBarrier2 barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.image = texture->image;
        if(texture->currentLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_GENERAL){
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        }else{
            barrier.srcAccessMask = GetVkAccessFlags(texture->currentLayout);
            barrier.dstAccessMask = GetVkAccessFlags(newLayout);
        }

        barrier.srcStageMask = GetVkStageFlags(texture->currentLayout);
        barrier.dstStageMask = GetVkStageFlags(newLayout);
        barrier.oldLayout = texture->currentLayout;
        barrier.newLayout = newLayout;
        barrier.subresourceRange.aspectMask = GetVkImageAspectFlags(texture->format); 
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

        texture->currentLayout = newLayout;
        
        textureBarriers[textureBarrierCount] = barrier;
        textureBarrierCount++;

    }
    
    vk.textureBarrierCount = 0;
    vk.bufferBarrierCount = 0;

    if(textureBarrierCount == 0 && bufferBarrierCount == 0){
        return;
    }


    if(!vk.caps.synchronization2){
        VkImageMemoryBarrier legacyImages[ARRAY_LEN(vk.textureBarriers)];
        VkBufferMemoryBarrier legacyBuffers[ARRAY_LEN(vk.bufferBarriers)];
        VkPipelineStageFlags srcStages = 0;
        VkPipelineStageFlags dstStages = 0;

        for(uint32_t i = 0; i < textureBarrierCount; i++){
            const VkImageMemoryBarrier2 *b = &textureBarriers[i];
            VkImageMemoryBarrier legacy = {};
            legacy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            legacy.srcAccessMask = GetLegacyAccessMask(b->srcAccessMask);
            legacy.dstAccessMask = GetLegacyAccessMask(b->dstAccessMask);
            legacy.oldLayout = b->oldLayout;
            legacy.newLayout = b->newLayout;
            legacy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            legacy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            legacy.image = b->image;
            legacy.subresourceRange = b->subresourceRange;
            legacyImages[i] = legacy;
            srcStages |= GetLegacyStageMask(b->srcStageMask, qfalse);
            dstStages |= GetLegacyStageMask(b->dstStageMask, qtrue);
        }
        for(uint32_t i = 0; i < bufferBarrierCount; i++){
            const VkBufferMemoryBarrier2 *b = &bufferBarriers[i];
            VkBufferMemoryBarrier legacy = {};
            legacy.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            legacy.srcAccessMask = GetLegacyAccessMask(b->srcAccessMask);
            legacy.dstAccessMask = GetLegacyAccessMask(b->dstAccessMask);
            legacy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            legacy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            legacy.buffer = b->buffer;
            legacy.offset = b->offset;
            legacy.size = b->size;
            legacyBuffers[i] = legacy;
            srcStages |= GetLegacyStageMask(b->srcStageMask, qfalse);
            dstStages |= GetLegacyStageMask(b->dstStageMask, qtrue);
        }
        vkCmdPipelineBarrier(vk.activeCommandBuffer, srcStages, dstStages, 0,
            0, NULL, bufferBarrierCount, legacyBuffers, textureBarrierCount, legacyImages);
        return;
    }

    VkDependencyInfo dep = {};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = textureBarrierCount;
    dep.pImageMemoryBarriers = textureBarriers;
    dep.bufferMemoryBarrierCount = bufferBarrierCount;
    dep.pBufferMemoryBarriers = bufferBarriers;
    vk.cmdPipelineBarrier2(vk.activeCommandBuffer, &dep);
}

void RHI_CmdTextureBarrier(rhiTexture handle, RHI_ResourceState flag)
{
    assert(vk.textureBarrierCount < ARRAY_LEN(vk.textureState));
    int idx = vk.textureBarrierCount++;
    vk.textureState[idx] = flag; 
    vk.textureBarriers[idx] = handle;
}

void RHI_CmdBufferBarrier(rhiBuffer handle, RHI_ResourceState state)
{
    assert(vk.bufferBarrierCount < ARRAY_LEN(vk.bufferState));
    int idx = vk.bufferBarrierCount++;
    vk.bufferState[idx] = state; 
    vk.bufferBarriers[idx] = handle;
}

void RHI_CmdCopyBuffer(rhiBuffer dstBuffer, uint32_t dstOffset, rhiBuffer srcBuffer, uint32_t srcOffset, uint32_t size)
{
    VkBufferCopy region = {};
    region.dstOffset = dstOffset;
    region.srcOffset = srcOffset;
    region.size = size;

    
    
    vkCmdCopyBuffer(vk.activeCommandBuffer, GET_BUFFER(srcBuffer)->buffer, GET_BUFFER(dstBuffer)->buffer, 1, &region);
}

void RHI_CmdCopyBufferRegions()
{
}

typedef struct DurationQuery {
    uint32_t frameIndex;
    uint32_t durationQueryIndex;
} DurationQuery;

DurationQuery DecodeDurationQuery(rhiDurationQuery handle){
    DurationQuery query;
    query.frameIndex = handle.h >> 32;
    query.durationQueryIndex = handle.h & 0xffffffff;
    return query;
}

rhiDurationQuery EncodeDurationQuery(uint32_t frameIndex, uint32_t durationQueryIndex){
    rhiDurationQuery handle;
    handle.h = (uint64_t)frameIndex << 32;
    handle.h |= (uint64_t)durationQueryIndex & 0xffffffff;
    return handle;
}

static qbool s_gpuTimestampsEnabled = qfalse;

qbool RHI_SupportsFragmentStores( void ){
    return vk.caps.fragmentStoresEnabled;
}

// Set once per frame, before any query is recorded for that frame.
void RHI_SetGPUTimestamps(qbool enabled){
    s_gpuTimestampsEnabled = enabled;
}

rhiDurationQuery RHI_CmdBeginDurationQuery(void)
{
    if(!vk.caps.timestamps || !s_gpuTimestampsEnabled){
        return (rhiDurationQuery) { 0 };
    }
    assert(vk.durationQueryCount[vk.currentFrameIndex] < MAX_DURATION_QUERIES);
    if(vk.durationQueryCount[vk.currentFrameIndex] == 0){
        vk.durationQueryCount[vk.currentFrameIndex]++;
    }
    uint32_t durationQueryIndex = vk.durationQueryCount[vk.currentFrameIndex]++;
    rhiDurationQuery durationQuery = EncodeDurationQuery(vk.currentFrameIndex, durationQueryIndex);
    if(vk.caps.synchronization2){
        vk.cmdWriteTimestamp2(vk.activeCommandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, vk.queryPool[vk.currentFrameIndex], durationQueryIndex * 2);
    }else{
        vkCmdWriteTimestamp(vk.activeCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.queryPool[vk.currentFrameIndex], durationQueryIndex * 2);
    }
    vk.query[vk.currentFrameIndex][durationQueryIndex] |= 1;
    return durationQuery;
}

void RHI_CmdEndDurationQuery(rhiDurationQuery handle)
{
    if(handle.h == 0){
        return;
    }
    DurationQuery query = DecodeDurationQuery(handle);
    assert(query.frameIndex == vk.currentFrameIndex);
    assert(query.durationQueryIndex < vk.durationQueryCount[vk.currentFrameIndex]);
    if(vk.caps.synchronization2){
        vk.cmdWriteTimestamp2(vk.activeCommandBuffer, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, vk.queryPool[vk.currentFrameIndex], query.durationQueryIndex * 2 + 1);
    }else{
        vkCmdWriteTimestamp(vk.activeCommandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vk.queryPool[vk.currentFrameIndex], query.durationQueryIndex * 2 + 1);
    }
    vk.query[query.frameIndex][query.durationQueryIndex] |= 2;
}

uint32_t RHI_GetDurationUs(rhiDurationQuery handle){
    if(handle.h == 0){
        return 0;
    }
    DurationQuery query = DecodeDurationQuery(handle);
    assert(query.durationQueryIndex < MAX_DURATION_QUERIES);
    assert(vk.query[query.frameIndex][query.durationQueryIndex] == 3);
    //assert(query.frameIndex == (vk.currentFrameIndex + 1) % RHI_FRAMES_IN_FLIGHT); //@TODO renderdoc not asserting
    //for previous frame
    //return actual duration in us
    uint64_t timestamps[2];

    vkGetQueryPoolResults(
        vk.device,
        vk.queryPool[query.frameIndex],
        query.durationQueryIndex * 2,
        2,
        2 * sizeof(uint64_t),
        timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    return ((float)(timestamps[1] - timestamps[0]) * vk.deviceProperties.limits.timestampPeriod) / 1000.0f;

}

qbool RHI_GetDurationTimestamps(rhiDurationQuery handle, uint64_t *beginTicks, uint64_t *endTicks){
    if(handle.h == 0){
        return qfalse;
    }
    DurationQuery query = DecodeDurationQuery(handle);
    assert(query.durationQueryIndex < MAX_DURATION_QUERIES);
    if(vk.query[query.frameIndex][query.durationQueryIndex] != 3){
        return qfalse;
    }

    uint64_t timestamps[2];
    vkGetQueryPoolResults(
        vk.device,
        vk.queryPool[query.frameIndex],
        query.durationQueryIndex * 2,
        2,
        2 * sizeof(uint64_t),
        timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    *beginTicks = timestamps[0];
    *endTicks = timestamps[1];
    return qtrue;
}

#if defined( ENABLE_PROFILER )

static qbool s_gpuCalibrationValid = qfalse;
static double s_gpuTicksToCpuUsOffset = 0.0; // cpuUs = gpuTicks * timestampPeriodUs + s_gpuTicksToCpuUsOffset
static int64_t s_lastCalibrationUs = 0;
#define GPU_CALIBRATION_INTERVAL_US 1000000 // recalibrate roughly once/second to absorb clock drift over a long session

void RHI_UpdateGPUCalibration(void){
    if(!vk.ext.EXT_calibrated_timestamps){
        return;
    }

    int64_t nowUs = Sys_Microseconds();
    if(s_gpuCalibrationValid && (nowUs - s_lastCalibrationUs) < GPU_CALIBRATION_INTERVAL_US){
        return;
    }

    // Only the GPU domain is requested here, not also the CPU/QPC domain --
    // deliberately simpler than a literal 2-domain calibrated read: pairing
    // this single GPU-domain sample with an immediately-adjacent
    // Sys_Microseconds() call gives the same (gpuTick, cpuUs) correspondence
    // needed for the offset below, without needing to expose
    // Sys_Microseconds()'s own private QueryPerformanceCounter start/
    // frequency statics (win_shared.c) just to convert a returned QPC value
    // onto its process-relative timeline. The two calls being adjacent
    // (not simultaneous) adds only a few microseconds of jitter, negligible
    // given this recalibrates every ~1s anyway.
    VkCalibratedTimestampInfoEXT info = {0};
    info.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
    info.timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;

    uint64_t gpuTicks;
    uint64_t maxDeviation;
    if(vkGetCalibratedTimestampsEXT(vk.device, 1, &info, &gpuTicks, &maxDeviation) != VK_SUCCESS){
        return;
    }

    double timestampPeriodUs = (double)vk.deviceProperties.limits.timestampPeriod / 1000.0;
    s_gpuTicksToCpuUsOffset = (double)nowUs - (double)gpuTicks * timestampPeriodUs;
    s_gpuCalibrationValid = qtrue;
    s_lastCalibrationUs = nowUs;
}

qbool RHI_GPUTicksToCpuUs(uint64_t gpuTicks, int64_t *outCpuUs){
    if(!s_gpuCalibrationValid){
        return qfalse;
    }
    double timestampPeriodUs = (double)vk.deviceProperties.limits.timestampPeriod / 1000.0;
    *outCpuUs = (int64_t)((double)gpuTicks * timestampPeriodUs + s_gpuTicksToCpuUsOffset);
    return qtrue;
}

// exposes vk.deviceProperties.limits.timestampPeriod (in microseconds) to
// callers outside rhi.c/tr_vulkan.c that don't otherwise include tr_vulkan.h
// (e.g. tr_backend.c's uncalibrated-fallback duration conversion)
double RHI_GetTimestampPeriodUs(void){
    return (double)vk.deviceProperties.limits.timestampPeriod / 1000.0;
}

#endif // ENABLE_PROFILER

void RHI_DurationQueryReset(void){
    // Flags are cleared even when disabled so a later readback of this
    // frame reports no data instead of stale results.
    if(vk.caps.timestamps && s_gpuTimestampsEnabled){
        vkResetQueryPool(vk.device, vk.queryPool[vk.currentFrameIndex],0, MAX_DURATION_QUERIES * 2);
    }
    memset(vk.query[vk.currentFrameIndex], 0, sizeof(vk.query[vk.currentFrameIndex]));
}

void RHI_BeginBufferUpload()
{
}

void RHI_EndBufferUpload()
{
}
static int uploadActive = 0;
void RHI_BeginTextureUpload(rhiTextureUpload *upload, const rhiTextureUploadDesc *desc)
{
    uploadActive++;
    assert(uploadActive == 1);
    assert(vk.uploadDesc.handle.h == 0);
    Texture *texture = GET_TEXTURE(desc->handle);

    upload->data = RHI_MapBuffer(vk.uploadBuffer);
    upload->width = max(texture->desc.width >> desc->mipLevel, 1);
    upload->height = max(texture->desc.height >> desc->mipLevel, 1);
    upload->rowPitch = upload->width * GetByteCountsPerPixel(texture->format); 
    vk.uploadDesc = *desc;
    vk.uploadByteCount = upload->rowPitch * upload->height;
    uint64_t signaledValue;
    if(vk.uploadByteCount + vk.uploadByteOffset > vk.uploadBufferSize){
        vk.uploadByteOffset = 0;
        signaledValue = vk.uploadSemaphoreCount;
    }else{
        upload->data += vk.uploadByteOffset;
        vk.uploadCmdBufferIndex = (vk.uploadCmdBufferIndex + 1) % MAX_UPLOADCMDBUFFERS;
        signaledValue = vk.uploadCmdBufferSignaledValue[vk.uploadCmdBufferIndex];
        
    }
    RHI_WaitOnSemaphore(vk.uploadSemaphore, signaledValue);

}

static void FastMips(){
    Texture *texture = GET_TEXTURE(vk.uploadDesc.handle);
    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(vk.uploadDesc.handle, RHI_ResourceState_ShaderReadWriteBit);
    RHI_CmdEndBarrier();
    rhiDescriptorSet set = vk.uploadDescriptorSets[vk.uploadCmdBufferIndex];
    for(int i = 0; i < texture->desc.mipCount; i++){
        RHI_UpdateDescriptorSet(set, 0, RHI_DescriptorType_ReadWriteTexture, i, 1, &vk.uploadDesc.handle, i);
    }

    RHI_CmdBindPipeline(vk.mipmapPipeline);
    RHI_CmdBindDescriptorSet(vk.mipmapPipeline, set);

    uint32_t w = max(texture->desc.width / 2, 1);
    uint32_t h = max(texture->desc.height / 2, 1);

    for(int i = 1; i < texture->desc.mipCount; i++){
        if(i >= 2){
            RHI_CmdBeginBarrier();
            RHI_CmdTextureBarrier(vk.uploadDesc.handle, RHI_ResourceState_ShaderReadWriteBit);
            RHI_CmdEndBarrier();
        }

        mipmapPushConstants pc = {};
        pc.srcIndex = i - 1;
        pc.dstIndex = i;
        pc.clamp = vk.uploadDesc.clamp;
        RHI_CmdPushConstants(vk.mipmapPipeline, RHI_Shader_Compute, &pc, sizeof(pc));
        
        uint32_t x = (w + 7) / 8;
        uint32_t y = (h + 7) / 8;

        RHI_CmdDispatch(x, y, 1);
        w = max(w / 2, 1);
        h = max(h / 2, 1);
    }
   
}

static void SlowMips(){
    



    Texture *texture = GET_TEXTURE(vk.uploadDesc.handle);
    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(vk.uploadDesc.handle, RHI_ResourceState_ShaderReadWriteBit);
    RHI_CmdEndBarrier();
    rhiDescriptorSet set = vk.uploadDescriptorSets[vk.uploadCmdBufferIndex];
    for(int i = 0; i < texture->desc.mipCount; i++){
        RHI_UpdateDescriptorSet(set, 0, RHI_DescriptorType_ReadWriteTexture, i, 1, &vk.uploadDesc.handle, i);
        
    }

    uint32_t sw = texture->desc.width;
    uint32_t sh = texture->desc.height;

    for(int i = 1; i < texture->desc.mipCount; i++){
        uint32_t dw = max(sw / 2, 1);
        uint32_t dh = sh;

        RHI_CmdBeginBarrier();
        RHI_CmdTextureBarrier(vk.uploadDesc.handle, RHI_ResourceState_ShaderReadWriteBit);
        RHI_CmdTextureBarrier(vk.mipmapScratchTexture, RHI_ResourceState_ShaderReadWriteBit);
        RHI_CmdEndBarrier();

        RHI_CmdBindPipeline(vk.mipmapXPipeline);
        RHI_CmdBindDescriptorSet(vk.mipmapXPipeline, set);

        mipmapXPushConstants pcX = {};
        pcX.srcIndex = i - 1;
        pcX.clamp = vk.uploadDesc.clamp;
        pcX.dstWidth = dw;
        pcX.dstHeight = dh;
        pcX.weights[0] = 0.479230106f;
        pcX.weights[1] =  0.151636884f;
        pcX.weights[2] = -0.0816986337f;
        pcX.weights[3] = -0.0491683967f;
        RHI_CmdPushConstants(vk.mipmapXPipeline, RHI_Shader_Compute, &pcX, sizeof(pcX));

        uint32_t x = (dw + 7) / 8;
        uint32_t y = (dh + 7) / 8;

        RHI_CmdDispatch(x, y, 1);

        
        
        RHI_CmdBeginBarrier();
        RHI_CmdTextureBarrier(vk.uploadDesc.handle, RHI_ResourceState_ShaderReadWriteBit);
        RHI_CmdTextureBarrier(vk.mipmapScratchTexture, RHI_ResourceState_ShaderReadWriteBit);
        RHI_CmdEndBarrier();

        RHI_CmdBindPipeline(vk.mipmapYPipeline);
        RHI_CmdBindDescriptorSet(vk.mipmapYPipeline, set);

        

        mipmapYPushConstants pcY = {};
        pcY.dstIndex = i;
        pcY.clamp = vk.uploadDesc.clamp;
        pcY.srcWidth = dw;
        pcY.srcHeight = dh;
        pcY.weights[0] = 0.479230106f;
        pcY.weights[1] =  0.151636884f;
        pcY.weights[2] = -0.0816986337f;
        pcY.weights[3] = -0.0491683967f;
        RHI_CmdPushConstants(vk.mipmapYPipeline, RHI_Shader_Compute, &pcY, sizeof(pcY));
        
        dh = max(dh / 2, 1);
        
        x = (dw + 7) / 8;
        y = (dh + 7) / 8;
        RHI_CmdDispatch(x, y, 1);

        sw = dw;
        sh = dh;
    }
}

void RHI_EndTextureUpload()
{
    uploadActive--;
    assert(uploadActive == 0);
    RHI_UnmapBuffer(vk.uploadBuffer);
    VkCommandBuffer previousCmdBuffer = vk.activeCommandBuffer;
    RHI_BindCommandBuffer(vk.uploadCmdBuffer[vk.uploadCmdBufferIndex]); //@TODO: return VkCommandBuffer to assign previous?
    Texture *texture = GET_TEXTURE(vk.uploadDesc.handle);
    RHI_BeginCommandBuffer();
    RHI_CmdBeginBarrier();
    RHI_CmdBufferBarrier(vk.uploadBuffer, RHI_ResourceState_CopySourceBit);
    RHI_CmdTextureBarrier(vk.uploadDesc.handle, RHI_ResourceState_CopyDestinationBit);
    RHI_CmdEndBarrier();

    // copy from the staging buffer into the texture mip
    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = vk.uploadDesc.mipLevel;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = 0;
    region.imageOffset.y = 0;
    region.imageOffset.z = 0;
    region.imageExtent.width = max(texture->desc.width >> vk.uploadDesc.mipLevel, 1);
    region.imageExtent.height = max(texture->desc.height >> vk.uploadDesc.mipLevel, 1);
    region.imageExtent.depth = 1;
    region.bufferOffset = vk.uploadByteOffset;

    VkDeviceSize n = vk.deviceProperties.limits.optimalBufferCopyOffsetAlignment;
    vk.uploadByteOffset = ALIGN_UP(vk.uploadByteOffset + vk.uploadByteCount, n);

    vkCmdCopyBufferToImage(vk.activeCommandBuffer, GET_BUFFER(vk.uploadBuffer)->buffer, texture->image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if(vk.uploadDesc.generateMips){
        if(!RB_UseFilteredMips()){
            FastMips();
        }else{
            SlowMips();
        }
    }

    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(vk.uploadDesc.handle, texture->desc.initialState);
    RHI_CmdEndBarrier();

    RHI_EndCommandBuffer();

    vk.uploadSemaphoreCount++;
    vk.uploadCmdBufferSignaledValue[vk.uploadCmdBufferIndex] = vk.uploadSemaphoreCount;

    rhiSubmitGraphicsDesc submitDesc = {};
    submitDesc.signalSemaphoreCount = 1;
    submitDesc.signalSemaphores[0] = vk.uploadSemaphore;
    submitDesc.signalSemaphoreValues[0] = vk.uploadSemaphoreCount;
    RHI_SubmitGraphics(&submitDesc);

    vk.uploadDesc = (rhiTextureUploadDesc){};
    vk.activeCommandBuffer = previousCmdBuffer;
}

// Every chunk starts with no upload pending, so offset 0 of the staging buffer
// is free, and the wait at the end leaves it free for the next texture upload.
void RHI_UploadBufferData(rhiBuffer dst, const void *data, uint32_t byteCount)
{
    assert(uploadActive == 0);
    assert(byteCount > 0);
    const Buffer *dstBuffer = GET_BUFFER(dst);
    if(byteCount > dstBuffer->desc.byteCount){
        ri.Error(ERR_FATAL, "RHI_UploadBufferData: %u bytes exceed the %u byte buffer '%s'\n",
            byteCount, dstBuffer->desc.byteCount, dstBuffer->desc.name);
    }

    VkCommandBuffer previousCmdBuffer = vk.activeCommandBuffer;
    byte *staging = RHI_MapBuffer(vk.uploadBuffer);

    for(uint32_t offset = 0; offset < byteCount;){
        const uint32_t chunkBytes = min(byteCount - offset, vk.uploadBufferSize);
        const qbool lastChunk = offset + chunkBytes == byteCount;

        RHI_WaitOnSemaphore(vk.uploadSemaphore, vk.uploadSemaphoreCount);
        vk.uploadByteOffset = 0;
        memcpy(staging, (const byte*)data + offset, chunkBytes);
        RHI_FlushBuffer(vk.uploadBuffer, 0, chunkBytes);

        RHI_BindCommandBuffer(vk.uploadCmdBuffer[vk.uploadCmdBufferIndex]);
        RHI_BeginCommandBuffer();
        RHI_CmdBeginBarrier();
        RHI_CmdBufferBarrier(vk.uploadBuffer, RHI_ResourceState_CopySourceBit);
        RHI_CmdBufferBarrier(dst, RHI_ResourceState_CopyDestinationBit);
        RHI_CmdEndBarrier();

        RHI_CmdCopyBuffer(dst, offset, vk.uploadBuffer, 0, chunkBytes);

        if(lastChunk){
            RHI_CmdBeginBarrier();
            RHI_CmdBufferBarrier(dst, RHI_ResourceState_VertexBufferBit);
            RHI_CmdEndBarrier();
        }
        RHI_EndCommandBuffer();

        vk.uploadSemaphoreCount++;
        vk.uploadCmdBufferSignaledValue[vk.uploadCmdBufferIndex] = vk.uploadSemaphoreCount;

        rhiSubmitGraphicsDesc submitDesc = {};
        submitDesc.signalSemaphoreCount = 1;
        submitDesc.signalSemaphores[0] = vk.uploadSemaphore;
        submitDesc.signalSemaphoreValues[0] = vk.uploadSemaphoreCount;
        RHI_SubmitGraphics(&submitDesc);

        offset += chunkBytes;
    }

    RHI_WaitOnSemaphore(vk.uploadSemaphore, vk.uploadSemaphoreCount);
    vk.activeCommandBuffer = previousCmdBuffer;
}

rhiSemaphore RHI_GetUploadSemaphore(void)
{
    return vk.uploadSemaphore;
}

uint64_t RHI_GetUploadSemaphoreValue(void){
    return vk.uploadSemaphoreCount;
}

void RHI_PrintPools(void){
    ri.Printf(PRINT_ALL, "%d in vk.commandBufferPool\n", Pool_Size(&vk.commandBufferPool));
    ri.Printf(PRINT_ALL, "%d in vk.semaphorePool\n", Pool_Size(&vk.semaphorePool));
    ri.Printf(PRINT_ALL, "%d in vk.texturePool\n", Pool_Size(&vk.texturePool));
    ri.Printf(PRINT_ALL, "%d in vk.bufferPool\n", Pool_Size(&vk.bufferPool));
    ri.Printf(PRINT_ALL, "%d in vk.descriptorSetLayoutPool\n", Pool_Size(&vk.descriptorSetLayoutPool));
    ri.Printf(PRINT_ALL, "%d in vk.descriptorSetPool\n", Pool_Size(&vk.descriptorSetPool));
    ri.Printf(PRINT_ALL, "%d in vk.pipelinePool\n", Pool_Size(&vk.pipelinePool));
    ri.Printf(PRINT_ALL, "%d in vk.samplerPool\n", Pool_Size(&vk.samplerPool));
}

rhiTextureFormatId RHI_GetSwapChainFormat(void) {
    return B8G8R8A8_UNorm; //@TODO: 
}

void RHI_CmdBeginDebugLabel(const char * scope){
    if(vk.ext.EXT_debug_utils && vkCmdBeginDebugUtilsLabelEXT){
        VkDebugUtilsLabelEXT labelInfo = {};
        Vector4Set(labelInfo.color, 1.0f, 1.0f, 1.0f, 1.0f);
        labelInfo.pLabelName = scope;
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        vkCmdBeginDebugUtilsLabelEXT(vk.activeCommandBuffer, &labelInfo);
    }
}

void RHI_CmdEndDebugLabel(void){
    if(vk.ext.EXT_debug_utils && vkCmdEndDebugUtilsLabelEXT){
        vkCmdEndDebugUtilsLabelEXT(vk.activeCommandBuffer);
    }
}

void RHI_CmdInsertDebugLabel(const char * label){
    if(vk.ext.EXT_debug_utils && vkCmdInsertDebugUtilsLabelEXT){
        VkDebugUtilsLabelEXT labelInfo = {};
        Vector4Set(labelInfo.color, 1.0f, 1.0f, 1.0f, 1.0f);
        labelInfo.pLabelName = label;
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        vkCmdInsertDebugUtilsLabelEXT(vk.activeCommandBuffer, &labelInfo);
    }
    
}

const char* RHI_GetDeviceName(void){
	return vk.deviceProperties.deviceName;
}

void RHI_Screenshot(byte *buffer, rhiTexture renderTarget){
    vkDeviceWaitIdle(vk.device);

    VkCommandBuffer previousCmdBuffer = vk.activeCommandBuffer;
    RHI_BindCommandBuffer(vk.screenshotCmdBuffer);
    RHI_BeginCommandBuffer();
    Texture *srcTex = GET_TEXTURE(renderTarget);
    Texture *dstTex = GET_TEXTURE(vk.screenshotTexture);
    
    VkOffset3D blitSize;
    blitSize.x = glConfig.vidWidth;
    blitSize.y = glConfig.vidHeight;
    blitSize.z = 1;

    VkImageBlit region = {};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.layerCount = 1;
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.srcOffsets[1] = blitSize;
    region.dstOffsets[1] = blitSize;

    RHI_CmdBeginBarrier();
    RHI_CmdTextureBarrier(renderTarget, RHI_ResourceState_CopySourceBit);
    RHI_CmdTextureBarrier(vk.screenshotTexture, RHI_ResourceState_CopyDestinationBit);
    RHI_CmdEndBarrier();

    vkCmdBlitImage(vk.activeCommandBuffer, srcTex->image, srcTex->currentLayout, dstTex->image, dstTex->currentLayout, 1, &region, VK_FILTER_LINEAR);

    RHI_EndCommandBuffer();

    rhiSubmitGraphicsDesc graphicsDesc = {};
    RHI_SubmitGraphics(&graphicsDesc);

    vkDeviceWaitIdle(vk.device);

    VkImageSubresource subresource = {};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    VkSubresourceLayout layout = {};
    vkGetImageSubresourceLayout(vk.device, dstTex->image, &subresource, &layout);

    void *readBackBuffer;
    vmaMapMemory(vk.allocator, dstTex->allocation, &readBackBuffer);
    for(int i = 0; i < glConfig.vidHeight; i++){
        memcpy(buffer + (glConfig.vidHeight - 1 - i) * glConfig.vidWidth * 4, (byte*)readBackBuffer + i * layout.rowPitch, glConfig.vidWidth * 4);
    }
    vmaUnmapMemory(vk.allocator, dstTex->allocation);
    
    vk.activeCommandBuffer = previousCmdBuffer;
}


qboolean RHI_IsFrameSleepEnabled(void){
    switch(vk.presentMode){
        case VK_PRESENT_MODE_MAILBOX_KHR:
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return qtrue;
        case VK_PRESENT_MODE_FIFO_KHR:
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return qfalse;
        default:
            assert(!"Unknown presentMode");
            return qfalse;
    }
}

// #define RHI_PRINT_POOL(name, x) \
//         TableRowInt(

#define POOL_LIST(P) \
        P(vk.commandBufferPool, "Command Buffers") \
        P(vk.semaphorePool, "Semaphores") \
        P(vk.texturePool, "Textures") \
        P(vk.bufferPool, "Buffers") \
        P(vk.descriptorSetLayoutPool, "Descriptor Set Layouts") \
        P(vk.descriptorSetPool, "Descriptor Set") \
        P(vk.pipelinePool, "Pipelines") \
        P(vk.samplerPool, "Samplers") 



void DrawGUI_RHI(void){

    static qbool breakdownActive = qfalse;
	ToggleBooleanWithShortcut(&breakdownActive, ImGuiKey_R, ImGUI_ShortcutOptions_Global);
	GUI_AddMainMenuItem(ImGUI_MainMenu_Info, "RHI Status", "Ctrl+Shift+R", &breakdownActive, qtrue);
	if(breakdownActive){
		if(igBegin("RHI", (bool*)&breakdownActive, 0)) {
			igNewLine();
			if(igBeginTable("Status",2,ImGuiTableFlags_RowBg,(ImVec2){0,0},0.0f)){
				TableHeader(2, "Pool", "Size");
                #define POOL_ITEM(pool, name) TableRowInt(name, Pool_Size(&pool));
                POOL_LIST(POOL_ITEM);
                #undef POOL_ITEM
				igEndTable();
			}

            if(igBeginTable("Other stats",2,ImGuiTableFlags_RowBg,(ImVec2){0,0},0.0f)){
				TableRowInt("Images", tr.numImages);
                TableRowInt("Shaders", tr.numShaders);

            	igEndTable();
			}
		}

		igEnd();
	}
}
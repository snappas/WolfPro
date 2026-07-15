#ifndef TR_VULKAN_H
#define TR_VULKAN_H
//#pragma once

#include "tr_local.h"
#include "volk.h"
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#include "vk_mem_alloc.h"
#include "memorypool.h"

#define GET_SEMAPHORE(handle) ((Semaphore*)Pool_Get(&vk.semaphorePool, handle.h))
#define GET_TEXTURE(handle) ((Texture*)Pool_Get(&vk.texturePool, handle.h))
#define GET_BUFFER(handle) ((Buffer*)Pool_Get(&vk.bufferPool, handle.h))
#define GET_LAYOUT(handle) ((DescriptorSetLayout*)Pool_Get(&vk.descriptorSetLayoutPool, handle.h))
#define GET_PIPELINE(handle) ((Pipeline*)Pool_Get(&vk.pipelinePool, handle.h))
#define GET_DESCRIPTORSET(handle) ((DescriptorSet*)Pool_Get(&vk.descriptorSetPool, handle.h))
#define GET_SAMPLER(handle) ((Sampler*)Pool_Get(&vk.samplerPool, handle.h))


#define MAX_LAYERS 16
#define MAX_EXTENSIONS 16
#define MAX_SWAP_CHAIN_IMAGES 16
#define MAX_TEXTURES (MAX_IMAGEDESCRIPTORS) // needs space for render targets too
#define MAX_DURATION_QUERIES 64
#define MAX_UPLOADCMDBUFFERS 64

#define MAX_TEXTURE_SIZE 2048

#define TEXTURE_FORMAT_RGBA VK_FORMAT_R8G8B8A8_UNORM

// nuke these two - set some preferred format list and use what's available
// option #1: preferred
// CS     VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
// FORMAT VK_FORMAT_B8G8R8A8_UNORM
// option #2:
// CS     VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
// FORMAT VK_FORMAT_B8G8R8A8_SRGB
#define SURFACE_FORMAT_RGBA VK_FORMAT_B8G8R8A8_UNORM

// Nvidia Nsight Systems 2020.2 is supposed to support Vulkan 1.2
// on my PC, it only captures with Vulkan 1.1 :-(
#define MINIMUM_VULKAN_API_VERSION (VK_MAKE_API_VERSION(0, 1, 3, 0))

#define BIT_MASK(BitCount) ((1 << BitCount) - 1)

#define VK(call) Check((call), #call)


#define VULKAN_SUCCESS_CODES(N) \
	N(VK_SUCCESS, "Command successfully completed") \
	N(VK_NOT_READY, "A fence or query has not yet completed") \
	N(VK_TIMEOUT, "A wait operation has not completed in the specified time") \
	N(VK_EVENT_SET, "An event is signaled") \
	N(VK_EVENT_RESET, "An event is unsignaled") \
	N(VK_INCOMPLETE, "A return array was too small for the result") \
	N(VK_SUBOPTIMAL_KHR, "A swapchain no longer matches the surface properties exactly, but can still be used to present to the surface successfully.") \
	N(VK_THREAD_IDLE_KHR, "A deferred operation is not complete but there is currently no work for this thread to do at the time of this call.") \
	N(VK_THREAD_DONE_KHR, "A deferred operation is not complete but there is no work remaining to assign to additional threads.") \
	N(VK_OPERATION_DEFERRED_KHR, "A deferred operation was requested and at least some of the work was deferred.") \
	N(VK_OPERATION_NOT_DEFERRED_KHR, "A deferred operation was requested and no operations were deferred.") \
	N(VK_PIPELINE_COMPILE_REQUIRED_EXT, "A requested pipeline creation would have required compilation, but the application requested compilation to not be performed.")

#define VULKAN_ERROR_CODES(N) \
	N(VK_ERROR_OUT_OF_HOST_MEMORY, "A host memory allocation has failed.") \
	N(VK_ERROR_OUT_OF_DEVICE_MEMORY, "A device memory allocation has failed.") \
	N(VK_ERROR_INITIALIZATION_FAILED, "Initialization of an object could not be completed for implementation-specific reasons.") \
	N(VK_ERROR_DEVICE_LOST, "The logical or physical device has been lost. See Lost Device") \
	N(VK_ERROR_MEMORY_MAP_FAILED, "Mapping of a memory object has failed.") \
	N(VK_ERROR_LAYER_NOT_PRESENT, "A requested layer is not present or could not be loaded.") \
	N(VK_ERROR_EXTENSION_NOT_PRESENT, "A requested extension is not supported.") \
	N(VK_ERROR_FEATURE_NOT_PRESENT, "A requested feature is not supported.") \
	N(VK_ERROR_INCOMPATIBLE_DRIVER, "The requested version of Vulkan is not supported by the driver or is otherwise incompatible for implementation-specific reasons.") \
	N(VK_ERROR_TOO_MANY_OBJECTS, "Too many objects of the type have already been created.") \
	N(VK_ERROR_FORMAT_NOT_SUPPORTED, "A requested format is not supported on this device.") \
	N(VK_ERROR_FRAGMENTED_POOL, "A pool allocation has failed due to fragmentation of the pool's memory. This must only be returned if no attempt to allocate host or device memory was made to accommodate the new allocation. This should be returned in preference to VK_ERROR_OUT_OF_POOL_MEMORY, but only if the implementation is certain that the pool allocation failure was due to fragmentation.") \
	N(VK_ERROR_SURFACE_LOST_KHR, "A surface is no longer available.") \
	N(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR, "The requested window is already in use by Vulkan or another API in a manner which prevents it from being used again.") \
	N(VK_ERROR_OUT_OF_DATE_KHR, "A surface has changed in such a way that it is no longer compatible with the swapchain, and further presentation requests using the swapchain will fail. Applications must query the new surface properties and recreate their swapchain if they wish to continue presenting to the surface.") \
	N(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR, "The display used by a swapchain does not use the same presentable image layout, or is incompatible in a way that prevents sharing an image.") \
	N(VK_ERROR_INVALID_SHADER_NV, "One or more shaders failed to compile or link. More details are reported back to the application via VK_EXT_debug_report if enabled.") \
	N(VK_ERROR_OUT_OF_POOL_MEMORY, "A pool memory allocation has failed. This must only be returned if no attempt to allocate host or device memory was made to accommodate the new allocation. If the failure was definitely due to fragmentation of the pool, VK_ERROR_FRAGMENTED_POOL should be returned instead.") \
	N(VK_ERROR_INVALID_EXTERNAL_HANDLE, "An external handle is not a valid handle of the specified type.") \
	N(VK_ERROR_FRAGMENTATION, "A descriptor pool creation has failed due to fragmentation.") \
	N(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS, "A buffer creation or memory allocation failed because the requested address is not available. A shader group handle assignment failed because the requested shader group handle information is no longer valid.") \
	N(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT, "An operation on a swapchain created with VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT failed as it did not have exlusive full-screen access. This may occur due to implementation - dependent reasons, outside of the application�s control.") \
	N(VK_ERROR_UNKNOWN, "An unknown error has occurred; either the application has provided invalid input, or an implementation failure has occurred.") \
	N(VK_ERROR_VALIDATION_FAILED_EXT, "A validation error has occurred.")


#define MAX_LAYERS 16
#define MAX_EXTENSIONS 16

typedef struct Queues
{
	// graphics is graphics *and* compute
	// present is presentation and *can* be the same as graphics
	VkQueue graphics;
	VkQueue present;
	uint32_t graphicsFamily;
	uint32_t presentFamily;
} Queues;

// typedef struct Fence
// {
// 	VkFence fence;
// 	qbool submitted;
// } Fence;

typedef struct Semaphore
{
	VkSemaphore semaphore;
	qbool signaled;
	qbool binary;
	qbool longLifetime;
} Semaphore;

typedef struct CommandBuffer
{
	VkCommandBuffer commandBuffer;
	VkCommandPool commandPool; // the owner of this command
	qbool longLifetime;
} CommandBuffer;

typedef struct Texture
{
	VkImage image;
	VkImageView view;
	VkImageView mipViews[16];
	VmaAllocation allocation;
	rhiTextureDesc desc;
	VkFormat format;
	qbool ownsImage;
	VkImageLayout currentLayout;
	
} Texture;

typedef struct PipelineLayout
{
	VkPipelineLayout pipelineLayout;
	PushConstantsRange constantRanges[rhiShaderTypeIdCount];
 } PipelineLayout;

typedef struct Pipeline
{
	PipelineLayout layout;
	VkPipeline pipeline;
	qbool compute;
	union {
		rhiGraphicsPipelineDesc graphicsDesc;
		rhiComputePipelineDesc computeDesc;
	};
	
	uint32_t pushConstantOffsets[RHI_Shader_Count];
	uint32_t pushConstantSize[RHI_Shader_Count];
} Pipeline;

typedef struct Sampler
{
	VkSampler sampler;
} Sampler;


// typedef struct Shader
// {
// 	VkShaderModule module;
// 	galShaderTypeId type;
// } Shader;

typedef struct DescriptorSetLayout
{
	VkDescriptorSetLayout layout;
	rhiDescriptorSetLayoutDesc desc;
} DescriptorSetLayout;

typedef struct DescriptorSet
{
	VkDescriptorSet set;
	rhiDescriptorSetLayout layout;
	qbool longLifetime;
} DescriptorSet;

typedef struct
{
	rhiBufferDesc desc;
	void* mappedData; // only if host coherent
	VkBuffer buffer;
	VmaAllocation allocation;
	RHI_MemoryUsage memoryUsage;
	qbool mapped;
	qbool hostCoherent;
	RHI_ResourceState currentLayout;

} Buffer;

typedef struct mipmapXPushConstants {
	uint32_t srcIndex;
	uint32_t clamp;
	uint32_t dstWidth;
	uint32_t dstHeight;
	float weights[4];
} mipmapXPushConstants;

typedef struct mipmapYPushConstants {
	uint32_t dstIndex;
	uint32_t clamp;
	uint32_t srcWidth;
	uint32_t srcHeight;
	float weights[4];
} mipmapYPushConstants;

typedef struct mipmapPushConstants {
	uint32_t srcIndex;
	uint32_t dstIndex;
	uint32_t clamp;
} mipmapPushConstants;

typedef struct Vulkan
{
    //
	// init state
	//

	qbool initialized;
	int width;
	int height;

	//
	// core
	// 
	int layerCount;
	int extensionCount;
	const char* layers[MAX_LAYERS];
	const char* extensions[MAX_LAYERS];

	VkInstance instance;
	VkSurfaceKHR surface;

	Queues queues;
	VkPhysicalDevice physicalDevice;
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;

	VkDevice device;

	VmaAllocator allocator;

	VkSwapchainKHR swapChain;
	VkSurfaceFormatKHR swapChainFormat;
	uint32_t swapChainImageCount;
	VkImage swapChainImages[MAX_SWAP_CHAIN_IMAGES];
	rhiTexture swapChainRenderTargets[MAX_SWAP_CHAIN_IMAGES];

	rhiTexture textureBarriers[2048];
	uint32_t textureBarrierCount;
	RHI_ResourceState textureState[2048];

	rhiBuffer bufferBarriers[64];
	uint32_t bufferBarrierCount;
	RHI_ResourceState bufferState[64];

	VkDescriptorPool descriptorPool;
	VkCommandPool commandPool;

	memoryPool commandBufferPool;

	VkCommandBuffer activeCommandBuffer;

	memoryPool semaphorePool;

	memoryPool texturePool;

	memoryPool bufferPool;

	memoryPool descriptorSetLayoutPool;
	memoryPool descriptorSetPool;

	memoryPool pipelinePool;
	memoryPool samplerPool;

	rhiBuffer uploadBuffer;
	rhiTextureUploadDesc uploadDesc;

	rhiCommandBuffer uploadCmdBuffer[MAX_UPLOADCMDBUFFERS];
	uint64_t uploadCmdBufferSignaledValue[MAX_UPLOADCMDBUFFERS];
	uint32_t uploadCmdBufferIndex;
	rhiDescriptorSet uploadDescriptorSets[MAX_UPLOADCMDBUFFERS];
	rhiDescriptorSetLayout mipmapLayout;
	rhiPipeline mipmapPipeline;
	rhiPipeline mipmapXPipeline;
	rhiPipeline mipmapYPipeline;
	rhiTexture mipmapScratchTexture;

	//
	// extensions
	// 
	struct Extensions
	{
		qbool EXT_validation_features;
		qbool EXT_debug_utils;
		VkDebugUtilsMessengerEXT debugMessenger; // EXT_debug_utils
		qbool EXT_calibrated_timestamps; // only ever requested/set when ENABLE_PROFILER is defined
	} ext;

	qboolean renderingActive;
    RHI_RenderPass currentRenderPass;
	VkQueryPool queryPool[RHI_FRAMES_IN_FLIGHT];
	uint32_t query[RHI_FRAMES_IN_FLIGHT][MAX_DURATION_QUERIES];
	uint32_t currentFrameIndex;
	uint32_t durationQueryCount[RHI_FRAMES_IN_FLIGHT];
	qboolean vsync;

	uint32_t uploadByteOffset;
	uint32_t uploadByteCount;
	uint32_t uploadBufferSize;

	rhiSemaphore uploadSemaphore;
	uint64_t uploadSemaphoreCount;

	rhiTexture screenshotTexture;
	rhiCommandBuffer screenshotCmdBuffer;

	VkPresentModeKHR presentMode;
	
} Vulkan;

extern Vulkan vk;

void Check(VkResult result, const char* function);
void SetObjectName(VkObjectType type, uint64_t object, const char* name);
VkFormat GetVkFormat(rhiTextureFormatId format);
VkImageAspectFlags GetVkImageAspectFlags(VkFormat format);
VkImageLayout GetVkImageLayout(RHI_ResourceState state);
VkAccessFlags2 GetVkAccessFlags(VkImageLayout state);
VkPipelineStageFlags2 GetVkStageFlags(VkImageLayout state);
VkAttachmentLoadOp GetVkAttachmentLoadOp(RHI_LoadOp load);
VmaMemoryUsage GetVmaMemoryUsage(RHI_MemoryUsage usage);
VkBufferUsageFlags GetVkBufferUsageFlags(RHI_ResourceState state);
VkCullModeFlags GetVkCullModeFlags(cullType_t cullType);
VkImageUsageFlags GetVkImageUsageFlags(RHI_ResourceState state);
uint32_t GetByteCountsPerPixel(VkFormat format);
VkPipelineStageFlags2 GetVkStageFlagsFromResource(RHI_ResourceState state);
VkAccessFlags2 GetVkAccessFlagsFromResource(RHI_ResourceState state);
VkDescriptorType GetVkDescriptorType(RHI_DescriptorType type);
VkShaderStageFlags GetVkShaderStageFlags(RHI_PipelineStage stage);
VkShaderStageFlags GetVkShaderStageFlagsFromShader(RHI_Shader stage);
VkBlendFactor GetSourceColorBlendFactor(unsigned int bits);
VkBlendFactor GetDestinationColorBlendFactor(unsigned int bits);
VkFormat GetVkFormatFromVertexFormat(RHI_VertexFormat format, uint32_t elementCount);
VkSampleCountFlagBits GetVkSampleCount(uint32_t samples);
void RecreateSwapchain(void);

#endif
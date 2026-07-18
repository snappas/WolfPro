#ifndef RHI_H
#define RHI_H
//#pragma once 

#include "../game/q_shared.h"
#include "../qcommon/qfiles.h"
#include "../qcommon/qcommon.h"

#define RHI_FRAMES_IN_FLIGHT			2


// Video initialization:
// - loading Vulkan
// - creating a window and changing video mode if needed,
// - respecting r_fullscreen, r_mode, r_width, r_height
// - creating a valid Vulkan surface
// - filling up the right glconfig fields (see glconfig_t definition)
// returns the surface handle
uint64_t Sys_Vulkan_Init(void* vkInstance);
void Sys_Vulkan_Shutdown(void);
qboolean Sys_Vulkan_GetRequiredExtensions(char **pNames, int *pCount);

typedef uint64_t rhiHandle;

#define RHI_HANDLE_TYPE(TypeName) typedef struct { rhiHandle h; }  TypeName;

RHI_HANDLE_TYPE(rhiSemaphore) //either binary or timeline semaphore
RHI_HANDLE_TYPE(rhiTexture) //1d,2d,3d data that can be filtered when read (render target when writing from graphics pipeline operation)
RHI_HANDLE_TYPE(rhiCommandPool) //memory region for allocating command buffers, once in init
RHI_HANDLE_TYPE(rhiCommandBuffer) //list of commands to be executed on the gpu
									//allocated from cmd pool in init, submitted to a queue
									//clear command buffer every frame
									//submit to gpu every frame
RHI_HANDLE_TYPE(rhiDescriptorSetLayout) //descriptor is any resource you use from a shader (buffers/textures/samplers can be descriptors)
										//layout describes which range in the set has which type of resource
										//ex: read only textures, read-write, samplers, RO/RW/WO buffers
RHI_HANDLE_TYPE(rhiPipeline) //pipeline state object (PSO) all of the state of the graphics pipeline 
							// (what is expensive to change on the GPU)
							// describes what state the pipeline needs to be in
							// viewport, scissor rect, cull mode (*?) are not in the pipeline state
							// viewport related to the projection matrix
							// scissor rect is where you are allowed to write (outside of this does not get written to render target)
RHI_HANDLE_TYPE(rhiShader) //programmable logic to run on the gpu (vertex/fragment shading) multiple fragments can contribute to a pixel (MSAA)

RHI_HANDLE_TYPE(rhiBuffer) //raw data in gpu space (vertex/index can be specailized for the API's knowledge of what is in it)
RHI_HANDLE_TYPE(rhiDescriptorSet)
RHI_HANDLE_TYPE(rhiSampler)

RHI_HANDLE_TYPE(rhiDurationQuery)


#define ALIGN_UP(x, n) ((x + n - 1) & (~(n - 1)))
#define ALIGN_DOWN(x, n) (x & ~(n - 1))


#define RHI_BIT(x)						(1 << x)


//typedef enum rhiResourceStateFlags {
//        rhiResourceStateUndefined = 0,
//        VertexBufferBit = RHI_BIT(0),
//        IndexBufferBit = RHI_BIT(1),
//        RenderTargetBit = RHI_BIT(2),
//        PresentBit = RHI_BIT(3),
//        ShaderInputBit = RHI_BIT(4),
//        CopySourceBit = RHI_BIT(5),
//        CopyDestinationBit = RHI_BIT(6),
//        DepthWriteBit = RHI_BIT(7),
//        UnorderedAccessBit = RHI_BIT(8),
//        CommonBit = RHI_BIT(9),
//        IndirectCommandBit = RHI_BIT(10),
//        rhiResourceStateUniformBufferBit = RHI_BIT(11),
//        rhiResourceStateStorageBufferBit = RHI_BIT(12),
//        DepthReadBit = RHI_BIT(13)
//} rhiResourceStateFlags;


typedef enum RHI_ResourceState {
	RHI_ResourceState_Undefined = 0,
	RHI_ResourceState_RenderTargetBit = RHI_BIT(0),
	RHI_ResourceState_PresentBit = RHI_BIT(1),
	RHI_ResourceState_VertexBufferBit = RHI_BIT(2),
	RHI_ResourceState_IndexBufferBit = RHI_BIT(3),
	RHI_ResourceState_DepthWriteBit = RHI_BIT(4),
	RHI_ResourceState_CopySourceBit = RHI_BIT(5),
	RHI_ResourceState_CopyDestinationBit = RHI_BIT(6),
	RHI_ResourceState_ShaderInputBit = RHI_BIT(7),
	RHI_ResourceState_ShaderReadWriteBit = RHI_BIT(8)
} RHI_ResourceState;

typedef enum RHI_LoadOp {
	RHI_LoadOp_Load,
	RHI_LoadOp_Clear,
	RHI_LoadOp_Discard
} RHI_LoadOp;

typedef struct RHI_RenderPass {
	RHI_LoadOp colorLoad;
	RHI_LoadOp depthLoad;
	rhiTexture colorTexture;
	rhiTexture depthTexture;
	vec4_t color;
	float depth;
	uint32_t width;
	uint32_t height;
	uint32_t x;
	uint32_t y;
} RHI_RenderPass;


typedef enum rhiTextureFormatId
{
	RHI_TextureFormat_Invalid,
	R8G8B8A8_UNorm,
	B8G8R8A8_UNorm,
	B8G8R8A8_sRGB,
	R16G16B16A16_SFloat,
	D16_UNorm,
	D32_SFloat,
	//D24_UNorm_S8_UInt, // this is not well supported (:wave: AMD)
	R32_UInt,
	RHI_TextureFormat_Count
} rhiTextureFormatId;

typedef enum RHI_MemoryUsage
{
	RHI_MemoryUsage_DeviceLocal,
	RHI_MemoryUsage_Upload,
	RHI_MemoryUsage_Readback,
	RHI_MemoryUsage_Count
} RHI_MemoryUsage;
typedef struct rhiTextureDesc
{
	uint64_t nativeImage;
    uint64_t nativeFormat;
	uint64_t nativeLayout; 
	const char* name;
	uint32_t width;
	uint32_t height;
	uint32_t mipCount;
	uint32_t sampleCount;
	RHI_ResourceState allowedStates;
	RHI_ResourceState initialState;
	rhiTextureFormatId format;
	qbool longLifetime;
	RHI_MemoryUsage memoryUsage;
} rhiTextureDesc;

typedef struct PushConstantsRange
{
	uint32_t byteOffset;
	uint32_t byteCount;
} PushConstantsRange; 

typedef enum rhiShaderTypeId
{
	rhiShaderTypeIdVertex,
	rhiShaderTypeIdPixel,
	rhiShaderTypeIdCompute,
	rhiShaderTypeIdCount
} rhiShaderTypeId;

typedef struct rhiPipelineLayoutDesc
{
	PushConstantsRange pushConstantsPerStage[rhiShaderTypeIdCount];
	const char* name;
	const rhiDescriptorSetLayout* descriptorSetLayouts;
	uint32_t descriptorSetLayoutCount;
} rhiPipelineLayoutDesc;

typedef enum rhiDataTypeId
{
	Float32,
	UNorm8,
	UInt32,
	rhiDataTypeIdCount
} rhiDataTypeId;

typedef struct rhiVertexAttribDesc
{
	uint32_t binding;
	uint32_t location;
	rhiDataTypeId dataType;
	uint32_t vectorLength;
	uint32_t byteOffset;
} rhiVertexAttribDesc;

typedef struct rhiVertexBindingDesc
{
	uint32_t binding;
	uint32_t stride;
} rhiVertexBindingDesc;

typedef struct VertexLayout
{
	const rhiVertexAttribDesc* attribs;
	const rhiVertexBindingDesc* bindings;
	uint32_t attribCount;
	uint32_t bindingCount;
} VertexLayout;

typedef struct rhiSpecializationEntry
{
	uint32_t constandId;
	uint32_t byteOffset;
	uint32_t byteCount;
} rhiSpecializationEntry;

typedef struct rhiSpecialization
{
	const rhiSpecializationEntry* entries;
	const void* data;
	uint32_t entryCount;
	uint32_t byteCount;
} rhiSpecialization;





typedef struct rhiBufferDesc
{
	const char* name;
	uint32_t byteCount;
	RHI_ResourceState initialState;
	RHI_MemoryUsage memoryUsage;
	RHI_ResourceState allowedStates;
	qbool longLifetime;
} rhiBufferDesc;

typedef struct rhiSubmitGraphicsDesc {
    rhiSemaphore signalSemaphores[8];
    uint16_t signalSemaphoreCount;
    uint64_t signalSemaphoreValues[8];
    rhiSemaphore waitSemaphores[8];
    uint16_t waitSemaphoreCount;
	uint64_t waitSemaphoreValues[8];
} rhiSubmitGraphicsDesc;

typedef enum rhiImageLayoutFormat {
	ImageLayoutUndefined = 0,
	ImageLayoutGeneral
} rhiImageLayoutFormat;

typedef struct rhiTextureUpload {
	byte *data;
	uint32_t rowPitch;
	uint32_t width;
	uint32_t height;
} rhiTextureUpload;

typedef void (*RHI_TextureUploadCallback)(void* userdata);

typedef struct rhiTextureUploadDesc {
	rhiTexture handle;
	uint32_t mipLevel;
	qboolean generateMips;
	qboolean clamp;
} rhiTextureUploadDesc;

typedef enum RHI_DescriptorType {
	RHI_DescriptorType_None = 0,
	RHI_DescriptorType_Sampler,
	RHI_DescriptorType_ReadOnlyBuffer, //Uniform buffer
	RHI_DescriptorType_ReadWriteBuffer, //Storage buffer
	RHI_DescriptorType_ReadOnlyTexture, //Sampled image
	RHI_DescriptorType_ReadWriteTexture // ???

} RHI_DescriptorType;

typedef enum RHI_PipelineStage {
	RHI_PipelineStage_None = 0,
	RHI_PipelineStage_VertexBit = RHI_BIT(0),
	RHI_PipelineStage_PixelBit = RHI_BIT(1),
	RHI_PipelineStage_ComputeBit = RHI_BIT(2)
} RHI_PipelineStage;

typedef struct rhiDescriptorSetLayoutBinding {
	uint32_t descriptorCount;
	RHI_DescriptorType descriptorType;
	RHI_PipelineStage stageFlags;

} rhiDescriptorSetLayoutBinding; 

typedef struct rhiDescriptorSetLayoutDesc {
	rhiDescriptorSetLayoutBinding bindings[4];
	uint32_t bindingCount;
	const char *name;
	qbool longLifetime;
} rhiDescriptorSetLayoutDesc; 

typedef enum RHI_TextureAddressing {
	RHI_TextureAddressing_Repeat = 0,
	RHI_TextureAddressing_Clamp,
	RHI_TextureAddressing_Count
} RHI_TextureAddressing;

typedef struct rhiPushConstants {
	uint32_t psBytes;
	uint32_t vsBytes;
	uint32_t csBytes;
} rhiPushConstants;

typedef enum RHI_Shader {
	RHI_Shader_Vertex = 0,
	RHI_Shader_Pixel,
	RHI_Shader_Compute,
	RHI_Shader_Count
} RHI_Shader;

typedef struct rhiShaderDesc {
	const void *data;
	uint32_t byteCount;

} rhiShaderDesc;

// typedef struct rhiGraphicsPipelineDesc
// {
// 	const char* name;
	
// 	VertexLayout vertexLayout;
// 	rhiShader vertexShader;
// 	rhiShader fragmentShader;
// 	rhiSpecialization vertexSpec;
// 	rhiSpecialization fragmentSpec;
// 	uint32_t cullType; // cullType_t
// 	uint32_t srcBlend; // stateBits & GLS_SRCBLEND_BITS
// 	uint32_t dstBlend; // stateBits & GLS_DSTBLEND_BITS
// 	uint32_t depthTest; // depthTest_t
// 	qbool enableDepthWrite;
// 	qbool enableDepthTest;
// } rhiGraphicsPipelineDesc;

typedef enum RHI_VertexFormat {
	RHI_VertexFormat_Float32 = 0,
	RHI_VertexFormat_UNorm8,
	RHI_VertexFormat_UInt32,
	RHI_VertexFormat_Count
} RHI_VertexFormat;

typedef struct rhiVertexAttributeDesc {
	uint32_t elementCount;
	RHI_VertexFormat elementFormat;
	uint32_t offset;
	uint32_t bufferBinding;
} rhiVertexAttributeDesc;

typedef struct rhiVertexBufferBindingDesc {
	uint32_t stride;
} rhiVertexBufferBindingDesc;

typedef struct rhiGraphicsPipelineDesc {
	const char *name;
	rhiDescriptorSetLayout descLayout;
	rhiPushConstants pushConstants;
	rhiShaderDesc vertexShader;
	rhiShaderDesc pixelShader;
	uint32_t cullType; //cullType_t
	qboolean polygonOffset;
	unsigned int srcBlend;
	unsigned int dstBlend;
	qboolean depthWrite;
	qboolean depthTest;
	qboolean depthTestEqual;
	qboolean wireframe;
	rhiVertexAttributeDesc attributes[8];
	rhiVertexBufferBindingDesc vertexBuffers[8];
	uint32_t vertexBufferCount;
	uint32_t attributeCount;
	qbool longLifetime;
	rhiTextureFormatId colorFormat;
	uint32_t sampleCount;
	qbool alphaToCoverage;
} rhiGraphicsPipelineDesc;

typedef struct rhiComputePipelineDesc {
	const char *name;
	rhiDescriptorSetLayout descLayout;
	rhiShaderDesc shader;
	qbool longLifetime;
	uint32_t pushConstantsBytes;
} rhiComputePipelineDesc;



void RHI_Init( void );
void RHI_Shutdown(qboolean destroyWindow);

void RHI_WaitUntilDeviceIdle(); //debug only

rhiSemaphore RHI_CreateBinarySemaphore( void );
rhiSemaphore RHI_CreateTimelineSemaphore( qboolean longLifetime );
void RHI_WaitOnSemaphore(rhiSemaphore semaphore, uint64_t semaphoreValue);

rhiBuffer RHI_CreateBuffer(const rhiBufferDesc *desc);
byte* RHI_MapBuffer(rhiBuffer buffer); //cpu visible buffers
void RHI_UnmapBuffer(rhiBuffer buffer);

rhiTexture RHI_CreateTexture(const rhiTextureDesc *desc);

rhiSampler RHI_CreateSampler(const char* name, RHI_TextureAddressing mode, uint32_t anisotropy);

void RHI_CreateShader();

rhiDescriptorSetLayout RHI_CreateDescriptorSetLayout(const rhiDescriptorSetLayoutDesc *desc);
rhiDescriptorSet RHI_CreateDescriptorSet(const char *name, rhiDescriptorSetLayout layoutHandle, qboolean longLifetime);
void RHI_UpdateDescriptorSet(rhiDescriptorSet descriptorHandle, uint32_t bindingIndex, RHI_DescriptorType type, uint32_t offset, uint32_t descriptorCount, const void *handles, uint32_t mipIndex); //rhiTexture, rhiSampler, rhiBuffer

rhiPipeline RHI_CreateGraphicsPipeline(const rhiGraphicsPipelineDesc *graphicsDesc);
rhiPipeline RHI_CreateComputePipeline(const rhiComputePipelineDesc *computeDesc);


rhiTexture* RHI_GetSwapChainImages( void );
uint32_t RHI_GetSwapChainImageCount( void );
rhiTextureFormatId RHI_GetSwapChainFormat(void);

void RHI_AcquireNextImage(uint32_t* outImageIndex, rhiSemaphore signalSemaphore);


void RHI_SubmitGraphics(const rhiSubmitGraphicsDesc* graphicsDesc);
void RHI_SubmitPresent(rhiSemaphore waitSemaphore, uint32_t swapChainImageIndex);

rhiCommandBuffer RHI_CreateCommandBuffer(qboolean longLifetime); // pass queue enum as argument
void RHI_BindCommandBuffer(rhiCommandBuffer cmdBuffer);
void RHI_BeginCommandBuffer( void );
void RHI_EndCommandBuffer( void );

void RHI_BeginRendering(const RHI_RenderPass *renderPass); //bind render targets
void RHI_EndRendering( void );
qboolean RHI_IsRenderingActive(void);
RHI_RenderPass* RHI_CurrentRenderPass(void);

void RHI_CmdBindPipeline(rhiPipeline pipeline); //create pipeline layout here
void RHI_CmdBindDescriptorSet(rhiPipeline pipeline, rhiDescriptorSet descriptorSet);
void RHI_CmdBindVertexBuffers(const rhiBuffer *vertexBuffers, uint32_t bufferCount);
void RHI_CmdBindIndexBuffer(rhiBuffer indexBuffer);
void RHI_CmdSetViewport(int32_t x, int32_t y, uint32_t width, uint32_t height, float minDepth, float maxDepth);
void RHI_CmdSetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
void RHI_CmdPushConstants(rhiPipeline pipeline, RHI_Shader shader, const void *constants, uint32_t byteCount);
void RHI_CmdDraw(uint32_t vertexCount, uint32_t firstVertex); //non indexed triangles (not for 2d)
void RHI_CmdDrawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex);
void RHI_CmdDispatch(uint32_t x, uint32_t y, uint32_t z);

void RHI_CmdBeginBarrier( void );
void RHI_CmdEndBarrier( void );
void RHI_CmdTextureBarrier(rhiTexture handle, RHI_ResourceState flag); //pass handle to this
void RHI_CmdBufferBarrier(rhiBuffer handle, RHI_ResourceState state); //pass handle to this //(not for 2d)

void RHI_CmdCopyBuffer(rhiBuffer dstBuffer, uint32_t dstOffset, rhiBuffer srcBuffer, uint32_t srcOffset, uint32_t size);
void RHI_CmdCopyBufferRegions();
void RHI_CmdBeginDebugLabel(const char * scope);
void RHI_CmdEndDebugLabel(void);
void RHI_CmdInsertDebugLabel(const char * label);

rhiDurationQuery RHI_CmdBeginDurationQuery(void);
void RHI_CmdEndDurationQuery(rhiDurationQuery handle);
uint32_t RHI_GetDurationUs(rhiDurationQuery query);
qbool RHI_GetDurationTimestamps(rhiDurationQuery query, uint64_t *beginTicks, uint64_t *endTicks); // raw GPU tick pair, before any timestampPeriod conversion -- qfalse (out-params untouched) if the handle is invalid/not fully recorded
#if defined( ENABLE_PROFILER )
void RHI_UpdateGPUCalibration(void); // cheap to call every frame; internally rate-limits itself
qbool RHI_GPUTicksToCpuUs(uint64_t gpuTicks, int64_t *outCpuUs); // qfalse if no successful calibration has happened yet
double RHI_GetTimestampPeriodUs(void); // vk.deviceProperties.limits.timestampPeriod, in microseconds -- lets callers outside rhi.c convert a raw GPU tick delta without pulling in tr_vulkan.h
#endif
void RHI_DurationQueryReset(void);

//upload manager
void RHI_BeginBufferUpload();
void RHI_EndBufferUpload();



void RHI_BeginTextureUpload(rhiTextureUpload *textureUpload, const rhiTextureUploadDesc *textureUploadDesc);
void RHI_EndTextureUpload();
rhiSemaphore RHI_GetUploadSemaphore(void); //(semaphore handle, value to wait on)
uint64_t RHI_GetUploadSemaphoreValue(void);

void RHI_PrintPools(void);

const char* RHI_GetDeviceName(void);
uint32_t RHI_GetIndexFromHandle(uint64_t handle);

void RHI_Screenshot(byte *buffer, rhiTexture renderTarget);

#endif
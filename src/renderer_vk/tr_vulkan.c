
#include "tr_vulkan.h"

#ifdef _WIN32
#include <Windows.h>
#endif


Vulkan vk;
RHIExport rhie;

static const char* GetStringForVkObjectType(VkObjectType type)
{
    switch(type)
    {
        case VK_OBJECT_TYPE_INSTANCE: return "instance";
        case VK_OBJECT_TYPE_PHYSICAL_DEVICE: return "physical device";
        case VK_OBJECT_TYPE_DEVICE: return "logical device";
        case VK_OBJECT_TYPE_QUEUE: return "queue";
        case VK_OBJECT_TYPE_SEMAPHORE: return "semaphore";
        case VK_OBJECT_TYPE_COMMAND_BUFFER: return "command buffer";
        case VK_OBJECT_TYPE_FENCE: return "fence";
        case VK_OBJECT_TYPE_DEVICE_MEMORY: return "device memory";
        case VK_OBJECT_TYPE_BUFFER: return "buffer";
        case VK_OBJECT_TYPE_IMAGE: return "texture";
        case VK_OBJECT_TYPE_EVENT: return "event";
        case VK_OBJECT_TYPE_QUERY_POOL: return "query pool";
        case VK_OBJECT_TYPE_BUFFER_VIEW: return "buffer view";
        case VK_OBJECT_TYPE_IMAGE_VIEW: return "texture view";
        case VK_OBJECT_TYPE_SHADER_MODULE: return "shader";
        case VK_OBJECT_TYPE_PIPELINE_CACHE: return "pipeline cache";
        case VK_OBJECT_TYPE_PIPELINE_LAYOUT: return "pipeline layout";
        case VK_OBJECT_TYPE_RENDER_PASS: return "render pass";
        case VK_OBJECT_TYPE_PIPELINE: return "pipeline";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT: return "descriptor set layout";
        case VK_OBJECT_TYPE_SAMPLER: return "sampler";
        case VK_OBJECT_TYPE_DESCRIPTOR_POOL: return "descriptor pool";
        case VK_OBJECT_TYPE_DESCRIPTOR_SET: return "descriptor set";
        case VK_OBJECT_TYPE_FRAMEBUFFER: return "framebuffer";
        case VK_OBJECT_TYPE_COMMAND_POOL: return "command pool";
        case VK_OBJECT_TYPE_SURFACE_KHR: return "surface";
        case VK_OBJECT_TYPE_SWAPCHAIN_KHR: return "swap chain";
        case VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT: return "debug callback";
        case VK_OBJECT_TYPE_DEBUG_UTILS_MESSENGER_EXT: return "debug messenger";
        default: return "???";
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if(pCallbackData->messageIdNumber == 0x48a09f6c)
    {
        // "You are using VK_PIPELINE_STAGE_ALL_COMMANDS_BIT when vkQueueSubmit is called"
        return VK_FALSE;
    }

    ri.Printf(PRINT_ALL, "Vulkan: %s\n", pCallbackData->pMessage);
#if defined(_WIN32)
    OutputDebugStringA(va("\n\n\nVulkan: %s\n\n\n", pCallbackData->pMessage));
    if((messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) != 0 &&
       messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT &&
       IsDebuggerPresent())
    {
        __debugbreak();
    }
#endif
    return VK_FALSE; // @NOTE: must be false as mandated by the spec
}

static const char* GetVulkanResultShortString(VkResult result)
{
#define VULKAN_CODE(Val, Desc) case Val: return #Val;
    switch(result)
    {
        VULKAN_SUCCESS_CODES(VULKAN_CODE)
        VULKAN_ERROR_CODES(VULKAN_CODE)
        default:
            return "???";
    }
#undef VULKAN_CODE
}

static const char* GetVulkanResultString(VkResult result)
{
#define VULKAN_CODE(Val, Desc) case Val: return Desc;
    switch(result)
    {
        VULKAN_SUCCESS_CODES(VULKAN_CODE)
        VULKAN_ERROR_CODES(VULKAN_CODE)
        default:
            return "???";
    }
#undef VULKAN_CODE
}

static qbool IsErrorCode(VkResult result)
{
#define VULKAN_CODE(Val, Desc) case Val:
    switch(result)
    {
        VULKAN_SUCCESS_CODES(VULKAN_CODE)
            return qfalse;
        default:
            return qtrue;
    }
#undef VULKAN_CODE
}

void Check(VkResult result, const char* function)
{
    if(!IsErrorCode(result))
    {
        return;
    }

    char funcName[256];
    Q_strncpyz(funcName, function, sizeof(funcName));
    char* s = funcName;
    while(*s != '\0')
    {
        if(*s == '(')
        {
            *s = '\0';
            break;
        }
        ++s;
    }

    ri.Error(ERR_FATAL, "'%s' failed with code %s (0x%08X)\n%s\n",
             funcName,
             GetVulkanResultShortString(result),
             (unsigned int)result,
             GetVulkanResultString(result));
}

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if(func != NULL)
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }

    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static qbool UseValidationLayer()
{
#if defined(_DEBUG)
    return qtrue;
#else
    return qfalse;
#endif
}

static qbool IsLayerAvailable(const char* name, int itemCount, const VkLayerProperties* itemNames)
{
    for(int i = 0; i < itemCount; ++i)
    {
        if(strcmp(name, itemNames[i].layerName) == 0)
        {
            return qtrue;
        }
    }

    return qfalse;
}

static qbool IsExtensionAvailable(const char* name, int itemCount, const VkExtensionProperties* itemNames)
{
    for(int i = 0; i < itemCount; ++i)
    {
        if(strcmp(name, itemNames[i].extensionName) == 0)
        {
            return qtrue;
        }
    }

    return qfalse;
}

static void BuildLayerAndExtensionLists(void)
{
    const char* neededLayers[MAX_LAYERS];
    const char* wantedLayers[MAX_LAYERS];
    const char* neededExtensions[MAX_EXTENSIONS];
    const char* wantedExtensions[MAX_EXTENSIONS];
    int neededLayerCount = 0;
    int wantedLayerCount = 0;
    int neededExtensionCount = 0;
    int wantedExtensionCount = 0;
    vk.extensionCount = 0;
    vk.layerCount = 0;

#if defined(__linux__)
    unsigned int requiredCount = 0;
    Sys_Vulkan_GetRequiredExtensions(NULL, &requiredCount);
    char **requiredExtensions = malloc(sizeof(char*) * requiredCount);
    if(!Sys_Vulkan_GetRequiredExtensions(requiredExtensions, &requiredCount)){
        ri.Error(ERR_FATAL, "Required Vulkan layer was not found\n");
    }

    for(int i = 0; i < requiredCount; i++){
        Com_Printf("%d: %s\n", i, requiredExtensions[i]);
    }
#endif

    if(UseValidationLayer())
    {
        wantedLayers[wantedLayerCount++] = "VK_LAYER_KHRONOS_validation"; // full validation
        //wantedExtensions[wantedExtensionCount++] = "VK_EXT_validation_features"; // update to non-deprecated
    }
#if defined(_WIN32)
    neededExtensions[neededExtensionCount++] = "VK_KHR_surface"; // swap chain
    neededExtensions[neededExtensionCount++] = "VK_KHR_win32_surface"; // Windows swap chain
#else
    for(int i = 0; i < requiredCount; i++){
        neededExtensions[neededExtensionCount++] = requiredExtensions[i];
    }
#endif

    wantedExtensions[wantedExtensionCount++] = "VK_EXT_debug_utils"; // naming resources

    uint32_t layerCount;
    VK(vkEnumerateInstanceLayerProperties(&layerCount, NULL));
    
    {
        VkLayerProperties *layers = (VkLayerProperties*)ri.Hunk_AllocateTempMemory(layerCount * sizeof(VkLayerProperties));

        VK(vkEnumerateInstanceLayerProperties(&layerCount, layers));
        for(int n = 0; n < neededLayerCount; ++n)
        {
            const char* name = neededLayers[n];
            if(!IsLayerAvailable(name, layerCount, layers))
            {
                ri.Error(ERR_FATAL, "Required Vulkan layer '%s' was not found\n", name);
            }
            vk.layers[vk.layerCount++] = name;
        }
        for(int w = 0; w < wantedLayerCount; ++w)
        {
            const char* name = wantedLayers[w];
            if(!IsLayerAvailable(name, layerCount, layers))
            {
                ri.Printf(PRINT_WARNING, "Desired Vulkan layer '%s' was not found, dropped from list\n", name);
            }
            else
            {
                vk.layers[vk.layerCount++] = name;
            }
        }
        ri.Hunk_FreeTempMemory(layers);
    }

    uint32_t extCount, tempCount;
    VK(vkEnumerateInstanceExtensionProperties(NULL, &tempCount, NULL));
    extCount = tempCount;
    for(int l = 0; l < vk.layerCount; ++l)
    {
        VK(vkEnumerateInstanceExtensionProperties(vk.layers[l], &tempCount, NULL));
        extCount += tempCount;
    }
    
    {
        VkExtensionProperties *ext = (VkExtensionProperties*)ri.Hunk_AllocateTempMemory(extCount * sizeof(VkExtensionProperties));
        VK(vkEnumerateInstanceExtensionProperties(NULL, &tempCount, NULL));
        VK(vkEnumerateInstanceExtensionProperties(NULL, &tempCount, ext));
        uint32_t startOffset = tempCount;
        for(int l = 0; l < vk.layerCount; ++l)
        {
            VK(vkEnumerateInstanceExtensionProperties(vk.layers[l], &tempCount, NULL));
            VK(vkEnumerateInstanceExtensionProperties(vk.layers[l], &tempCount, ext + startOffset));
            startOffset += tempCount;
        }
        for(int n = 0; n < neededExtensionCount; ++n)
        {
            const char* name = neededExtensions[n];
            if(!IsExtensionAvailable(name, extCount, ext))
            {
                ri.Error(ERR_FATAL, "Required Vulkan extension '%s' was not found\n", name);
            }
            vk.extensions[vk.extensionCount++] = name;
        }
        for(int w = 0; w < wantedExtensionCount; ++w)
        {
            const char* name = wantedExtensions[w];
            if(!IsExtensionAvailable(name, extCount, ext))
            {
                ri.Printf(PRINT_WARNING, "Desired Vulkan extension '%s' was not found, dropped from list\n", name);
            }
            else
            {
                vk.extensions[vk.extensionCount++] = name;
            }
        }

        vk.ext.EXT_validation_features = IsExtensionAvailable("VK_EXT_validation_features", extCount, ext);
        vk.ext.EXT_debug_utils = IsExtensionAvailable("VK_EXT_debug_utils", extCount, ext);
        ri.Hunk_FreeTempMemory(ext);
    }
}


void CreateInstance()
{
    ri.Printf( PRINT_ALL, "Vulkan Create Instance\n" );

    VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {};
    messengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerCreateInfo.pfnUserCallback = &DebugCallback;
    messengerCreateInfo.pUserData = NULL;
    messengerCreateInfo.pNext = NULL;
    messengerCreateInfo.flags = 0;


    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = MINIMUM_VULKAN_API_VERSION;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 4, 0);
    appInfo.engineVersion = VK_MAKE_VERSION(1, 4, 0);
    appInfo.pApplicationName = "Wolfenstein-vk";
    appInfo.pEngineName = "Wolfenstein-vk";
    appInfo.pNext = NULL;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.pNext = &messengerCreateInfo;
    if (vk.extensionCount > 0) {
        createInfo.enabledExtensionCount = vk.extensionCount;
        createInfo.ppEnabledExtensionNames = vk.extensions;
    }
    
    if (vk.layerCount > 0) {
        createInfo.enabledLayerCount = vk.layerCount;
        createInfo.ppEnabledLayerNames = vk.layers;
    }

    VK(vkCreateInstance(&createInfo, NULL, &vk.instance));
    volkLoadInstance(vk.instance);

    if (vk.ext.EXT_debug_utils)
    {
        if (CreateDebugUtilsMessengerEXT(vk.instance, &messengerCreateInfo, NULL, &vk.ext.debugMessenger) != VK_SUCCESS)
        {
            ri.Printf(PRINT_WARNING, "Failed to register Vulkan debug messenger\n");
            
        }
    }
}


static qbool FindSuitableQueueFamilies(Queues* queues, VkPhysicalDevice physicalDevice)
{
    uint32_t familyCount = 0;
    qbool graphicsFound = qfalse;
    qbool presentFound = qfalse;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, NULL);

    
    VkQueueFamilyProperties *families = (VkQueueFamilyProperties*)ri.Hunk_AllocateTempMemory(familyCount * sizeof(VkQueueFamilyProperties));
    {
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families);

        //
        // look for graphics + compute + present
        //

        for(int f = 0; f < familyCount; ++f)
        {
            if(families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            {
                VkBool32 presentSupport = qfalse;
                VK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, f, vk.surface, &presentSupport));
                if(presentSupport)
                {
                    queues->graphicsFamily = f;
                    queues->presentFamily = f;
                    ri.Hunk_FreeTempMemory(families);
                    return qtrue;
                }
            }
        }

        //
        // look for graphics + compute and present separately
        //

        for(int f = 0; f < familyCount; ++f)
        {
            if(families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
            {
                queues->graphicsFamily = f;
                graphicsFound = qtrue;
                break;
            }
        }
    
        for(int f = 0; f < familyCount; ++f)
        {
            VkBool32 presentSupport = qfalse;
            VK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, f, vk.surface, &presentSupport));
            if(presentSupport)
            {
                queues->presentFamily = f;
                presentFound = qtrue;
                break;
            }
        }
    }
    ri.Hunk_FreeTempMemory(families);

    return graphicsFound && presentFound;
}
void R_Gpulist_f(void){
    uint32_t deviceCount = 0;
    VK(vkEnumeratePhysicalDevices(vk.instance, &deviceCount, NULL));
    if(deviceCount == 0)
    {
        return;
    }

    VkPhysicalDevice *devices = (VkPhysicalDevice*)ri.Hunk_AllocateTempMemory(deviceCount * sizeof(VkPhysicalDevice));

    VK(vkEnumeratePhysicalDevices(vk.instance, &deviceCount, devices));
    for(int d = 0; d < deviceCount; ++d)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        ri.Printf(PRINT_ALL, "%d: %s\n", d+1, properties.deviceName);
    }
    ri.Hunk_FreeTempMemory(devices);
}

void PickPhysicalDevice(void)
{
    uint32_t deviceCount = 0;
    VK(vkEnumeratePhysicalDevices(vk.instance, &deviceCount, NULL));
    if(deviceCount == 0)
    {
        ri.Error(ERR_FATAL, "Vulkan found no physical device\n");
    }

    VkPhysicalDevice *devices = (VkPhysicalDevice*)ri.Hunk_AllocateTempMemory(deviceCount * sizeof(VkPhysicalDevice));
    {
        VK(vkEnumeratePhysicalDevices(vk.instance, &deviceCount, devices));

        int selection = -1;
        int highestScore = 0;
        for(int d = 0; d < deviceCount; ++d)
        {
            if(r_gpu->integer > 0 && r_gpu->integer <= deviceCount && r_gpu->integer != d+1){
                continue;
            }
            const VkPhysicalDevice physicalDevice = devices[d];

            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

            VkPhysicalDeviceFeatures deviceFeatures;
            vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

            Queues queues;
            if(deviceProperties.apiVersion >= MINIMUM_VULKAN_API_VERSION &&
                FindSuitableQueueFamilies(&queues, physicalDevice) &&
                deviceFeatures.shaderSampledImageArrayDynamicIndexing && //TODO is this the right variable? want dynamic uniform indexing vs nonuniform indexing
                deviceFeatures.samplerAnisotropy &&
                deviceProperties.limits.timestampComputeAndGraphics &&
                deviceProperties.limits.maxDescriptorSetSampledImages >= MAX_IMAGEDESCRIPTORS)
            {
                // score the physical device based on specifics that aren't requirements
                
                int score = 1;
                if(deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    score += 1000;
                }

                // select the physical device if it scored better than the others
                if(score > highestScore)
                {
                    highestScore = score;
                    selection = d;
                }
            }
        }

        if(selection < 0)
        {
            ri.Error(ERR_FATAL, "No suitable physical device found\n");
        }

        vk.physicalDevice = devices[selection];
        vkGetPhysicalDeviceProperties(vk.physicalDevice, &vk.deviceProperties);
        vkGetPhysicalDeviceFeatures(vk.physicalDevice, &vk.deviceFeatures);
        FindSuitableQueueFamilies(&vk.queues, devices[selection]);
    }
    ri.Hunk_FreeTempMemory(devices);
    ri.Printf(PRINT_ALL, "Physical device selected: %s\n", vk.deviceProperties.deviceName);
}

static void CreateQueryPool(void){
    for(int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++){
        VkQueryPoolCreateInfo query_pool_info = {};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_info.queryCount = MAX_DURATION_QUERIES * 2;
        VK(vkCreateQueryPool(vk.device, &query_pool_info, NULL, &vk.queryPool[i]));
    }
    
}

static void CreateDevice(void)
{
    uint32_t queueCount = 0;
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo[2];
    memset(queueCreateInfo, 0, sizeof(queueCreateInfo));
    queueCreateInfo[queueCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo[queueCount].queueFamilyIndex = vk.queues.graphicsFamily;
    queueCreateInfo[queueCount].queueCount = 1;
    queueCreateInfo[queueCount].pQueuePriorities = &queuePriority;
    ++queueCount;
    if(vk.queues.presentFamily != vk.queues.graphicsFamily)
    {
        queueCreateInfo[queueCount].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo[queueCount].queueFamilyIndex = vk.queues.presentFamily;
        queueCreateInfo[queueCount].queueCount = 1;
        queueCreateInfo[queueCount].pQueuePriorities = &queuePriority;
        ++queueCount;
    }

    const char* neededDeviceExtensions[] =
    {
        "VK_KHR_swapchain"
    };
    const char* extensions[ARRAY_LEN(neededDeviceExtensions) + 1]; // +1 for the optional calibrated-timestamps extension below
    uint32_t extensionCount = 0;
    int neededIdx;

    for(neededIdx = 0; neededIdx < ARRAY_LEN(neededDeviceExtensions); ++neededIdx)
    {
        extensions[extensionCount++] = neededDeviceExtensions[neededIdx];
    }

#if defined( ENABLE_PROFILER )
    {
        uint32_t availableCount = 0;
        VK(vkEnumerateDeviceExtensionProperties(vk.physicalDevice, NULL, &availableCount, NULL));
        VkExtensionProperties *available = (VkExtensionProperties*)ri.Hunk_AllocateTempMemory(availableCount * sizeof(VkExtensionProperties));
        VK(vkEnumerateDeviceExtensionProperties(vk.physicalDevice, NULL, &availableCount, available));
        vk.ext.EXT_calibrated_timestamps = IsExtensionAvailable("VK_EXT_calibrated_timestamps", availableCount, available);
        if(vk.ext.EXT_calibrated_timestamps)
        {
            extensions[extensionCount++] = "VK_EXT_calibrated_timestamps";
        }
        else
        {
            ri.Printf(PRINT_WARNING, "Desired Vulkan extension 'VK_EXT_calibrated_timestamps' was not found -- GPU pass timing in the Timeline tab will use approximate positioning\n");
        }
        ri.Hunk_FreeTempMemory(available);
    }
#endif

    VkPhysicalDeviceVulkan12Features vk12f = {};
    vk12f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12f.hostQueryReset = VK_TRUE;
    //vk12f.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    //vk12f.descriptorBindingPartiallyBound = VK_TRUE;
	vk12f.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	vk12f.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
    vk12f.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE; //@TODO: some older hardware may not support update after bind for storage images
    vk12f.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    vk12f.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vk13f = {};
    vk13f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13f.synchronization2 = VK_TRUE;
    vk13f.dynamicRendering = VK_TRUE;

    vk13f.pNext = &vk12f;

    // @TODO: copy over results from vk.deviceFeatures when they're optional
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    features2.features.samplerAnisotropy = VK_TRUE;
    //features2.features.shaderClipDistance = VK_TRUE;
    features2.features.depthClamp = vk.deviceFeatures.depthClamp;
    features2.features.fragmentStoresAndAtomics = VK_TRUE;
    features2.pNext = &vk13f;

    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = queueCreateInfo;
    createInfo.queueCreateInfoCount = queueCount;
    createInfo.ppEnabledExtensionNames = extensions;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.pEnabledFeatures = NULL;
    createInfo.pNext = &features2;

    VK(vkCreateDevice(vk.physicalDevice, &createInfo, NULL, &vk.device));
    volkLoadDevice(vk.device);
    vkGetDeviceQueue(vk.device, vk.queues.graphicsFamily, 0, &vk.queues.graphics);
    vkGetDeviceQueue(vk.device, vk.queues.presentFamily, 0, &vk.queues.present);
}

static void CreateAllocator(void)
{
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = MINIMUM_VULKAN_API_VERSION;
    allocatorInfo.physicalDevice = vk.physicalDevice;
    allocatorInfo.device = vk.device;
    allocatorInfo.instance = vk.instance;
    allocatorInfo.flags = 0;

    VmaVulkanFunctions vkFuncs;
    VK(vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vkFuncs));
    allocatorInfo.pVulkanFunctions = &vkFuncs;

    VK(vmaCreateAllocator(&allocatorInfo, &vk.allocator));
}

void SetObjectName(VkObjectType type, uint64_t object, const char* name)
{
    if(name == NULL)
    {
        return;
    }

    PFN_vkSetDebugUtilsObjectNameEXT pfnSetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(vk.device, "vkSetDebugUtilsObjectNameEXT");
    if(pfnSetDebugUtilsObjectNameEXT == NULL)
    {
        return;
    }

    VkDebugUtilsObjectNameInfoEXT nameInfo = {};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.pObjectName = va("%s [%s]", name, GetStringForVkObjectType(type));
    nameInfo.objectHandle = object;
    nameInfo.objectType = type;
    nameInfo.pNext = NULL;
    if(pfnSetDebugUtilsObjectNameEXT(vk.device, &nameInfo) != VK_SUCCESS)
    {
        ri.Printf(PRINT_DEVELOPER, "vkSetDebugUtilsObjectNameEXT failed\n");
    }
}

VkImageUsageFlags GetVkImageUsageFlags(RHI_ResourceState state)
{
    VkImageUsageFlags flags = VK_IMAGE_USAGE_SAMPLED_BIT;
    if(state & RHI_ResourceState_RenderTargetBit)
    {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if(state & RHI_ResourceState_DepthWriteBit)
    {
       flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if(state & RHI_ResourceState_CopySourceBit)
    {
       flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if(state & RHI_ResourceState_CopyDestinationBit)
    {
       flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if(state & RHI_ResourceState_ShaderInputBit)
    {
       // @TODO: is this correct ???
       flags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    }
    if(state & RHI_ResourceState_ShaderReadWriteBit)
    {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    return flags;
}


VkFormat GetVkFormat(rhiTextureFormatId format)
{
    assert((unsigned int)format < RHI_TextureFormat_Count);

    switch(format)
    {
        case R8G8B8A8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case B8G8R8A8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case B8G8R8A8_sRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case R16G16B16A16_SFloat: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case D16_UNorm: return VK_FORMAT_D16_UNORM;
        case D32_SFloat: return VK_FORMAT_D32_SFLOAT;
        //case D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
        case R32_UInt: return VK_FORMAT_R32_UINT;
        default: assert(0); return VK_FORMAT_R8G8B8A8_UNORM;
    }
}


VkImageAspectFlags GetVkImageAspectFlags(VkFormat format)
{
    switch(format)
    {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}


static void CreateSwapChain(void)
{
    VkSurfaceCapabilitiesKHR caps;
    VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physicalDevice, vk.surface, &caps));

    int w, h;
    w = glInfo.winWidth;
    h = glInfo.winHeight;

    if(w < caps.minImageExtent.width ||
      w > caps.maxImageExtent.width ||
      h < caps.minImageExtent.height ||
      h > caps.maxImageExtent.height)
    {
       ri.Error(ERR_FATAL, "Can't create a swap chain of the requested dimensions (%d x %d)\n",
                w, h);
    }

    uint32_t formatCount;
    VK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &formatCount, NULL));

    qbool formatFound = qfalse;
    VkSurfaceFormatKHR selectedFormat;
    selectedFormat.format = VK_FORMAT_MAX_ENUM;
    selectedFormat.colorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
    if(formatCount > 0)
    {
        VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR*)ri.Hunk_AllocateTempMemory(formatCount * sizeof(VkSurfaceFormatKHR));
        {
            VK(vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physicalDevice, vk.surface, &formatCount, formats));

            for(int f = 0; f < formatCount; ++f)
            {
                if(formats[f].format == SURFACE_FORMAT_RGBA) // @TODO:
                {
                    selectedFormat = formats[f];
                    formatFound = qtrue;
                    break;
                }
            }
        }
        ri.Hunk_FreeTempMemory(formats);
    }

    if(!formatFound)
    {
        ri.Error(ERR_FATAL, "No suitable format found\n");
    }
    vk.swapChainFormat = selectedFormat;

    uint32_t presentModeCount;
    VK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physicalDevice, vk.surface, &presentModeCount, NULL));

    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    qboolean immediateAvailable = qfalse; //no vsync, no queue
    qboolean fifoRelaxedAvailable = qfalse; //adaptive vsync, has queue
    qboolean mailboxAvailable = qfalse; //vsync, no queue
    if(presentModeCount > 0)
    {
        VkPresentModeKHR *presentModes = (VkPresentModeKHR*)ri.Hunk_AllocateTempMemory(presentModeCount * sizeof(VkPresentModeKHR));
        {

            VK(vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physicalDevice, vk.surface, &presentModeCount, presentModes));

            for(int p = 0; p < presentModeCount; ++p)
            {
                if(presentModes[p] == VK_PRESENT_MODE_IMMEDIATE_KHR)
                {
                    immediateAvailable = qtrue;
                }
                if(presentModes[p] == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
                {
                    fifoRelaxedAvailable = qtrue;
                }
                if(presentModes[p] == VK_PRESENT_MODE_MAILBOX_KHR)
                {
                    mailboxAvailable = qtrue;
                }
            }
        }
        ri.Hunk_FreeTempMemory(presentModes);
    }

    /*
    -- vsync --
    VK_PRESENT_MODE_MAILBOX_KHR:
      1 image in a queue, a new image replaces the existing one
    VK_PRESENT_MODE_FIFO_RELAXED_KHR:
      "adaptive vsync" if you miss the deadline and provide an image after, it will draw it anyway

    -- no vsync --
    VK_PRESENT_MODE_IMMEDIATE_KHR:
      no queue
    VK_PRESENT_MODE_FIFO_KHR:
      standard fifo queue

    */

    if(r_swapInterval->integer){
        vk.vsync = qtrue;
        if(mailboxAvailable){
            ri.Printf(PRINT_ALL, "Present mode selected: VK_PRESENT_MODE_MAILBOX_KHR\n");
            selectedPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        }else if (fifoRelaxedAvailable){
            ri.Printf(PRINT_ALL, "Present mode selected: VK_PRESENT_MODE_FIFO_RELAXED_KHR\n");
            selectedPresentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
        }else{
            ri.Printf(PRINT_ALL, "Present mode selected: VK_PRESENT_MODE_FIFO_KHR\n");
            selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        }
    }else{
        vk.vsync = qfalse;
        if (immediateAvailable){
            ri.Printf(PRINT_ALL, "Present mode selected: VK_PRESENT_MODE_IMMEDIATE_KHR\n");
            selectedPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }else{
            ri.Printf(PRINT_ALL, "Present mode selected: VK_PRESENT_MODE_FIFO_KHR\n");
            ri.Printf(PRINT_WARNING, "V-Sync was forced on, check for the latest graphics driver\n");
            selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        }
    }

    vk.presentMode = selectedPresentMode;


    // @NOTE: VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT is the only image usage flag
    // that is guaranteed to be available
    const uint32_t queueFamilyIndices[] = { vk.queues.graphicsFamily, vk.queues.presentFamily };
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vk.surface;
    createInfo.minImageCount = caps.minImageCount;
    createInfo.imageFormat = selectedFormat.format;
    createInfo.imageColorSpace = selectedFormat.colorSpace;
    createInfo.imageExtent.width = w;
    createInfo.imageExtent.height = h;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT; //vk error needed to add TRANSFER_DST_BIT
    createInfo.preTransform = caps.currentTransform; // keep whatever is already going on
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = selectedPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    if(vk.queues.presentFamily != vk.queues.graphicsFamily)
    {
        // @TODO: handle properly and make exclusive
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = ARRAY_LEN(queueFamilyIndices);
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = NULL;
    }

    VK(vkCreateSwapchainKHR(vk.device, &createInfo, NULL, &vk.swapChain));

    uint32_t imageCount;
    VK(vkGetSwapchainImagesKHR(vk.device, vk.swapChain, &imageCount, NULL));
    vk.swapChainImageCount = imageCount;
    if(imageCount > MAX_SWAP_CHAIN_IMAGES)
    {
        ri.Error(ERR_FATAL, "Too many swap chain images returned (%d > %d)\n", imageCount, MAX_SWAP_CHAIN_IMAGES);
    }
    VkImage *images = (VkImage*)ri.Hunk_AllocateTempMemory(imageCount * sizeof(VkImage));
    {
        VK(vkGetSwapchainImagesKHR(vk.device, vk.swapChain, &imageCount, images));

        rhiTextureDesc rtDesc = {};
        rtDesc.width = w;
        rtDesc.height = h;
        rtDesc.mipCount = 1;
        rtDesc.sampleCount = 1;
        rtDesc.allowedStates = RHI_ResourceState_RenderTargetBit | RHI_ResourceState_PresentBit;
        rtDesc.initialState = RHI_ResourceState_PresentBit;
        for(int i = 0; i < imageCount; ++i)
        {
            rtDesc.nativeImage = (uint64_t)images[i];
            rtDesc.nativeFormat = SURFACE_FORMAT_RGBA;
            rtDesc.nativeLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            rtDesc.longLifetime = qtrue;
            rtDesc.name = va("swap chain render target #%d", i + 1);
            vk.swapChainImages[i] = images[i];
            vk.swapChainRenderTargets[i] = RHI_CreateTexture(&rtDesc);
        }
    }
    ri.Hunk_FreeTempMemory(images);

    // //Create ImageViews for swap chain images
    // for(int i = 0; i < vk.swapChainImageCount; i++){
    //     VkImageView view;
    //     VkImageViewCreateInfo viewInfo = {};
    //     viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    //     viewInfo.image = vk.swapChainImages[i];
    //     viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    //     viewInfo.format = SURFACE_FORMAT_RGBA;
    //     viewInfo.subresourceRange.aspectMask = GetVkImageAspectFlags(SURFACE_FORMAT_RGBA);
    //     viewInfo.subresourceRange.baseMipLevel = 0;
    //     viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    //     viewInfo.subresourceRange.baseArrayLayer = 0;
    //     viewInfo.subresourceRange.layerCount = 1;
    //     VK(vkCreateImageView(vk.device, &viewInfo, NULL, &view));
    //     SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, "ImageView");
    //     vk.swapChainImageViews[i] = view;
    // }
}

void RecreateSwapchain(void){
    vkDeviceWaitIdle(vk.device);
    vkDestroySwapchainKHR(vk.device, vk.swapChain, NULL);
    for(int i = 0; i < vk.swapChainImageCount; i++){
        Texture *texture = GET_TEXTURE(vk.swapChainRenderTargets[i]);
        vkDestroyImageView(vk.device,texture->view,NULL);
        for(int i = 0; i < texture->desc.mipCount; i++){
            vkDestroyImageView(vk.device, texture->mipViews[i], NULL); 
        }
        Pool_Remove(&vk.texturePool, vk.swapChainRenderTargets[i].h);
    }
    
    CreateSwapChain();
}


// void CreateCommandBuffer(int instance)
// {
//     const VkCommandPool commandPool = vk.commandPool.commandPool;

//     CommandBuffer buffer = {};
//     buffer.commandPool = commandPool;
//     VkCommandBufferAllocateInfo allocInfo = {};
//     allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
//     allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//     allocInfo.commandPool = commandPool;
//     allocInfo.commandBufferCount = 1;
//     VK(vkAllocateCommandBuffers(vk.device, &allocInfo, &buffer.commandBuffer));
//     vk.commandBuffer[instance] = buffer;
// }

// void CreateTempCommandPool()
// {
//     CommandPool pool = {};
//     VkCommandPoolCreateInfo createInfo = {};
//     createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
//     createInfo.queueFamilyIndex = vk.queues.graphicsFamily;
//     createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
//     createInfo.flags |= VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // @TODO:
//     VK(vkCreateCommandPool(vk.device, &createInfo, NULL, &pool.commandPool));
//     vk.tempCommandPool = pool;
// }

// void CreateTempCommandBuffer()
// {
//     const VkCommandPool commandPool = vk.tempCommandPool.commandPool;

//     CommandBuffer buffer = {};
//     buffer.commandPool = commandPool;
//     VkCommandBufferAllocateInfo allocInfo = {};
//     allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
//     allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//     allocInfo.commandPool = commandPool;
//     allocInfo.commandBufferCount = 1;
//     VK(vkAllocateCommandBuffers(vk.device, &allocInfo, &buffer.commandBuffer));
//     vk.tempCommandBuffer = buffer;
// }

static void CreateCommandPool(void)
{
    VkCommandPoolCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.queueFamilyIndex = vk.queues.graphicsFamily;
    createInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK(vkCreateCommandPool(vk.device, &createInfo, NULL, &vk.commandPool));

}

// void AcquireSubmitPresent() {
//     vk.timelineValue++;
//     uint64_t timelineValues[2] = {0, vk.timelineValue};
//     VkTimelineSemaphoreSubmitInfo timelineInfo1;
//     timelineInfo1.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
//     timelineInfo1.pNext = NULL;
//     timelineInfo1.waitSemaphoreValueCount = 0;
//     timelineInfo1.pWaitSemaphoreValues = VK_NULL_HANDLE;
//     timelineInfo1.signalSemaphoreValueCount = 2;
//     timelineInfo1.pSignalSemaphoreValues = timelineValues;

//     VkSemaphore semaphores[2] = { vk.renderComplete.semaphore, vk.timelineSemaphore};

//     VkSubmitInfo submitInfo = {};
//     submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//     submitInfo.pNext                = &timelineInfo1;
//     submitInfo.commandBufferCount   = 1;
//     submitInfo.pCommandBuffers      = &vk.commandBuffer[vk.currentFrameIndex].commandBuffer;
//     submitInfo.waitSemaphoreCount   = 1;
//     submitInfo.pWaitSemaphores      = &vk.imageAcquired.semaphore;
//     submitInfo.signalSemaphoreCount = 2;
//     submitInfo.pSignalSemaphores    = semaphores;
//     const VkPipelineStageFlags flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
//     submitInfo.pWaitDstStageMask = &flags;

    
//     VK(vkQueueSubmit(vk.queues.present, 1, &submitInfo, VK_NULL_HANDLE));    
    

    
//     VkPresentInfoKHR presentInfo = {};
//     presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
//     presentInfo.swapchainCount     = 1;
//     presentInfo.pSwapchains        = &vk.swapChain;
//     presentInfo.pImageIndices      = &vk.swapChainImageIndex;
//     presentInfo.waitSemaphoreCount = 1;
//     presentInfo.pWaitSemaphores    = &vk.renderComplete.semaphore;
    
    
//     VK(vkQueuePresentKHR(vk.queues.present, &presentInfo));    

// }

// static void InitSwapChainImages()
// {
//     VkCommandBufferBeginInfo beginInfo = {};
//     beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//     beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    
//     VkClearColorValue clearColor = { 1.0f, 0.0f, 1.0f, 0.0f };
//     VkClearValue clearValue = {};
//     clearValue.color = clearColor;
    
//     VkImageSubresourceRange imageRange = {};
//     imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     imageRange.levelCount = 1;
//     imageRange.layerCount = 1;
    
//     for (uint32_t i = 0 ; i < vk.swapChainImageCount ; i++) {
//         VkCommandBuffer commandBuffer = vk.commandBuffer[0].commandBuffer;
//         VK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

//         {
//             VkImageMemoryBarrier2 barrier = {};
//             barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
//             barrier.image = vk.swapChainImages[i];
//             barrier.srcAccessMask = VK_ACCESS_2_NONE;
//             barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
//             barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
//             barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//             barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//             barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
//             barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//             barrier.subresourceRange.baseArrayLayer = 0;
//             barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//             barrier.subresourceRange.baseMipLevel = 0;
//             barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

//             VkDependencyInfo dep = {};
//             dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
//             dep.imageMemoryBarrierCount = 1;
//             dep.pImageMemoryBarriers = &barrier;
//             vkCmdPipelineBarrier2(commandBuffer, &dep);
//         }

//         vkCmdClearColorImage(commandBuffer, vk.swapChainImages[i], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &imageRange);                

//         {
//             VkImageMemoryBarrier2 barrier = {};
//             barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
//             barrier.image = vk.swapChainImages[i];
//             barrier.srcAccessMask = VK_ACCESS_2_NONE;
//             barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
//             barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
//             barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//             barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
//             barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//             barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//             barrier.subresourceRange.baseArrayLayer = 0;
//             barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//             barrier.subresourceRange.baseMipLevel = 0;
//             barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

//             VkDependencyInfo dep = {};
//             dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
//             dep.imageMemoryBarrierCount = 1;
//             dep.pImageMemoryBarriers = &barrier;
//             vkCmdPipelineBarrier2(commandBuffer, &dep);
//         }

//         VkSemaphoreWaitInfo waitInfo = {};
//         waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
//         waitInfo.semaphoreCount = 1;
//         waitInfo.pSemaphores = &vk.renderComplete.semaphore;
        
//         VK(vkEndCommandBuffer(commandBuffer));
        
//         VkSubmitInfo submitInfo = {};
//         submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//         submitInfo.commandBufferCount = 1;
//         submitInfo.pCommandBuffers = &commandBuffer;

//         const VkPipelineStageFlags flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
//         submitInfo.pWaitDstStageMask = &flags;
//         VK(vkQueueSubmit(vk.queues.present, 1, &submitInfo, VK_NULL_HANDLE));
//         VK(vkDeviceWaitIdle(vk.device));
         
//     }
    
    
// }


// int n = 0;
// static void BuildCommandBuffer()
// {
//     VkCommandBufferBeginInfo beginInfo = {};
//     beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//     beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

//     n += 1;
//     if (n == 2500)
//         n = 0;
//     double sine = sin(2 * M_PI * 8.4e-3 * n);
    
//     VkClearColorValue clearColor = { 0.0f, (float)sine*0.5+0.5, 1.0f, 0.0f };
//     VkClearValue clearValue = {};
//     clearValue.color = clearColor;
    
//     VkImageSubresourceRange imageRange = {};
//     imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     imageRange.levelCount = 1;
//     imageRange.layerCount = 1;
          
        
//     VK(vkBeginCommandBuffer(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, &beginInfo));

//     {
//         VkImageMemoryBarrier2 barrier = {};
//         barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
//         barrier.image = vk.swapChainImages[vk.currentFrameIndex];
//         barrier.srcAccessMask = VK_ACCESS_2_NONE;
//         barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
//         barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
//         barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//         barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//         barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
//         barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//         barrier.subresourceRange.baseArrayLayer = 0;
//         barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//         barrier.subresourceRange.baseMipLevel = 0;
//         barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

//         VkDependencyInfo dep = {};
//         dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
//         dep.imageMemoryBarrierCount = 1;
//         dep.pImageMemoryBarriers = &barrier;
//         vkCmdPipelineBarrier2(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, &dep);
//     }


//     vkCmdClearColorImage(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, vk.swapChainImages[vk.currentFrameIndex], VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &imageRange);                

//     {
//         VkImageMemoryBarrier2 barrier = {};
//         barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
//         barrier.image = vk.swapChainImages[vk.currentFrameIndex];
//         barrier.srcAccessMask = VK_ACCESS_2_NONE;
//         barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
//         barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
//         barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//         barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
//         barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//         barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//         barrier.subresourceRange.baseArrayLayer = 0;
//         barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//         barrier.subresourceRange.baseMipLevel = 0;
//         barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

//         VkDependencyInfo dep = {};
//         dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
//         dep.imageMemoryBarrierCount = 1;
//         dep.pImageMemoryBarriers = &barrier;
//         vkCmdPipelineBarrier2(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, &dep);
//     }

    

//     VkRenderingAttachmentInfo colorAttachmentInfo = {};
//     colorAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
//     colorAttachmentInfo.imageView = vk.swapChainImageViews[vk.swapChainImageIndex];
//     colorAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//     colorAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
//     colorAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    

//     VkRenderingInfo renderingInfo = {};
//     renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
//     renderingInfo.colorAttachmentCount = 1;
//     renderingInfo.pColorAttachments = &colorAttachmentInfo;
//     renderingInfo.layerCount = 1;
//     renderingInfo.renderArea.extent.height = glConfig.vidHeight;
//     renderingInfo.renderArea.extent.width = glConfig.vidWidth;

    

//     vkCmdBeginRendering(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, &renderingInfo);
// 	vkCmdBindPipeline(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline.pipeline);
//     vkCmdBindDescriptorSets(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
// 							vk.pipelineLayout.pipelineLayout, 0, 1, &vk.descriptorSet, 0, NULL);
//     VkDeviceSize vertexBufferOffset = 0;
//     VkDeviceSize colorBufferOffset = 0;
//     VkDeviceSize offsets[3] = {vertexBufferOffset, colorBufferOffset, 0 };
//     VkBuffer buffers[3] = {vk.vertexBuffer, vk.colorBuffer, vk.tcBuffer};
//     vkCmdBindVertexBuffers(vk.commandBuffer[vk.currentFrameIndex].commandBuffer,0, sizeof(buffers)/sizeof(buffers[0]), buffers, offsets);
//     vkCmdBindIndexBuffer(vk.commandBuffer[vk.currentFrameIndex].commandBuffer,vk.indexBuffer,0,VK_INDEX_TYPE_UINT32);

//     uint32_t samplerIndex = r_dynamiclight->integer == 0 ? 0 : 1;
//     vkCmdPushConstants(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, vk.pipelineLayout.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &samplerIndex);

//     vkCmdDrawIndexed(vk.commandBuffer[vk.currentFrameIndex].commandBuffer,6,1,0,0,0);
    
//     vkCmdEndRendering(vk.commandBuffer[vk.currentFrameIndex].commandBuffer);
//     {
//         VkImageMemoryBarrier2 barrier = {};
//         barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
//         barrier.image = vk.swapChainImages[vk.currentFrameIndex];
//         barrier.srcAccessMask = VK_ACCESS_2_NONE;
//         barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
//         barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
//         barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
//         barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//         barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
//         barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//         barrier.subresourceRange.baseArrayLayer = 0;
//         barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//         barrier.subresourceRange.baseMipLevel = 0;
//         barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;

//         VkDependencyInfo dep = {};
//         dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
//         dep.imageMemoryBarrierCount = 1;
//         dep.pImageMemoryBarriers = &barrier;
//         vkCmdPipelineBarrier2(vk.commandBuffer[vk.currentFrameIndex].commandBuffer, &dep);
//     }

//     VK(vkEndCommandBuffer(vk.commandBuffer[vk.currentFrameIndex].commandBuffer));
// }

// static void CreateSyncObjects(){
//     Semaphore imageAcquired = {};
//     {
//         VkSemaphoreCreateInfo createInfo = {};
//         createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
//         VK(vkCreateSemaphore(vk.device, &createInfo, NULL, &imageAcquired.semaphore));
//         imageAcquired.signaled = qfalse;

//         SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)imageAcquired.semaphore, "imageAcquired semaphore");
//     }
//     vk.imageAcquired = imageAcquired;
    
//     Semaphore renderComplete = {};
//     {
//         VkSemaphoreCreateInfo createInfo = {};
//         createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
//         VK(vkCreateSemaphore(vk.device, &createInfo, NULL, &renderComplete.semaphore));
//         renderComplete.signaled = qfalse;

//         SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)renderComplete.semaphore, "renderComplete semaphore");
//     }
//     vk.renderComplete = renderComplete;

//     Semaphore timelineSemaphore = {};
//     {
//         VkSemaphoreTypeCreateInfo timelineCreateInfo = {};
//         timelineCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
//         timelineCreateInfo.pNext = NULL;
//         timelineCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
//         timelineCreateInfo.initialValue = 0;
//         vk.timelineValue = 0;

//         VkSemaphoreCreateInfo createInfo = {};
//         createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
//         createInfo.pNext = &timelineCreateInfo;
//         createInfo.flags = 0;


//         VK(vkCreateSemaphore(vk.device, &createInfo, NULL, &timelineSemaphore.semaphore));
//         timelineSemaphore.signaled = qfalse;

//         SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, (uint64_t)timelineSemaphore.semaphore, "timelineSemaphore semaphore");
//         vk.timelineSemaphore = timelineSemaphore.semaphore;
//     }

// }




void RHI_BeginFrame(void) {
    if(r_swapInterval->modified){
        qbool currentVsync = r_swapInterval->integer != 0;
        if(currentVsync != vk.vsync){
            RecreateSwapchain();
        }
        r_swapInterval->modified = qfalse;
    }
    vk.currentFrameIndex = (vk.currentFrameIndex + 1) % RHI_FRAMES_IN_FLIGHT;
    vk.durationQueryCount[vk.currentFrameIndex] = 0;
    
    // VkSemaphoreWaitInfo waitInfo = {};
    // waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    // waitInfo.pNext = NULL;
    // waitInfo.flags = 0;
    // waitInfo.semaphoreCount = 1;
    // waitInfo.pSemaphores = &vk.timelineSemaphore;
    // waitInfo.pValues = &vk.timelineValue;
    // VK(vkWaitSemaphores(vk.device, &waitInfo, UINT64_MAX));
    
    // const VkResult r = vkAcquireNextImageKHR(vk.device, vk.swapChain, UINT64_MAX, vk.imageAcquired.semaphore, VK_NULL_HANDLE, &vk.swapChainImageIndex); 
 
    // // @TODO: when r is VK_ERROR_OUT_OF_DATE_KHR, recreate the swap chain
    // if(r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR)
    // {
    //     vk.imageAcquired.signaled = qtrue;
    // }
    // else
    // {
    //     Check(r, "vkAcquireNextImageKHR");
    //     vk.imageAcquired.signaled = qfalse;
    // }

    // BuildCommandBuffer();
}

void RHI_EndFrame(void) {
    int64_t currentTime = Sys_Microseconds();
    static int64_t previousTime = INT64_MIN;
    const int64_t us = currentTime - previousTime;
    previousTime = currentTime;
    rhie.presentToPresentUS = us;

}

// static void CreateTrianglePipelineLayout()
// {

//     VkDescriptorBindingFlags bindingFlags = 0;
    

//     VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFlagsCreateInfo = {};
// 	descSetFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
// 	// descSetFlagsCreateInfo.bindingCount = 1;
// 	// descSetFlagsCreateInfo.pBindingFlags = &bindingFlags;


//     VkDescriptorSetLayoutBinding binding = {};

//     binding.binding = 0;
//     binding.descriptorCount = 1;
    //  binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
//     binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
//     binding.pImmutableSamplers = VK_NULL_HANDLE;

//     VkDescriptorSetLayoutBinding binding2 = {};

//     binding2.binding = 1;
//     binding2.descriptorCount = 2;
//     binding2.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
//     binding2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
//     binding2.pImmutableSamplers = VK_NULL_HANDLE;

//     VkDescriptorSetLayoutBinding binding3 = {};

//     binding3.binding = 2;
//     binding3.descriptorCount = 1;
//     binding3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
//     binding3.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
//     binding3.pImmutableSamplers = VK_NULL_HANDLE;


//     VkDescriptorSetLayoutBinding bindings[3] = {binding, binding2, binding3};

 //	DescriptorSetLayout descLayout = {};
// 	VkDescriptorSetLayoutCreateInfo descSetCreateInfo = {};
// 	descSetCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
// 	descSetCreateInfo.bindingCount = ARRAY_LEN(bindings);
// 	descSetCreateInfo.pBindings = &bindings;
// 	descSetCreateInfo.pNext = &descSetFlagsCreateInfo;
// 	VK(vkCreateDescriptorSetLayout(vk.device, &descSetCreateInfo, NULL, &descLayout.layout));
// 	vk.descriptorSetLayout = descLayout.layout;

// 	SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, (uint64_t)descLayout.layout, "Desc Layout");

//     VkPushConstantRange pcr = {};
//     pcr.size = 4;
//     pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

//     PipelineLayout layout = {};
// 	//memcpy(layout.constantRanges, desc->pushConstantsPerStage, sizeof(layout.constantRanges));
// 	VkPipelineLayoutCreateInfo createInfo = {};
// 	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
// 	createInfo.setLayoutCount = 1;
// 	createInfo.pSetLayouts = &vk.descriptorSetLayout;
//     createInfo.pPushConstantRanges = &pcr;
//     createInfo.pushConstantRangeCount = 1;
    
// 	//createInfo.pushConstantRangeCount = pushConstantsRangeCount;
// 	//createInfo.pPushConstantRanges = pushConstantRanges;
// 	VK(vkCreatePipelineLayout(vk.device, &createInfo, NULL, &layout.pipelineLayout));
// 	//*layoutHandle = vk.pipelineLayoutPool.Add(layout);
//     vk.pipelineLayout = layout;

// 	SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, (uint64_t)layout.pipelineLayout, "Pipeline Layout");
// }


VkBlendFactor GetSourceColorBlendFactor(unsigned int bits)
{
	switch(bits)
	{
		case GLS_SRCBLEND_ZERO: return VK_BLEND_FACTOR_ZERO;
		case GLS_SRCBLEND_ONE: return VK_BLEND_FACTOR_ONE;
		case GLS_SRCBLEND_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case GLS_SRCBLEND_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_SRCBLEND_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case GLS_SRCBLEND_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		default: return VK_BLEND_FACTOR_ONE;
	}
}

VkBlendFactor GetDestinationColorBlendFactor(unsigned int bits)
{
	switch(bits)
	{
		case GLS_DSTBLEND_ZERO: return VK_BLEND_FACTOR_ZERO;
		case GLS_DSTBLEND_ONE: return VK_BLEND_FACTOR_ONE;
		case GLS_DSTBLEND_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case GLS_DSTBLEND_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case GLS_DSTBLEND_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		default: return VK_BLEND_FACTOR_ZERO;
	}
}

VkCullModeFlags GetVkCullModeFlags(cullType_t cullType)
{
	assert((unsigned int)cullType < CT_COUNT);

	switch(cullType)
	{
		case CT_FRONT_SIDED: return VK_CULL_MODE_BACK_BIT;
		case CT_BACK_SIDED: return VK_CULL_MODE_FRONT_BIT;
		default: return VK_CULL_MODE_NONE;
	}
}

// #include "shaders/triangle_ps.h"
// #include "shaders/triangle_vs.h"

// static void CreatePipeline() {

//     VkShaderModule vsModule;
//     VkShaderModuleCreateInfo vsCreateInfo = {};
//     vsCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
//     vsCreateInfo.codeSize = sizeof(triangle_vs);
//     vsCreateInfo.pCode = triangle_vs;

//     VkShaderModule psModule;
//     VkShaderModuleCreateInfo psCreateInfo = {};
//     psCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
//     psCreateInfo.codeSize = sizeof(triangle_ps);
//     psCreateInfo.pCode = triangle_ps;

//     VK(vkCreateShaderModule(vk.device, &vsCreateInfo,NULL,&vsModule));
//     VK(vkCreateShaderModule(vk.device, &psCreateInfo,NULL,&psModule));

// 	int firstStage = 0;
// 	int stageCount = 2;
// 	VkPipelineShaderStageCreateInfo stages[2] = {};
// 	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
// 	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
// 	stages[0].pName = "vs";
//     stages[0].module = vsModule;
// 	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
// 	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
// 	stages[1].pName = "ps";
//     stages[1].module = psModule;


// 	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
// 	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
// 	inputAssembly.primitiveRestartEnable = VK_FALSE;
// 	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

// 	VkViewport viewport;
// 	viewport.x = 0.0f;
// 	viewport.y = 0.0f;
// 	viewport.width = glConfig.vidWidth;
// 	viewport.height = glConfig.vidHeight;
// 	viewport.minDepth = 0.0f;
// 	viewport.maxDepth = 1.0f;

// 	VkRect2D scissor;
// 	scissor.offset.x = 0;
// 	scissor.offset.y = 0;
// 	scissor.extent.width = glConfig.vidWidth;
// 	scissor.extent.height = glConfig.vidHeight;

// 	VkPipelineViewportStateCreateInfo viewportState = {};
// 	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
// 	viewportState.viewportCount = 1;
// 	viewportState.pViewports = &viewport;
// 	viewportState.scissorCount = 1;
// 	viewportState.pScissors = &scissor;

// 	VkPipelineRasterizationStateCreateInfo rasterizer = {};
// 	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
// 	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
// 	rasterizer.cullMode = GetVkCullModeFlags(CT_TWO_SIDED);
// 	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
// 	rasterizer.depthClampEnable = VK_FALSE;
// 	rasterizer.rasterizerDiscardEnable = VK_FALSE;
// 	rasterizer.depthBiasEnable = VK_FALSE;
// 	rasterizer.lineWidth = 1.0f;

// 	// VkPipelineMultisampleStateCreateInfo multiSampling {};
// 	// multiSampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
// 	// multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
// 	// multiSampling.alphaToCoverageEnable = VK_FALSE;

// 	/*const qbool disableBlend =
// 		desc->srcBlend == GLS_SRCBLEND_ONE &&
// 		desc->dstBlend == GLS_DSTBLEND_ZERO;*/
// 	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
// 	colorBlendAttachment.blendEnable = VK_FALSE;
// 	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
// 	// colorBlendAttachment.srcColorBlendFactor = GetSourceColorBlendFactor(desc->srcBlend);
// 	// colorBlendAttachment.dstColorBlendFactor = GetDestinationColorBlendFactor(desc->dstBlend);
// 	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
// 	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // @TODO:
// 	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // @TODO:
// 	colorBlendAttachment.colorWriteMask =
// 		VK_COLOR_COMPONENT_R_BIT |
// 		VK_COLOR_COMPONENT_G_BIT |
// 		VK_COLOR_COMPONENT_B_BIT |
// 		VK_COLOR_COMPONENT_A_BIT;

// 	VkPipelineColorBlendStateCreateInfo colorBlending = {};
// 	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
// 	colorBlending.logicOpEnable = VK_FALSE;
// 	colorBlending.logicOp = VK_LOGIC_OP_COPY;
// 	colorBlending.attachmentCount = 1;
// 	colorBlending.pAttachments = &colorBlendAttachment;


// 	// @NOTE: VK_DYNAMIC_STATE_CULL_MODE_EXT is not widely available (VK_EXT_extended_dynamic_state)
// 	// const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
// 	// VkPipelineDynamicStateCreateInfo dynamicState {};
// 	// dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
// 	// dynamicState.dynamicStateCount = ARRAY_LEN(dynamicStates);
// 	// dynamicState.pDynamicStates = dynamicStates;

// 	// VkPipelineDepthStencilStateCreateInfo depthStencil = {};
// 	// VkPipelineDepthStencilStateCreateInfo* depthStencilPtr = NULL;
// 	// if(desc->renderTargets.hasDepthStencil)
// 	// {
// 	// 	depthStencilPtr = &depthStencil;
// 	// 	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
// 	// 	depthStencil.depthCompareOp = GetVkCompareOp((depthTest_t)desc->depthTest);
// 	// 	depthStencil.depthTestEnable = desc->enableDepthTest ? VK_TRUE : VK_FALSE;
// 	// 	depthStencil.depthWriteEnable = desc->enableDepthWrite ? VK_TRUE : VK_FALSE;
// 	// }

// 	Pipeline pipeline = {};
// 	pipeline.compute = qfalse;
//     // Provide information for dynamic rendering
//     VkPipelineRenderingCreateInfo pipeline_create = {};
//     pipeline_create.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
//     pipeline_create.pNext                   = VK_NULL_HANDLE;
//     pipeline_create.colorAttachmentCount    = 1;

//     VkFormat color_rendering_format = SURFACE_FORMAT_RGBA;
//     pipeline_create.pColorAttachmentFormats = &color_rendering_format;
//     //pipeline_create.depthAttachmentFormat   = depth_format;
//     //pipeline_create.stencilAttachmentFormat = depth_format;

    
//     VkVertexInputBindingDescription vertexPositionBindingInfo = {};
//     vertexPositionBindingInfo.binding = 0; //shader binding point
//     vertexPositionBindingInfo.stride = 4 * sizeof(float); //only has vertex positions (XYZW)
//     vertexPositionBindingInfo.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

//     VkVertexInputBindingDescription colorBindingInfo = {};
//     colorBindingInfo.binding = 1; //shader binding point
//     colorBindingInfo.stride = 4 * sizeof(uint8_t); //only has colors (RGBA)
//     colorBindingInfo.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

//     VkVertexInputBindingDescription tcBindingInfo = {};
//     tcBindingInfo.binding = 2; //shader binding point
//     tcBindingInfo.stride = 2 * sizeof(float); //only has colors (RGBA)
//     tcBindingInfo.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    

//     VkVertexInputAttributeDescription vertexPositionAttributeInfo = {};
//     //a binding can have multiple attributes (interleaved vertex format)
//     vertexPositionAttributeInfo.location = 0; //shader bindings / shader input location (vk::location in HLSL) 
//     vertexPositionAttributeInfo.binding = vertexPositionBindingInfo.binding; //buffer bindings / vertex buffer index (CmdBindVertexBuffers in C)
//     vertexPositionAttributeInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
//     vertexPositionAttributeInfo.offset = 0; //attribute byte offset

//     VkVertexInputAttributeDescription colorAttributeInfo = {};
//     //a binding can have multiple attributes (interleaved vertex format)
//     colorAttributeInfo.location = 1; //shader bindings / shader input location (vk::location in HLSL) 
//     colorAttributeInfo.binding = colorBindingInfo.binding; //buffer bindings / vertex buffer index (CmdBindVertexBuffers in C)
//     colorAttributeInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
//     colorAttributeInfo.offset = 0; //attribute byte offset

//     VkVertexInputAttributeDescription tcAttributeInfo = {};
//     //a binding can have multiple attributes (interleaved vertex format)
//     tcAttributeInfo.location = 2; //shader bindings / shader input location (vk::location in HLSL) 
//     tcAttributeInfo.binding = tcBindingInfo.binding; //buffer bindings / vertex buffer index (CmdBindVertexBuffers in C)
//     tcAttributeInfo.format = VK_FORMAT_R32G32_SFLOAT;
//     tcAttributeInfo.offset = 0; //attribute byte offset

//     VkVertexInputAttributeDescription vertexAttributes[3] = {vertexPositionAttributeInfo,  colorAttributeInfo, tcAttributeInfo};
//     VkVertexInputBindingDescription vertexBindings[3] = {vertexPositionBindingInfo, colorBindingInfo, tcBindingInfo };

//     VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
// 	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
//     vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;
//     vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
//     vertexInputInfo.vertexAttributeDescriptionCount = ARRAY_LEN(vertexAttributes);
//     vertexInputInfo.vertexBindingDescriptionCount = ARRAY_LEN(vertexBindings);

//     VkPipelineMultisampleStateCreateInfo multiSampling = {};
// 	multiSampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
// 	multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
// 	multiSampling.alphaToCoverageEnable = VK_FALSE;

//     VkPipelineDepthStencilStateCreateInfo depthStencil = {};
//     depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;


//     // Use the pNext to point to the rendering create struct
//     VkGraphicsPipelineCreateInfo createInfo = {};
//     createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
//     createInfo.pNext               = &pipeline_create; // reference the new dynamic structure
//     createInfo.renderPass          = VK_NULL_HANDLE; // previously required non-null
//     createInfo.layout = vk.pipelineLayout.pipelineLayout;
// 	createInfo.subpass = 0; // we always target sub-pass 0
// 	createInfo.stageCount = stageCount;
// 	createInfo.pStages = stages + firstStage;
// 	createInfo.pColorBlendState = &colorBlending;
// 	createInfo.pDepthStencilState = &depthStencil;
// 	createInfo.pDynamicState = NULL;
// 	createInfo.pInputAssemblyState = &inputAssembly;
// 	createInfo.pMultisampleState = &multiSampling;
// 	createInfo.pRasterizationState = &rasterizer;
// 	createInfo.pVertexInputState = &vertexInputInfo; //add later
// 	createInfo.pViewportState = &viewportState;
//     VK(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &createInfo, NULL, &pipeline.pipeline));

//     SetObjectName(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipeline.pipeline, "Pipeline");
//     vk.pipeline = pipeline;
// }

// static void CreateBuffers(void *vertexData, uint32_t vertexSize, 
//                             void *indexData, uint32_t indexSize, 
//                             void *colorData, uint32_t colorSize,
//                             void *imgData, uint32_t imgSize,
//                             void *tcData, uint32_t tcSize){
//     //vertex 
//     {
//         VmaAllocationCreateInfo allocCreateInfo = {};
//         allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

//         VkBufferCreateInfo bufferInfo = {};
//         bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//         bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//         bufferInfo.size = vertexSize;
//         bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

//         VmaAllocation vmaAlloc = {};

//         VmaAllocationInfo allocInfo = {};
//         VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &vk.vertexBuffer, &vmaAlloc, &allocInfo));
//         SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vk.vertexBuffer, "Vertex Buffer");
//         void *mapped;
//         VK(vmaMapMemory(vk.allocator, vmaAlloc, &mapped));
//         memcpy(mapped, vertexData, vertexSize);
//         vmaUnmapMemory(vk.allocator, vmaAlloc);
//         vmaFlushAllocation(vk.allocator, vmaAlloc, 0, VK_WHOLE_SIZE);
//     }

//     //Texture coordinates
//     {
//         VmaAllocationCreateInfo allocCreateInfo = {};
//         allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

//         VkBufferCreateInfo bufferInfo = {};
//         bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//         bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//         bufferInfo.size = tcSize;
//         bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

//         VmaAllocation vmaAlloc = {};

//         VmaAllocationInfo allocInfo = {};
//         VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &vk.tcBuffer, &vmaAlloc, &allocInfo));
//         SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vk.tcBuffer, "Texture Coordinates Buffer");
//         void *mapped;
//         VK(vmaMapMemory(vk.allocator, vmaAlloc, &mapped));
//         memcpy(mapped, tcData, tcSize);
//         vmaUnmapMemory(vk.allocator, vmaAlloc);
//         vmaFlushAllocation(vk.allocator, vmaAlloc, 0, VK_WHOLE_SIZE);
//     }
//     //index 
//     {
//         VmaAllocationCreateInfo allocCreateInfo = {};
//         allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

//         VkBufferCreateInfo bufferInfo = {};
//         bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//         bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//         bufferInfo.size = indexSize;
//         bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

//         VmaAllocation vmaAlloc = {};

//         VmaAllocationInfo allocInfo = {};
//         VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &vk.indexBuffer, &vmaAlloc, &allocInfo));
//         SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vk.indexBuffer, "Index Buffer");
//         void *mapped;
//         VK(vmaMapMemory(vk.allocator, vmaAlloc, &mapped));
//         memcpy(mapped, indexData, indexSize);
//         vmaUnmapMemory(vk.allocator, vmaAlloc);
//         vmaFlushAllocation(vk.allocator, vmaAlloc, 0, VK_WHOLE_SIZE);

//     }
//     //color 
//     {
//         VmaAllocationCreateInfo allocCreateInfo = {};
//         allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

//         VkBufferCreateInfo bufferInfo = {};
//         bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//         bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//         bufferInfo.size = colorSize;
//         bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

//         VmaAllocation vmaAlloc = {};

//         VmaAllocationInfo allocInfo = {};
//         VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &vk.colorBuffer, &vmaAlloc, &allocInfo));
//         SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vk.colorBuffer, "Color Buffer");
//         void *mapped;
//         VK(vmaMapMemory(vk.allocator, vmaAlloc, &mapped));
//         memcpy(mapped, colorData, colorSize);
//         vmaUnmapMemory(vk.allocator, vmaAlloc);
//         vmaFlushAllocation(vk.allocator, vmaAlloc, 0, VK_WHOLE_SIZE);

//     }
//     //sampler indices
//     {
//         float samplerIndicies[16] = {
//             1.0f, 0.0f, 0.0f, 1.0f,
//             1.0f, 1.0f, 0.0f, 1.0f,
//             1.0f, 0.0f, 1.0f, 1.0f,
//             0.0f, 0.0f, 1.0f, 1.0f
//                                     };

//         VmaAllocationCreateInfo allocCreateInfo = {};
//         allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

//         VkBufferCreateInfo bufferInfo = {};
//         bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//         bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//         bufferInfo.size = sizeof(samplerIndicies);
//         bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

//         VmaAllocation vmaAlloc = {};

//         VmaAllocationInfo allocInfo = {};
//         VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &vk.samplerBuffer, &vmaAlloc, &allocInfo));
//         SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vk.samplerBuffer, "Sampler Indicies Buffer");
//         void *mapped;
//         VK(vmaMapMemory(vk.allocator, vmaAlloc, &mapped));
//         memcpy(mapped, samplerIndicies, sizeof(samplerIndicies));
//         vmaUnmapMemory(vk.allocator, vmaAlloc);
//         vmaFlushAllocation(vk.allocator, vmaAlloc, 0, VK_WHOLE_SIZE);

//     }
//     //texture staging buffer
//     {
//         VmaAllocationCreateInfo allocCreateInfo = {};
//         allocCreateInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; //upload

//         VkBufferCreateInfo bufferInfo = {};
//         bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
//         bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//         bufferInfo.size = imgSize;
//         bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; //copy src bit

//         VmaAllocation vmaAlloc = {};

//         VmaAllocationInfo allocInfo = {};
//         VK(vmaCreateBuffer(vk.allocator, &bufferInfo, &allocCreateInfo, &vk.textureStagingBuffer, &vmaAlloc, &allocInfo));
//         SetObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)vk.textureStagingBuffer, "Texture Staging Buffer");
//         void *mapped;
//         VK(vmaMapMemory(vk.allocator, vmaAlloc, &mapped));
//         memcpy(mapped, imgData, imgSize);
//         vmaUnmapMemory(vk.allocator, vmaAlloc);
//         vmaFlushAllocation(vk.allocator, vmaAlloc, 0, VK_WHOLE_SIZE);

//         // create and begin a temporary command buffer
//         //Reset temp command pool
//         vkResetCommandPool(vk.device, vk.tempCommandPool.commandPool, 0);

//         //Bind command buffer
//         vk.activeCommandBuffer = vk.tempCommandBuffer.commandBuffer;

//         //Begin command buffer
//         VkCommandBufferBeginInfo beginInfo = {};
//         beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
//         beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//         VK(vkBeginCommandBuffer(vk.activeCommandBuffer, &beginInfo));


//         // insert a barrier to change the mip's layout
//         {
//             VkAccessFlags srcFlags = 0;
//             VkAccessFlags dstFlags = 0;
//             //Barrier
//             VkBufferMemoryBarrier bufferBarriers[1];
//             {
//                 VkBufferMemoryBarrier barrier = {};
//                 barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
//                 barrier.offset = 0;
//                 barrier.size = VK_WHOLE_SIZE;
//                 barrier.srcAccessMask = 0;
//                 barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//                 barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//                 barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//                 barrier.buffer = vk.textureStagingBuffer;

//                 srcFlags |= barrier.srcAccessMask;
//                 dstFlags |= barrier.dstAccessMask;
//                 bufferBarriers[0] = barrier;
//             }

//             VkImageMemoryBarrier imageBarriers[1];
//             {
//                 VkImageMemoryBarrier barrier = {};
//                 barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
//                 barrier.srcAccessMask = 0;
//                 barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//                 barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
//                 barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//                 barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//                 barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//                 barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//                 barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//                 barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
//                 barrier.image = vk.generatedImage;

//                 srcFlags |= barrier.srcAccessMask;
//                 dstFlags |= barrier.dstAccessMask;
//                 imageBarriers[0] = barrier;
//             }

//             const VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
//             const VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
//             vkCmdPipelineBarrier(vk.activeCommandBuffer, srcStageMask, dstStageMask, 0, 0, NULL,
//                                 1, bufferBarriers, 1, imageBarriers);
//         }


//         // copy from the staging buffer into the texture mip
//         VkBufferImageCopy region = {};
//         region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//         region.imageSubresource.mipLevel = 0;
//         region.imageSubresource.layerCount = 1;
//         region.imageOffset.x = 0;
//         region.imageOffset.y = 0;
//         region.imageOffset.z = 0;
//         region.imageExtent.width = 64;
//         region.imageExtent.height = 64;
//         region.imageExtent.depth = 1;

//         vkCmdCopyBufferToImage(vk.activeCommandBuffer, vk.textureStagingBuffer, vk.generatedImage,
//                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

//         // insert a barrier to change the mip's layout

//         {
//             VkAccessFlags srcFlags = 0;
//             VkAccessFlags dstFlags = 0;
//             //Barrier

//             VkImageMemoryBarrier imageBarriers[1];
//             {
//                 VkImageMemoryBarrier barrier = {};
//                 barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
//                 barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
//                 barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
//                 barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//                 barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//                 barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//                 barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
//                 barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//                 barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
//                 barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
//                 barrier.image = vk.generatedImage;

//                 srcFlags |= barrier.srcAccessMask;
//                 dstFlags |= barrier.dstAccessMask;
//                 imageBarriers[0] = barrier;
//             }

//             const VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
//             const VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
//             vkCmdPipelineBarrier(vk.activeCommandBuffer, srcStageMask, dstStageMask, 0, 0, NULL,
//                                 0, NULL, 1, imageBarriers);
//         }

//         // end, submit and destroy the command buffer
//         //commandBuffer.EndAndSubmit();
//         VK(vkEndCommandBuffer(vk.activeCommandBuffer));

//         const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
//         VkSubmitInfo info = {};
//         info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//         info.commandBufferCount = 1;
//         info.pCommandBuffers = &vk.activeCommandBuffer;

//         VK(vkQueueSubmit(vk.queues.graphics, 1, &info, VK_NULL_HANDLE));

//         VK(vkDeviceWaitIdle(vk.device));

//         vk.activeCommandBuffer = vk.commandBuffer[vk.currentFrameIndex].commandBuffer;

//     }
// }



// void CreateTexture(uint32_t w, uint32_t h)
// {
    


//     VkFormat format = TEXTURE_FORMAT_RGBA;
//     VkImage image = VK_NULL_HANDLE;
//     VmaAllocation allocation = VK_NULL_HANDLE;
    
//     VmaAllocationCreateInfo allocCreateInfo = {};
//     allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

//     VkImageCreateInfo imageInfo = {};
//     imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
//     imageInfo.imageType = VK_IMAGE_TYPE_2D;
//     imageInfo.extent.width = 64;
//     imageInfo.extent.height = 64;
//     imageInfo.extent.depth = 1;
//     imageInfo.mipLevels = 1;
//     imageInfo.arrayLayers = 1;
//     imageInfo.format = format;
//     imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
//     imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // @TODO: desc->initialState
//     imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
//     imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // @TODO: desc->sampleCount
//     imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
//     VK(vmaCreateImage(vk.allocator, &imageInfo, &allocCreateInfo, &image, &allocation, NULL));

//     SetObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)image, "Generated Texture Image");


//     VkImageView view;
//     VkImageViewCreateInfo viewInfo = {};
//     viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
//     viewInfo.image = image;
//     viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
//     viewInfo.format = format;
//     viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//     viewInfo.subresourceRange.baseMipLevel = 0;
//     viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
//     viewInfo.subresourceRange.baseArrayLayer = 0;
//     viewInfo.subresourceRange.layerCount = 1;
//     VK(vkCreateImageView(vk.device, &viewInfo, NULL, &view));
//     SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)view, "Generated Texture ImageView");

//     vk.generatedImage = image;
//     vk.generatedImageView = view;
//     vk.generatedImageAllocation = allocation;
// }

static void CreateDescriptorPool(){
    const VkDescriptorPoolSize poolSizes[] =
	{
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_IMAGEDESCRIPTORS * 2 },
        { VK_DESCRIPTOR_TYPE_SAMPLER, 16 * 2 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 * 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16 * 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_UPLOADCMDBUFFERS * 12}
	};

    VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = ARRAY_LEN(poolSizes);
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = 16 + MAX_UPLOADCMDBUFFERS;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
	VK(vkCreateDescriptorPool(vk.device, &poolInfo, NULL, &vk.descriptorPool));


    // VkDescriptorSet descriptorSet;

    // VkDescriptorSetAllocateInfo allocInfo = {};
	// allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	// allocInfo.descriptorPool = vk.descriptorPool;
	// allocInfo.descriptorSetCount = 1;
	// allocInfo.pSetLayouts = &vk.descriptorSetLayout;
    
	// VK(vkAllocateDescriptorSets(vk.device, &allocInfo, &descriptorSet));
	
	// SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, (uint64_t)descriptorSet, "Descriptor Set");
    // vk.descriptorSet = descriptorSet;

    // //TEXTURE
    // {
    //     VkDescriptorImageInfo image = {};
    //     image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    //     image.imageView = vk.generatedImageView;
    //     image.sampler = VK_NULL_HANDLE;

    //     VkWriteDescriptorSet write = {};
    //     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     write.dstSet = vk.descriptorSet;
    //     write.dstBinding = 0;
    //     write.descriptorCount = 1;
    //     write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    //     write.pBufferInfo = NULL;
    //     write.pImageInfo = &image;

    //     vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    // }
    // //SAMPLER1
    // {
        

    //     VkDescriptorImageInfo image = {};
    //     image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    //     image.imageView = VK_NULL_HANDLE;
    //     image.sampler = vk.sampler[0];

    //     VkDescriptorImageInfo image2 = {};
    //     image2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    //     image2.imageView = VK_NULL_HANDLE;
    //     image2.sampler = vk.sampler[1];

    //     VkDescriptorImageInfo images[2] = {image, image2};

    //     VkWriteDescriptorSet write = {};
    //     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     write.dstSet = vk.descriptorSet;
    //     write.dstBinding = 1;
    //     write.descriptorCount = 2;
    //     write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    //     write.pBufferInfo = NULL;
    //     write.pImageInfo = &images;

    //     vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    // }

    // //sampler indices buffer

    // {
    //     VkDescriptorBufferInfo bufferInfo = {};
    //     bufferInfo.buffer = vk.samplerBuffer;
    //     bufferInfo.offset = 0;
    //     bufferInfo.range = VK_WHOLE_SIZE;

    //     VkWriteDescriptorSet write = {};
    //     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     write.dstSet = vk.descriptorSet;
    //     write.dstBinding = 2;
    //     write.descriptorCount = 1;
    //     write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    //     write.pBufferInfo = &bufferInfo;
    //     write.pImageInfo = NULL;

    //     vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    // }



}

// static void CreateSampler(){
//     const VkSamplerAddressMode wrapMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ;

//     {
// 	VkSampler sampler;
// 	VkSamplerCreateInfo createInfo = {};
// 	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
// 	createInfo.addressModeU = wrapMode;
// 	createInfo.addressModeV = wrapMode;
// 	createInfo.addressModeW = wrapMode;
// 	createInfo.anisotropyEnable = VK_FALSE;
// 	createInfo.maxAnisotropy = 0; // @NOTE: ignored when anisotropyEnable is VK_FALSE
// 	createInfo.minFilter = VK_FILTER_LINEAR;
// 	createInfo.magFilter = VK_FILTER_LINEAR;
// 	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
// 	createInfo.minLod = 0.0f;
// 	createInfo.maxLod = 666.0f;
// 	createInfo.mipLodBias = 0.0f;
// 	VK(vkCreateSampler(vk.device, &createInfo, NULL, &sampler));

// 	SetObjectName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "Linear Sampler");
//     vk.sampler[0] = sampler;
//     }

//     {
// 	VkSampler sampler;
// 	VkSamplerCreateInfo createInfo = {};
// 	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
// 	createInfo.addressModeU = wrapMode;
// 	createInfo.addressModeV = wrapMode;
// 	createInfo.addressModeW = wrapMode;
// 	createInfo.anisotropyEnable = VK_FALSE;
// 	createInfo.maxAnisotropy = 0; // @NOTE: ignored when anisotropyEnable is VK_FALSE
// 	createInfo.minFilter = VK_FILTER_NEAREST;
// 	createInfo.magFilter = VK_FILTER_NEAREST;
// 	createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
// 	createInfo.minLod = 0.0f;
// 	createInfo.maxLod = 666.0f;
// 	createInfo.mipLodBias = 0.0f;
// 	VK(vkCreateSampler(vk.device, &createInfo, NULL, &sampler));

// 	SetObjectName(VK_OBJECT_TYPE_SAMPLER, (uint64_t)sampler, "Nearest Sampler");
//     vk.sampler[1] = sampler;
//     }
// }

VkPipelineStageFlags2 GetVkStageFlags(VkImageLayout state)
{
    const VkPipelineStageFlags2 vertexFragmentCompute =
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    const VkPipelineStageFlags2 fragmentTests =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

    switch(state){
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_PIPELINE_STAGE_2_NONE;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return vertexFragmentCompute;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return vertexFragmentCompute;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return fragmentTests;
        case VK_IMAGE_LAYOUT_GENERAL:
            return vertexFragmentCompute;

        default:
            assert(!"Unhandled image layout for stage flags");
            return VK_PIPELINE_STAGE_2_NONE;
    }
    
    
    
	// const Pair pairs[] = 
	// {
	// 	{ VK_ACCESS_INDEX_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT },
	// 	{ VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT },
	// 	{ VK_ACCESS_UNIFORM_READ_BIT, vertexFragmentCompute },
	// 	{ VK_ACCESS_SHADER_READ_BIT, vertexFragmentCompute },
	// 	{ VK_ACCESS_SHADER_WRITE_BIT, vertexFragmentCompute },
	// 	{ VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
	// 		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
	// 		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
	// 	{ VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
	// 	{ VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, fragmentTests },
	// 	{ VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, fragmentTests },
	// 	{ VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT },
	// 	{ VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT },
	// 	{ VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT },
	// 	{ VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT },
	// 	{ VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT }
	// };
}

VkAccessFlags2 GetVkAccessFlags(VkImageLayout state)
{
    

    switch(state){
        case VK_IMAGE_LAYOUT_UNDEFINED:
            return VK_ACCESS_2_NONE;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return VK_ACCESS_2_MEMORY_READ_BIT;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return VK_ACCESS_2_TRANSFER_READ_BIT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return VK_ACCESS_2_SHADER_READ_BIT;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        case VK_IMAGE_LAYOUT_GENERAL:
            return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        default:
            assert(!"Unhandled image layout");
            return VK_ACCESS_2_NONE;
    }
    
    
    
	// const Pair pairs[] =
	// {
		// { galResourceState::CopySourceBit, VK_ACCESS_TRANSFER_READ_BIT },
		// { galResourceState::CopyDestinationBit, VK_ACCESS_TRANSFER_WRITE_BIT },
		// { galResourceState::VertexBufferBit, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT },
		// { galResourceState::IndexBufferBit, VK_ACCESS_INDEX_READ_BIT },
		// { galResourceState::UnorderedAccessBit, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT },
		// { galResourceState::IndirectCommandBit, VK_ACCESS_INDIRECT_COMMAND_READ_BIT },
		// { galResourceState::RenderTargetBit, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT },
		// { galResourceState::DepthWriteBit, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT },
		// { galResourceState::ShaderInputBit, VK_ACCESS_SHADER_READ_BIT },
		// { galResourceState::UniformBufferBit, VK_ACCESS_UNIFORM_READ_BIT },
		// { galResourceState::StorageBufferBit, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT },
		// { galResourceState::PresentBit, VK_ACCESS_MEMORY_READ_BIT }
	// };
}

VkAccessFlags2 GetVkAccessFlagsFromResource(RHI_ResourceState state)
{
    switch(state){
        case RHI_ResourceState_CopySourceBit:
            return VK_ACCESS_2_TRANSFER_READ_BIT;
        case RHI_ResourceState_CopyDestinationBit:
            return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case RHI_ResourceState_VertexBufferBit:
            return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        case RHI_ResourceState_IndexBufferBit:
            return VK_ACCESS_2_INDEX_READ_BIT;
        case RHI_ResourceState_ShaderReadWriteBit:
            return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        case RHI_ResourceState_ShaderInputBit:
            return VK_ACCESS_2_SHADER_READ_BIT;
        default:
            assert(!"Unhandled resource state access flags");
            return 0;
    }

}

VkPipelineStageFlags2 GetVkStageFlagsFromResource(RHI_ResourceState state)
{
    switch(state){
        case RHI_ResourceState_CopySourceBit:
            return VK_PIPELINE_STAGE_2_COPY_BIT;
        case RHI_ResourceState_CopyDestinationBit:
            return VK_PIPELINE_STAGE_2_COPY_BIT;
        case RHI_ResourceState_VertexBufferBit:
            return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        case RHI_ResourceState_IndexBufferBit:
            return VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        case RHI_ResourceState_ShaderReadWriteBit:
            return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case RHI_ResourceState_ShaderInputBit:
            return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        default:
            assert(!"Unhandled resource state stage flags");
            return 0;
    }
}

    

VkAttachmentLoadOp GetVkAttachmentLoadOp(RHI_LoadOp load)
{
    

    switch(load){
        case RHI_LoadOp_Clear:
            return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case RHI_LoadOp_Load:
            return VK_ATTACHMENT_LOAD_OP_LOAD;
        case RHI_LoadOp_Discard:
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        default:
            assert(!"Unhandled load op");
            return VK_ATTACHMENT_LOAD_OP_LOAD;
    }
}

VkImageLayout GetVkImageLayout(RHI_ResourceState state)
{
    assert(popcnt(state) == 1); //TODO

    typedef struct {
        RHI_ResourceState state;
        VkImageLayout layout;
    } Pair;

    const Pair pairs[] = {
        { RHI_ResourceState_Undefined, VK_IMAGE_LAYOUT_UNDEFINED },
        { RHI_ResourceState_PresentBit, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
        { RHI_ResourceState_RenderTargetBit, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        { RHI_ResourceState_ShaderInputBit, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { RHI_ResourceState_CopySourceBit, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
     	{ RHI_ResourceState_CopyDestinationBit, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
        { RHI_ResourceState_DepthWriteBit, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
        { RHI_ResourceState_ShaderReadWriteBit, VK_IMAGE_LAYOUT_GENERAL }
    };

    for(int i = 0; i < ARRAY_LEN(pairs); i++){
        if(pairs[i].state == state){
            return pairs[i].layout;
        }
    }
    


    assert(!"Should have a resource state");
	return VK_IMAGE_LAYOUT_UNDEFINED;
}

VmaMemoryUsage GetVmaMemoryUsage(RHI_MemoryUsage usage)
{
	assert((unsigned int)usage < RHI_MemoryUsage_Count);

	switch(usage)
	{
		case RHI_MemoryUsage_DeviceLocal: return VMA_MEMORY_USAGE_GPU_ONLY;
		case RHI_MemoryUsage_Upload: return VMA_MEMORY_USAGE_CPU_TO_GPU;
		case RHI_MemoryUsage_Readback: return VMA_MEMORY_USAGE_GPU_TO_CPU;
		default: assert(0); return VMA_MEMORY_USAGE_UNKNOWN;
	}
}

VkBufferUsageFlags GetVkBufferUsageFlags(RHI_ResourceState state)
{
    typedef struct Pair
	{
		RHI_ResourceState inBit;
		VkBufferUsageFlags outBit;
	} Pair;
	const Pair pairs[] =
	{
		{ RHI_ResourceState_VertexBufferBit, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT },
		{ RHI_ResourceState_IndexBufferBit, VK_BUFFER_USAGE_INDEX_BUFFER_BIT },
		{ RHI_ResourceState_CopySourceBit, VK_BUFFER_USAGE_TRANSFER_SRC_BIT },
		{ RHI_ResourceState_CopyDestinationBit, VK_BUFFER_USAGE_TRANSFER_DST_BIT },
        { RHI_ResourceState_ShaderInputBit, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT },
		// { galResourceState::IndirectCommandBit, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT },
		{ RHI_ResourceState_ShaderReadWriteBit, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT }
	};

	VkBufferUsageFlags flags = 0;
	for(int p = 0; p < ARRAY_LEN(pairs); ++p)
	{
		if(state & pairs[p].inBit)
		{
			flags |= pairs[p].outBit;
		}
	}
    assert(flags != 0);

	return flags;
}

#include "shaders/mipmap_cs.h"
#include "shaders/mipmap_x_cs.h"
#include "shaders/mipmap_y_cs.h"

static void CreateUploadManager(){
    vk.uploadBufferSize = 64 << 20;

    rhiBufferDesc textureStagingBufferDesc = {};
    textureStagingBufferDesc.name = "Upload Buffer";
    textureStagingBufferDesc.initialState = RHI_ResourceState_CopySourceBit;
    textureStagingBufferDesc.memoryUsage = RHI_MemoryUsage_Upload;
    textureStagingBufferDesc.byteCount = vk.uploadBufferSize;
    textureStagingBufferDesc.longLifetime = qtrue;
    textureStagingBufferDesc.allowedStates = RHI_ResourceState_CopySourceBit;

    vk.uploadBuffer = RHI_CreateBuffer(&textureStagingBufferDesc);

    rhiDescriptorSetLayoutDesc mipmapLayoutDesc = {};
    mipmapLayoutDesc.bindingCount = 2;
    mipmapLayoutDesc.bindings[0].descriptorCount = 12;
    mipmapLayoutDesc.bindings[0].descriptorType = RHI_DescriptorType_ReadWriteTexture;
    mipmapLayoutDesc.bindings[0].stageFlags = RHI_PipelineStage_ComputeBit;
    mipmapLayoutDesc.bindings[1].descriptorCount = 1;
    mipmapLayoutDesc.bindings[1].descriptorType = RHI_DescriptorType_ReadWriteTexture;
    mipmapLayoutDesc.bindings[1].stageFlags = RHI_PipelineStage_ComputeBit;
    mipmapLayoutDesc.longLifetime = qtrue;

    

    vk.mipmapLayout = RHI_CreateDescriptorSetLayout(&mipmapLayoutDesc);

    rhiComputePipelineDesc mipmapPipelineDesc = {};
    mipmapPipelineDesc.descLayout = vk.mipmapLayout;
    mipmapPipelineDesc.longLifetime = qtrue;
    mipmapPipelineDesc.name = "Mipmap";
    mipmapPipelineDesc.pushConstantsBytes = sizeof(mipmapPushConstants);
    mipmapPipelineDesc.shader.byteCount = sizeof(mipmap_cs);
    mipmapPipelineDesc.shader.data = mipmap_cs;

    vk.mipmapPipeline = RHI_CreateComputePipeline(&mipmapPipelineDesc);

    mipmapPipelineDesc.pushConstantsBytes = sizeof(mipmapXPushConstants);
    mipmapPipelineDesc.shader.byteCount = sizeof(mipmap_x_cs);
    mipmapPipelineDesc.shader.data = mipmap_x_cs;
    mipmapPipelineDesc.name = "Mipmap X";
    vk.mipmapXPipeline = RHI_CreateComputePipeline(&mipmapPipelineDesc);


    mipmapPipelineDesc.pushConstantsBytes = sizeof(mipmapYPushConstants);
    mipmapPipelineDesc.shader.byteCount = sizeof(mipmap_y_cs);
    mipmapPipelineDesc.shader.data = mipmap_y_cs;
    mipmapPipelineDesc.name = "Mipmap Y";
    vk.mipmapYPipeline = RHI_CreateComputePipeline(&mipmapPipelineDesc);

    rhiTextureDesc mipmapScratchTexDesc = {};
    mipmapScratchTexDesc.allowedStates = RHI_ResourceState_ShaderReadWriteBit;
    mipmapScratchTexDesc.format = R16G16B16A16_SFloat;
    mipmapScratchTexDesc.height = 2048;
    mipmapScratchTexDesc.width = 2048;
    mipmapScratchTexDesc.initialState = RHI_ResourceState_ShaderReadWriteBit;
    mipmapScratchTexDesc.longLifetime = qtrue;
    mipmapScratchTexDesc.memoryUsage = RHI_MemoryUsage_DeviceLocal;
    mipmapScratchTexDesc.mipCount = 1;
    mipmapScratchTexDesc.name = "Mipmap Scratch";
    mipmapScratchTexDesc.sampleCount = 1;

    vk.mipmapScratchTexture = RHI_CreateTexture(&mipmapScratchTexDesc);


    for(int i = 0; i < MAX_UPLOADCMDBUFFERS; i++){
        vk.uploadCmdBuffer[i] = RHI_CreateCommandBuffer(qtrue);
        vk.uploadCmdBufferSignaledValue[i] = 0;
        vk.uploadDescriptorSets[i] = RHI_CreateDescriptorSet(va("Upload Desc Set #%d", i), vk.mipmapLayout, qtrue);
        RHI_UpdateDescriptorSet(vk.uploadDescriptorSets[i], 1, RHI_DescriptorType_ReadWriteTexture, 0, 1, &vk.mipmapScratchTexture, 0);
    }
    vk.uploadCmdBufferIndex = 0;
 
    vk.uploadSemaphore = RHI_CreateTimelineSemaphore(qtrue);
    vk.uploadSemaphoreCount = 0;
}

static void CreateScreenshotManager(){
    rhiTextureDesc screenshotTex = {};
    screenshotTex.allowedStates = RHI_ResourceState_CopyDestinationBit;
    screenshotTex.format = R8G8B8A8_UNorm;
    screenshotTex.height = glConfig.vidHeight;
    screenshotTex.width = glConfig.vidWidth;
    screenshotTex.initialState = RHI_ResourceState_CopyDestinationBit;
    screenshotTex.mipCount = 1;
    screenshotTex.name = "Screenshot readback";
    screenshotTex.sampleCount = 1;
    screenshotTex.memoryUsage = RHI_MemoryUsage_Readback;

    vk.screenshotTexture = RHI_CreateTexture(&screenshotTex);
    vk.screenshotCmdBuffer = RHI_CreateCommandBuffer(qfalse);
}

uint32_t GetByteCountsPerPixel(VkFormat format){
    if(format == VK_FORMAT_R8G8B8A8_UNORM){
        return 4;
    }
    
    assert(!"Unrecognized texture format");
    return 4;
}

VkDescriptorType GetVkDescriptorType(RHI_DescriptorType type){
    switch(type){
        case RHI_DescriptorType_Sampler:
            return VK_DESCRIPTOR_TYPE_SAMPLER;
	    case RHI_DescriptorType_ReadOnlyBuffer: 
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	    case RHI_DescriptorType_ReadWriteBuffer: 
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	    case RHI_DescriptorType_ReadOnlyTexture:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	    case RHI_DescriptorType_ReadWriteTexture:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        default:
            assert(!"Invalid descriptor type");
            return -1;
    }
    	
}
VkShaderStageFlags GetVkShaderStageFlags(RHI_PipelineStage stage){
    VkShaderStageFlags flags = 0;
    if(stage & RHI_PipelineStage_VertexBit){
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if(stage & RHI_PipelineStage_PixelBit){
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if(stage & RHI_PipelineStage_ComputeBit){
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return flags;
}

VkShaderStageFlags GetVkShaderStageFlagsFromShader(RHI_Shader stage){
    switch(stage){
        case RHI_Shader_Vertex:
            return VK_SHADER_STAGE_VERTEX_BIT;
        case RHI_Shader_Pixel:
            return VK_SHADER_STAGE_FRAGMENT_BIT;
        case RHI_Shader_Compute:
            return VK_SHADER_STAGE_COMPUTE_BIT;
        default:
            assert(!"Invalid shader stage");
            return 0;
    }
}

VkFormat GetVkFormatFromVertexFormat(RHI_VertexFormat format, uint32_t elementCount){
    assert((unsigned int)format < RHI_VertexFormat_Count);
	assert(elementCount >= 1 && elementCount <= 4);

    switch(format){
        case RHI_VertexFormat_Float32:
            return (VkFormat[4]) { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT }[elementCount - 1];

        case RHI_VertexFormat_UNorm8:
            return (VkFormat[4]) { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM }[elementCount - 1];

        case RHI_VertexFormat_UInt32:
            return (VkFormat[4]) { VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT }[elementCount - 1];

        default:
            assert(!"Invalid vertex format");
            return 0;
    }
}

VkSampleCountFlagBits GetVkSampleCount(uint32_t samples){
    switch(samples){
        case 2:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16:
            return VK_SAMPLE_COUNT_16_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
    }
}


void RHI_Init( void ) {
    if(vk.initialized)
    {
        CreateScreenshotManager();
        return;
    }
    
    ri.Printf( PRINT_ALL, "Initializing Vulkan subsystem\n" );

    
    Pool_Init(&vk.commandBufferPool, MAX_UPLOADCMDBUFFERS * 2, sizeof(CommandBuffer), 0);
    Pool_Init(&vk.semaphorePool, 64, sizeof(Semaphore), 0);
    Pool_Init(&vk.texturePool, MAX_IMAGEDESCRIPTORS, sizeof(Texture), 0);
    Pool_Init(&vk.bufferPool, 64, sizeof(Buffer), 0);
    Pool_Init(&vk.descriptorSetLayoutPool, 64, sizeof(DescriptorSetLayout), 0);
    Pool_Init(&vk.descriptorSetPool, 64 + MAX_UPLOADCMDBUFFERS, sizeof(DescriptorSet), 0);
    Pool_Init(&vk.pipelinePool, 256, sizeof(Pipeline), 0);
    Pool_Init(&vk.samplerPool, 16, sizeof(Sampler), 0);
    
    
    VK(volkInitialize());
    vk.instance = VK_NULL_HANDLE;
    BuildLayerAndExtensionLists();
    CreateInstance();
    vk.surface = (VkSurfaceKHR)Sys_Vulkan_Init(vk.instance);
    PickPhysicalDevice();
    CreateDevice();
    CreateAllocator();
    CreateSwapChain();
    CreateCommandPool();
    CreateDescriptorPool();

    CreateUploadManager();
    CreateScreenshotManager();

    CreateQueryPool();
    
    


    vk.initialized = qtrue;
    vk.width = glConfig.vidWidth;
	vk.height = glConfig.vidHeight;
}


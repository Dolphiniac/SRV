#include "Common.h"
#define VK_USE_PLATFORM_WIN32_KHR
#include "vulkan/vk_icd.h"
#include <windows.h>
#include <string.h>
#include <vector>
#include <dxgi.h>
#include <d3d11.h>

#pragma comment( lib, "dxgi" )
#pragma comment( lib, "d3d11" )

#define VK_ICD_EXPORT extern "C" __declspec( dllexport )
#define VK_PATCH_FUNCTION( funcName ) do {\
if ( !strcmp( pName, #funcName ) ) {\
	return ( PFN_vkVoidFunction )&funcName;\
} } while ( false )

#define VK_DENY_FUNCTION( funcName ) do {\
if ( !strcmp( pName, #funcName ) ) {\
	return NULL;\
} } while ( false )

#define VK_VALIDATION_FAILED_LABEL validationFailed

#define VK_VALIDATE( cond ) do { if ( ( cond ) == false ) { goto VK_VALIDATION_FAILED_LABEL; } } while ( false )

#define VK_SUBCALL_FAILED_LABEL subcallFailed

#define VK_ASSERT_SUBCALL( result ) do { if ( ( result ) != VK_SUCCESS ) { goto VK_SUBCALL_FAILED_LABEL; } } while ( false )

struct allocationInfo_t {
	size_t offsetFromBeginningOfBlock;
	size_t alignment;
	size_t size;
};

static void * VKAPI_PTR vkAllocateHostMemory( void *, size_t size, size_t alignment, VkSystemAllocationScope ) {
	size_t fullAllocation = size + ( alignment - 1 ) + sizeof( allocationInfo_t );
	void * baseAllocation = malloc( fullAllocation );
	size_t valuePtr = reinterpret_cast< size_t >( baseAllocation );
	size_t offsetFromAlignment = ( alignment - ( valuePtr % alignment ) );
	while ( offsetFromAlignment < sizeof( allocationInfo_t ) ) {
		offsetFromAlignment += alignment;
	}
	offsetFromAlignment -= sizeof( allocationInfo_t );
	valuePtr += offsetFromAlignment;
	allocationInfo_t * offsetStorage = reinterpret_cast< allocationInfo_t * >( valuePtr );
	offsetStorage->offsetFromBeginningOfBlock = offsetFromAlignment;
	offsetStorage->alignment = alignment;
	offsetStorage->size = fullAllocation - offsetFromAlignment - sizeof( allocationInfo_t );
	return offsetStorage + 1;
}

static void VKAPI_PTR vkFreeHostMemory( void *, void * pMemory ) {
	allocationInfo_t * offsetStorage = reinterpret_cast< allocationInfo_t * >( pMemory );
	--offsetStorage;
	size_t offsetFromBlockBeginning = offsetStorage->offsetFromBeginningOfBlock;
	size_t valuePtr = reinterpret_cast< size_t >( offsetStorage );
	valuePtr -= offsetFromBlockBeginning;
	void * toDelete = reinterpret_cast< void * >( valuePtr );
	free( toDelete );
}

static void * VKAPI_PTR vkReallocateHostMemory( void * pUserData, void * pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope ) {
	if ( pOriginal == NULL ) {
		return vkAllocateHostMemory( pUserData, size, alignment, allocationScope );
	}

	if ( size == 0 ) {
		vkFreeHostMemory( pUserData, pOriginal );
		return NULL;
	}

	allocationInfo_t * allocInfo = reinterpret_cast< allocationInfo_t * >( pOriginal );
	--allocInfo;
	if ( allocInfo->alignment != alignment ) {
		return NULL;
	}

	if ( allocInfo->size >= size ) {
		return pOriginal;
	}

	void * result = vkAllocateHostMemory( pUserData, size, alignment, allocationScope );
	memcpy( result, pOriginal, Min( size, allocInfo->size ) );

	vkFreeHostMemory( pUserData, pOriginal );

	return result;
}

VkAllocationCallbacks defaultAllocator = {
	NULL,
	vkAllocateHostMemory,
	vkReallocateHostMemory,
	vkFreeHostMemory,
	NULL,
	NULL
};

struct VkDispatchObject_t {
	VK_LOADER_DATA loaderData;
};

struct VkPhysicalDevice_t : VkDispatchObject_t {
	VkPhysicalDeviceProperties	properties;
	VkPhysicalDeviceFeatures	supportedFeatures;
	VkQueueFamilyProperties *	pQueueFamilyProperties;
	uint32						queueFamilyPropertyCount;
};

enum class instanceExtensions_t {
	SURFACE_KHR =		BIT( 0 ),
	WIN32_SURFACE_KHR = BIT( 1 )
};
static const char * supportedInstanceExtensions[] = {
	VK_KHR_SURFACE_EXTENSION_NAME,
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME
};

template< typename __enumType__ >
class VkBitFlags {
public:
	bool CheckFlag( __enumType__ flag ) const { return ( flags & ( uint64 )flag ) != 0; }
	bool CheckFlag( uint64 flag ) const { return ( flags & flag ) != 0; }
	void SetFlag( __enumType__ flag ) { flags |= ( uint64 )flag; }
	void SetFlag( uint64 flag ) { flags |= flag; }
	void ClearFlag( __enumType__ flag ) { flags &= ~( uint64 )flag; }
	void ClearFlag( uint64 flag ) { flags &= ~flag; }
	void Clear() { flags = 0; }

	operator uint64() { return flags; }
	operator uint32() { return ( uint32 )flags; }
private:
	uint64 flags;
};
typedef VkBitFlags< instanceExtensions_t > VkInstanceExtensionFlags;

struct VkInstance_t : VkDispatchObject_t {
	static const uint32	PHYSICAL_DEVICE_COUNT = 1;
	VkPhysicalDevice_t			physicalDevices[ PHYSICAL_DEVICE_COUNT ];
	VkInstanceExtensionFlags	enabledExtensions;
};


VkResult VKAPI_CALL vkCreateInstance( const VkInstanceCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkInstance * pInstance ) {
	for ( uint32 i = 0; i < pCreateInfo->enabledExtensionCount; i++ ) {
		bool foundExtensionInSupportedList = false;
		for ( uint32 j = 0; j < ARRAY_LENGTH( supportedInstanceExtensions ); j++ ) {
			if ( !strcmp( pCreateInfo->ppEnabledExtensionNames[ i ], supportedInstanceExtensions[ j ] ) ) {
				foundExtensionInSupportedList = true;
				break;
			}
		}
		if ( !foundExtensionInSupportedList ) {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;
	VkInstance_t * instance = reinterpret_cast< VkInstance_t * >( allocator->pfnAllocation( allocator->pUserData, sizeof( VkInstance_t ), 4, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE ) );
	memset( instance, 0, sizeof( *instance ) );
	set_loader_magic_value( instance );
	instance->enabledExtensions.Clear();
	for ( uint32 i = 0; i < pCreateInfo->enabledExtensionCount; i++ ) {
		for ( uint32 j = 0; j < ARRAY_LENGTH( supportedInstanceExtensions ); j++ ) {
			if ( !strcmp( supportedInstanceExtensions[ j ], pCreateInfo->ppEnabledExtensionNames[ i ] ) ) {
				instance->enabledExtensions.SetFlag( 1ULL << j );
				break;
			}
		}
	}

	VkPhysicalDevice_t * device = &instance->physicalDevices[ 0 ];
	set_loader_magic_value( device );
	VkPhysicalDeviceProperties & properties = device->properties;
	properties.apiVersion = VK_MAKE_VERSION( 1, 0, VK_HEADER_VERSION );
	properties.deviceID = 'R' << 24 | 'C' << 16 | 'S' << 8 | 'R';
	const char deviceName[] = "RC Software Rasterizer";
	memcpy( properties.deviceName, deviceName, sizeof( deviceName ) );
	properties.deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
	properties.driverVersion = VK_MAKE_VERSION( 0, 1, 0 );
	uint8 uuid[] = {
		0x2c, 0x2b, 0x43, 0xe3,
		0x39, 0x38,
		0x4f, 0xea,
		0xa7, 0x46,
		0xcd, 0xc7, 0x45, 0xfd, 0x52, 0x83
	};
	memcpy( properties.pipelineCacheUUID, uuid, sizeof( uuid ) );
	memset( &properties.sparseProperties, 0, sizeof( properties.sparseProperties ) );
	properties.limits = {
		/* uint32_t              maxImageDimension1D;							  */ 2048,
		/* uint32_t              maxImageDimension2D;							  */ 2048,
		/* uint32_t              maxImageDimension3D;							  */ 2048,
		/* uint32_t              maxImageDimensionCube;							  */ 2048,
		/* uint32_t              maxImageArrayLayers;							  */ 1,
		/* uint32_t              maxTexelBufferElements;						  */ 65536,
		/* uint32_t              maxUniformBufferRange;							  */ 64 * 1024,
		/* uint32_t              maxStorageBufferRange;							  */ 16 * 1024 * 1024,
		/* uint32_t              maxPushConstantsSize;							  */ 1024,
		/* uint32_t              maxMemoryAllocationCount;						  */ 65536,
		/* uint32_t              maxSamplerAllocationCount;						  */ 1024,
		/* VkDeviceSize          bufferImageGranularity;						  */ 4,
		/* VkDeviceSize          sparseAddressSpaceSize;						  */ 0,
		/* uint32_t              maxBoundDescriptorSets;						  */ 16,
		/* uint32_t              maxPerStageDescriptorSamplers;					  */ 128,
		/* uint32_t              maxPerStageDescriptorUniformBuffers;			  */ 128,
		/* uint32_t              maxPerStageDescriptorStorageBuffers;			  */ 128,
		/* uint32_t              maxPerStageDescriptorSampledImages;			  */ 128,
		/* uint32_t              maxPerStageDescriptorStorageImages;			  */ 128,
		/* uint32_t              maxPerStageDescriptorInputAttachments;			  */ 128,
		/* uint32_t              maxPerStageResources;							  */ 256,
		/* uint32_t              maxDescriptorSetSamplers;						  */ 256,
		/* uint32_t              maxDescriptorSetUniformBuffers;				  */ 256,
		/* uint32_t              maxDescriptorSetUniformBuffersDynamic;			  */ 256,
		/* uint32_t              maxDescriptorSetStorageBuffers;				  */ 256,
		/* uint32_t              maxDescriptorSetStorageBuffersDynamic;			  */ 256,
		/* uint32_t              maxDescriptorSetSampledImages;					  */ 256,
		/* uint32_t              maxDescriptorSetStorageImages;					  */ 256,
		/* uint32_t              maxDescriptorSetInputAttachments;				  */ 256,
		/* uint32_t              maxVertexInputAttributes;						  */ 32,
		/* uint32_t              maxVertexInputBindings;						  */ 32,
		/* uint32_t              maxVertexInputAttributeOffset;					  */ 128,
		/* uint32_t              maxVertexInputBindingStride;					  */ 64,
		/* uint32_t              maxVertexOutputComponents;						  */ 32,
		/* uint32_t              maxTessellationGenerationLevel;				  */ 0,
		/* uint32_t              maxTessellationPatchSize;						  */ 0,
		/* uint32_t              maxTessellationControlPerVertexInputComponents;  */ 0,
		/* uint32_t              maxTessellationControlPerVertexOutputComponents; */ 0,
		/* uint32_t              maxTessellationControlPerPatchOutputComponents;  */ 0,
		/* uint32_t              maxTessellationControlTotalOutputComponents;	  */ 0,
		/* uint32_t              maxTessellationEvaluationInputComponents;		  */ 0,
		/* uint32_t              maxTessellationEvaluationOutputComponents;		  */ 0,
		/* uint32_t              maxGeometryShaderInvocations;					  */ 0,
		/* uint32_t              maxGeometryInputComponents;					  */ 0,
		/* uint32_t              maxGeometryOutputComponents;					  */ 0,
		/* uint32_t              maxGeometryOutputVertices;						  */ 0,
		/* uint32_t              maxGeometryTotalOutputComponents;				  */ 0,
		/* uint32_t              maxFragmentInputComponents;					  */ 32,
		/* uint32_t              maxFragmentOutputAttachments;					  */ 16,
		/* uint32_t              maxFragmentDualSrcAttachments;					  */ 0,
		/* uint32_t              maxFragmentCombinedOutputResources;			  */ 64,
		/* uint32_t              maxComputeSharedMemorySize;					  */ 16 * 1024,
		/* uint32_t              maxComputeWorkGroupCount[ 3 ];					  */ { 4ULL * 1024 * 1024 * 1024 - 1, 65536, 64 },
		/* uint32_t              maxComputeWorkGroupInvocations;				  */ 256,
		/* uint32_t              maxComputeWorkGroupSize[ 3 ];					  */ { 64, 64, 32 },
		/* uint32_t              subPixelPrecisionBits;							  */ 8,
		/* uint32_t              subTexelPrecisionBits;							  */ 12,
		/* uint32_t              mipmapPrecisionBits;							  */ 5,
		/* uint32_t              maxDrawIndexedIndexValue;						  */ 4ULL * 1024 * 1024 * 1024 - 1,
		/* uint32_t              maxDrawIndirectCount;							  */ 2048,
		/* float                 maxSamplerLodBias;								  */ 0.0f,
		/* float                 maxSamplerAnisotropy;							  */ 0.0f,
		/* uint32_t              maxViewports;									  */ 8,
		/* uint32_t              maxViewportDimensions[ 2 ];					  */ { 2048, 2048 },
		/* float                 viewportBoundsRange[ 2 ];						  */ { -2048.0f, 2047.0f },
		/* uint32_t              viewportSubPixelBits;							  */ 8,
		/* size_t                minMemoryMapAlignment;							  */ 4,
		/* VkDeviceSize          minTexelBufferOffsetAlignment;					  */ 4,
		/* VkDeviceSize          minUniformBufferOffsetAlignment;				  */ 4,
		/* VkDeviceSize          minStorageBufferOffsetAlignment;				  */ 4,
		/* int32_t               minTexelOffset;								  */ -2047,
		/* uint32_t              maxTexelOffset;								  */ 2047,
		/* int32_t               minTexelGatherOffset;							  */ -2047,
		/* uint32_t              maxTexelGatherOffset;							  */ 2047,
		/* float                 minInterpolationOffset;						  */ 0.0f,
		/* float                 maxInterpolationOffset;						  */ 0.0f,
		/* uint32_t              subPixelInterpolationOffsetBits;				  */ 0,
		/* uint32_t              maxFramebufferWidth;							  */ 2048,
		/* uint32_t              maxFramebufferHeight;							  */ 2048,
		/* uint32_t              maxFramebufferLayers;							  */ 1,
		/* VkSampleCountFlags    framebufferColorSampleCounts;					  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    framebufferDepthSampleCounts;					  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    framebufferStencilSampleCounts;				  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    framebufferNoAttachmentsSampleCounts;			  */ VK_SAMPLE_COUNT_1_BIT,
		/* uint32_t              maxColorAttachments;							  */ 15,
		/* VkSampleCountFlags    sampledImageColorSampleCounts;					  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    sampledImageIntegerSampleCounts;				  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    sampledImageDepthSampleCounts;					  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    sampledImageStencilSampleCounts;				  */ VK_SAMPLE_COUNT_1_BIT,
		/* VkSampleCountFlags    storageImageSampleCounts;						  */ VK_SAMPLE_COUNT_1_BIT,
		/* uint32_t              maxSampleMaskWords;							  */ 1,
		/* VkBool32              timestampComputeAndGraphics;					  */ VK_FALSE,
		/* float                 timestampPeriod;								  */ 0.0f,
		/* uint32_t              maxClipDistances;								  */ 0,
		/* uint32_t              maxCullDistances;								  */ 0,
		/* uint32_t              maxCombinedClipAndCullDistances;				  */ 0,
		/* uint32_t              discreteQueuePriorities;						  */ 2,
		/* float                 pointSizeRange[ 2 ];							  */ { 1.0f, 1.0f },
		/* float                 lineWidthRange[ 2 ];							  */ { 1.0f, 1.0f },
		/* float                 pointSizeGranularity;							  */ 0.0f,
		/* float                 lineWidthGranularity;							  */ 0.0f,
		/* VkBool32              strictLines;									  */ VK_TRUE,
		/* VkBool32              standardSampleLocations;						  */ VK_TRUE,
		/* VkDeviceSize          optimalBufferCopyOffsetAlignment;				  */ 4,
		/* VkDeviceSize          optimalBufferCopyRowPitchAlignment;			  */ 4,
		/* VkDeviceSize          nonCoherentAtomSize;							  */ 4
	};
	properties.vendorID = 'R' << 16 | 'S' << 8 | 'C';
	VkPhysicalDeviceFeatures & features = device->supportedFeatures;
	memset( &features, 0, sizeof( features ) );
	features.fullDrawIndexUint32 = VK_TRUE;
	features.independentBlend = VK_TRUE;
	features.multiDrawIndirect = VK_TRUE;
	features.multiViewport = VK_TRUE;
	device->queueFamilyPropertyCount = 1;
	device->pQueueFamilyProperties = reinterpret_cast< VkQueueFamilyProperties * >( allocator->pfnAllocation( allocator->pUserData, sizeof( VkQueueFamilyProperties ), 4, VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE ) );
	VkQueueFamilyProperties & queueFamilyProperties = device->pQueueFamilyProperties[ 0 ];
	memset( &queueFamilyProperties, 0, sizeof( queueFamilyProperties ) );
	queueFamilyProperties.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	queueFamilyProperties.queueCount = 3;

	*pInstance = reinterpret_cast< VkInstance >( instance );
	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties( const char * pLayerName, uint32 * pPropertyCount, VkExtensionProperties * pProperties ) {
	if ( pLayerName != NULL ) {
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}

	if ( pProperties == NULL ) {
		*pPropertyCount = ARRAY_LENGTH( supportedInstanceExtensions );
		return VK_SUCCESS;
	}

	uint32 propertiesToWrite = Min( *pPropertyCount, ARRAY_LENGTH( supportedInstanceExtensions ) );
	for ( uint32 i = 0; i < propertiesToWrite; i++ ) {
		pProperties[ i ].specVersion = VK_MAKE_VERSION( 1, 0, VK_HEADER_VERSION );
		strcpy_s( pProperties[ i ].extensionName, supportedInstanceExtensions[ i ] );
	}
	*pPropertyCount = propertiesToWrite;
	if ( propertiesToWrite < ARRAY_LENGTH( supportedInstanceExtensions ) ) {
		return VK_INCOMPLETE;
	}
	return VK_SUCCESS;
}

void VKAPI_CALL vkDestroyInstance( VkInstance instance, const VkAllocationCallbacks * pAllocator ) {
	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;

	allocator->pfnFree( allocator->pUserData, instance );
}

VkResult VKAPI_CALL vkEnumeratePhysicalDevices( VkInstance instance, uint32 * pPhysicalDeviceCount, VkPhysicalDevice * pPhysicalDevices ) {
	if ( pPhysicalDevices == NULL ) {
		*pPhysicalDeviceCount = 1;
		return VK_SUCCESS;
	}

	VkInstance_t * inst = reinterpret_cast< VkInstance_t * >( instance );

	uint32 physicalDevicesToWrite = Min( *pPhysicalDeviceCount, VkInstance_t::PHYSICAL_DEVICE_COUNT );
	for ( uint32 i = 0; i < physicalDevicesToWrite; i++ ) {
		pPhysicalDevices[ i ] = reinterpret_cast< VkPhysicalDevice >( &inst->physicalDevices[ i ] );
	}
	*pPhysicalDeviceCount = physicalDevicesToWrite;
	if ( physicalDevicesToWrite < VkInstance_t::PHYSICAL_DEVICE_COUNT ) {
		return VK_INCOMPLETE;
	}
	return VK_SUCCESS;
}

void VKAPI_CALL vkGetPhysicalDeviceFeatures( VkPhysicalDevice vPhysicalDevice, VkPhysicalDeviceFeatures * pFeatures ) {
	VkPhysicalDevice_t * physicalDevice = reinterpret_cast< VkPhysicalDevice_t * >( vPhysicalDevice );
	*pFeatures = physicalDevice->supportedFeatures;
}

void VKAPI_CALL vkGetPhysicalDeviceFormatProperties( VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties * pFormatProperties ) {
	pFormatProperties = NULL;
	*pFormatProperties = VkFormatProperties();
}

void VKAPI_CALL vkGetPhysicalDeviceProperties( VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties * pProperties ) {
	VkPhysicalDevice_t * device = reinterpret_cast< VkPhysicalDevice_t * >( physicalDevice );
	*pProperties = device->properties;
}

void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties( VkPhysicalDevice vPhysicalDevice, uint32 * pQueueFamilyPropertyCount, VkQueueFamilyProperties * pQueueFamilyProperties ) {
	VkPhysicalDevice_t * physicalDevice = reinterpret_cast< VkPhysicalDevice_t * >( vPhysicalDevice );
	if ( pQueueFamilyProperties == NULL ) {
		*pQueueFamilyPropertyCount = physicalDevice->queueFamilyPropertyCount;
		return;
	}

	uint32 queueFamilyPropertiesToWrite = Min( *pQueueFamilyPropertyCount, physicalDevice->queueFamilyPropertyCount );
	for ( uint32 i = 0; i < queueFamilyPropertiesToWrite; i++ ) {
		pQueueFamilyProperties[ i ] = physicalDevice->pQueueFamilyProperties[ i ];
	}

	*pQueueFamilyPropertyCount = queueFamilyPropertiesToWrite;
}

enum class deviceExtension_t {
	SWAPCHAIN_KHR = BIT( 0 )
};
typedef VkBitFlags< deviceExtension_t > idDeviceExtensionFlags;
static const char * supportedDeviceExtensions[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

struct VkQueue_t : public VkDispatchObject_t {

};

struct VkDeviceObject_t {
	uint32 valid;
};

struct VkQueueFamily_t {
	VkQueue_t * pQueues;
	uint32		queueCount;
};

struct VkSwapchainImage_t {
	VkImage			image;
	VkDeviceMemory	memory;
};

struct VkInternalImage_t {
	HBITMAP	bitmap;
	HDC		dc;
};

struct VkSwapchain_t : public VkDeviceObject_t {
	IDXGISwapChain *		internalSwapchain;
	IDXGISurface1 *			internalBackbuffer;
	VkInternalImage_t *		pInternalImages;
	VkExtent2D				extent;
	VkFormat				imageFormat;
	VkPresentModeKHR		presentMode;
	VkImageUsageFlags		imageUsage;
	VkSwapchainImage_t *	pImages;
	uint32					imageCount;
	uint32					inUseImageCount;
	double					performanceFrequency;
	double					approximateSyncInterval;
};

enum class handleClass_t {
	SWAPCHAIN = 1,	//Can't start at 0, or we'd get a handle with all 0s, which indicates VK_NULL_HANDLE
	IMAGE,
	DEVICE_MEMORY,
	RENDER_PASS,
};

#define HANDLE_CLASS_BITS 16
#define ENCODE_OBJECT_HANDLE( handleClass, handle ) ( ( ( uint64 )handleClass << ( 64ULL - HANDLE_CLASS_BITS ) ) + ( uint64 )handle )
#define DECODE_OBJECT_HANDLE( handle ) ( ( uint64 )handle & ( ( 1ULL << ( 64ULL - HANDLE_CLASS_BITS ) ) - 1ULL ) )
#define DECODE_OBJECT_CLASS( handle ) ( ( ( uint64 )handle >> ( 64ULL - HANDLE_CLASS_BITS ) ) & ( ( 1ULL << HANDLE_CLASS_BITS ) - 1ULL ) )

struct VkImage_t : public VkDeviceObject_t {
	VkExtent3D	extent;
	void *		data;
};

struct VkDeviceMemory_t : public VkDeviceObject_t {
	void * data;
};

struct VkAttachmentDescription_t {
	VkFormat			format;
	VkAttachmentLoadOp	loadOp;
	VkAttachmentStoreOp storeOp;
};

struct VkRenderPass_t : public VkDeviceObject_t {
	VkAttachmentDescription_t *	pAttachments;
	uint32						attachmentCount;
};

struct VkDevice_t : public VkDispatchObject_t {
	VkPhysicalDevice_t *		physicalDevice;
	idDeviceExtensionFlags		enabledExtensions;
	VkPhysicalDeviceFeatures	enabledFeatures;
	VkQueueFamily_t *			pQueueFamilies;
	uint32						queueFamilyCount;
	VkSwapchain_t *				pSwapchains;
	uint64						currentSwapchainHandle;
	VkImage_t *					pImages;
	uint64						currentImageHandle;
	VkDeviceMemory_t *			pMemories;
	uint64						currentMemoryHandle;
	VkRenderPass_t *			pRenderPasses;
	uint64						currentRenderPassHandle;
};

VkResult VKAPI_CALL vkCreateDevice( VkPhysicalDevice vPhysicalDevice, const VkDeviceCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkDevice * pDevice ) {
	for ( uint32 i = 0; i < pCreateInfo->enabledExtensionCount; i++ ) {
		bool foundExtension = false;
		for ( uint32 j = 0; j < ARRAY_LENGTH( supportedDeviceExtensions ); j++ ) {
			if ( !strcmp( pCreateInfo->ppEnabledExtensionNames[ i ], supportedDeviceExtensions[ j ] ) ) {
				foundExtension = true;
				break;
			}
		}
		if ( !foundExtension ) {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}

	VkResult result;
	VkPhysicalDevice_t * physicalDevice = reinterpret_cast< VkPhysicalDevice_t * >( vPhysicalDevice );

	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( allocator->pfnAllocation( allocator->pUserData, sizeof( VkDevice_t ), 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	memset( device, 0, sizeof( *device ) );
	set_loader_magic_value( device );
	device->physicalDevice = physicalDevice;
	device->enabledExtensions.Clear();
	for ( uint32 i = 0; i < pCreateInfo->enabledExtensionCount; i++ ) {
		for ( uint32 j = 0; j < ARRAY_LENGTH( supportedDeviceExtensions ); j++ ) {
			if ( !strcmp( supportedDeviceExtensions[ j ], pCreateInfo->ppEnabledExtensionNames[ i ] ) ) {
				device->enabledExtensions.SetFlag( 1ULL << j );
				break;
			}
		}
	}

	if ( pCreateInfo->pEnabledFeatures == NULL ) {
		memset( &device->enabledFeatures, 0, sizeof( device->enabledFeatures ) );
	} else {
		const VkBool32 * currentRequestedFeature = reinterpret_cast< const VkBool32 * >( pCreateInfo->pEnabledFeatures );
		const VkBool32 * currentSupportedFeature = reinterpret_cast< const VkBool32 * >( &physicalDevice->supportedFeatures );
		VkBool32 * currentSetFeature = reinterpret_cast< VkBool32 * >( &device->enabledFeatures );
		for ( uint32 i = 0; i < sizeof( VkPhysicalDeviceFeatures ) / sizeof( VkBool32 ); i++ ) {
			if ( currentRequestedFeature[ i ] && !currentSupportedFeature[ i ] ) {
				result = VK_ERROR_FEATURE_NOT_PRESENT;
				goto deviceCreateKillDevice;
			}
			currentSetFeature[ i ] = currentRequestedFeature[ i ];
		}
	}

	for ( uint32 i = 0; i < pCreateInfo->queueCreateInfoCount; i++ ) {
		const VkDeviceQueueCreateInfo & queueCreateInfo = pCreateInfo->pQueueCreateInfos[ i ];
		if ( queueCreateInfo.queueFamilyIndex > physicalDevice->queueFamilyPropertyCount ) {
			result = VK_ERROR_INITIALIZATION_FAILED;
			goto deviceCreateDestroyQueues;
		}

		if ( device->queueFamilyCount <= queueCreateInfo.queueFamilyIndex ) {
			uint32 oldCount = device->queueFamilyCount;
			device->queueFamilyCount = queueCreateInfo.queueFamilyIndex + 1;
			device->pQueueFamilies = reinterpret_cast< VkQueueFamily_t * >( allocator->pfnReallocation( allocator->pUserData, device->pQueueFamilies, sizeof( VkQueueFamily_t ) * device->queueFamilyCount, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
			for ( uint32 j = oldCount; j < device->queueFamilyCount; j++ ) {
				memset( &device->pQueueFamilies[ j ], 0, sizeof( device->pQueueFamilies[ j ] ) );
			}
		}
		
		if ( device->pQueueFamilies[ queueCreateInfo.queueFamilyIndex ].queueCount + queueCreateInfo.queueCount > physicalDevice->pQueueFamilyProperties[ queueCreateInfo.queueFamilyIndex ].queueCount ) {
			result = VK_ERROR_INITIALIZATION_FAILED;
			goto deviceCreateDestroyQueues;
		}
		VkQueueFamily_t * queueFamily = &device->pQueueFamilies[ queueCreateInfo.queueFamilyIndex ];
		uint32 oldQueueCount = queueFamily->queueCount;
		queueFamily->pQueues = reinterpret_cast< VkQueue_t * >( allocator->pfnReallocation( allocator->pUserData, queueFamily->pQueues, sizeof( VkQueue_t ) * queueFamily->queueCount + queueCreateInfo.queueCount, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
		queueFamily->queueCount += queueCreateInfo.queueCount;
		for ( uint32 j = oldQueueCount; j < queueFamily->queueCount; j++ ) {
			memset( &queueFamily->pQueues[ j ], 0, sizeof( queueFamily->pQueues[ j ] ) );
			set_loader_magic_value( &queueFamily->pQueues[ j ] );
		}
	}

	*pDevice = reinterpret_cast< VkDevice >( device );
	return VK_SUCCESS;

deviceCreateDestroyQueues:
	for ( uint32 i = 0; i < device->queueFamilyCount; i++ ) {
		allocator->pfnFree( allocator->pUserData, device->pQueueFamilies[ i ].pQueues );
	}
	allocator->pfnFree( allocator->pUserData, device->pQueueFamilies );

deviceCreateKillDevice:
	allocator->pfnFree( allocator->pUserData, device );

	return result;
}

VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties( VkPhysicalDevice physicalDevice, const char * pLayerName, uint32 * pPropertyCount, VkExtensionProperties * pProperties ) {
	if ( pLayerName != NULL ) {
		*pPropertyCount = 0;
		return VK_SUCCESS;
	}

	if ( pProperties == NULL ) {
		*pPropertyCount = ARRAY_LENGTH( supportedDeviceExtensions );
		return VK_SUCCESS;
	}

	uint32 propertiesToWrite = Min( *pPropertyCount, ARRAY_LENGTH( supportedDeviceExtensions ) );
	for ( uint32 i = 0; i < propertiesToWrite; i++ ) {
		pProperties[ i ].specVersion = VK_MAKE_VERSION( 1, 0, VK_HEADER_VERSION );
		strcpy_s( pProperties[ i ].extensionName, supportedDeviceExtensions[ i ] );
	}
	*pPropertyCount = propertiesToWrite;

	if ( propertiesToWrite < ARRAY_LENGTH( supportedDeviceExtensions ) ) {
		return VK_INCOMPLETE;
	}
	return VK_SUCCESS;
}

void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties( VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkSampleCountFlagBits samples, VkImageUsageFlags usage, VkImageTiling tiling, uint32 * pPropertyCount, VkSparseImageFormatProperties * pProperties ) {
	*pPropertyCount = 0;
}

VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR( VkPhysicalDevice physicalDevice, VkSurfaceKHR vSurface, VkSurfaceCapabilitiesKHR * pSurfaceCapabilities ) {
	VkIcdSurfaceWin32 * surface = reinterpret_cast< VkIcdSurfaceWin32 * >( vSurface );
	VkResult result;
	RECT rect;
	BOOL success = GetClientRect( surface->hwnd, &rect );
	if ( success == FALSE ) {
		result = VK_ERROR_SURFACE_LOST_KHR;
		goto surfaceCapabilitiesFail;
	}

	pSurfaceCapabilities->minImageCount = 2;
	pSurfaceCapabilities->maxImageCount = 2;
	pSurfaceCapabilities->currentExtent = { ( uint32 )rect.right - rect.left, ( uint32 )rect.bottom - rect.top };
	pSurfaceCapabilities->minImageExtent = pSurfaceCapabilities->currentExtent;
	pSurfaceCapabilities->maxImageExtent = pSurfaceCapabilities->currentExtent;
	pSurfaceCapabilities->maxImageArrayLayers = 1;
	pSurfaceCapabilities->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	pSurfaceCapabilities->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	pSurfaceCapabilities->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	pSurfaceCapabilities->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	return VK_SUCCESS;

surfaceCapabilitiesFail:
	memset( pSurfaceCapabilities, 0, sizeof( *pSurfaceCapabilities ) );
	return result;
}

VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR( VkPhysicalDevice physicalDevice, uint32 queueFamilyIndex, VkSurfaceKHR surface, VkBool32 * pSupported ) {
	VkIcdSurfaceBase * base = reinterpret_cast< VkIcdSurfaceBase * >( surface );
	*pSupported = ( base->platform == VK_ICD_WSI_PLATFORM_WIN32 ) ? VK_TRUE : VK_FALSE;
	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR( VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32 * pSurfaceFormatCount, VkSurfaceFormatKHR * pSurfaceFormats ) {
	static VkSurfaceFormatKHR supportedFormats[] = {
		{
			VK_FORMAT_B8G8R8A8_UNORM,
			VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
		}
	};

	if ( pSurfaceFormats == NULL ) {
		*pSurfaceFormatCount = ARRAY_LENGTH( supportedFormats );
		return VK_SUCCESS;
	}

	uint32 surfaceFormatsToWrite = Min( *pSurfaceFormatCount, ARRAY_LENGTH( supportedFormats ) );
	for ( uint32 i = 0; i < surfaceFormatsToWrite; i++ ) {
		pSurfaceFormats[ i ] = supportedFormats[ i ];
	}
	*pSurfaceFormatCount = surfaceFormatsToWrite;
	if ( surfaceFormatsToWrite < ARRAY_LENGTH( supportedFormats ) ) {
		return VK_INCOMPLETE;
	}
	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR( VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32 * pPresentModeCount, VkPresentModeKHR * pPresentModes ) {
	static VkPresentModeKHR supportedPresentModes[] = {
		VK_PRESENT_MODE_FIFO_KHR
	};
	if ( pPresentModes == NULL ) {
		*pPresentModeCount = ARRAY_LENGTH( supportedPresentModes );
		return VK_SUCCESS;
	}
	
	uint32 presentModesToWrite = Min( *pPresentModeCount, ARRAY_LENGTH( supportedPresentModes ) );
	for ( uint32 i = 0; i < presentModesToWrite; i++ ) {
		pPresentModes[ i ] = supportedPresentModes[ i ];
	}
	*pPresentModeCount = presentModesToWrite;
	if ( presentModesToWrite < ARRAY_LENGTH( supportedPresentModes ) ) {
		return VK_INCOMPLETE;
	}

	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties( VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags, VkImageFormatProperties * pImageFormatProperties ) {
	if ( format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_B8G8R8A8_UNORM ) {
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}
	if ( type != VK_IMAGE_TYPE_2D ) {
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}
	if ( ( usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT ) != 0 ) {
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}
	if ( flags != 0 ) {
		return VK_ERROR_FORMAT_NOT_SUPPORTED;
	}

	pImageFormatProperties->maxArrayLayers = 1;
	pImageFormatProperties->maxExtent = { 2048, 2048, 1 };
	pImageFormatProperties->maxMipLevels = 1;
	pImageFormatProperties->maxResourceSize = 4ULL * 1024 * 1024 * 1024 - 1;
	pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;

	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkCreateImage( VkDevice vDevice, const VkImageCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkImage * pImage ) {
	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	VkPhysicalDevice physicalDevice = reinterpret_cast< VkPhysicalDevice >( device->physicalDevice );
	VkImageFormatProperties imageFormatProperties;
	VkResult result = vkGetPhysicalDeviceImageFormatProperties( physicalDevice, pCreateInfo->format, pCreateInfo->imageType, pCreateInfo->tiling, pCreateInfo->usage, pCreateInfo->flags, &imageFormatProperties );
	VK_ASSERT_SUBCALL( result );
	VK_VALIDATE( pCreateInfo->arrayLayers <= imageFormatProperties.maxArrayLayers );
	VK_VALIDATE( pCreateInfo->extent.width <= imageFormatProperties.maxExtent.width );
	VK_VALIDATE( pCreateInfo->extent.height <= imageFormatProperties.maxExtent.height );
	VK_VALIDATE( pCreateInfo->extent.depth <= imageFormatProperties.maxExtent.depth );
	VK_VALIDATE( pCreateInfo->mipLevels <= imageFormatProperties.maxMipLevels );
	VK_VALIDATE( ( pCreateInfo->samples & ( ~imageFormatProperties.sampleCounts ) ) == 0 );
	VK_VALIDATE( pCreateInfo->initialLayout == VK_IMAGE_LAYOUT_UNDEFINED || VK_IMAGE_LAYOUT_PREINITIALIZED );
	uint64 baseHandle = device->currentImageHandle;
	device->currentImageHandle++;
	device->pImages = reinterpret_cast< VkImage_t * >( allocator->pfnReallocation( allocator->pUserData, device->pImages, sizeof( VkImage_t ) * device->currentImageHandle, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	VkImage_t * image = &device->pImages[ baseHandle ];
	memset( image, 0, sizeof( *image ) );
	image->valid = true;
	*pImage = reinterpret_cast< VkImage >( ENCODE_OBJECT_HANDLE( handleClass_t::IMAGE, baseHandle ) );
	return VK_SUCCESS;

VK_SUBCALL_FAILED_LABEL:
	return result;

VK_VALIDATION_FAILED_LABEL:
	return VK_ERROR_VALIDATION_FAILED_EXT;
}

void VKAPI_CALL vkGetImageMemoryRequirements( VkDevice vDevice, VkImage vImage, VkMemoryRequirements * pMemoryRequirements ) {
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	VkImage_t * image = &device->pImages[ DECODE_OBJECT_HANDLE( vImage ) ];
	//We currently only have 1 heap and two types (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT and VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	//TODO: Make this more variable-based
	pMemoryRequirements->memoryTypeBits = 3;
	pMemoryRequirements->alignment = 16;
	pMemoryRequirements->size = image->extent.width * image->extent.height * sizeof( uint32 );
}

void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties( VkPhysicalDevice vPhysicalDevice, VkPhysicalDeviceMemoryProperties * pMemoryProperties ) {
	pMemoryProperties->memoryHeapCount = 1;
	pMemoryProperties->memoryHeaps[ 0 ].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
	pMemoryProperties->memoryHeaps[ 0 ].size = 8ULL * 1024 * 1024 * 1024;
	pMemoryProperties->memoryTypeCount = 2;
	pMemoryProperties->memoryTypes[ 0 ].heapIndex = 0;
	pMemoryProperties->memoryTypes[ 0 ].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	pMemoryProperties->memoryTypes[ 1 ].heapIndex = 0;
	pMemoryProperties->memoryTypes[ 1 ].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

VkResult VKAPI_CALL vkAllocateMemory( VkDevice vDevice, const VkMemoryAllocateInfo * pAllocateInfo, const VkAllocationCallbacks * pAllocator, VkDeviceMemory * pMemory ) {
	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	uint64 baseHandle = device->currentMemoryHandle;
	device->currentMemoryHandle++;
	device->pMemories = reinterpret_cast< VkDeviceMemory_t * >( allocator->pfnReallocation( allocator->pUserData, device->pMemories, sizeof( VkDeviceMemory_t ) * device->currentMemoryHandle, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	VkDeviceMemory_t * memory = &device->pMemories[ baseHandle ];
	memory->valid = true;
	memory->data = defaultAllocator.pfnAllocation( NULL, pAllocateInfo->allocationSize, 16, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE );
	*pMemory = reinterpret_cast< VkDeviceMemory >( ENCODE_OBJECT_HANDLE( handleClass_t::DEVICE_MEMORY, baseHandle ) );
	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkBindImageMemory( VkDevice vDevice, VkImage vImage, VkDeviceMemory vMemory, VkDeviceSize memoryOffset ) {
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	VkImage_t * image = &device->pImages[ DECODE_OBJECT_HANDLE( vImage ) ];
	VkDeviceMemory_t * memory = &device->pMemories[ DECODE_OBJECT_HANDLE( vMemory ) ];
	uint8 * bytes = reinterpret_cast< uint8 * >( memory->data );
	if ( image->data != NULL ) {
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}
	image->data = bytes + memoryOffset;
	return VK_SUCCESS;
}

void VKAPI_CALL vkFreeMemory( VkDevice vDevice, VkDeviceMemory vMemory, const VkAllocationCallbacks * ) {
	if ( vMemory == VK_NULL_HANDLE ) {
		return;
	}
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	VkDeviceMemory_t * memory = &device->pMemories[ DECODE_OBJECT_HANDLE( vMemory ) ];
	defaultAllocator.pfnFree( NULL, memory->data );
	memset( memory, 0, sizeof( *memory ) );
	do {
		device->currentMemoryHandle--;
	} while ( device->pMemories[ device->currentMemoryHandle ].valid == false );
	device->currentMemoryHandle++;
}

void VKAPI_CALL vkDestroyImage( VkDevice vDevice, VkImage vImage, const VkAllocationCallbacks * ) {
	if ( vImage == VK_NULL_HANDLE ) {
		return;
	}
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	VkImage_t * image = &device->pImages[ DECODE_OBJECT_HANDLE( vImage ) ];
	memset( image, 0, sizeof( *image ) );
	do {
		device->currentImageHandle--;
	} while ( device->pImages[ device->currentImageHandle ].valid == false );
	device->currentImageHandle++;
}

void Swapchain_InitializePresentTiming( VkSwapchain_t * swapchain ) {
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	//First, store the frequency, so we can do timing
	swapchain->performanceFrequency = static_cast< double >( freq.QuadPart );
	//Next, we need to get an approximation of the time between two vertical blanks
	//To do this, we'll ensure a sync as the starting time, then sync to the next blank for an ending time
	LARGE_INTEGER startCounter;
	LARGE_INTEGER endCounter;
	//We pump a few times to normalize the interval (first few frames seem to have erratic syncs)
	const uint32 TEST_FRAME = 5;
	for ( uint32 i = 0; i < TEST_FRAME; i++ ) {
		swapchain->internalSwapchain->Present( 1, 0 );
	}
	QueryPerformanceCounter( &startCounter );
	swapchain->internalSwapchain->Present( 1, 0 );
	QueryPerformanceCounter( &endCounter );
	swapchain->approximateSyncInterval = static_cast< double >( endCounter.QuadPart - startCounter.QuadPart );
}

VkResult Swapchain_Init( VkSwapchain_t * swapchain, const VkAllocationCallbacks * pAllocator, VkDevice vDevice, VkIcdSurfaceWin32 * surface ) {
	DXGI_SWAP_CHAIN_DESC internalSwapchainDesc;
	memset( &internalSwapchainDesc, 0, sizeof( internalSwapchainDesc ) );
	internalSwapchainDesc.BufferCount = 1;
	internalSwapchainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	internalSwapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	internalSwapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
	internalSwapchainDesc.OutputWindow = surface->hwnd;
	internalSwapchainDesc.SampleDesc.Count = 1;
	internalSwapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	internalSwapchainDesc.Windowed = TRUE;

	IDXGIFactory1 * factory;
	HRESULT hresult = CreateDXGIFactory1( IID_PPV_ARGS( &factory ) );
	if ( hresult != S_OK ) {
		return VK_ERROR_SURFACE_LOST_KHR;
	}
	IDXGIAdapter1 * adapter;
	hresult = factory->EnumAdapters1( 0, &adapter );
	factory->Release();
	if ( hresult != S_OK ) {
		return VK_ERROR_SURFACE_LOST_KHR;
	}
	hresult = D3D11CreateDeviceAndSwapChain( adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &internalSwapchainDesc, &swapchain->internalSwapchain, NULL, NULL, NULL );
	adapter->Release();
	if ( hresult != S_OK ) {
		return VK_ERROR_SURFACE_LOST_KHR;
	}
	hresult = swapchain->internalSwapchain->GetBuffer( 0, IID_PPV_ARGS( &swapchain->internalBackbuffer ) );
	if ( hresult != S_OK ) {
		swapchain->internalSwapchain->Release();
		return VK_ERROR_SURFACE_LOST_KHR;
	}
	HDC backbufferDC;
	swapchain->internalBackbuffer->GetDC( TRUE, &backbufferDC );
	Swapchain_InitializePresentTiming( swapchain );
	swapchain->pInternalImages = reinterpret_cast< VkInternalImage_t * >( pAllocator->pfnAllocation( pAllocator->pUserData, sizeof( VkInternalImage_t ) * swapchain->imageCount, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	for ( uint32 i = 0; i < swapchain->imageCount; i++ ) {
		VkInternalImage_t * currentImage = &swapchain->pInternalImages[ i ];
		currentImage->dc = CreateCompatibleDC( backbufferDC );
		currentImage->bitmap = CreateBitmap( swapchain->extent.width, swapchain->extent.height, 1, 32, NULL );
		SelectObject( currentImage->dc, currentImage->bitmap );
	}
	uint32 * bits = new uint32[ swapchain->extent.width * swapchain->extent.height ];
	uint32 color = 0xFF00FF00;
	for ( uint32 row = 0; row < swapchain->extent.height; row++ ) {
		for ( uint32 column = 0; column < swapchain->extent.width; column++ ) {
			bits[ row * swapchain->extent.width + column ] = color;
		}
	}
	SetBitmapBits( swapchain->pInternalImages[ 0 ].bitmap, swapchain->extent.width * swapchain->extent.height * sizeof( uint32 ), bits );
	BitBlt( backbufferDC, 0, 0, swapchain->extent.width, swapchain->extent.height, swapchain->pInternalImages[ 0 ].dc, 0, 0, SRCCOPY );
	swapchain->internalBackbuffer->ReleaseDC( NULL );
	swapchain->internalSwapchain->Present( 1, 0 );

	VkImageCreateInfo imageCreateInfo;
	memset( &imageCreateInfo, 0, sizeof( imageCreateInfo ) );
	imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.extent = {
		swapchain->extent.width,
		swapchain->extent.height,
		1
	};
	imageCreateInfo.format = swapchain->imageFormat;
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.usage = swapchain->imageUsage;
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	VkPhysicalDevice physicalDevice = reinterpret_cast< VkPhysicalDevice >( device->physicalDevice );

	swapchain->pImages = new VkSwapchainImage_t[ swapchain->imageCount ];
	memset( swapchain->pImages, 0, sizeof( *swapchain->pImages ) * swapchain->imageCount );
	VkResult result;
	for ( uint32 i = 0; i < swapchain->imageCount; i++ ) {
		result = vkCreateImage( vDevice, &imageCreateInfo, pAllocator, &swapchain->pImages[ i ].image );
		VK_ASSERT_SUBCALL( result );
		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements( vDevice, swapchain->pImages[ i ].image, &memReq );
		VkMemoryAllocateInfo memoryAllocateInfo;
		memset( &memoryAllocateInfo, 0, sizeof( memoryAllocateInfo ) );

		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties( physicalDevice, &memProps );
		uint32 memoryTypeIndex;
		for ( memoryTypeIndex = 0; memoryTypeIndex < memProps.memoryTypeCount; memoryTypeIndex++ ) {
			if ( ( memReq.memoryTypeBits & ( 1 << memoryTypeIndex ) ) != 0 ) {
				if ( ( memProps.memoryTypes[ memoryTypeIndex ].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) {
					break;
				}
			}
		}
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.allocationSize = memReq.size;
		memoryAllocateInfo.memoryTypeIndex = memoryTypeIndex;
		result = vkAllocateMemory( vDevice, &memoryAllocateInfo, pAllocator, &swapchain->pImages[ i ].memory );
		VK_ASSERT_SUBCALL( result );
		result = vkBindImageMemory( vDevice, swapchain->pImages[ i ].image, swapchain->pImages[ i ].memory, 0 );
		VK_ASSERT_SUBCALL( result );
	}

	return VK_SUCCESS;

VK_SUBCALL_FAILED_LABEL:
	for ( uint32 i = 0; i < swapchain->imageCount; i++ ) {
		vkFreeMemory( vDevice, swapchain->pImages[ i ].memory, pAllocator );
		vkDestroyImage( vDevice, swapchain->pImages[ i ].image, pAllocator );
	}
	return result;
}

VkResult VKAPI_CALL vkCreateSwapchainKHR( VkDevice vDevice, const VkSwapchainCreateInfoKHR * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkSwapchainKHR * pSwapchain ) {
	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	uint64 baseHandle = device->currentSwapchainHandle;
	device->currentSwapchainHandle++;
	VK_VALIDATE( device->enabledExtensions.CheckFlag( deviceExtension_t::SWAPCHAIN_KHR ) );
	device->pSwapchains = reinterpret_cast< VkSwapchain_t * >( allocator->pfnReallocation( allocator->pUserData, device->pSwapchains, sizeof( VkSwapchain_t ) * device->currentSwapchainHandle, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	VkSwapchain_t * swapchain = &device->pSwapchains[ baseHandle ];
	memset( swapchain, 0, sizeof( *swapchain ) );
	VK_VALIDATE( pCreateInfo->compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR );
	VK_VALIDATE( pCreateInfo->imageArrayLayers == 1 );
	VK_VALIDATE( pCreateInfo->imageColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR );
	VkIcdSurfaceWin32 * surface = reinterpret_cast< VkIcdSurfaceWin32 * >( pCreateInfo->surface );
	RECT clientRect;
	BOOL getExtentSuccess = GetClientRect( surface->hwnd, &clientRect );
	if ( getExtentSuccess == FALSE ) {
		device->currentSwapchainHandle--;
		return VK_ERROR_SURFACE_LOST_KHR;
	}
	VK_VALIDATE( pCreateInfo->imageExtent.width == ( clientRect.right - clientRect.left ) && pCreateInfo->imageExtent.height == ( clientRect.bottom - clientRect.top ) );
	VK_VALIDATE( ( pCreateInfo->imageUsage & ( ~( VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT ) ) ) == 0 );
	VK_VALIDATE( pCreateInfo->minImageCount <= 3 && pCreateInfo->minImageCount >= 2 );
	VK_VALIDATE( pCreateInfo->preTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR );
	VK_VALIDATE( pCreateInfo->presentMode == VK_PRESENT_MODE_FIFO_KHR || pCreateInfo->presentMode == VK_PRESENT_MODE_MAILBOX_KHR );
	if ( pCreateInfo->oldSwapchain != VK_NULL_HANDLE ) {
		uint64 oldHandle = DECODE_OBJECT_HANDLE( pCreateInfo->oldSwapchain );
		device->pSwapchains[ oldHandle ].valid = false;
	}
	swapchain->valid = true;
	swapchain->extent = pCreateInfo->imageExtent;
	swapchain->imageFormat = pCreateInfo->imageFormat;
	swapchain->presentMode = pCreateInfo->presentMode;
	swapchain->imageCount = pCreateInfo->minImageCount;
	swapchain->imageUsage = pCreateInfo->imageUsage;
	VkResult result = Swapchain_Init( swapchain, allocator, vDevice, surface );
	VK_ASSERT_SUBCALL( result );
	*pSwapchain = reinterpret_cast< VkSwapchainKHR >( ENCODE_OBJECT_HANDLE( handleClass_t::SWAPCHAIN, baseHandle ) );

	return VK_SUCCESS;

VK_VALIDATION_FAILED_LABEL:
	device->currentSwapchainHandle--;
	return VK_ERROR_VALIDATION_FAILED_EXT;

VK_SUBCALL_FAILED_LABEL:
	device->currentSwapchainHandle--;
	return result;
}

VK_ICD_EXPORT PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr( VkInstance instance, const char * pName ) {
	VK_PATCH_FUNCTION( vkCreateInstance );
	VK_PATCH_FUNCTION( vkEnumerateInstanceExtensionProperties );
	VK_PATCH_FUNCTION( vkDestroyInstance );
	VK_PATCH_FUNCTION( vkEnumeratePhysicalDevices );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceFeatures );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceFormatProperties );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceImageFormatProperties );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceProperties );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceQueueFamilyProperties );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceMemoryProperties );
	VK_PATCH_FUNCTION( vkGetDeviceProcAddr );
	VK_PATCH_FUNCTION( vkCreateDevice );
	VK_PATCH_FUNCTION( vkEnumerateDeviceExtensionProperties );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceSparseImageFormatProperties );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceSurfaceCapabilitiesKHR );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceSurfaceSupportKHR );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceSurfaceFormatsKHR );
	VK_PATCH_FUNCTION( vkGetPhysicalDeviceSurfacePresentModesKHR );
	VK_PATCH_FUNCTION( vkCreateSwapchainKHR );
	VK_DENY_FUNCTION( vkEnumerateInstanceVersion );

	return ( PFN_vkVoidFunction )_strdup( pName );
}

VkResult VKAPI_CALL vkGetSwapchainImagesKHR( VkDevice vDevice, VkSwapchainKHR vSwapchain, uint32 * pSwapchainImageCount, VkImage * pSwapchainImages ) {
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	uint64 handle = DECODE_OBJECT_HANDLE( vSwapchain );
	VkSwapchain_t * swapchain = &device->pSwapchains[ handle ];
	if ( pSwapchainImages == NULL ) {
		*pSwapchainImageCount = swapchain->imageCount;
		return VK_SUCCESS;
	}

	uint32 imagesToWrite = Min( *pSwapchainImageCount, swapchain->imageCount );
	for ( uint32 i = 0; i < imagesToWrite; i++ ) {
		pSwapchainImages[ i ] = swapchain->pImages[ i ].image;
	}
	*pSwapchainImageCount = imagesToWrite;
	swapchain->inUseImageCount = imagesToWrite;
	if ( imagesToWrite < swapchain->imageCount ) {
		return VK_INCOMPLETE;
	}

	return VK_SUCCESS;
}

VkResult RenderPass_Init( VkRenderPass_t * renderPass, const VkRenderPassCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator ) {
	renderPass->pAttachments = reinterpret_cast< VkAttachmentDescription_t * >( pAllocator->pfnAllocation( pAllocator->pUserData, sizeof( VkAttachmentDescription_t ) * pCreateInfo->attachmentCount, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	renderPass->attachmentCount = pCreateInfo->attachmentCount;
	for ( uint32 i = 0; i < renderPass->attachmentCount; i++ ) {
		VkAttachmentDescription_t * dst = &renderPass->pAttachments[ i ];
		memset( dst, 0, sizeof( *dst ) );
		const VkAttachmentDescription * src = &pCreateInfo->pAttachments[ i ];
		dst->format = src->format;
		dst->loadOp = src->loadOp;
		dst->storeOp = src->storeOp;
	}

	return VK_SUCCESS;
}

VkResult VKAPI_CALL vkCreateRenderPass( VkDevice vDevice, const VkRenderPassCreateInfo * pCreateInfo, const VkAllocationCallbacks * pAllocator, VkRenderPass * pRenderPass ) {
	VkDevice_t * device = reinterpret_cast< VkDevice_t * >( vDevice );
	const VkAllocationCallbacks * allocator = ( pAllocator != NULL ) ? pAllocator : &defaultAllocator;
	uint64 baseHandle = device->currentRenderPassHandle;
	device->currentRenderPassHandle++;
	device->pRenderPasses = reinterpret_cast< VkRenderPass_t * >( allocator->pfnReallocation( allocator->pUserData, device->pRenderPasses, sizeof( VkRenderPass_t ) * device->currentRenderPassHandle, 4, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE ) );
	VkRenderPass_t * renderPass = &device->pRenderPasses[ baseHandle ];
	memset( renderPass, 0, sizeof( *renderPass ) );
	renderPass->valid = true;
	VkResult result = RenderPass_Init( renderPass, pCreateInfo, allocator );
	VK_ASSERT_SUBCALL( result );
	*pRenderPass = reinterpret_cast< VkRenderPass >( ENCODE_OBJECT_HANDLE( handleClass_t::RENDER_PASS, baseHandle ) );

	return VK_SUCCESS;

VK_SUBCALL_FAILED_LABEL:
	return result;
}

PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr( VkDevice device, const char * pName ) {
	VK_PATCH_FUNCTION( vkGetSwapchainImagesKHR );
	VK_PATCH_FUNCTION( vkCreateRenderPass );
	return ( PFN_vkVoidFunction )_strdup( pName );
}
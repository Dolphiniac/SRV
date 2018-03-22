#include "../../SoftwareVulkan/Code/Common.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <objidl.h>
#include <stdint.h>
#include <gdiplus.h>
#pragma comment( lib, "gdiplus" )

#define RC_USE_SOFTWARE_IMPLEMENTATION

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <d3d11.h>
#pragma comment( lib, "d3d11" )
#pragma comment( lib, "dxgi" )
#pragma comment( lib, "vulkan-1" )

void DebuggerPrintf( const char * fmt, ... ) {
	char buff[ 2048 ];
	va_list v;
	va_start( v, fmt );
	vsnprintf_s( buff, 2048, fmt, v );
	va_end( v );

	OutputDebugString( buff );
}

int WinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd ) {
	WNDCLASSEX windowClass;
	memset( &windowClass, 0, sizeof( windowClass ) );
	windowClass.cbSize = sizeof( windowClass );
	windowClass.style = CS_OWNDC;
	windowClass.lpfnWndProc = DefWindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hIcon = NULL;
	windowClass.hCursor = LoadCursor( hInstance, IDC_ARROW );
	windowClass.lpszClassName = "Default Window Class";

	RegisterClassEx( &windowClass );

	DWORD windowStyles = WS_CAPTION | WS_POPUP | WS_VISIBLE;
	RECT windowRect;
	memset( &windowRect, 0, sizeof( windowRect ) );
	windowRect.top = 50;
	windowRect.left = 50;
	windowRect.right = windowRect.left + 1600;
	windowRect.bottom = windowRect.top + 900;
	AdjustWindowRect( &windowRect, windowStyles, FALSE );
	HWND hwnd = CreateWindowEx( 0, windowClass.lpszClassName, "Vulkan Implementation Test", windowStyles,
					windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top,
					NULL, NULL, hInstance, NULL );
	HRESULT hresult;
	DXGI_SWAP_CHAIN_DESC swapchainDesc;
	memset( &swapchainDesc, 0, sizeof( swapchainDesc ) );
	swapchainDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = 1;
	swapchainDesc.OutputWindow = hwnd;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapchainDesc.Windowed = TRUE;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
	IDXGISwapChain * dSwapchain;
	IDXGIFactory1 * dxgiFactory;
	hresult = CreateDXGIFactory1( IID_PPV_ARGS( &dxgiFactory ) );
	IDXGIAdapter1 * adapter;
	hresult = dxgiFactory->EnumAdapters1( 0, &adapter );
	dxgiFactory->Release();
	hresult = D3D11CreateDeviceAndSwapChain( adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &swapchainDesc, &dSwapchain, NULL, NULL, NULL );
	IDXGISurface1 * dSurface;
	hresult = dSwapchain->GetBuffer( 0, IID_PPV_ARGS( &dSurface ) );
	HDC dc;
	hresult = dSurface->GetDC( TRUE, &dc );
	ULONG_PTR token;
	Gdiplus::GdiplusStartupInput input;

	GdiplusStartup( &token, &input, NULL );
	Gdiplus::Bitmap bitmap( 1600, 900, PixelFormat32bppARGB );
	Gdiplus::Graphics graphics( hwnd );
	
	Gdiplus::Rect rect = Gdiplus::Rect( 0, 0, bitmap.GetWidth(), bitmap.GetHeight() );

	Gdiplus::BitmapData data;
	bitmap.LockBits( &rect, 0, PixelFormat32bppARGB, &data );

	uint32 * pixelData = reinterpret_cast< uint32 * >( data.Scan0 );
	uint32 pixel = 0xFFFF0000;
	
	for ( uint32 row = 0; row < data.Height; row++ ) {
		for ( uint32 col = 0; col < data.Width - 3; col++ ) {
			pixelData[ row * data.Stride / sizeof( uint32 ) + col ] = pixel;
		}
	}

	bitmap.UnlockBits( &data );
	graphics.DrawImage( &bitmap, rect );

	VkApplicationInfo appInfo;
	memset( &appInfo, 0, sizeof( appInfo ) );
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.apiVersion = VK_MAKE_VERSION( 1, 0, VK_HEADER_VERSION );
	appInfo.applicationVersion = VK_MAKE_VERSION( 0, 1, 0 );
	appInfo.pApplicationName = "Vulkan Implementation Test";
	appInfo.engineVersion = VK_MAKE_VERSION( 0, 1, 0 );
	appInfo.pEngineName = "NULL";
	
	VkInstanceCreateInfo instanceCreateInfo;
	memset( &instanceCreateInfo, 0, sizeof( instanceCreateInfo ) );
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pApplicationInfo = &appInfo;
	instanceCreateInfo.enabledExtensionCount = 2;
	const char * instanceExtensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME
	};
	instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions;
	LoadLibraryA( "SoftwareVulkan.dll" );
	VkInstance instance;
	VkResult result = vkCreateInstance( &instanceCreateInfo, NULL, &instance );

	uint32 physicalDeviceCount;
	vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, NULL );

	VkPhysicalDevice * devices = new VkPhysicalDevice[ physicalDeviceCount ];
	vkEnumeratePhysicalDevices( instance, &physicalDeviceCount, devices );

	VkPhysicalDevice physicalDevice = NULL;
	for ( uint32 i = 0; i < physicalDeviceCount; i++ ) {
		VkPhysicalDeviceProperties properties;
		vkGetPhysicalDeviceProperties( devices[ i ], &properties );
#if defined( RC_USE_SOFTWARE_IMPLEMENTATION )
		if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ) {
#else
		if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
#endif
			physicalDevice = devices[ i ];
			break;
		}
	}

	delete[] devices;
	
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties( physicalDevice, &props );

	VkDeviceCreateInfo deviceCreateInfo;
	memset( &deviceCreateInfo, 0, sizeof( deviceCreateInfo ) );
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.enabledExtensionCount = 1;
	const char * deviceExtensions[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;
	VkPhysicalDeviceFeatures supportedFeatures;
	vkGetPhysicalDeviceFeatures( physicalDevice, &supportedFeatures );
	VkPhysicalDeviceFeatures features = { VK_FALSE };
	features.fullDrawIndexUint32 = supportedFeatures.fullDrawIndexUint32;
	deviceCreateInfo.pEnabledFeatures = &features;
	VkDeviceQueueCreateInfo queueCreateInfo;
	memset( &queueCreateInfo, 0, sizeof( queueCreateInfo ) );
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	uint32 queueFamilyPropertyCount;
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyPropertyCount, NULL );
	VkQueueFamilyProperties * queueFamilyProperties = new VkQueueFamilyProperties[ queueFamilyPropertyCount ];
	vkGetPhysicalDeviceQueueFamilyProperties( physicalDevice, &queueFamilyPropertyCount, queueFamilyProperties );
	float queuePriorities[] = { 1.0f, 1.0f, 1.0f };
	queueCreateInfo.pQueuePriorities = queuePriorities;
	queueCreateInfo.queueCount = 3;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	VkDevice device;
	vkCreateDevice( physicalDevice, &deviceCreateInfo, NULL, &device );

	delete[] queueFamilyProperties;

	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo;
	memset( &surfaceCreateInfo, 0, sizeof( surfaceCreateInfo ) );
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.hinstance = hInstance;
	surfaceCreateInfo.hwnd = hwnd;

	VkSurfaceKHR surface;
	vkCreateWin32SurfaceKHR( instance, &surfaceCreateInfo, NULL, &surface );

	VkBool32 supported;
	result = vkGetPhysicalDeviceSurfaceSupportKHR( physicalDevice, 0, surface, &supported );

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( physicalDevice, surface, &surfaceCapabilities );

	uint32 surfaceFormatCount;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, &surfaceFormatCount, NULL );

	VkSurfaceFormatKHR * surfaceFormats = new VkSurfaceFormatKHR[ surfaceFormatCount ];
	result = vkGetPhysicalDeviceSurfaceFormatsKHR( physicalDevice, surface, &surfaceFormatCount, surfaceFormats );

	uint32 presentModeCount;
	result = vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, surface, &presentModeCount, NULL );

	VkPresentModeKHR * presentModes = new VkPresentModeKHR[ presentModeCount ];
	result = vkGetPhysicalDeviceSurfacePresentModesKHR( physicalDevice, surface, &presentModeCount, presentModes );

	VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	for ( uint32 i = 0; i < presentModeCount; i++ ) {
		if ( presentModes[ i ] == VK_PRESENT_MODE_MAILBOX_KHR ) {
			desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			break;
		}
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfoKHR;
	memset( &swapchainCreateInfoKHR, 0, sizeof( swapchainCreateInfoKHR ) );
	swapchainCreateInfoKHR.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfoKHR.surface = surface;
	swapchainCreateInfoKHR.minImageCount = Min( 3, surfaceCapabilities.maxImageCount );
	swapchainCreateInfoKHR.imageFormat = surfaceFormats[ 0 ].format;
	swapchainCreateInfoKHR.imageColorSpace = surfaceFormats[ 0 ].colorSpace;
	swapchainCreateInfoKHR.imageExtent = surfaceCapabilities.currentExtent;
	swapchainCreateInfoKHR.imageArrayLayers = 1;
	swapchainCreateInfoKHR.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfoKHR.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfoKHR.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapchainCreateInfoKHR.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfoKHR.presentMode = desiredPresentMode;

	VkSwapchainKHR swapchain;
	result = vkCreateSwapchainKHR( device, &swapchainCreateInfoKHR, NULL, &swapchain );

	uint32 swapchainImageCount;
	result = vkGetSwapchainImagesKHR( device, swapchain, &swapchainImageCount, NULL );

	VkImage * swapchainImages = new VkImage[ swapchainImageCount ];
	result = vkGetSwapchainImagesKHR( device, swapchain, &swapchainImageCount, swapchainImages );

	VkAttachmentDescription attachments[] = {
		{
			/* VkAttachmentDescriptionFlags    flags;		   */ 0,
			/* VkFormat                        format;		   */ VK_FORMAT_R8G8B8A8_UNORM,
			/* VkSampleCountFlagBits           samples;		   */ VK_SAMPLE_COUNT_1_BIT,
			/* VkAttachmentLoadOp              loadOp;		   */ VK_ATTACHMENT_LOAD_OP_CLEAR,
			/* VkAttachmentStoreOp             storeOp;		   */ VK_ATTACHMENT_STORE_OP_STORE,
			/* VkAttachmentLoadOp              stencilLoadOp;  */ VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			/* VkAttachmentStoreOp             stencilStoreOp; */ VK_ATTACHMENT_STORE_OP_DONT_CARE,
			/* VkImageLayout                   initialLayout;  */ VK_IMAGE_LAYOUT_UNDEFINED,
			/* VkImageLayout                   finalLayout;	   */ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		}
	};

	VkAttachmentReference colorAttachments[] = {
		{
			/* uint32_t         attachment;	*/ 0,
			/* VkImageLayout    layout;		*/ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		}
	};

	VkSubpassDescription subpasses[] = {
		{
			/* VkSubpassDescriptionFlags       flags;					*/ 0,
			/* VkPipelineBindPoint             pipelineBindPoint;		*/ VK_PIPELINE_BIND_POINT_GRAPHICS,
			/* uint32_t                        inputAttachmentCount;	*/ 0,
			/* const VkAttachmentReference*    pInputAttachments;		*/ NULL,
			/* uint32_t                        colorAttachmentCount;	*/ ARRAY_LENGTH( colorAttachments ),
			/* const VkAttachmentReference*    pColorAttachments;		*/ colorAttachments,
			/* const VkAttachmentReference*    pResolveAttachments;		*/ NULL,
			/* const VkAttachmentReference*    pDepthStencilAttachment;	*/ NULL,
			/* uint32_t                        preserveAttachmentCount;	*/ 0,
			/* const uint32_t*                 pPreserveAttachments;	*/ NULL
		}
	};

	VkRenderPassCreateInfo renderPassCreateInfo;
	memset( &renderPassCreateInfo, 0, sizeof( renderPassCreateInfo ) );
	renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCreateInfo.attachmentCount = ARRAY_LENGTH( attachments );
	renderPassCreateInfo.pAttachments = attachments;
	renderPassCreateInfo.subpassCount = ARRAY_LENGTH( subpasses );
	renderPassCreateInfo.pSubpasses = subpasses;

	VkRenderPass renderPass;
	result = vkCreateRenderPass( device, &renderPassCreateInfo, NULL, &renderPass );


}
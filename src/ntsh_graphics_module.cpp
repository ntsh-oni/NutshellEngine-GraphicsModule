#include "ntsh_graphics_module.h"
#include "../external/Module/utils/ntsh_dynamic_library.h"
#include "../external/Common/module_interfaces/ntsh_window_module_interface.h"
#include <limits>
#include <array>

void NutshellGraphicsModule::init() {
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		m_framesInFlight = 2;
	}
	else {
		m_framesInFlight = 1;
	}

	// Create instance
	VkApplicationInfo applicationInfo = {};
	applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	applicationInfo.pNext = nullptr;
	applicationInfo.pApplicationName = "NutshellEngine Vulkan Model Graphics Module";
	applicationInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
	applicationInfo.pEngineName = "NutshellEngine";
	applicationInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
	applicationInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pNext = nullptr;
	instanceCreateInfo.flags = 0;
	instanceCreateInfo.pApplicationInfo = &applicationInfo;
#if defined(NTSH_DEBUG)
	std::array<const char*, 1> explicitLayers = { "VK_LAYER_KHRONOS_validation" };
	bool foundValidationLayer = false;
	uint32_t instanceLayerPropertyCount;
	NTSH_VK_CHECK(vkEnumerateInstanceLayerProperties(&instanceLayerPropertyCount, nullptr));
	std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerPropertyCount);
	NTSH_VK_CHECK(vkEnumerateInstanceLayerProperties(&instanceLayerPropertyCount, instanceLayerProperties.data()));

	for (const VkLayerProperties& availableLayer : instanceLayerProperties) {
		if (strcmp(availableLayer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
			foundValidationLayer = true;
			break;
		}
	}

	if (foundValidationLayer) {
		instanceCreateInfo.enabledLayerCount = 1;
		instanceCreateInfo.ppEnabledLayerNames = explicitLayers.data();
	}
	else {
		NTSH_MODULE_WARNING("Could not find validation layer VK_LAYER_KHRONOS_validation.");
		instanceCreateInfo.enabledLayerCount = 0;
		instanceCreateInfo.ppEnabledLayerNames = nullptr;
	}
#else
	instanceCreateInfo.enabledLayerCount = 0;
	instanceCreateInfo.ppEnabledLayerNames = nullptr;
#endif
	std::vector<const char*> instanceExtensions;
#if defined(NTSH_DEBUG)
	instanceExtensions.push_back("VK_EXT_debug_utils");
#endif
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		instanceExtensions.push_back("VK_KHR_surface");
		instanceExtensions.push_back("VK_KHR_get_surface_capabilities2");
		instanceExtensions.push_back("VK_KHR_get_physical_device_properties2");
#if defined(NTSH_OS_WINDOWS)
		instanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(NTSH_OS_LINUX)
		instanceExtensions.push_back("VK_KHR_xlib_surface");
#endif
	}
	instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
	instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	NTSH_VK_CHECK(vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance));

#if defined(NTSH_DEBUG)
	// Create debug messenger
	VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo = {};
	debugMessengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugMessengerCreateInfo.pNext = nullptr;
	debugMessengerCreateInfo.flags = 0;
	debugMessengerCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugMessengerCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugMessengerCreateInfo.pfnUserCallback = debugCallback;
	debugMessengerCreateInfo.pUserData = nullptr;

	auto createDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
	NTSH_VK_CHECK(createDebugUtilsMessengerEXT(m_instance, &debugMessengerCreateInfo, nullptr, &m_debugMessenger));
#endif

	// Create surface
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
#if defined(NTSH_OS_WINDOWS)
		HWND windowHandle = m_windowModule->getNativeHandle(NTSH_MAIN_WINDOW);
		VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.pNext = nullptr;
		surfaceCreateInfo.flags = 0;
		surfaceCreateInfo.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(windowHandle, GWLP_HINSTANCE));
		surfaceCreateInfo.hwnd = windowHandle;
		auto createWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(m_instance, "vkCreateWin32SurfaceKHR");
		NTSH_VK_CHECK(createWin32SurfaceKHR(m_instance, &surfaceCreateInfo, nullptr, &m_surface));
#elif defined(NTSH_OS_LINUX)
		m_display = XOpenDisplay(NULL);
		Window windowHandle = m_windowModule->getNativeHandle(NTSH_MAIN_WINDOW);
		VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.pNext = nullptr;
		surfaceCreateInfo.flags = 0;
		surfaceCreateInfo.dpy = m_display;
		surfaceCreateInfo.window = windowHandle;
		auto createXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)vkGetInstanceProcAddr(m_instance, "vkCreateXlibSurfaceKHR");
		NTSH_VK_CHECK(createXlibSurfaceKHR(m_instance, &surfaceCreateInfo, nullptr, &m_surface));
#endif
	}

	// Pick a physical device
	uint32_t deviceCount;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
	if (deviceCount == 0) {
		NTSH_MODULE_ERROR("Vulkan: Found no suitable GPU.", NTSH_RESULT_UNKNOWN_ERROR);
	}
	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, physicalDevices.data());
	m_physicalDevice = physicalDevices[0];

	VkPhysicalDeviceProperties2 physicalDeviceProperties2 = {};
	physicalDeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	physicalDeviceProperties2.pNext = nullptr;
	vkGetPhysicalDeviceProperties2(m_physicalDevice, &physicalDeviceProperties2);

	std::string physicalDeviceType;
	switch (physicalDeviceProperties2.properties.deviceType) {
	case VK_PHYSICAL_DEVICE_TYPE_OTHER:
		physicalDeviceType = "Other";
		break;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
		physicalDeviceType = "Integrated";
		break;
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
		physicalDeviceType = "Discrete";
		break;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
		physicalDeviceType = "Virtual";
		break;
	case VK_PHYSICAL_DEVICE_TYPE_CPU:
		physicalDeviceType = "CPU";
		break;
	}

	std::string driverVersion = std::to_string(VK_API_VERSION_MAJOR(physicalDeviceProperties2.properties.driverVersion)) + "."
		+ std::to_string(VK_API_VERSION_MINOR(physicalDeviceProperties2.properties.driverVersion)) + "."
		+ std::to_string(VK_API_VERSION_PATCH(physicalDeviceProperties2.properties.driverVersion));
	if (physicalDeviceProperties2.properties.vendorID == 4318) { // NVIDIA
		uint32_t major = (physicalDeviceProperties2.properties.driverVersion >> 22) & 0x3ff;
		uint32_t minor = (physicalDeviceProperties2.properties.driverVersion >> 14) & 0x0ff;
		uint32_t patch = (physicalDeviceProperties2.properties.driverVersion >> 6) & 0x0ff;
		driverVersion = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
	}
#if defined(NTSH_OS_WINDOWS)
	else if (physicalDeviceProperties2.properties.vendorID == 0x8086) { // Intel
		uint32_t major = (physicalDeviceProperties2.properties.driverVersion >> 14);
		uint32_t minor = (physicalDeviceProperties2.properties.driverVersion) & 0x3fff;
		driverVersion = std::to_string(major) + "." + std::to_string(minor);
	}
#endif

	NTSH_MODULE_INFO("Physical Device Name: " + std::string(physicalDeviceProperties2.properties.deviceName));
	NTSH_MODULE_INFO("Physical Device Type: " + physicalDeviceType);
	NTSH_MODULE_INFO("Physical Device Driver Version: " + driverVersion);
	NTSH_MODULE_INFO("Physical Device Vulkan API Version: " + std::to_string(VK_API_VERSION_MAJOR(physicalDeviceProperties2.properties.apiVersion)) + "."
		+ std::to_string(VK_API_VERSION_MINOR(physicalDeviceProperties2.properties.apiVersion)) + "."
		+ std::to_string(VK_API_VERSION_PATCH(physicalDeviceProperties2.properties.apiVersion)));

	// Find a queue family supporting graphics
	uint32_t queueFamilyPropertyCount;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyPropertyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyPropertyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyPropertyCount, queueFamilyProperties.data());
	m_graphicsQueueFamilyIndex = 0;
	for (const VkQueueFamilyProperties& queueFamilyProperty : queueFamilyProperties) {
		if (queueFamilyProperty.queueCount > 0 && queueFamilyProperty.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
				VkBool32 presentSupport;
				vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_graphicsQueueFamilyIndex, m_surface, &presentSupport);
				if (presentSupport) {
					break;
				}
			}
			else {
				break;
			}
		}
		m_graphicsQueueFamilyIndex++;
	}

	// Create a queue supporting graphics
	float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo deviceQueueCreateInfo = {};
	deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	deviceQueueCreateInfo.pNext = nullptr;
	deviceQueueCreateInfo.flags = 0;
	deviceQueueCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
	deviceQueueCreateInfo.queueCount = 1;
	deviceQueueCreateInfo.pQueuePriorities = &queuePriority;

	// Enable features
	VkPhysicalDeviceDescriptorIndexingFeatures physicalDeviceDescriptorIndexingFeatures = {};
	physicalDeviceDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	physicalDeviceDescriptorIndexingFeatures.pNext = nullptr;
	physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
	physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;

	VkPhysicalDeviceDynamicRenderingFeatures physicalDeviceDynamicRenderingFeatures = {};
	physicalDeviceDynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
	physicalDeviceDynamicRenderingFeatures.pNext = &physicalDeviceDescriptorIndexingFeatures;
	physicalDeviceDynamicRenderingFeatures.dynamicRendering = VK_TRUE;

	VkPhysicalDeviceSynchronization2Features physicalDeviceSynchronization2Features = {};
	physicalDeviceSynchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
	physicalDeviceSynchronization2Features.pNext = &physicalDeviceDynamicRenderingFeatures;
	physicalDeviceSynchronization2Features.synchronization2 = VK_TRUE;

	VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
	physicalDeviceFeatures.samplerAnisotropy = VK_TRUE;
	
	// Create the logical device
	VkDeviceCreateInfo deviceCreateInfo = {};
	deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceCreateInfo.pNext = &physicalDeviceSynchronization2Features;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &deviceQueueCreateInfo;
	deviceCreateInfo.enabledLayerCount = 0;
	deviceCreateInfo.ppEnabledLayerNames = nullptr;
	std::vector<const char*> deviceExtensions = { "VK_KHR_synchronization2",
		"VK_KHR_create_renderpass2",
		"VK_KHR_depth_stencil_resolve", 
		"VK_KHR_dynamic_rendering",
		"VK_KHR_maintenance3",
		"VK_EXT_descriptor_indexing" };
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		deviceExtensions.push_back("VK_KHR_swapchain");
	}
	deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
	deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
	deviceCreateInfo.pEnabledFeatures = &physicalDeviceFeatures;
	NTSH_VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device));

	vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);

	// Get functions
	m_vkCmdPipelineBarrier2KHR = (PFN_vkCmdPipelineBarrier2KHR)vkGetDeviceProcAddr(m_device, "vkCmdPipelineBarrier2KHR");
	m_vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(m_device, "vkCmdBeginRenderingKHR");
	m_vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(m_device, "vkCmdEndRenderingKHR");

	// Create the swapchain
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		createSwapchain(VK_NULL_HANDLE);
	}
	// Or create an image to draw on
	else {
		m_imageCount = 1;

		m_viewport.x = 0.0f;
		m_viewport.y = 0.0f;
		m_viewport.width = 1280.0f;
		m_viewport.height = 720.0f;
		m_viewport.minDepth = 0.0f;
		m_viewport.maxDepth = 1.0f;

		m_scissor.offset.x = 0;
		m_scissor.offset.y = 0;
		m_scissor.extent.width = 1280;
		m_scissor.extent.height = 720;

		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.pNext = nullptr;
		imageCreateInfo.flags = 0;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
		imageCreateInfo.extent.width = 1280;
		imageCreateInfo.extent.height = 720;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.queueFamilyIndexCount = 0;
		imageCreateInfo.pQueueFamilyIndices = nullptr;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VmaAllocationCreateInfo imageAllocationCreateInfo = {};
		imageAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		vmaCreateImage(m_allocator, &imageCreateInfo, &imageAllocationCreateInfo, &m_drawImage, &m_drawImageAllocation, nullptr);

		// Create the image view
		VkImageViewCreateInfo imageViewCreateInfo = {};
		imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		imageViewCreateInfo.pNext = nullptr;
		imageViewCreateInfo.flags = 0;
		imageViewCreateInfo.image = m_drawImage;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = imageCreateInfo.format;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		NTSH_VK_CHECK(vkCreateImageView(m_device, &imageViewCreateInfo, nullptr, &m_drawImageView));
	}

	// Initialize VMA
	VmaAllocatorCreateInfo vmaAllocatorCreateInfo = {};
	vmaAllocatorCreateInfo.flags = 0;
	vmaAllocatorCreateInfo.physicalDevice = m_physicalDevice;
	vmaAllocatorCreateInfo.device = m_device;
	vmaAllocatorCreateInfo.preferredLargeHeapBlockSize = 0;
	vmaAllocatorCreateInfo.pAllocationCallbacks = nullptr;
	vmaAllocatorCreateInfo.pDeviceMemoryCallbacks = nullptr;
	vmaAllocatorCreateInfo.pHeapSizeLimit = nullptr;
	vmaAllocatorCreateInfo.pVulkanFunctions = nullptr;
	vmaAllocatorCreateInfo.instance = m_instance;
	vmaAllocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_1;
	NTSH_VK_CHECK(vmaCreateAllocator(&vmaAllocatorCreateInfo, &m_allocator));

	createVertexAndIndexBuffers();

	loadCubeModel();

	createDepthImage();

	createGraphicsPipeline();

	// Create texture sampler
	VkSamplerCreateInfo textureSamplerCreateInfo = {};
	textureSamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	textureSamplerCreateInfo.pNext = nullptr;
	textureSamplerCreateInfo.flags = 0;
	textureSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	textureSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	textureSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	textureSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	textureSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	textureSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	textureSamplerCreateInfo.mipLodBias = 0.0f;
	textureSamplerCreateInfo.anisotropyEnable = VK_TRUE;
	textureSamplerCreateInfo.maxAnisotropy = 16.0f;
	textureSamplerCreateInfo.compareEnable = VK_TRUE;
	textureSamplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	textureSamplerCreateInfo.minLod = 0.0f;
	textureSamplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
	textureSamplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	textureSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
	NTSH_VK_CHECK(vkCreateSampler(m_device, &textureSamplerCreateInfo, nullptr, &m_textureSampler));


	// Create camera uniform buffer
	m_cameraBuffers.resize(m_framesInFlight);
	m_cameraBufferAllocations.resize(m_framesInFlight);
	VkBufferCreateInfo cameraBufferCreateInfo = {};
	cameraBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	cameraBufferCreateInfo.pNext = nullptr;
	cameraBufferCreateInfo.flags = 0;
	cameraBufferCreateInfo.size = sizeof(nml::mat4) * 2;
	cameraBufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	cameraBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	cameraBufferCreateInfo.queueFamilyIndexCount = 1;
	cameraBufferCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;

	VmaAllocationCreateInfo bufferAllocationCreateInfo = {};
	bufferAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	bufferAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		NTSH_VK_CHECK(vmaCreateBuffer(m_allocator, &cameraBufferCreateInfo, &bufferAllocationCreateInfo, &m_cameraBuffers[i], &m_cameraBufferAllocations[i], nullptr));
	}

	// Create object storage buffer
	m_objectBuffers.resize(m_framesInFlight);
	m_objectBufferAllocations.resize(m_framesInFlight);
	VkBufferCreateInfo objectBufferCreateInfo = {};
	objectBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	objectBufferCreateInfo.pNext = nullptr;
	objectBufferCreateInfo.flags = 0;
	objectBufferCreateInfo.size = 32768;
	objectBufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	objectBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	objectBufferCreateInfo.queueFamilyIndexCount = 1;
	objectBufferCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;

	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		NTSH_VK_CHECK(vmaCreateBuffer(m_allocator, &objectBufferCreateInfo, &bufferAllocationCreateInfo, &m_objectBuffers[i], &m_objectBufferAllocations[i], nullptr));
	}

	createDescriptorSets();

	// Resize buffers according to number of frames in flight and swapchain size
	m_renderingCommandPools.resize(m_framesInFlight);
	m_renderingCommandBuffers.resize(m_framesInFlight);

	m_fences.resize(m_framesInFlight);
	m_imageAvailableSemaphores.resize(m_framesInFlight);
	m_renderFinishedSemaphores.resize(m_imageCount);

	VkCommandPoolCreateInfo commandPoolCreateInfo = {};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.pNext = nullptr;
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;

	VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
	commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandBufferAllocateInfo.pNext = nullptr;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = 1;

	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		// Create rendering command pools and buffers
		NTSH_VK_CHECK(vkCreateCommandPool(m_device, &commandPoolCreateInfo, nullptr, &m_renderingCommandPools[i]));

		commandBufferAllocateInfo.commandPool = m_renderingCommandPools[i];
		NTSH_VK_CHECK(vkAllocateCommandBuffers(m_device, &commandBufferAllocateInfo, &m_renderingCommandBuffers[i]));

		// Create sync objects
		NTSH_VK_CHECK(vkCreateFence(m_device, &fenceCreateInfo, nullptr, &m_fences[i]));

		NTSH_VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[i]));
	}
	for (uint32_t i = 0; i < m_imageCount; i++) {
		NTSH_VK_CHECK(vkCreateSemaphore(m_device, &semaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[i]));
	}

	// Set current frame-in-flight to 0
	m_currentFrameInFlight = 0;
}

void NutshellGraphicsModule::update(double dt) {
	NTSH_UNUSED(dt);

	if (m_windowModule && !m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		// Do not update if the main window got closed
		return;
	}

	NTSH_VK_CHECK(vkWaitForFences(m_device, 1, &m_fences[m_currentFrameInFlight], VK_TRUE, std::numeric_limits<uint64_t>::max()));

	uint32_t imageIndex = m_imageCount - 1;
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		VkResult acquireNextImageResult = vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<uint64_t>::max(), m_imageAvailableSemaphores[m_currentFrameInFlight], VK_NULL_HANDLE, &imageIndex);
		if (acquireNextImageResult == VK_ERROR_OUT_OF_DATE_KHR) {
			resize();
		}
		else if (acquireNextImageResult != VK_SUCCESS && acquireNextImageResult != VK_SUBOPTIMAL_KHR) {
			NTSH_MODULE_ERROR("Next swapchain image acquire failed.", NTSH_RESULT_MODULE_ERROR);
		}
	}
	else {
		VkSubmitInfo emptySignalSubmitInfo = {};
		emptySignalSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		emptySignalSubmitInfo.pNext = nullptr;
		emptySignalSubmitInfo.waitSemaphoreCount = 0;
		emptySignalSubmitInfo.pWaitSemaphores = nullptr;
		emptySignalSubmitInfo.pWaitDstStageMask = nullptr;
		emptySignalSubmitInfo.commandBufferCount = 0;
		emptySignalSubmitInfo.pCommandBuffers = nullptr;
		emptySignalSubmitInfo.signalSemaphoreCount = 1;
		emptySignalSubmitInfo.pSignalSemaphores = &m_imageAvailableSemaphores[m_currentFrameInFlight];
		NTSH_VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &emptySignalSubmitInfo, VK_NULL_HANDLE));
	}

	// Update camera buffer
	cameraPosition.x = 0.0f;
	cameraPosition.y = 3.0f;
	cameraPosition.z = -5.0f;
	nml::mat4 cameraView = nml::lookAtRH(cameraPosition, nml::vec3(0.0f), nml::vec3(0.0f, 1.0f, 0.0));
	nml::mat4 cameraProjection = nml::perspectiveRH(90.0f * toRad, m_viewport.width / m_viewport.height, 0.05f, 100.0f);
	cameraProjection[1][1] *= -1.0f;
	std::array<nml::mat4, 2> cameraMatrices{ cameraView, cameraProjection };

	void* data;
	vmaMapMemory(m_allocator, m_cameraBufferAllocations[m_currentFrameInFlight], &data);
	memcpy(data, cameraMatrices.data(), sizeof(nml::mat4) * 2);
	vmaUnmapMemory(m_allocator, m_cameraBufferAllocations[m_currentFrameInFlight]);

	// Update objects buffer
	vmaMapMemory(m_allocator, m_objectBufferAllocations[m_currentFrameInFlight], &data);
	for (size_t i = 0; i < m_objects.size(); i++) {
		m_objectAngle = std::fmodf(m_objectAngle + (m_objectRotationSpeed * static_cast<float>(dt)), 360.0f);
		m_objects[i].rotation.y = m_objectAngle * toRad;
		nml::mat4 objectModel = nml::translate(m_objects[i].position) * nml::rotate(m_objects[i].rotation.x, nml::vec3(1.0f, 0.0f, 0.0f)) * nml::rotate(m_objects[i].rotation.y, nml::vec3(0.0f, 1.0f, 0.0f)) * nml::rotate(m_objects[i].rotation.z, nml::vec3(0.0f, 0.0f, 1.0f)) * nml::scale(m_objects[i].scale);

		size_t offset = (i * (sizeof(nml::mat4) + sizeof(nml::vec4)));

		memcpy(reinterpret_cast<char*>(data) + offset, objectModel.data(), sizeof(nml::mat4));
		memcpy(reinterpret_cast<char*>(data) + offset + sizeof(nml::mat4), &m_objects[i].textureID, sizeof(uint32_t));
	}
	vmaUnmapMemory(m_allocator, m_objectBufferAllocations[m_currentFrameInFlight]);

	// Record rendering commands
	NTSH_VK_CHECK(vkResetCommandPool(m_device, m_renderingCommandPools[m_currentFrameInFlight], 0));

	// Begin command buffer recording
	VkCommandBufferBeginInfo commandBufferBeginInfo = {};
	commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBufferBeginInfo.pNext = nullptr;
	commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	NTSH_VK_CHECK(vkBeginCommandBuffer(m_renderingCommandBuffers[m_currentFrameInFlight], &commandBufferBeginInfo));

	// Layout transition VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
	VkImageMemoryBarrier2 undefinedToColorAttachmentOptimalImageMemoryBarrier = {};
	undefinedToColorAttachmentOptimalImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.pNext = nullptr;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.srcAccessMask = 0;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.srcQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.dstQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		undefinedToColorAttachmentOptimalImageMemoryBarrier.image = m_swapchainImages[imageIndex];
	}
	else {
		undefinedToColorAttachmentOptimalImageMemoryBarrier.image = m_drawImage;
	}
	undefinedToColorAttachmentOptimalImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.subresourceRange.levelCount = 1;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
	undefinedToColorAttachmentOptimalImageMemoryBarrier.subresourceRange.layerCount = 1;

	VkDependencyInfo undefinedToColorAttachmentOptimalDependencyInfo = {};
	undefinedToColorAttachmentOptimalDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	undefinedToColorAttachmentOptimalDependencyInfo.pNext = nullptr;
	undefinedToColorAttachmentOptimalDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	undefinedToColorAttachmentOptimalDependencyInfo.memoryBarrierCount = 0;
	undefinedToColorAttachmentOptimalDependencyInfo.pMemoryBarriers = nullptr;
	undefinedToColorAttachmentOptimalDependencyInfo.bufferMemoryBarrierCount = 0;
	undefinedToColorAttachmentOptimalDependencyInfo.pBufferMemoryBarriers = nullptr;
	undefinedToColorAttachmentOptimalDependencyInfo.imageMemoryBarrierCount = 1;
	undefinedToColorAttachmentOptimalDependencyInfo.pImageMemoryBarriers = &undefinedToColorAttachmentOptimalImageMemoryBarrier;
	m_vkCmdPipelineBarrier2KHR(m_renderingCommandBuffers[m_currentFrameInFlight], &undefinedToColorAttachmentOptimalDependencyInfo);

	// Bind descriptor set 0
	vkCmdBindDescriptorSets(m_renderingCommandBuffers[m_currentFrameInFlight], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineLayout, 0, 1, &m_descriptorSets[m_currentFrameInFlight], 0, nullptr);

	// Begin rendering
	VkRenderingAttachmentInfo renderingSwapchainAttachmentInfo = {};
	renderingSwapchainAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	renderingSwapchainAttachmentInfo.pNext = nullptr;
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		renderingSwapchainAttachmentInfo.imageView = m_swapchainImageViews[imageIndex];
	}
	else {
		renderingSwapchainAttachmentInfo.imageView = m_drawImageView;
	}
	renderingSwapchainAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	renderingSwapchainAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
	renderingSwapchainAttachmentInfo.resolveImageView = VK_NULL_HANDLE;
	renderingSwapchainAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	renderingSwapchainAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	renderingSwapchainAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	renderingSwapchainAttachmentInfo.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	renderingSwapchainAttachmentInfo.clearValue.depthStencil = { 0.0f, 0 };

	VkRenderingAttachmentInfo renderingDepthAttachmentInfo = {};
	renderingDepthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	renderingDepthAttachmentInfo.pNext = nullptr;
	renderingDepthAttachmentInfo.imageView = m_depthImageView;
	renderingDepthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	renderingDepthAttachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
	renderingDepthAttachmentInfo.resolveImageView = VK_NULL_HANDLE;
	renderingDepthAttachmentInfo.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	renderingDepthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	renderingDepthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	renderingDepthAttachmentInfo.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };
	renderingDepthAttachmentInfo.clearValue.depthStencil = { 1.0f, 0 };

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.pNext = nullptr;
	renderingInfo.flags = 0;
	renderingInfo.renderArea = m_scissor;
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &renderingSwapchainAttachmentInfo;
	renderingInfo.pDepthAttachment = &renderingDepthAttachmentInfo;
	renderingInfo.pStencilAttachment = nullptr;
	m_vkCmdBeginRenderingKHR(m_renderingCommandBuffers[m_currentFrameInFlight], &renderingInfo);

	// Bind graphics pipeline
	vkCmdBindPipeline(m_renderingCommandBuffers[m_currentFrameInFlight], VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);
	vkCmdSetViewport(m_renderingCommandBuffers[m_currentFrameInFlight], 0, 1, &m_viewport);
	vkCmdSetScissor(m_renderingCommandBuffers[m_currentFrameInFlight], 0, 1, &m_scissor);

	// Bind vertex and index buffers
	VkDeviceSize vertexBufferOffset = 0;
	vkCmdBindVertexBuffers(m_renderingCommandBuffers[m_currentFrameInFlight], 0, 1, &m_vertexBuffer, &vertexBufferOffset);
	vkCmdBindIndexBuffer(m_renderingCommandBuffers[m_currentFrameInFlight], m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	for (size_t i = 0; i < m_objects.size(); i++) {
		// Object index as push constant
		vkCmdPushConstants(m_renderingCommandBuffers[m_currentFrameInFlight], m_graphicsPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(uint32_t), &m_objects[i].index);

		// Draw
		vkCmdDrawIndexed(m_renderingCommandBuffers[m_currentFrameInFlight], m_objects[i].indexCount, 1, m_objects[i].firstIndex, m_objects[i].vertexOffset, 0);
	}

	// End rendering
	m_vkCmdEndRenderingKHR(m_renderingCommandBuffers[m_currentFrameInFlight]);

	// Layout transition VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL -> VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		VkImageMemoryBarrier2 colorAttachmentOptimalToPresentSrcImageMemoryBarrier = {};
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.pNext = nullptr;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.dstAccessMask = 0;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.srcQueueFamilyIndex = m_graphicsQueueFamilyIndex;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.dstQueueFamilyIndex = m_graphicsQueueFamilyIndex;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.image = m_swapchainImages[imageIndex];
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.subresourceRange.levelCount = 1;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
		colorAttachmentOptimalToPresentSrcImageMemoryBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo colorAttachmentOptimalToPresentSrcDependencyInfo = {};
		colorAttachmentOptimalToPresentSrcDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		colorAttachmentOptimalToPresentSrcDependencyInfo.pNext = nullptr;
		colorAttachmentOptimalToPresentSrcDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		colorAttachmentOptimalToPresentSrcDependencyInfo.memoryBarrierCount = 0;
		colorAttachmentOptimalToPresentSrcDependencyInfo.pMemoryBarriers = nullptr;
		colorAttachmentOptimalToPresentSrcDependencyInfo.bufferMemoryBarrierCount = 0;
		colorAttachmentOptimalToPresentSrcDependencyInfo.pBufferMemoryBarriers = nullptr;
		colorAttachmentOptimalToPresentSrcDependencyInfo.imageMemoryBarrierCount = 1;
		colorAttachmentOptimalToPresentSrcDependencyInfo.pImageMemoryBarriers = &colorAttachmentOptimalToPresentSrcImageMemoryBarrier;
		m_vkCmdPipelineBarrier2KHR(m_renderingCommandBuffers[m_currentFrameInFlight], &colorAttachmentOptimalToPresentSrcDependencyInfo);
	}

	// End command buffer recording
	NTSH_VK_CHECK(vkEndCommandBuffer(m_renderingCommandBuffers[m_currentFrameInFlight]));

	NTSH_VK_CHECK(vkResetFences(m_device, 1, &m_fences[m_currentFrameInFlight]));

	VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_imageAvailableSemaphores[m_currentFrameInFlight];
	submitInfo.pWaitDstStageMask = &waitDstStageMask;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_renderingCommandBuffers[m_currentFrameInFlight];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[imageIndex];
	NTSH_VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_fences[m_currentFrameInFlight]));

	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = nullptr;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[imageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_swapchain;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;
		VkResult queuePresentResult = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
		if (queuePresentResult == VK_ERROR_OUT_OF_DATE_KHR || queuePresentResult == VK_SUBOPTIMAL_KHR) {
			resize();
		}
		else if (queuePresentResult != VK_SUCCESS) {
			NTSH_MODULE_ERROR("Queue present swapchain image failed.", NTSH_RESULT_MODULE_ERROR);
		}
	}
	else {
		VkPipelineStageFlags emptyWaitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkSubmitInfo emptyWaitSubmitInfo = {};
		emptyWaitSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		emptyWaitSubmitInfo.pNext = nullptr;
		emptyWaitSubmitInfo.waitSemaphoreCount = 1;
		emptyWaitSubmitInfo.pWaitSemaphores = &m_renderFinishedSemaphores[imageIndex];
		emptyWaitSubmitInfo.pWaitDstStageMask = &emptyWaitDstStageMask;
		emptyWaitSubmitInfo.commandBufferCount = 0;
		emptyWaitSubmitInfo.pCommandBuffers = nullptr;
		emptyWaitSubmitInfo.signalSemaphoreCount = 0;
		emptyWaitSubmitInfo.pSignalSemaphores = nullptr;
		NTSH_VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &emptyWaitSubmitInfo, VK_NULL_HANDLE));
	}

	m_currentFrameInFlight = (m_currentFrameInFlight + 1) % m_framesInFlight;
}

void NutshellGraphicsModule::destroy() {
	NTSH_VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));

	// Destroy sync objects
	for (uint32_t i = 0; i < m_imageCount; i++) {
		vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
	}
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);

		vkDestroyFence(m_device, m_fences[i], nullptr);

		// Destroy rendering command pools
		vkDestroyCommandPool(m_device, m_renderingCommandPools[i], nullptr);
	}

	// Destroy objects buffers
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		vmaDestroyBuffer(m_allocator, m_objectBuffers[i], m_objectBufferAllocations[i]);
	}

	// Destroy camera buffers
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		vmaDestroyBuffer(m_allocator, m_cameraBuffers[i], m_cameraBufferAllocations[i]);
	}

	// Destroy descriptor pool
	vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

	// Destroy graphics pipeline
	vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);

	// Destroy graphics pipeline layout
	vkDestroyPipelineLayout(m_device, m_graphicsPipelineLayout, nullptr);

	// Destroy descriptor set layout
	vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

	// Destroy depth image and image view
	vkDestroyImageView(m_device, m_depthImageView, nullptr);
	vmaDestroyImage(m_allocator, m_depthImage, m_depthImageAllocation);

	// Destroy texture
	vkDestroyImageView(m_device, m_cubeTextureImageView, nullptr);
	vmaDestroyImage(m_allocator, m_cubeTextureImage, m_cubeTextureImageAllocation);

	// Destroy vertex and index buffers
	vmaDestroyBuffer(m_allocator, m_indexBuffer, m_indexBufferAllocation);
	vmaDestroyBuffer(m_allocator, m_vertexBuffer, m_vertexBufferAllocation);

	// Destroy texture sampler
	vkDestroySampler(m_device, m_textureSampler, nullptr);

	// Destroy VMA Allocator
	vmaDestroyAllocator(m_allocator);

	// Destroy swapchain
	if (m_swapchain != VK_NULL_HANDLE) {
		for (VkImageView& swapchainImageView : m_swapchainImageViews) {
			vkDestroyImageView(m_device, swapchainImageView, nullptr);
		}
		vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	}
	// Or destroy the image
	else {
		vkDestroyImageView(m_device, m_drawImageView, nullptr);
		vmaDestroyImage(m_allocator, m_drawImage, m_drawImageAllocation);
	}

	// Destroy device
	vkDestroyDevice(m_device, nullptr);

	// Destroy surface
	if (m_surface != VK_NULL_HANDLE) {
		vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	}

#if defined(NTSH_OS_LINUX)
	// Close X display
	XCloseDisplay(m_display);
#endif

#if defined(NTSH_DEBUG)
	// Destroy debug messenger
	auto destroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
	destroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
#endif

	// Destroy instance
	vkDestroyInstance(m_instance, nullptr);
}

VkSurfaceCapabilitiesKHR NutshellGraphicsModule::getSurfaceCapabilities() {
	VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
	surfaceInfo.pNext = nullptr;
	surfaceInfo.surface = m_surface;

	VkSurfaceCapabilities2KHR surfaceCapabilities;
	surfaceCapabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;
	surfaceCapabilities.pNext = nullptr;
	NTSH_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilities2KHR(m_physicalDevice, &surfaceInfo, &surfaceCapabilities));

	return surfaceCapabilities.surfaceCapabilities;
}

std::vector<VkSurfaceFormatKHR> NutshellGraphicsModule::getSurfaceFormats() {
	VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
	surfaceInfo.pNext = nullptr;
	surfaceInfo.surface = m_surface;

	uint32_t surfaceFormatsCount;
	NTSH_VK_CHECK(vkGetPhysicalDeviceSurfaceFormats2KHR(m_physicalDevice, &surfaceInfo, &surfaceFormatsCount, nullptr));
	std::vector<VkSurfaceFormat2KHR> surfaceFormats2(surfaceFormatsCount);
	for (VkSurfaceFormat2KHR& surfaceFormat2 : surfaceFormats2) {
		surfaceFormat2.sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR;
		surfaceFormat2.pNext = nullptr;
	}
	NTSH_VK_CHECK(vkGetPhysicalDeviceSurfaceFormats2KHR(m_physicalDevice, &surfaceInfo, &surfaceFormatsCount, surfaceFormats2.data()));
	
	std::vector<VkSurfaceFormatKHR> surfaceFormats;
	for (const VkSurfaceFormat2KHR surfaceFormat2 : surfaceFormats2) {
		surfaceFormats.push_back(surfaceFormat2.surfaceFormat);
	}

	return surfaceFormats;
}

std::vector<VkPresentModeKHR> NutshellGraphicsModule::getSurfacePresentModes() {
	uint32_t presentModesCount;
	NTSH_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModesCount, nullptr));
	std::vector<VkPresentModeKHR> presentModes(presentModesCount);
	NTSH_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModesCount, presentModes.data()));

	return presentModes;
}

VkPhysicalDeviceMemoryProperties NutshellGraphicsModule::getMemoryProperties() {
	VkPhysicalDeviceMemoryProperties2 memoryProperties = {};
	memoryProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
	memoryProperties.pNext = nullptr;
	vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &memoryProperties);

	return memoryProperties.memoryProperties;
}

uint32_t NutshellGraphicsModule::findMipLevels(uint32_t width, uint32_t height) {
	return static_cast<uint32_t>(std::floor(std::log2(std::min(width, height)) + 1));
}

void NutshellGraphicsModule::createSwapchain(VkSwapchainKHR oldSwapchain) {
	VkSurfaceCapabilitiesKHR surfaceCapabilities = getSurfaceCapabilities();
	uint32_t minImageCount = surfaceCapabilities.minImageCount + 1;
	if (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) {
		minImageCount = surfaceCapabilities.maxImageCount;
	}

	std::vector<VkSurfaceFormatKHR> surfaceFormats = getSurfaceFormats();
	m_swapchainFormat = surfaceFormats[0].format;
	VkColorSpaceKHR swapchainColorSpace = surfaceFormats[0].colorSpace;
	for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats) {
		if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB && surfaceFormat.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
			m_swapchainFormat = surfaceFormat.format;
			swapchainColorSpace = surfaceFormat.colorSpace;
			break;
		}
	}

	std::vector<VkPresentModeKHR> presentModes = getSurfacePresentModes();
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	for (const VkPresentModeKHR& presentMode : presentModes) {
		if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			swapchainPresentMode = presentMode;
			break;
		}
		else if (presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
			swapchainPresentMode = presentMode;
		}
	}

	VkExtent2D swapchainExtent = {};
	swapchainExtent.width = static_cast<uint32_t>(m_windowModule->getWidth(NTSH_MAIN_WINDOW));
	swapchainExtent.height = static_cast<uint32_t>(m_windowModule->getHeight(NTSH_MAIN_WINDOW));

	m_viewport.x = 0.0f;
	m_viewport.y = 0.0f;
	m_viewport.width = static_cast<float>(swapchainExtent.width);
	m_viewport.height = static_cast<float>(swapchainExtent.height);
	m_viewport.minDepth = 0.0f;
	m_viewport.maxDepth = 1.0f;

	m_scissor.offset.x = 0;
	m_scissor.offset.y = 0;
	m_scissor.extent.width = swapchainExtent.width;
	m_scissor.extent.height = swapchainExtent.height;

	VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.pNext = nullptr;
	swapchainCreateInfo.flags = 0;
	swapchainCreateInfo.surface = m_surface;
	swapchainCreateInfo.minImageCount = minImageCount;
	swapchainCreateInfo.imageFormat = m_swapchainFormat;
	swapchainCreateInfo.imageColorSpace = swapchainColorSpace;
	swapchainCreateInfo.imageExtent = swapchainExtent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.queueFamilyIndexCount = 0;
	swapchainCreateInfo.pQueueFamilyIndices = nullptr;
	swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.presentMode = swapchainPresentMode;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = oldSwapchain;
	NTSH_VK_CHECK(vkCreateSwapchainKHR(m_device, &swapchainCreateInfo, nullptr, &m_swapchain));

	NTSH_VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_imageCount, nullptr));
	m_swapchainImages.resize(m_imageCount);
	NTSH_VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &m_imageCount, m_swapchainImages.data()));

	// Create the swapchain image views
	m_swapchainImageViews.resize(m_imageCount);
	for (uint32_t i = 0; i < m_imageCount; i++) {
		VkImageViewCreateInfo swapchainImageViewCreateInfo = {};
		swapchainImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		swapchainImageViewCreateInfo.pNext = nullptr;
		swapchainImageViewCreateInfo.flags = 0;
		swapchainImageViewCreateInfo.image = m_swapchainImages[i];
		swapchainImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		swapchainImageViewCreateInfo.format = m_swapchainFormat;
		swapchainImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
		swapchainImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
		swapchainImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
		swapchainImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
		swapchainImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		swapchainImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		swapchainImageViewCreateInfo.subresourceRange.levelCount = 1;
		swapchainImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		swapchainImageViewCreateInfo.subresourceRange.layerCount = 1;
		NTSH_VK_CHECK(vkCreateImageView(m_device, &swapchainImageViewCreateInfo, nullptr, &m_swapchainImageViews[i]));
	}
}

void NutshellGraphicsModule::createVertexAndIndexBuffers() {
	// Vertex and index buffers
	VkBufferCreateInfo vertexAndIndexBufferCreateInfo = {};
	vertexAndIndexBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexAndIndexBufferCreateInfo.pNext = nullptr;
	vertexAndIndexBufferCreateInfo.flags = 0;
	vertexAndIndexBufferCreateInfo.size = 65536;
	vertexAndIndexBufferCreateInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vertexAndIndexBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vertexAndIndexBufferCreateInfo.queueFamilyIndexCount = 1;
	vertexAndIndexBufferCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;

	VmaAllocationCreateInfo vertexAndIndexBufferAllocationCreateInfo = {};
	vertexAndIndexBufferAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	vmaCreateBuffer(m_allocator, &vertexAndIndexBufferCreateInfo, &vertexAndIndexBufferAllocationCreateInfo, &m_vertexBuffer, &m_vertexBufferAllocation, nullptr);

	vertexAndIndexBufferCreateInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vmaCreateBuffer(m_allocator, &vertexAndIndexBufferCreateInfo, &vertexAndIndexBufferAllocationCreateInfo, &m_indexBuffer, &m_indexBufferAllocation, nullptr);
}

void NutshellGraphicsModule::loadCubeModel() {
	// Cube
	NtshMesh cubeMesh;
	cubeMesh.vertices = {
		{ {1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.625f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.875f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.875f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {0.625f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.375f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.625f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.625f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.375f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, -1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.375f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, 1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}, {0.625f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, 1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.625f, 0.25f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.375f, 0.25f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.125f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.375f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {0.375f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, -1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}, {0.125f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.375f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, 1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.625f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.625f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.375f, 0.75f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.375f, 0.25f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.625f, 0.25f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.625f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} },
		{ {1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.375f, 0.5f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f} }
	};
	cubeMesh.indices = {
		0,
		1,
		2,
		0,
		2,
		3,
		4,
		5,
		6,
		4,
		6,
		7,
		8,
		9,
		10,
		8,
		10,
		11,
		12,
		13,
		14,
		12,
		14,
		15,
		16,
		17,
		18,
		16,
		18,
		19,
		20,
		21,
		22,
		20,
		22,
		23
	};

	m_cube.primitives.push_back({ cubeMesh, NtshMaterial() });

	std::array<unsigned char, 16 * 16 * 4> textureData;
	for (size_t i = 0; i < 256; i++) {
		size_t component = 0;
		textureData[i * 4 + component++] = static_cast<unsigned char>(i % 256);
		textureData[i * 4 + component++] = static_cast<unsigned char>(i % 64);
		textureData[i * 4 + component++] = static_cast<unsigned char>(i % 128);
		textureData[i * 4 + component] = static_cast<unsigned char>(255);
	}

	m_objects.resize(3);
	m_objects[0].index = 0;
	m_objects[0].indexCount = 36;
	m_objects[0].firstIndex = 0;
	m_objects[0].vertexOffset = 0;

	m_objects[0].position = nml::vec3(0.0f, 0.0f, 0.0f);
	m_objects[0].rotation = nml::vec3(0.0f, 0.0f, 0.0f);
	m_objects[0].scale = nml::vec3(1.0f, 1.0f, 1.0f);

	m_objects[0].textureID = 0;

	m_objects[1].index = 1;
	m_objects[1].indexCount = 36;
	m_objects[1].firstIndex = 0;
	m_objects[1].vertexOffset = 0;

	m_objects[1].position = nml::vec3(0.0f, 2.0f, 0.0f);
	m_objects[1].rotation = nml::vec3(0.0f, 0.0f, 0.0f);
	m_objects[1].scale = nml::vec3(0.5f, 1.0f, 2.5f);

	m_objects[1].textureID = 0;

	m_objects[2].index = 2;
	m_objects[2].indexCount = 36;
	m_objects[2].firstIndex = 0;
	m_objects[2].vertexOffset = 0;

	m_objects[2].position = nml::vec3(0.0f, -2.0f, 0.0f);
	m_objects[2].rotation = nml::vec3(0.0f, 0.0f, 0.0f);
	m_objects[2].scale = nml::vec3(0.5f, 1.0f, 2.5f);

	m_objects[2].textureID = 0;

	// Vertex and Index staging buffer
	VkBuffer vertexStagingBuffer;
	VkBuffer indexStagingBuffer;
	VmaAllocation vertexStagingBufferAllocation;
	VmaAllocation indexStagingBufferAllocation;

	VkBufferCreateInfo vertexAndIndexStagingBufferCreateInfo = {};
	vertexAndIndexStagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexAndIndexStagingBufferCreateInfo.pNext = nullptr;
	vertexAndIndexStagingBufferCreateInfo.flags = 0;
	vertexAndIndexStagingBufferCreateInfo.size = 65536;
	vertexAndIndexStagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	vertexAndIndexStagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vertexAndIndexStagingBufferCreateInfo.queueFamilyIndexCount = 1;
	vertexAndIndexStagingBufferCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;

	VmaAllocationCreateInfo vertexAndIndexStagingBufferAllocationCreateInfo = {};
	vertexAndIndexStagingBufferAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	vertexAndIndexStagingBufferAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	NTSH_VK_CHECK(vmaCreateBuffer(m_allocator, &vertexAndIndexStagingBufferCreateInfo, &vertexAndIndexStagingBufferAllocationCreateInfo, &vertexStagingBuffer, &vertexStagingBufferAllocation, nullptr));
	NTSH_VK_CHECK(vmaCreateBuffer(m_allocator, &vertexAndIndexStagingBufferCreateInfo, &vertexAndIndexStagingBufferAllocationCreateInfo, &indexStagingBuffer, &indexStagingBufferAllocation, nullptr));

	void* data;

	NTSH_VK_CHECK(vmaMapMemory(m_allocator, vertexStagingBufferAllocation, &data));
	memcpy(data, cubeMesh.vertices.data(), cubeMesh.vertices.size() * sizeof(NtshVertex));
	vmaUnmapMemory(m_allocator, vertexStagingBufferAllocation);

	NTSH_VK_CHECK(vmaMapMemory(m_allocator, indexStagingBufferAllocation, &data));
	memcpy(data, cubeMesh.indices.data(), cubeMesh.indices.size() * sizeof(uint32_t));
	vmaUnmapMemory(m_allocator, indexStagingBufferAllocation);

	// Create texture
	VkImageCreateInfo cubeTextureImageCreateInfo = {};
	cubeTextureImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	cubeTextureImageCreateInfo.pNext = nullptr;
	cubeTextureImageCreateInfo.flags = 0;
	cubeTextureImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	cubeTextureImageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	cubeTextureImageCreateInfo.extent.width = 16;
	cubeTextureImageCreateInfo.extent.height = 16;
	cubeTextureImageCreateInfo.extent.depth = 1;
	cubeTextureImageCreateInfo.mipLevels = findMipLevels(cubeTextureImageCreateInfo.extent.width, cubeTextureImageCreateInfo.extent.height);
	cubeTextureImageCreateInfo.arrayLayers = 1;
	cubeTextureImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	cubeTextureImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	cubeTextureImageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	cubeTextureImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	cubeTextureImageCreateInfo.queueFamilyIndexCount = 1;
	cubeTextureImageCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;
	cubeTextureImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo cubeTextureImageAllocationCreateInfo = {};
	cubeTextureImageAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	NTSH_VK_CHECK(vmaCreateImage(m_allocator, &cubeTextureImageCreateInfo, &cubeTextureImageAllocationCreateInfo, &m_cubeTextureImage, &m_cubeTextureImageAllocation, nullptr));

	VkImageViewCreateInfo cubeTextureImageViewCreateInfo = {};
	cubeTextureImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	cubeTextureImageViewCreateInfo.pNext = nullptr;
	cubeTextureImageViewCreateInfo.flags = 0;
	cubeTextureImageViewCreateInfo.image = m_cubeTextureImage;
	cubeTextureImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	cubeTextureImageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	cubeTextureImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
	cubeTextureImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
	cubeTextureImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
	cubeTextureImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
	cubeTextureImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	cubeTextureImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	cubeTextureImageViewCreateInfo.subresourceRange.levelCount = cubeTextureImageCreateInfo.mipLevels;
	cubeTextureImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	cubeTextureImageViewCreateInfo.subresourceRange.layerCount = 1;
	NTSH_VK_CHECK(vkCreateImageView(m_device, &cubeTextureImageViewCreateInfo, nullptr, &m_cubeTextureImageView));

	// Create texture staging buffer
	VkBuffer cubeTextureStagingBuffer;
	VmaAllocation cubeTextureStagingBufferAllocation;

	VkBufferCreateInfo cubeTextureStagingBufferCreateInfo = {};
	cubeTextureStagingBufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	cubeTextureStagingBufferCreateInfo.pNext = nullptr;
	cubeTextureStagingBufferCreateInfo.flags = 0;
	cubeTextureStagingBufferCreateInfo.size = 16 * 16 * 4;
	cubeTextureStagingBufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	cubeTextureStagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	cubeTextureStagingBufferCreateInfo.queueFamilyIndexCount = 1;
	cubeTextureStagingBufferCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;

	VmaAllocationCreateInfo cubeTextureStagingBufferAllocationCreateInfo = {};
	cubeTextureStagingBufferAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	cubeTextureStagingBufferAllocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	NTSH_VK_CHECK(vmaCreateBuffer(m_allocator, &cubeTextureStagingBufferCreateInfo, &cubeTextureStagingBufferAllocationCreateInfo, &cubeTextureStagingBuffer, &cubeTextureStagingBufferAllocation, nullptr));

	NTSH_VK_CHECK(vmaMapMemory(m_allocator, cubeTextureStagingBufferAllocation, &data));
	memcpy(data, textureData.data(), 16 * 16 * 4);
	vmaUnmapMemory(m_allocator, cubeTextureStagingBufferAllocation);

	// Copy staging buffers
	VkCommandPool buffersCopyCommandPool;

	VkCommandPoolCreateInfo buffersCopyCommandPoolCreateInfo = {};
	buffersCopyCommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	buffersCopyCommandPoolCreateInfo.pNext = nullptr;
	buffersCopyCommandPoolCreateInfo.flags = 0;
	buffersCopyCommandPoolCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
	NTSH_VK_CHECK(vkCreateCommandPool(m_device, &buffersCopyCommandPoolCreateInfo, nullptr, &buffersCopyCommandPool));

	VkCommandBuffer buffersCopyCommandBuffer;

	VkCommandBufferAllocateInfo buffersCopyCommandBufferAllocateInfo = {};
	buffersCopyCommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	buffersCopyCommandBufferAllocateInfo.pNext = nullptr;
	buffersCopyCommandBufferAllocateInfo.commandPool = buffersCopyCommandPool;
	buffersCopyCommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	buffersCopyCommandBufferAllocateInfo.commandBufferCount = 1;
	NTSH_VK_CHECK(vkAllocateCommandBuffers(m_device, &buffersCopyCommandBufferAllocateInfo, &buffersCopyCommandBuffer));

	VkCommandBufferBeginInfo vertexAndIndexBuffersCopyBeginInfo = {};
	vertexAndIndexBuffersCopyBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vertexAndIndexBuffersCopyBeginInfo.pNext = nullptr;
	vertexAndIndexBuffersCopyBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vertexAndIndexBuffersCopyBeginInfo.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(buffersCopyCommandBuffer, &vertexAndIndexBuffersCopyBeginInfo);

	VkBufferCopy vertexBufferCopy = {};
	vertexBufferCopy.srcOffset = 0;
	vertexBufferCopy.dstOffset = 0;
	vertexBufferCopy.size = cubeMesh.vertices.size() * sizeof(NtshVertex);
	vkCmdCopyBuffer(buffersCopyCommandBuffer, vertexStagingBuffer, m_vertexBuffer, 1, &vertexBufferCopy);

	VkBufferCopy indexBufferCopy = {};
	indexBufferCopy.srcOffset = 0;
	indexBufferCopy.dstOffset = 0;
	indexBufferCopy.size = cubeMesh.indices.size() * sizeof(uint32_t);
	vkCmdCopyBuffer(buffersCopyCommandBuffer, indexStagingBuffer, m_indexBuffer, 1, &indexBufferCopy);

	VkImageMemoryBarrier2 undefinedToTransferDstOptimalImageMemoryBarrier = {};
	undefinedToTransferDstOptimalImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	undefinedToTransferDstOptimalImageMemoryBarrier.pNext = nullptr;
	undefinedToTransferDstOptimalImageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	undefinedToTransferDstOptimalImageMemoryBarrier.srcAccessMask = 0;
	undefinedToTransferDstOptimalImageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	undefinedToTransferDstOptimalImageMemoryBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	undefinedToTransferDstOptimalImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	undefinedToTransferDstOptimalImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	undefinedToTransferDstOptimalImageMemoryBarrier.srcQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	undefinedToTransferDstOptimalImageMemoryBarrier.dstQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	undefinedToTransferDstOptimalImageMemoryBarrier.image = m_cubeTextureImage;
	undefinedToTransferDstOptimalImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	undefinedToTransferDstOptimalImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
	undefinedToTransferDstOptimalImageMemoryBarrier.subresourceRange.levelCount = cubeTextureImageCreateInfo.mipLevels;
	undefinedToTransferDstOptimalImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
	undefinedToTransferDstOptimalImageMemoryBarrier.subresourceRange.layerCount = 1;

	VkDependencyInfo undefinedToTransferDstOptimalDependencyInfo = {};
	undefinedToTransferDstOptimalDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	undefinedToTransferDstOptimalDependencyInfo.pNext = nullptr;
	undefinedToTransferDstOptimalDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	undefinedToTransferDstOptimalDependencyInfo.memoryBarrierCount = 0;
	undefinedToTransferDstOptimalDependencyInfo.pMemoryBarriers = nullptr;
	undefinedToTransferDstOptimalDependencyInfo.bufferMemoryBarrierCount = 0;
	undefinedToTransferDstOptimalDependencyInfo.pBufferMemoryBarriers = nullptr;
	undefinedToTransferDstOptimalDependencyInfo.imageMemoryBarrierCount = 1;
	undefinedToTransferDstOptimalDependencyInfo.pImageMemoryBarriers = &undefinedToTransferDstOptimalImageMemoryBarrier;
	m_vkCmdPipelineBarrier2KHR(buffersCopyCommandBuffer, &undefinedToTransferDstOptimalDependencyInfo);

	VkBufferImageCopy cubeTextureBufferCopy = {};
	cubeTextureBufferCopy.bufferOffset = 0;
	cubeTextureBufferCopy.bufferRowLength = 0;
	cubeTextureBufferCopy.bufferImageHeight = 0;
	cubeTextureBufferCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	cubeTextureBufferCopy.imageSubresource.mipLevel = 0;
	cubeTextureBufferCopy.imageSubresource.baseArrayLayer = 0;
	cubeTextureBufferCopy.imageSubresource.layerCount = 1;
	cubeTextureBufferCopy.imageOffset.x = 0;
	cubeTextureBufferCopy.imageOffset.y = 0;
	cubeTextureBufferCopy.imageOffset.z = 0;
	cubeTextureBufferCopy.imageExtent.width = 16;
	cubeTextureBufferCopy.imageExtent.height = 16;
	cubeTextureBufferCopy.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(buffersCopyCommandBuffer, cubeTextureStagingBuffer, m_cubeTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cubeTextureBufferCopy);

	VkImageMemoryBarrier2 mipMapGenerationImageMemoryBarrier = {};
	mipMapGenerationImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	mipMapGenerationImageMemoryBarrier.pNext = nullptr;
	mipMapGenerationImageMemoryBarrier.srcQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	mipMapGenerationImageMemoryBarrier.dstQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	mipMapGenerationImageMemoryBarrier.image = m_cubeTextureImage;
	mipMapGenerationImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	mipMapGenerationImageMemoryBarrier.subresourceRange.levelCount = 1;
	mipMapGenerationImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
	mipMapGenerationImageMemoryBarrier.subresourceRange.layerCount = 1;

	uint32_t mipWidth = cubeTextureImageCreateInfo.extent.width;
	uint32_t mipHeight = cubeTextureImageCreateInfo.extent.height;
	for (size_t i = 1; i < cubeTextureImageCreateInfo.mipLevels; i++) {
		mipMapGenerationImageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		mipMapGenerationImageMemoryBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		mipMapGenerationImageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		mipMapGenerationImageMemoryBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		mipMapGenerationImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		mipMapGenerationImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		mipMapGenerationImageMemoryBarrier.subresourceRange.baseMipLevel = static_cast<uint32_t>(i) - 1;

		VkDependencyInfo mipMapGenerationDependencyInfo = {};
		mipMapGenerationDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		mipMapGenerationDependencyInfo.pNext = nullptr;
		mipMapGenerationDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
		mipMapGenerationDependencyInfo.memoryBarrierCount = 0;
		mipMapGenerationDependencyInfo.pMemoryBarriers = nullptr;
		mipMapGenerationDependencyInfo.bufferMemoryBarrierCount = 0;
		mipMapGenerationDependencyInfo.pBufferMemoryBarriers = nullptr;
		mipMapGenerationDependencyInfo.imageMemoryBarrierCount = 1;
		mipMapGenerationDependencyInfo.pImageMemoryBarriers = &mipMapGenerationImageMemoryBarrier;
		m_vkCmdPipelineBarrier2KHR(buffersCopyCommandBuffer, &mipMapGenerationDependencyInfo);

		VkImageBlit imageBlit = {};
		imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlit.srcSubresource.mipLevel = static_cast<uint32_t>(i) - 1;
		imageBlit.srcSubresource.baseArrayLayer = 0;
		imageBlit.srcSubresource.layerCount = 1;
		imageBlit.srcOffsets[0].x = 0;
		imageBlit.srcOffsets[0].y = 0;
		imageBlit.srcOffsets[0].z = 0;
		imageBlit.srcOffsets[1].x = mipWidth;
		imageBlit.srcOffsets[1].y = mipHeight;
		imageBlit.srcOffsets[1].z = 1;
		imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlit.dstSubresource.mipLevel = static_cast<uint32_t>(i);
		imageBlit.dstSubresource.baseArrayLayer = 0;
		imageBlit.dstSubresource.layerCount = 1;
		imageBlit.dstOffsets[0].x = 0;
		imageBlit.dstOffsets[0].y = 0;
		imageBlit.dstOffsets[0].z = 0;
		imageBlit.dstOffsets[1].x = mipWidth > 1 ? mipWidth / 2 : 1;
		imageBlit.dstOffsets[1].y = mipHeight > 1 ? mipHeight / 2 : 1;
		imageBlit.dstOffsets[1].z = 1;
		vkCmdBlitImage(buffersCopyCommandBuffer, m_cubeTextureImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_cubeTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

		mipMapGenerationImageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		mipMapGenerationImageMemoryBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		mipMapGenerationImageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		mipMapGenerationImageMemoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
		mipMapGenerationImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		mipMapGenerationImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		mipMapGenerationDependencyInfo.pImageMemoryBarriers = &mipMapGenerationImageMemoryBarrier;
		m_vkCmdPipelineBarrier2KHR(buffersCopyCommandBuffer, &mipMapGenerationDependencyInfo);

		mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
	}

	vkEndCommandBuffer(buffersCopyCommandBuffer);

	VkFence buffersCopyFence;

	VkFenceCreateInfo buffersCopyFenceCreateInfo = {};
	buffersCopyFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	buffersCopyFenceCreateInfo.pNext = nullptr;
	buffersCopyFenceCreateInfo.flags = 0;
	NTSH_VK_CHECK(vkCreateFence(m_device, &buffersCopyFenceCreateInfo, nullptr, &buffersCopyFence));

	VkSubmitInfo buffersCopySubmitInfo = {};
	buffersCopySubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	buffersCopySubmitInfo.pNext = nullptr;
	buffersCopySubmitInfo.waitSemaphoreCount = 0;
	buffersCopySubmitInfo.pWaitSemaphores = nullptr;
	buffersCopySubmitInfo.pWaitDstStageMask = nullptr;
	buffersCopySubmitInfo.commandBufferCount = 1;
	buffersCopySubmitInfo.pCommandBuffers = &buffersCopyCommandBuffer;
	buffersCopySubmitInfo.signalSemaphoreCount = 0;
	buffersCopySubmitInfo.pSignalSemaphores = nullptr;
	NTSH_VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &buffersCopySubmitInfo, buffersCopyFence));
	NTSH_VK_CHECK(vkWaitForFences(m_device, 1, &buffersCopyFence, VK_TRUE, std::numeric_limits<uint64_t>::max()));

	vkDestroyFence(m_device, buffersCopyFence, nullptr);
	vkDestroyCommandPool(m_device, buffersCopyCommandPool, nullptr);
	vmaDestroyBuffer(m_allocator, vertexStagingBuffer, vertexStagingBufferAllocation);
	vmaDestroyBuffer(m_allocator, indexStagingBuffer, indexStagingBufferAllocation);
	vmaDestroyBuffer(m_allocator, cubeTextureStagingBuffer, cubeTextureStagingBufferAllocation);
}

void NutshellGraphicsModule::createDepthImage() {
	VkImageCreateInfo depthImageCreateInfo = {};
	depthImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	depthImageCreateInfo.pNext = nullptr;
	depthImageCreateInfo.flags = 0;
	depthImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	depthImageCreateInfo.format = VK_FORMAT_D32_SFLOAT;
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		depthImageCreateInfo.extent.width = static_cast<uint32_t>(m_windowModule->getWidth(NTSH_MAIN_WINDOW));
		depthImageCreateInfo.extent.height = static_cast<uint32_t>(m_windowModule->getHeight(NTSH_MAIN_WINDOW));
	}
	else {
		depthImageCreateInfo.extent.width = 1280;
		depthImageCreateInfo.extent.height = 720;
	}
	depthImageCreateInfo.extent.depth = 1;
	depthImageCreateInfo.mipLevels = 1;
	depthImageCreateInfo.arrayLayers = 1;
	depthImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	depthImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	depthImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	depthImageCreateInfo.queueFamilyIndexCount = 1;
	depthImageCreateInfo.pQueueFamilyIndices = &m_graphicsQueueFamilyIndex;
	depthImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo depthImageAllocationCreateInfo = {};
	depthImageAllocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	NTSH_VK_CHECK(vmaCreateImage(m_allocator, &depthImageCreateInfo, &depthImageAllocationCreateInfo, &m_depthImage, &m_depthImageAllocation, nullptr));

	VkImageViewCreateInfo depthImageViewCreateInfo = {};
	depthImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthImageViewCreateInfo.pNext = nullptr;
	depthImageViewCreateInfo.flags = 0;
	depthImageViewCreateInfo.image = m_depthImage;
	depthImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthImageViewCreateInfo.format = VK_FORMAT_D32_SFLOAT;
	depthImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
	depthImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
	depthImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
	depthImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
	depthImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
	depthImageViewCreateInfo.subresourceRange.levelCount = 1;
	depthImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
	depthImageViewCreateInfo.subresourceRange.layerCount = 1;
	NTSH_VK_CHECK(vkCreateImageView(m_device, &depthImageViewCreateInfo, nullptr, &m_depthImageView));

	// Layout transition VK_IMAGE_LAYOUT_UNDEFINED -> VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	VkCommandPool depthImageTransitionCommandPool;

	VkCommandPoolCreateInfo depthImageTransitionCommandPoolCreateInfo = {};
	depthImageTransitionCommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	depthImageTransitionCommandPoolCreateInfo.pNext = nullptr;
	depthImageTransitionCommandPoolCreateInfo.flags = 0;
	depthImageTransitionCommandPoolCreateInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
	NTSH_VK_CHECK(vkCreateCommandPool(m_device, &depthImageTransitionCommandPoolCreateInfo, nullptr, &depthImageTransitionCommandPool));

	VkCommandBuffer depthImageTransitionCommandBuffer;

	VkCommandBufferAllocateInfo depthImageTransitionCommandBufferAllocateInfo = {};
	depthImageTransitionCommandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	depthImageTransitionCommandBufferAllocateInfo.pNext = nullptr;
	depthImageTransitionCommandBufferAllocateInfo.commandPool = depthImageTransitionCommandPool;
	depthImageTransitionCommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	depthImageTransitionCommandBufferAllocateInfo.commandBufferCount = 1;
	NTSH_VK_CHECK(vkAllocateCommandBuffers(m_device, &depthImageTransitionCommandBufferAllocateInfo, &depthImageTransitionCommandBuffer));

	VkCommandBufferBeginInfo depthImageTransitionBeginInfo = {};
	depthImageTransitionBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	depthImageTransitionBeginInfo.pNext = nullptr;
	depthImageTransitionBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	depthImageTransitionBeginInfo.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(depthImageTransitionCommandBuffer, &depthImageTransitionBeginInfo);

	VkImageMemoryBarrier2 undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier = {};
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.pNext = nullptr;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.srcAccessMask = 0;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.srcQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.dstQueueFamilyIndex = m_graphicsQueueFamilyIndex;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.image = m_depthImage;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.subresourceRange.levelCount = 1;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
	undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier.subresourceRange.layerCount = 1;

	VkDependencyInfo undefinedToDepthStencilAttachmentOptimalDependencyInfo = {};
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.pNext = nullptr;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.memoryBarrierCount = 0;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.pMemoryBarriers = nullptr;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.bufferMemoryBarrierCount = 0;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.pBufferMemoryBarriers = nullptr;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.imageMemoryBarrierCount = 1;
	undefinedToDepthStencilAttachmentOptimalDependencyInfo.pImageMemoryBarriers = &undefinedToDepthStencilAttachmentOptimalImageMemoryBarrier;
	m_vkCmdPipelineBarrier2KHR(depthImageTransitionCommandBuffer, &undefinedToDepthStencilAttachmentOptimalDependencyInfo);

	vkEndCommandBuffer(depthImageTransitionCommandBuffer);

	VkFence depthImageTransitionFence;

	VkFenceCreateInfo depthImageTransitionFenceCreateInfo = {};
	depthImageTransitionFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	depthImageTransitionFenceCreateInfo.pNext = nullptr;
	depthImageTransitionFenceCreateInfo.flags = 0;
	NTSH_VK_CHECK(vkCreateFence(m_device, &depthImageTransitionFenceCreateInfo, nullptr, &depthImageTransitionFence));

	VkSubmitInfo depthImageTransitionSubmitInfo = {};
	depthImageTransitionSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	depthImageTransitionSubmitInfo.pNext = nullptr;
	depthImageTransitionSubmitInfo.waitSemaphoreCount = 0;
	depthImageTransitionSubmitInfo.pWaitSemaphores = nullptr;
	depthImageTransitionSubmitInfo.pWaitDstStageMask = nullptr;
	depthImageTransitionSubmitInfo.commandBufferCount = 1;
	depthImageTransitionSubmitInfo.pCommandBuffers = &depthImageTransitionCommandBuffer;
	depthImageTransitionSubmitInfo.signalSemaphoreCount = 0;
	depthImageTransitionSubmitInfo.pSignalSemaphores = nullptr;
	NTSH_VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &depthImageTransitionSubmitInfo, depthImageTransitionFence));
	NTSH_VK_CHECK(vkWaitForFences(m_device, 1, &depthImageTransitionFence, VK_TRUE, std::numeric_limits<uint64_t>::max()));

	vkDestroyFence(m_device, depthImageTransitionFence, nullptr);
	vkDestroyCommandPool(m_device, depthImageTransitionCommandPool, nullptr);
}

void NutshellGraphicsModule::createGraphicsPipeline() {
	// Create graphics pipeline
	VkFormat pipelineRenderingColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		pipelineRenderingColorFormat = m_swapchainFormat;
	}

	VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo = {};
	pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	pipelineRenderingCreateInfo.pNext = nullptr;
	pipelineRenderingCreateInfo.viewMask = 0;
	pipelineRenderingCreateInfo.colorAttachmentCount = 1;
	pipelineRenderingCreateInfo.pColorAttachmentFormats = &pipelineRenderingColorFormat;
	pipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
	pipelineRenderingCreateInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

	const std::vector<uint32_t> vertexShaderCode = { 0x07230203,0x00010000,0x0008000b,0x00000049,0x00000000,0x00020011,0x00000001,0x0006000b,
	0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
	0x000e000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000b,0x0000000f,
	0x00000011,0x00000015,0x0000002d,0x0000003c,0x00000046,0x00000048,0x00030003,0x00000002,
	0x000001cc,0x00080004,0x455f4c47,0x6e5f5458,0x6e756e6f,0x726f6669,0x75715f6d,0x66696c61,
	0x00726569,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00050005,0x00000009,0x4e74756f,
	0x616d726f,0x0000006c,0x00040005,0x0000000b,0x6d726f6e,0x00006c61,0x00040005,0x0000000f,
	0x5574756f,0x00000056,0x00030005,0x00000011,0x00007675,0x00060005,0x00000015,0x5474756f,
	0x75747865,0x44496572,0x00000000,0x00050005,0x00000018,0x656a624f,0x6e497463,0x00006f66,
	0x00050006,0x00000018,0x00000000,0x65646f6d,0x0000006c,0x00060006,0x00000018,0x00000001,
	0x74786574,0x49657275,0x00000044,0x00040005,0x0000001a,0x656a624f,0x00737463,0x00050006,
	0x0000001a,0x00000000,0x6f666e69,0x00000000,0x00040005,0x0000001c,0x656a626f,0x00737463,
	0x00050005,0x0000001f,0x656a624f,0x44497463,0x00000000,0x00060006,0x0000001f,0x00000000,
	0x656a626f,0x44497463,0x00000000,0x00030005,0x00000021,0x0044496f,0x00060005,0x0000002b,
	0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x0000002b,0x00000000,0x505f6c67,
	0x7469736f,0x006e6f69,0x00070006,0x0000002b,0x00000001,0x505f6c67,0x746e696f,0x657a6953,
	0x00000000,0x00070006,0x0000002b,0x00000002,0x435f6c67,0x4470696c,0x61747369,0x0065636e,
	0x00070006,0x0000002b,0x00000003,0x435f6c67,0x446c6c75,0x61747369,0x0065636e,0x00030005,
	0x0000002d,0x00000000,0x00040005,0x0000002e,0x656d6143,0x00006172,0x00050006,0x0000002e,
	0x00000000,0x77656976,0x00000000,0x00060006,0x0000002e,0x00000001,0x6a6f7270,0x69746365,
	0x00006e6f,0x00040005,0x00000030,0x656d6163,0x00006172,0x00050005,0x0000003c,0x69736f70,
	0x6e6f6974,0x00000000,0x00040005,0x00000046,0x6f6c6f63,0x00000072,0x00040005,0x00000048,
	0x676e6174,0x00746e65,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000b,
	0x0000001e,0x00000001,0x00040047,0x0000000f,0x0000001e,0x00000001,0x00040047,0x00000011,
	0x0000001e,0x00000002,0x00030047,0x00000015,0x0000000e,0x00040047,0x00000015,0x0000001e,
	0x00000002,0x00040048,0x00000018,0x00000000,0x00000005,0x00050048,0x00000018,0x00000000,
	0x00000023,0x00000000,0x00050048,0x00000018,0x00000000,0x00000007,0x00000010,0x00050048,
	0x00000018,0x00000001,0x00000023,0x00000040,0x00040047,0x00000019,0x00000006,0x00000050,
	0x00040048,0x0000001a,0x00000000,0x00000013,0x00040048,0x0000001a,0x00000000,0x00000018,
	0x00050048,0x0000001a,0x00000000,0x00000023,0x00000000,0x00030047,0x0000001a,0x00000003,
	0x00040047,0x0000001c,0x00000022,0x00000000,0x00040047,0x0000001c,0x00000021,0x00000001,
	0x00050048,0x0000001f,0x00000000,0x00000023,0x00000000,0x00030047,0x0000001f,0x00000002,
	0x00050048,0x0000002b,0x00000000,0x0000000b,0x00000000,0x00050048,0x0000002b,0x00000001,
	0x0000000b,0x00000001,0x00050048,0x0000002b,0x00000002,0x0000000b,0x00000003,0x00050048,
	0x0000002b,0x00000003,0x0000000b,0x00000004,0x00030047,0x0000002b,0x00000002,0x00040048,
	0x0000002e,0x00000000,0x00000005,0x00050048,0x0000002e,0x00000000,0x00000023,0x00000000,
	0x00050048,0x0000002e,0x00000000,0x00000007,0x00000010,0x00040048,0x0000002e,0x00000001,
	0x00000005,0x00050048,0x0000002e,0x00000001,0x00000023,0x00000040,0x00050048,0x0000002e,
	0x00000001,0x00000007,0x00000010,0x00030047,0x0000002e,0x00000002,0x00040047,0x00000030,
	0x00000022,0x00000000,0x00040047,0x00000030,0x00000021,0x00000000,0x00040047,0x0000003c,
	0x0000001e,0x00000000,0x00040047,0x00000046,0x0000001e,0x00000003,0x00040047,0x00000048,
	0x0000001e,0x00000004,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,
	0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000003,0x00040020,0x00000008,
	0x00000003,0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040020,0x0000000a,
	0x00000001,0x00000007,0x0004003b,0x0000000a,0x0000000b,0x00000001,0x00040017,0x0000000d,
	0x00000006,0x00000002,0x00040020,0x0000000e,0x00000003,0x0000000d,0x0004003b,0x0000000e,
	0x0000000f,0x00000003,0x00040020,0x00000010,0x00000001,0x0000000d,0x0004003b,0x00000010,
	0x00000011,0x00000001,0x00040015,0x00000013,0x00000020,0x00000000,0x00040020,0x00000014,
	0x00000003,0x00000013,0x0004003b,0x00000014,0x00000015,0x00000003,0x00040017,0x00000016,
	0x00000006,0x00000004,0x00040018,0x00000017,0x00000016,0x00000004,0x0004001e,0x00000018,
	0x00000017,0x00000013,0x0003001d,0x00000019,0x00000018,0x0003001e,0x0000001a,0x00000019,
	0x00040020,0x0000001b,0x00000002,0x0000001a,0x0004003b,0x0000001b,0x0000001c,0x00000002,
	0x00040015,0x0000001d,0x00000020,0x00000001,0x0004002b,0x0000001d,0x0000001e,0x00000000,
	0x0003001e,0x0000001f,0x00000013,0x00040020,0x00000020,0x00000009,0x0000001f,0x0004003b,
	0x00000020,0x00000021,0x00000009,0x00040020,0x00000022,0x00000009,0x00000013,0x0004002b,
	0x0000001d,0x00000025,0x00000001,0x00040020,0x00000026,0x00000002,0x00000013,0x0004002b,
	0x00000013,0x00000029,0x00000001,0x0004001c,0x0000002a,0x00000006,0x00000029,0x0006001e,
	0x0000002b,0x00000016,0x00000006,0x0000002a,0x0000002a,0x00040020,0x0000002c,0x00000003,
	0x0000002b,0x0004003b,0x0000002c,0x0000002d,0x00000003,0x0004001e,0x0000002e,0x00000017,
	0x00000017,0x00040020,0x0000002f,0x00000002,0x0000002e,0x0004003b,0x0000002f,0x00000030,
	0x00000002,0x00040020,0x00000031,0x00000002,0x00000017,0x0004003b,0x0000000a,0x0000003c,
	0x00000001,0x0004002b,0x00000006,0x0000003e,0x3f800000,0x00040020,0x00000044,0x00000003,
	0x00000016,0x0004003b,0x0000000a,0x00000046,0x00000001,0x00040020,0x00000047,0x00000001,
	0x00000016,0x0004003b,0x00000047,0x00000048,0x00000001,0x00050036,0x00000002,0x00000004,
	0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003d,0x00000007,0x0000000c,0x0000000b,
	0x0003003e,0x00000009,0x0000000c,0x0004003d,0x0000000d,0x00000012,0x00000011,0x0003003e,
	0x0000000f,0x00000012,0x00050041,0x00000022,0x00000023,0x00000021,0x0000001e,0x0004003d,
	0x00000013,0x00000024,0x00000023,0x00070041,0x00000026,0x00000027,0x0000001c,0x0000001e,
	0x00000024,0x00000025,0x0004003d,0x00000013,0x00000028,0x00000027,0x0003003e,0x00000015,
	0x00000028,0x00050041,0x00000031,0x00000032,0x00000030,0x00000025,0x0004003d,0x00000017,
	0x00000033,0x00000032,0x00050041,0x00000031,0x00000034,0x00000030,0x0000001e,0x0004003d,
	0x00000017,0x00000035,0x00000034,0x00050092,0x00000017,0x00000036,0x00000033,0x00000035,
	0x00050041,0x00000022,0x00000037,0x00000021,0x0000001e,0x0004003d,0x00000013,0x00000038,
	0x00000037,0x00070041,0x00000031,0x00000039,0x0000001c,0x0000001e,0x00000038,0x0000001e,
	0x0004003d,0x00000017,0x0000003a,0x00000039,0x00050092,0x00000017,0x0000003b,0x00000036,
	0x0000003a,0x0004003d,0x00000007,0x0000003d,0x0000003c,0x00050051,0x00000006,0x0000003f,
	0x0000003d,0x00000000,0x00050051,0x00000006,0x00000040,0x0000003d,0x00000001,0x00050051,
	0x00000006,0x00000041,0x0000003d,0x00000002,0x00070050,0x00000016,0x00000042,0x0000003f,
	0x00000040,0x00000041,0x0000003e,0x00050091,0x00000016,0x00000043,0x0000003b,0x00000042,
	0x00050041,0x00000044,0x00000045,0x0000002d,0x0000001e,0x0003003e,0x00000045,0x00000043,
	0x000100fd,0x00010038 };

	VkShaderModule vertexShaderModule;
	VkShaderModuleCreateInfo vertexShaderModuleCreateInfo = {};
	vertexShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	vertexShaderModuleCreateInfo.pNext = nullptr;
	vertexShaderModuleCreateInfo.flags = 0;
	vertexShaderModuleCreateInfo.codeSize = vertexShaderCode.size() * sizeof(uint32_t);
	vertexShaderModuleCreateInfo.pCode = vertexShaderCode.data();
	NTSH_VK_CHECK(vkCreateShaderModule(m_device, &vertexShaderModuleCreateInfo, nullptr, &vertexShaderModule));

	VkPipelineShaderStageCreateInfo vertexShaderStageCreateInfo = {};
	vertexShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertexShaderStageCreateInfo.pNext = nullptr;
	vertexShaderStageCreateInfo.flags = 0;
	vertexShaderStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertexShaderStageCreateInfo.module = vertexShaderModule;
	vertexShaderStageCreateInfo.pName = "main";
	vertexShaderStageCreateInfo.pSpecializationInfo = nullptr;

	const std::vector<uint32_t> fragmentShaderCode = { 0x07230203,0x00010000,0x0008000a,0x00000027,0x00000000,0x00020011,0x00000001,0x00020011,
	0x000014b6,0x0008000a,0x5f565053,0x5f545845,0x63736564,0x74706972,0x695f726f,0x7865646e,
	0x00676e69,0x0006000b,0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,
	0x00000000,0x00000001,0x0009000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000011,
	0x00000018,0x0000001c,0x00000026,0x00030010,0x00000004,0x00000007,0x00030003,0x00000002,
	0x000001cc,0x00080004,0x455f4c47,0x6e5f5458,0x6e756e6f,0x726f6669,0x75715f6d,0x66696c61,
	0x00726569,0x00040005,0x00000004,0x6e69616d,0x00000000,0x00060005,0x00000009,0x74786574,
	0x43657275,0x726f6c6f,0x00000000,0x00050005,0x0000000e,0x74786574,0x73657275,0x00000000,
	0x00050005,0x00000011,0x65546e69,0x72757478,0x00444965,0x00040005,0x00000018,0x56556e69,
	0x00000000,0x00050005,0x0000001c,0x4374756f,0x726f6c6f,0x00000000,0x00050005,0x00000026,
	0x6f4e6e69,0x6c616d72,0x00000000,0x00040047,0x0000000e,0x00000022,0x00000000,0x00040047,
	0x0000000e,0x00000021,0x00000002,0x00030047,0x00000011,0x0000000e,0x00040047,0x00000011,
	0x0000001e,0x00000002,0x00040047,0x00000018,0x0000001e,0x00000001,0x00040047,0x0000001c,
	0x0000001e,0x00000000,0x00040047,0x00000026,0x0000001e,0x00000000,0x00020013,0x00000002,
	0x00030021,0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,
	0x00000006,0x00000004,0x00040020,0x00000008,0x00000007,0x00000007,0x00090019,0x0000000a,
	0x00000006,0x00000001,0x00000000,0x00000000,0x00000000,0x00000001,0x00000000,0x0003001b,
	0x0000000b,0x0000000a,0x0003001d,0x0000000c,0x0000000b,0x00040020,0x0000000d,0x00000000,
	0x0000000c,0x0004003b,0x0000000d,0x0000000e,0x00000000,0x00040015,0x0000000f,0x00000020,
	0x00000000,0x00040020,0x00000010,0x00000001,0x0000000f,0x0004003b,0x00000010,0x00000011,
	0x00000001,0x00040020,0x00000013,0x00000000,0x0000000b,0x00040017,0x00000016,0x00000006,
	0x00000002,0x00040020,0x00000017,0x00000001,0x00000016,0x0004003b,0x00000017,0x00000018,
	0x00000001,0x00040020,0x0000001b,0x00000003,0x00000007,0x0004003b,0x0000001b,0x0000001c,
	0x00000003,0x00040017,0x0000001d,0x00000006,0x00000003,0x0004002b,0x00000006,0x00000020,
	0x3f800000,0x00040020,0x00000025,0x00000001,0x0000001d,0x0004003b,0x00000025,0x00000026,
	0x00000001,0x00050036,0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,
	0x0004003b,0x00000008,0x00000009,0x00000007,0x0004003d,0x0000000f,0x00000012,0x00000011,
	0x00050041,0x00000013,0x00000014,0x0000000e,0x00000012,0x0004003d,0x0000000b,0x00000015,
	0x00000014,0x0004003d,0x00000016,0x00000019,0x00000018,0x00050057,0x00000007,0x0000001a,
	0x00000015,0x00000019,0x0003003e,0x00000009,0x0000001a,0x0004003d,0x00000007,0x0000001e,
	0x00000009,0x0008004f,0x0000001d,0x0000001f,0x0000001e,0x0000001e,0x00000000,0x00000001,
	0x00000002,0x00050051,0x00000006,0x00000021,0x0000001f,0x00000000,0x00050051,0x00000006,
	0x00000022,0x0000001f,0x00000001,0x00050051,0x00000006,0x00000023,0x0000001f,0x00000002,
	0x00070050,0x00000007,0x00000024,0x00000021,0x00000022,0x00000023,0x00000020,0x0003003e,
	0x0000001c,0x00000024,0x000100fd,0x00010038 };

	VkShaderModule fragmentShaderModule;
	VkShaderModuleCreateInfo fragmentShaderModuleCreateInfo = {};
	fragmentShaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	fragmentShaderModuleCreateInfo.pNext = nullptr;
	fragmentShaderModuleCreateInfo.flags = 0;
	fragmentShaderModuleCreateInfo.codeSize = fragmentShaderCode.size() * sizeof(uint32_t);
	fragmentShaderModuleCreateInfo.pCode = fragmentShaderCode.data();
	NTSH_VK_CHECK(vkCreateShaderModule(m_device, &fragmentShaderModuleCreateInfo, nullptr, &fragmentShaderModule));

	VkPipelineShaderStageCreateInfo fragmentShaderStageCreateInfo = {};
	fragmentShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragmentShaderStageCreateInfo.pNext = nullptr;
	fragmentShaderStageCreateInfo.flags = 0;
	fragmentShaderStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragmentShaderStageCreateInfo.module = fragmentShaderModule;
	fragmentShaderStageCreateInfo.pName = "main";
	fragmentShaderStageCreateInfo.pSpecializationInfo = nullptr;

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStageCreateInfos = { vertexShaderStageCreateInfo, fragmentShaderStageCreateInfo };

	VkVertexInputBindingDescription vertexInputBindingDescription = {};
	vertexInputBindingDescription.binding = 0;
	vertexInputBindingDescription.stride = sizeof(NtshVertex);
	vertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription vertexPositionInputAttributeDescription = {};
	vertexPositionInputAttributeDescription.location = 0;
	vertexPositionInputAttributeDescription.binding = 0;
	vertexPositionInputAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexPositionInputAttributeDescription.offset = 0;

	VkVertexInputAttributeDescription vertexNormalInputAttributeDescription = {};
	vertexNormalInputAttributeDescription.location = 1;
	vertexNormalInputAttributeDescription.binding = 0;
	vertexNormalInputAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexNormalInputAttributeDescription.offset = offsetof(NtshVertex, normal);

	VkVertexInputAttributeDescription vertexUVInputAttributeDescription = {};
	vertexUVInputAttributeDescription.location = 2;
	vertexUVInputAttributeDescription.binding = 0;
	vertexUVInputAttributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
	vertexUVInputAttributeDescription.offset = offsetof(NtshVertex, uv);

	VkVertexInputAttributeDescription vertexColorInputAttributeDescription = {};
	vertexColorInputAttributeDescription.location = 3;
	vertexColorInputAttributeDescription.binding = 0;
	vertexColorInputAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexColorInputAttributeDescription.offset = offsetof(NtshVertex, color);

	VkVertexInputAttributeDescription vertexTangentInputAttributeDescription = {};
	vertexTangentInputAttributeDescription.location = 4;
	vertexTangentInputAttributeDescription.binding = 0;
	vertexTangentInputAttributeDescription.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	vertexTangentInputAttributeDescription.offset = offsetof(NtshVertex, tangent);

	std::array<VkVertexInputAttributeDescription, 5> vertexInputAttributeDescriptions = { vertexPositionInputAttributeDescription, vertexNormalInputAttributeDescription, vertexUVInputAttributeDescription, vertexColorInputAttributeDescription, vertexTangentInputAttributeDescription };

	VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = {};
	vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCreateInfo.pNext = nullptr;
	vertexInputStateCreateInfo.flags = 0;
	vertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
	vertexInputStateCreateInfo.pVertexBindingDescriptions = &vertexInputBindingDescription;
	vertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributeDescriptions.size());
	vertexInputStateCreateInfo.pVertexAttributeDescriptions = vertexInputAttributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = {};
	inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCreateInfo.pNext = nullptr;
	inputAssemblyStateCreateInfo.flags = 0;
	inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
	viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCreateInfo.pNext = nullptr;
	viewportStateCreateInfo.flags = 0;
	viewportStateCreateInfo.viewportCount = 1;
	viewportStateCreateInfo.pViewports = &m_viewport;
	viewportStateCreateInfo.scissorCount = 1;
	viewportStateCreateInfo.pScissors = &m_scissor;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = {};
	rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCreateInfo.pNext = nullptr;
	rasterizationStateCreateInfo.flags = 0;
	rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
	rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
	rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
	rasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
	rasterizationStateCreateInfo.depthBiasClamp = 0.0f;
	rasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
	rasterizationStateCreateInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = {};
	multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCreateInfo.pNext = nullptr;
	multisampleStateCreateInfo.flags = 0;
	multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
	multisampleStateCreateInfo.minSampleShading = 0.0f;
	multisampleStateCreateInfo.pSampleMask = nullptr;
	multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE;
	multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = {};
	depthStencilStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCreateInfo.pNext = nullptr;
	depthStencilStateCreateInfo.flags = 0;
	depthStencilStateCreateInfo.depthTestEnable = VK_TRUE;
	depthStencilStateCreateInfo.depthWriteEnable = VK_TRUE;
	depthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencilStateCreateInfo.depthBoundsTestEnable = VK_FALSE;
	depthStencilStateCreateInfo.stencilTestEnable = VK_FALSE;
	depthStencilStateCreateInfo.front = {};
	depthStencilStateCreateInfo.back = {};
	depthStencilStateCreateInfo.minDepthBounds = 0.0f;
	depthStencilStateCreateInfo.maxDepthBounds = 1.0f;

	VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
	colorBlendAttachmentState.blendEnable = VK_FALSE;
	colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachmentState.colorWriteMask = { VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT };

	VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = {};
	colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCreateInfo.pNext = nullptr;
	colorBlendStateCreateInfo.flags = 0;
	colorBlendStateCreateInfo.logicOpEnable = VK_FALSE;
	colorBlendStateCreateInfo.logicOp = VK_LOGIC_OP_COPY;
	colorBlendStateCreateInfo.attachmentCount = 1;
	colorBlendStateCreateInfo.pAttachments = &colorBlendAttachmentState;

	std::array<VkDynamicState, 2> dynamicStates = { VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_VIEWPORT };
	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
	dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCreateInfo.pNext = nullptr;
	dynamicStateCreateInfo.flags = 0;
	dynamicStateCreateInfo.dynamicStateCount = 2;
	dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();

	VkDescriptorSetLayoutBinding cameraDescriptorSetLayoutBinding = {};
	cameraDescriptorSetLayoutBinding.binding = 0;
	cameraDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraDescriptorSetLayoutBinding.descriptorCount = 1;
	cameraDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	cameraDescriptorSetLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding objectsDescriptorSetLayoutBinding = {};
	objectsDescriptorSetLayoutBinding.binding = 1;
	objectsDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	objectsDescriptorSetLayoutBinding.descriptorCount = 1;
	objectsDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	objectsDescriptorSetLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding texturesDescriptorSetLayoutBinding = {};
	texturesDescriptorSetLayoutBinding.binding = 2;
	texturesDescriptorSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texturesDescriptorSetLayoutBinding.descriptorCount = 524288;
	texturesDescriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	texturesDescriptorSetLayoutBinding.pImmutableSamplers = nullptr;

	std::array<VkDescriptorBindingFlags, 3> descriptorBindingFlags = { 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT };
	VkDescriptorSetLayoutBindingFlagsCreateInfo descriptorSetLayoutBindingFlagsCreateInfo = {};
	descriptorSetLayoutBindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	descriptorSetLayoutBindingFlagsCreateInfo.pNext = nullptr;
	descriptorSetLayoutBindingFlagsCreateInfo.bindingCount = static_cast<uint32_t>(descriptorBindingFlags.size());
	descriptorSetLayoutBindingFlagsCreateInfo.pBindingFlags = descriptorBindingFlags.data();

	std::array<VkDescriptorSetLayoutBinding, 3> descriptorSetLayoutBindings = { cameraDescriptorSetLayoutBinding, objectsDescriptorSetLayoutBinding, texturesDescriptorSetLayoutBinding };
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
	descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutCreateInfo.pNext = &descriptorSetLayoutBindingFlagsCreateInfo;
	descriptorSetLayoutCreateInfo.flags = 0;
	descriptorSetLayoutCreateInfo.bindingCount = static_cast<uint32_t>(descriptorSetLayoutBindings.size());
	descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBindings.data();
	NTSH_VK_CHECK(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &m_descriptorSetLayout));

	VkPushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(uint32_t);

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.pNext = nullptr;
	pipelineLayoutCreateInfo.flags = 0;
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &m_descriptorSetLayout;
	pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
	NTSH_VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutCreateInfo, nullptr, &m_graphicsPipelineLayout));

	VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {};
	graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	graphicsPipelineCreateInfo.pNext = &pipelineRenderingCreateInfo;
	graphicsPipelineCreateInfo.flags = 0;
	graphicsPipelineCreateInfo.stageCount = 2;
	graphicsPipelineCreateInfo.pStages = shaderStageCreateInfos.data();
	graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
	graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
	graphicsPipelineCreateInfo.pTessellationState = nullptr;
	graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
	graphicsPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
	graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
	graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;
	graphicsPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
	graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
	graphicsPipelineCreateInfo.layout = m_graphicsPipelineLayout;
	graphicsPipelineCreateInfo.renderPass = VK_NULL_HANDLE;
	graphicsPipelineCreateInfo.subpass = 0;
	graphicsPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	graphicsPipelineCreateInfo.basePipelineIndex = 0;
	NTSH_VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &m_graphicsPipeline));

	vkDestroyShaderModule(m_device, vertexShaderModule, nullptr);
	vkDestroyShaderModule(m_device, fragmentShaderModule, nullptr);
}

void NutshellGraphicsModule::createDescriptorSets() {
	// Create descriptor pool
	VkDescriptorPoolSize cameraDescriptorPoolSize = {};
	cameraDescriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	cameraDescriptorPoolSize.descriptorCount = 1;

	VkDescriptorPoolSize objectsDescriptorPoolSize = {};
	objectsDescriptorPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	objectsDescriptorPoolSize.descriptorCount = 1;

	VkDescriptorPoolSize texturesDescriptorPoolSize = {};
	texturesDescriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	texturesDescriptorPoolSize.descriptorCount = 524288;

	std::array<VkDescriptorPoolSize, 3> descriptorPoolSizes = { cameraDescriptorPoolSize, objectsDescriptorPoolSize, texturesDescriptorPoolSize };
	VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
	descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCreateInfo.pNext = nullptr;
	descriptorPoolCreateInfo.flags = 0;
	descriptorPoolCreateInfo.maxSets = m_framesInFlight;
	descriptorPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
	descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
	NTSH_VK_CHECK(vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &m_descriptorPool));

	// Allocate descriptor sets
	m_descriptorSets.resize(m_framesInFlight);
	VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = {};
	descriptorSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAllocateInfo.pNext = nullptr;
	descriptorSetAllocateInfo.descriptorPool = m_descriptorPool;
	descriptorSetAllocateInfo.descriptorSetCount = 1;
	descriptorSetAllocateInfo.pSetLayouts = &m_descriptorSetLayout;

	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		NTSH_VK_CHECK(vkAllocateDescriptorSets(m_device, &descriptorSetAllocateInfo, &m_descriptorSets[i]));
	}

	// Update descriptor sets
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		VkDescriptorBufferInfo cameraDescriptorBufferInfo;
		VkDescriptorBufferInfo objectsDescriptorBufferInfo;
		std::vector<VkDescriptorImageInfo> texturesDescriptorImageInfos;

		cameraDescriptorBufferInfo.buffer = m_cameraBuffers[i];
		cameraDescriptorBufferInfo.offset = 0;
		cameraDescriptorBufferInfo.range = sizeof(nml::mat4) * 2;

		VkWriteDescriptorSet cameraDescriptorWriteDescriptorSet = {};
		cameraDescriptorWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		cameraDescriptorWriteDescriptorSet.pNext = nullptr;
		cameraDescriptorWriteDescriptorSet.dstSet = m_descriptorSets[i];
		cameraDescriptorWriteDescriptorSet.dstBinding = 0;
		cameraDescriptorWriteDescriptorSet.dstArrayElement = 0;
		cameraDescriptorWriteDescriptorSet.descriptorCount = 1;
		cameraDescriptorWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		cameraDescriptorWriteDescriptorSet.pImageInfo = nullptr;
		cameraDescriptorWriteDescriptorSet.pBufferInfo = &cameraDescriptorBufferInfo;
		cameraDescriptorWriteDescriptorSet.pTexelBufferView = nullptr;
		writeDescriptorSets.push_back(cameraDescriptorWriteDescriptorSet);

		objectsDescriptorBufferInfo.buffer = m_objectBuffers[i];
		objectsDescriptorBufferInfo.offset = 0;
		objectsDescriptorBufferInfo.range = 32768;

		VkWriteDescriptorSet objectsDescriptorWriteDescriptorSet = {};
		objectsDescriptorWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		objectsDescriptorWriteDescriptorSet.pNext = nullptr;
		objectsDescriptorWriteDescriptorSet.dstSet = m_descriptorSets[i];
		objectsDescriptorWriteDescriptorSet.dstBinding = 1;
		objectsDescriptorWriteDescriptorSet.dstArrayElement = 0;
		objectsDescriptorWriteDescriptorSet.descriptorCount = 1;
		objectsDescriptorWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		objectsDescriptorWriteDescriptorSet.pImageInfo = nullptr;
		objectsDescriptorWriteDescriptorSet.pBufferInfo = &objectsDescriptorBufferInfo;
		objectsDescriptorWriteDescriptorSet.pTexelBufferView = nullptr;
		writeDescriptorSets.push_back(objectsDescriptorWriteDescriptorSet);

		texturesDescriptorImageInfos.resize(1);
		for (size_t j = 0; j < texturesDescriptorImageInfos.size(); j++) {
			texturesDescriptorImageInfos[j].sampler = m_textureSampler;
			texturesDescriptorImageInfos[j].imageView = m_cubeTextureImageView;
			texturesDescriptorImageInfos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}

		VkWriteDescriptorSet texturesDescriptorWriteDescriptorSet = {};
		texturesDescriptorWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		texturesDescriptorWriteDescriptorSet.pNext = nullptr;
		texturesDescriptorWriteDescriptorSet.dstSet = m_descriptorSets[i];
		texturesDescriptorWriteDescriptorSet.dstBinding = 2;
		texturesDescriptorWriteDescriptorSet.dstArrayElement = 0;
		texturesDescriptorWriteDescriptorSet.descriptorCount = static_cast<uint32_t>(texturesDescriptorImageInfos.size());
		texturesDescriptorWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		texturesDescriptorWriteDescriptorSet.pImageInfo = texturesDescriptorImageInfos.data();
		texturesDescriptorWriteDescriptorSet.pBufferInfo = nullptr;
		texturesDescriptorWriteDescriptorSet.pTexelBufferView = nullptr;
		writeDescriptorSets.push_back(texturesDescriptorWriteDescriptorSet);

		vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
}

void NutshellGraphicsModule::resize() {
	if (m_windowModule && m_windowModule->isOpen(NTSH_MAIN_WINDOW)) {
		while (m_windowModule->getWidth(NTSH_MAIN_WINDOW) == 0 || m_windowModule->getHeight(NTSH_MAIN_WINDOW) == 0) {
			m_windowModule->pollEvents();
		}

		NTSH_VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));

		// Destroy swapchain image views
		for (VkImageView& swapchainImageView : m_swapchainImageViews) {
			vkDestroyImageView(m_device, swapchainImageView, nullptr);
		}

		// Recreate the swapchain
		createSwapchain(m_swapchain);
		
		// Destroy depth image and image view
		vkDestroyImageView(m_device, m_depthImageView, nullptr);
		vmaDestroyImage(m_allocator, m_depthImage, m_depthImageAllocation);

		// Recreate depth image
		createDepthImage();
	}
}

extern "C" NTSH_MODULE_API NutshellGraphicsModuleInterface* createModule() {
	return new NutshellGraphicsModule;
}

extern "C" NTSH_MODULE_API void destroyModule(NutshellGraphicsModuleInterface* m) {
	delete m;
}
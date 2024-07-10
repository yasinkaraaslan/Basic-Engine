#define VMA_IMPLEMENTATION

#include "Engine.h"
#include "Initializers.h"
#include "Images.h"
#include "Pipelines.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <SDL_image.h>
#include <chrono>
#include <thread>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>
#include <glm/gtx/transform.hpp>

constexpr bool bUseValidationLayers =
#ifdef DEBUG
true;
#else
false;
#endif

MaterialPipeline* lastPipeline = nullptr;
MaterialInstance* lastMaterial = nullptr;
VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

Engine* _loadedEngine = nullptr;


bool IsVisible(const RenderObject& obj, const glm::mat4& viewproj) {
	std::array<glm::vec3, 8> corners{
		glm::vec3 { 1, 1, 1 },
		glm::vec3 { 1, 1, -1 },
		glm::vec3 { 1, -1, 1 },
		glm::vec3 { 1, -1, -1 },
		glm::vec3 { -1, 1, 1 },
		glm::vec3 { -1, 1, -1 },
		glm::vec3 { -1, -1, 1 },
		glm::vec3 { -1, -1, -1 },
	};

	glm::mat4 matrix = viewproj * obj.transform;

	glm::vec3 min = { 1.5, 1.5, 1.5 };
	glm::vec3 max = { -1.5, -1.5, -1.5 };

	for (int c = 0; c < 8; c++) {
		// project each corner into clip space
		glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

		v.x = v.x / v.w;
		v.y = v.y / v.w;
		v.z = v.z / v.w;

		min = glm::min(glm::vec3{ v.x, v.y, v.z }, min);
		max = glm::max(glm::vec3{ v.x, v.y, v.z }, max);
	}

	if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) 
		return false;

	return true;
}
Engine* Engine::Get() { return _loadedEngine; }

const VkDevice& Engine::GetMainDevice()
{
	return _loadedEngine->_device;
}
void Engine::ShowError(const char* title, const char* message)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}
void Engine::ShowSDLError()
{
	ShowError("SDL Error", SDL_GetError());
}

void Engine::Init()
{
	assert(_loadedEngine == nullptr);
	_loadedEngine = this;
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
	{
		ShowSDLError();
		return;
	}
	if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) == 0)
	{
		ShowError("SDL_image error", IMG_GetError());
		return;
	}
	SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	_window = SDL_CreateWindow(
		"Scimulator",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width, _windowExtent.height,
		windowFlags);
	if (_window == nullptr)
	{
		ShowSDLError();
		return;
	}

	InitVulkan();
	InitSwapchain();
	InitCommands();
	InitSyncStructures();
	InitDescriptors();
	InitPipelines();
	InitImGui();
	InitDefaultData();

	_isInitialized = true;
}

void Engine::Run()
{

	SDL_Event e;
	bool bQuit = false;
	while (!bQuit)
	{
		auto start = std::chrono::system_clock::now();

		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_QUIT)
				bQuit = true;
			if (e.type == SDL_WINDOWEVENT)
			{
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
					_stopRendering = true;
				if (e.window.event == SDL_WINDOWEVENT_RESTORED)
					_stopRendering = false;
			}
			_camera.ProcessSDLEvent(e);
			ImGui_ImplSDL2_ProcessEvent(&e);
		}
		if (_resizeRequested)
			ResizeSwapchain();
		if (_stopRendering)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		ProcessImGui();
		Draw();
		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
		_stats.frameTime = elapsed.count() / 1000.f;
	}
	

}

void Engine::Cleanup()
{
	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);
		_loadedScenes.clear();

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);

			vkDestroyFence(_device, _frames[i].renderFence, nullptr);
			vkDestroySemaphore(_device, _frames[i].renderSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i].swapchainSemaphore, nullptr);
			_frames[i].deletionQueue.Flush();
		}
		for (auto& mesh : _testMeshes)
		{
			DestroyBuffer(mesh->meshBuffers.indexBuffer);
			DestroyBuffer(mesh->meshBuffers.vertexBuffer);
		}
		_mainDeletionQueue.Flush();
		DestroySwapchain();
		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyInstance(_instance, nullptr);
		IMG_Quit();
		SDL_DestroyWindow(_window);
		SDL_Quit();
	}
	_loadedEngine = nullptr;
}

void Engine::InitVulkan()
{
	vkb::InstanceBuilder builder;
	auto res = builder.set_app_name("Scimulator")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0)
		.build();
	vkb::Instance vkbInstance = res.value();

	_instance = vkbInstance.instance;
	_debugMessenger = vkbInstance.debug_messenger;

	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);
	VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features.dynamicRendering = true;
	features.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();
	_device = vkbDevice.device;
	_chosenGPU = physicalDevice.physical_device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _chosenGPU;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	_mainDeletionQueue.Push([&]() {
		vmaDestroyAllocator(_allocator);
		});
}

void Engine::InitSwapchain()
{
	CreateSwapchain(_windowExtent.width, _windowExtent.height);

	VkExtent3D drawImageExtent = { _windowExtent.width, _windowExtent.height, 1 };
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	VkImageCreateInfo imgInfo = Init::ImageCreateInfo(_drawImage.imageFormat, drawImageUsages, drawImageExtent);
	
	// alloc from gpu local memory
	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &imgInfo, &imgAllocInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	VkImageViewCreateInfo imageViewInfo = Init::ImageViewCreateInfo(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &imageViewInfo, nullptr, &_drawImage.imageView));

	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimgInfo = Init::ImageCreateInfo(_depthImage.imageFormat, depthImageUsages, drawImageExtent);
	vmaCreateImage(_allocator, &dimgInfo, &imgAllocInfo, &_depthImage.image, &_depthImage.allocation, nullptr);
	VkImageViewCreateInfo dviewInfo = Init::ImageViewCreateInfo(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
	VK_CHECK(vkCreateImageView(_device, &dviewInfo, nullptr, &_depthImage.imageView));
	
	
	_mainDeletionQueue.Push([=]() {
		vkDestroyImageView(_device, _drawImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
		vkDestroyImageView(_device, _depthImage.imageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
		});


}

void Engine::InitCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = Init::CommandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));
		
		VkCommandBufferAllocateInfo cmdAllocInfo = Init::CommandBufferAllocateInfo(_frames[i].commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));
	}

	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo = Init::CommandBufferAllocateInfo(_immCommandPool, 1);

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

	_mainDeletionQueue.Push([=]()
		{
			vkDestroyCommandPool(_device, _immCommandPool, nullptr);
		});
}

void Engine::InitSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = Init::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = Init::SemaphoreCreateInfo(0);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].renderSemaphore));
	}

	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.Push([=]() { vkDestroyFence(_device, _immFence, nullptr); });
}

void Engine::InitDescriptors()
{
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes =
	{
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
	};
	_descriptorAllocator.Init(10, sizes);

	{
		DescriptorLayoutBuilder builder;
		builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.Build(VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		_sceneDataDescriptorLayout = builder.Build(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		_singleImageDescriptorLayout = builder.Build(VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	_drawImageDescriptors = _descriptorAllocator.Allocate(_drawImageDescriptorLayout);

	DescriptorWriter writer;
	writer.WriteImage(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	writer.UpdateSet(_drawImageDescriptors);

	for (int i = 0; i < FRAME_OVERLAP; i++) {
		// create a descriptor pool
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frameSizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		_frames[i].descriptors = DescriptorAllocatorGrowable{};
		_frames[i].descriptors.Init(1000, frameSizes);

		_mainDeletionQueue.Push([&, i]() {
			_frames[i].descriptors.DestroyPools();
			});
	}

	

	_mainDeletionQueue.Push([&]()
		{
			_descriptorAllocator.DestroyPools();
			vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
			vkDestroyDescriptorSetLayout(_device, _sceneDataDescriptorLayout, nullptr);
			vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);
		});
}

void Engine::InitPipelines()
{
	InitBackgroundPipelines();
	_metalRoughMat.BuildPipelines();
	_mainDeletionQueue.Push([&]()
		{
			_metalRoughMat.CleanResources();
		});
}

void Engine::InitBackgroundPipelines()
{
	VkPipelineLayoutCreateInfo computeLayout = {};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;
	computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant = {};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

	VkShaderModule gradientShader = Util::LoadShader("gradient_color.comp.spv");
	VkShaderModule skyShader = Util::LoadShader("sky.comp.spv");

	VkPipelineShaderStageCreateInfo stageInfo = {};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = gradientShader;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineInfo = {};
	computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineInfo.pNext = nullptr;
	computePipelineInfo.layout = _gradientPipelineLayout;
	computePipelineInfo.stage = stageInfo;

	ComputeEffect gradient;
	gradient.layout = _gradientPipelineLayout;
	gradient.name = "Gradient";
	gradient.data = {};
	gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &gradient.pipeline));

	computePipelineInfo.stage.module = skyShader;

	ComputeEffect sky;
	sky.layout = _gradientPipelineLayout;
	sky.name = "Sky";
	sky.data = {};
	sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &sky.pipeline));

	_backgroundEffects.push_back(gradient);
	_backgroundEffects.push_back(sky);

	vkDestroyShaderModule(_device, gradientShader, nullptr);
	vkDestroyShaderModule(_device, skyShader, nullptr);
	_mainDeletionQueue.Push([=]()
		{
			vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
			vkDestroyPipeline(_device, sky.pipeline, nullptr);
			vkDestroyPipeline(_device, gradient.pipeline, nullptr);
		});
}

void Engine::InitImGui()
{
	VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = _instance;
	initInfo.PhysicalDevice = _chosenGPU;
	initInfo.Device = _device;
	initInfo.Queue = _graphicsQueue;
	initInfo.DescriptorPool = imguiPool;
	initInfo.MinImageCount = 3;
	initInfo.ImageCount = 3;
	initInfo.UseDynamicRendering = true;

	initInfo.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;


	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);

	ImGui_ImplVulkan_CreateFontsTexture();

	_mainDeletionQueue.Push([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		});
}


void Engine::InitDefaultData()
{	
	//3 default textures, white, grey, black. 1 pixel each
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	_whiteImage = CreateImage((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	_greyImage = CreateImage((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
	_blackImage = CreateImage((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}
	_errorCheckerboardImage = CreateImage(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		VK_IMAGE_USAGE_SAMPLED_BIT);

	VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

	sampl.magFilter = VK_FILTER_NEAREST;
	sampl.minFilter = VK_FILTER_NEAREST;

	vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

	sampl.magFilter = VK_FILTER_LINEAR;
	sampl.minFilter = VK_FILTER_LINEAR;
	vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

	
	_camera.SetVelocity(glm::vec3(0.f));
	_camera.SetPosition(glm::vec3(30.f, -00.f, -085.f));
	_camera.SetPitch(0.f);
	_camera.SetYaw(0.f);

	auto structureFile = LoadedGLTF::Load("../../../assets/structure.glb");
	assert(structureFile.has_value());
	_loadedScenes["structure"] = *structureFile;

	_mainDeletionQueue.Push([&]() {
		vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
		vkDestroySampler(_device, _defaultSamplerLinear, nullptr);

		DestroyImage(_whiteImage);
		DestroyImage(_greyImage);
		DestroyImage(_blackImage);
		DestroyImage(_errorCheckerboardImage);
		});
}

void Engine::Draw()
{
	UpdateScene();
	// Wait until GPU has finished rendering the last frame
	VK_CHECK(vkWaitForFences(_device, 1, &GetCurrentFrame().renderFence, true, 1000000000)); // Timeout of 1 second
	GetCurrentFrame().deletionQueue.Flush();
	GetCurrentFrame().descriptors.ClearPools();

	VK_CHECK(vkResetFences(_device, 1, &GetCurrentFrame().renderFence));

	// Request image from the swapchain
	uint32_t swapchainImageIndex;
	VkResult e = vkAcquireNextImageKHR(_device, _swapchain, 1000000000, GetCurrentFrame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR) {
		_resizeRequested = true;
		return;
	}
	VkCommandBuffer cmd = GetCurrentFrame().mainCommandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));
	auto cmdBeginInfo = Init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	_drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * _renderScale;
	_drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * _renderScale;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// Make the swapchain image into writeable mode before rendering
	Util::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	DrawBackground(cmd);

	Util::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	Util::TransitionImage(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	DrawGeometry(cmd);
	Util::TransitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	Util::TransitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	Util::CopyImage(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);
	Util::TransitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	DrawImGui(cmd, _swapchainImageViews[swapchainImageIndex]);
	Util::TransitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// Finalize the command buffer
	VK_CHECK(vkEndCommandBuffer(cmd));

	auto cmdInfo = Init::CommandBufferSubmitInfo(cmd);
	auto waitInfo = Init::SemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, GetCurrentFrame().swapchainSemaphore);
	auto signalInfo = Init::SemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, GetCurrentFrame().renderSemaphore);
	auto submitInfo = Init::SubmitInfo(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submitInfo, GetCurrentFrame().renderFence));
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &GetCurrentFrame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;
	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
		_resizeRequested = true;
	_frameNumber++;

}

void Engine::DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = Init::AttachmentInfo(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = Init::RenderingInfo(_swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void Engine::DrawBackground(VkCommandBuffer cmd)
{
	ComputeEffect& effect = _backgroundEffects[_currentBackgroundEffect];
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
		_gradientPipelineLayout, 0, 1,
		&_drawImageDescriptors, 0, nullptr);
	vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);
	vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void Engine::DrawGeometry(VkCommandBuffer cmd)
{

	_stats.drawCallCount = 0;
	_stats.triangleCount = 0;

	auto start = std::chrono::system_clock::now();
	std::vector<uint32_t> opaqueDraws;
	for (int i = 0; i < _drawContext.opaqueSurfaces.size(); i++) {
		//if (IsVisible(_drawContext.opaqueSurfaces[i], _sceneData.viewproj)) {
			opaqueDraws.push_back(i);
		//}
	}
	VkRenderingAttachmentInfo colorAttachment = Init::AttachmentInfo(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = Init::DepthAttachmentInfo(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = Init::RenderingInfo(_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	AllocatedBuffer sceneDataBuffer = CreateBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	GetCurrentFrame().deletionQueue.Push([=, this]() {
		DestroyBuffer(sceneDataBuffer);
		});

	SceneData* sceneUniformData = (SceneData*)sceneDataBuffer.allocation->GetMappedData();
	*sceneUniformData = _sceneData;

	VkDescriptorSet globalDescriptor = GetCurrentFrame().descriptors.Allocate(_sceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.WriteBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.UpdateSet(globalDescriptor);

	auto draw = [&](const RenderObject& r) {

		if (r.material != lastMaterial) {
			lastMaterial = r.material;
			if (r.material->pipeline != lastPipeline) {

				lastPipeline = r.material->pipeline;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 0, 1,
					&globalDescriptor, 0, nullptr);

				VkViewport viewport = {};
				viewport.x = 0;
				viewport.y = 0;
				viewport.width = (float)_windowExtent.width;
				viewport.height = (float)_windowExtent.height;
				viewport.minDepth = 0.f;
				viewport.maxDepth = 1.f;

				vkCmdSetViewport(cmd, 0, 1, &viewport);

				VkRect2D scissor = {};
				scissor.offset.x = 0;
				scissor.offset.y = 0;
				scissor.extent.width = _windowExtent.width;
				scissor.extent.height = _windowExtent.height;

				vkCmdSetScissor(cmd, 0, 1, &scissor);
			}

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->layout, 1, 1,
				&r.material->materialSet, 0, nullptr);
		}

		if (r.indexBuffer != lastIndexBuffer)
		{
			lastIndexBuffer = r.indexBuffer;
			vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}

		DrawPushConstants pushConstants;
		pushConstants.vertexBuffer = r.vertexBufferAddress;
		pushConstants.worldMatrix = r.transform;
		vkCmdPushConstants(cmd, r.material->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(DrawPushConstants), &pushConstants);

		vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);
		_stats.drawCallCount++;
		_stats.triangleCount += (r.indexCount) / 3;

	};

	for (auto& r : opaqueDraws) {
			draw(_drawContext.opaqueSurfaces[r]);
	}

	for (auto& r : _drawContext.transparentSurfaces) {
		draw(r);
	}
	vkCmdEndRendering(cmd);
	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	_stats.meshDrawTime = elapsed.count() / 1000.f;
}

void Engine::UpdateScene()
{
	auto start = std::chrono::system_clock::now();

	_drawContext.opaqueSurfaces.clear();
	_drawContext.transparentSurfaces.clear();
	_loadedScenes["structure"]->Draw(glm::mat4{1.f}, _drawContext);

	_camera.Update(_stats.frameTime);

	_sceneData.view = _camera.GetViewMatrix();
	_sceneData.proj = glm::perspective(glm::radians(_fov), (float)_windowExtent.width / (float)_windowExtent.height, 10000.f, 0.1f);
	
	// invert y
	// TODO: handle shader compilation with glslc to invert y in shader stage
	_sceneData.proj[1][1] *= -1;
	_sceneData.viewproj = _sceneData.proj * _sceneData.view;
	// some random lighting params
	_sceneData.ambientColor = glm::vec4(.1f);
	_sceneData.sunlightColor = glm::vec4(1.f);
	_sceneData.sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);
	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	_stats.sceneUpdateTime = elapsed.count() / 1000.f;
}

void Engine::ProcessImGui()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	
	if (ImGui::Begin("Background"))
	{
		ComputeEffect& selected = _backgroundEffects[_currentBackgroundEffect];
		ImGui::Text("Selected effect: ", selected.name);
		ImGui::SliderInt("Effect Index", &_currentBackgroundEffect, 0, _backgroundEffects.size() - 1);

		ImGui::InputFloat4("data1", (float*)&selected.data.data1);
		ImGui::InputFloat4("data2", (float*)&selected.data.data2);
		ImGui::InputFloat4("data3", (float*)&selected.data.data3);
		ImGui::InputFloat4("data4", (float*)&selected.data.data4);
	}
	ImGui::End();

	if (ImGui::Begin("Settings"))
	{
		ImGui::SliderFloat("FOV", &_fov, 0.f, 180.f);
	}
	ImGui::End();


	if (ImGui::Begin("Stats"))
	{
		ImGui::Text("frametime %f s", _stats.frameTime);
		ImGui::Text("draw time %f ms", _stats.meshDrawTime);
		ImGui::Text("update time %f ms", _stats.sceneUpdateTime);
		ImGui::Text("triangles %i", _stats.triangleCount);
		ImGui::Text("draws %i", _stats.drawCallCount);
	}
	
	ImGui::End();

	ImGui::Render();
}


void Engine::CreateSwapchain(uint32_t width, uint32_t height)
{
	vkb::SwapchainBuilder builder{ _chosenGPU, _device, _surface };
	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
	VkSurfaceFormatKHR surfaceFormat{ .format=_swapchainImageFormat, .colorSpace= VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
	vkb::Swapchain vkbSwapchain = builder
		.set_desired_format(surfaceFormat)
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();
	_swapchainExtent = vkbSwapchain.extent;
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();

}

void Engine::DestroySwapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);
	for (int i = 0; i < _swapchainImageViews.size(); i++) {

		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}

void Engine::ResizeSwapchain()
{
	vkDeviceWaitIdle(_device);
	DestroySwapchain();
	int width, height;
	SDL_GetWindowSize(_window, &width, &height);
	_windowExtent.width = width;
	_windowExtent.height = height;

	CreateSwapchain(_windowExtent.width, _windowExtent.height);
	_resizeRequested = false;
}

void Engine::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = Init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = Init::CommandBufferSubmitInfo(cmd);
	VkSubmitInfo2 submit = Init::SubmitInfo(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

AllocatedBuffer Engine::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = { };
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memoryUsage;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	return newBuffer;
}

void Engine::DestroyBuffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage Engine::CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
	AllocatedImage newImage;
	newImage.imageFormat = format;
	newImage.imageExtent = size;

	VkImageCreateInfo imgInfo = Init::ImageCreateInfo(format, usage, size);
	if (mipmapped) {
		imgInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
	}

	// always allocate images on dedicated GPU memory
	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// allocate and create the image
	VK_CHECK(vmaCreateImage(_allocator, &imgInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));

	// if the format is a depth format, we will need to have it use the correct
	// aspect flag
	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT) {
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	// build a image-view for the image
	VkImageViewCreateInfo viewInfo = Init::ImageViewCreateInfo(format, newImage.image, aspectFlag);
	viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;

	VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage Engine::CreateImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
	size_t dataSize = size.depth * size.width * size.height * 4;
	AllocatedBuffer uploadbuffer = CreateBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	memcpy(uploadbuffer.info.pMappedData, data, dataSize);

	AllocatedImage newImage = CreateImage(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

	ImmediateSubmit([&](VkCommandBuffer cmd) {
		Util::TransitionImage(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = size;

		// copy the buffer into the image
		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copyRegion);
		if (mipmapped)
			Util::GenerateMipmaps(cmd, newImage.image, VkExtent2D{ newImage.imageExtent.width, newImage.imageExtent.height });
		else
			Util::TransitionImage(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	DestroyBuffer(uploadbuffer);

	return newImage;
}

void Engine::DestroyImage(const AllocatedImage& img)
{
	vkDestroyImageView(_device, img.imageView, nullptr);
	vmaDestroyImage(_allocator, img.image, img.allocation);
}

MeshBuffers Engine::UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	MeshBuffers newSurface;
	newSurface.vertexBuffer = CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	VkBufferDeviceAddressInfo deviceAddressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

	newSurface.indexBuffer = CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);


	AllocatedBuffer staging = CreateBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
	void* data = staging.allocation->GetMappedData();
	memcpy(data, vertices.data(), vertexBufferSize);
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);
	ImmediateSubmit([&](VkCommandBuffer cmd)
		{
			VkBufferCopy vertexCopy{ 0 };
			vertexCopy.dstOffset = 0;
			vertexCopy.srcOffset = 0;
			vertexCopy.size = vertexBufferSize;

			vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);
			VkBufferCopy indexCopy{ 0 };
			indexCopy.dstOffset = 0;
			indexCopy.srcOffset = vertexBufferSize;
			indexCopy.size = indexBufferSize;

			vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
		});
	DestroyBuffer(staging);
	return newSurface;
}


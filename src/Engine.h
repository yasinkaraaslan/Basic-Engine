#pragma once
#include "Mesh.h"
#include "Render.h"
#include "Camera.h"

constexpr uint32_t FRAME_OVERLAP = 2;

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void Push(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void Flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

struct FrameData {

	VkCommandPool commandPool;
	VkFence renderFence;

	VkSemaphore swapchainSemaphore, renderSemaphore;
	VkCommandBuffer mainCommandBuffer;
	
	DeletionQueue deletionQueue;
	DescriptorAllocatorGrowable descriptors;
};

struct EngineStats {
	float frameTime;
	int triangleCount;
	int drawCallCount;
	float sceneUpdateTime;
	float meshDrawTime;
};

class Engine
{
public:
	static Engine* Get();
	static const VkDevice& GetMainDevice();
	VkDevice& GetDevice() { return _device; };
	AllocatedImage& GetDrawImage() { return _drawImage; };
	AllocatedImage& GetDepthImage() { return _depthImage; };
	AllocatedImage& GetErrorImage() { return _errorCheckerboardImage; };
	AllocatedImage& GetWhiteImage() { return _whiteImage; };

	VkSampler& GetSamplerLinear() { return _defaultSamplerLinear; };
	VkSampler& GetSamplerNearest() { return _defaultSamplerNearest; };
	MetallicRougness& GetMetalMaterial() { return _metalRoughMat; };

	VkDescriptorSetLayout& GetSceneDataLayout() { return _sceneDataDescriptorLayout; };
	MeshBuffers UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
	
	AllocatedBuffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void DestroyBuffer(const AllocatedBuffer& buffer);

	AllocatedImage CreateImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void DestroyImage(const AllocatedImage& img);
	
	float GetFrameTime() { return _stats.frameTime;};
	void ShowError(const char* title, const char* message);
	void Init();
	void Run();
	void Cleanup();
private:
	void ShowSDLError();
	void InitVulkan();
	void InitSwapchain();
	void InitCommands();
	void InitSyncStructures();
	void InitDescriptors();
	void InitPipelines();
	void InitBackgroundPipelines();
	void InitImGui();
	void InitDefaultData();
	
	void Draw();
	void DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView);
	void DrawBackground(VkCommandBuffer cmd);
	void DrawGeometry(VkCommandBuffer cmd);
	void UpdateScene();
	void ProcessImGui();
	void CreateSwapchain(uint32_t width, uint32_t height);
	void DestroySwapchain();
	void ResizeSwapchain();
	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	FrameData& GetCurrentFrame() { return _frames[_frameNumber % FRAME_OVERLAP]; };
	AllocatedImage CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

	bool _isInitialized{ false };
	int _frameNumber{ 0 };
	bool _stopRendering{ false };
	bool _resizeRequested{ false };
	VkExtent2D _windowExtent{ 1700, 900 };
	struct SDL_Window* _window{ nullptr };

	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debugMessenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; 
	VkDevice _device;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	FrameData _frames[FRAME_OVERLAP];
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;
	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;
	float _renderScale = 1.f;

	DescriptorAllocatorGrowable _descriptorAllocator;
	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	std::vector<ComputeEffect> _backgroundEffects;
	int _currentBackgroundEffect{ 0 };

	SceneData _sceneData;
	VkDescriptorSetLayout _sceneDataDescriptorLayout;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	VkDescriptorSetLayout _singleImageDescriptorLayout;

	MaterialInstance _defaultData;
	MetallicRougness _metalRoughMat;

	DrawContext _drawContext;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> _loadedScenes;

	std::vector<std::shared_ptr<MeshAsset>> _testMeshes;
	Camera _camera;
	float _fov = 70.f;
	EngineStats _stats;

};
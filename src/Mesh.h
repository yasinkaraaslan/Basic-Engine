#pragma once
#include "Materials.h"

#include <unordered_map>
#include <filesystem>
struct MeshBuffers {

	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
	VkDeviceAddress vertexBufferAddress;
};

struct Bounds {
	glm::vec3 origin;
	float sphereRadius;
	glm::vec3 extents;
};

struct GeoSurface
{
	uint32_t startIndex;
	uint32_t count;
	Bounds bounds;
	std::shared_ptr<Material> material;
};


struct MeshAsset
{
	std::string name;
	std::vector<GeoSurface> surfaces;
	MeshBuffers meshBuffers;
};
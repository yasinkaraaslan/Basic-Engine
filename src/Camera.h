#pragma once
#include "Types.h"
#include <SDL_events.h>

// TODO: abstract camera away from the engine
class Camera
{
public:
	glm::mat4 GetViewMatrix();
	glm::mat4 GetRotationMatrix();

	const glm::vec3& GetVelocity() { return _velocity; };
	const glm::vec3& GetPosition() { return _position; };
	float GetPitch() { return _pitch; };
	float GetYaw() { return _yaw; };


	void SetVelocity(const glm::vec3& velocity) { _velocity = velocity; };
	void SetPosition(const glm::vec3& position) { _position = position; };
	void SetPitch(float pitch) { _pitch = pitch; };
	void SetYaw(float yaw) { _yaw = yaw; };

	void Update(float deltaTime);
	void ProcessSDLEvent(SDL_Event& e);
private:
	glm::vec3 _velocity, _position;
	float _pitch{ 0.f }, _yaw{ 0.f }, _speed{ 20.f };


};

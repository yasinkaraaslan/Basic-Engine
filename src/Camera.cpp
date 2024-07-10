#include "Camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

glm::mat4 Camera::GetViewMatrix()
{
	glm::mat4 translation = glm::translate(_position);
	glm::mat4 rotation = GetRotationMatrix();
	return glm::inverse(translation * rotation);
}

glm::mat4 Camera::GetRotationMatrix()
{
	glm::quat pitchRotation = glm::angleAxis(_pitch, glm::vec3{ 1.f, 0.f, 0.f });
	glm::quat yawRotation = glm::angleAxis(_yaw, glm::vec3{ 0.f, -1.f, 0.f });

	return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

void Camera::Update(float deltaTime)
{
	glm::mat4 cameraRotation = GetRotationMatrix();
	_position += glm::vec3(cameraRotation * deltaTime * glm::vec4(_velocity * _speed, 0.f));
}

void Camera::ProcessSDLEvent(SDL_Event& e)
{
	const auto sym = e.key.keysym.sym;
	if (e.type == SDL_KEYDOWN)
	{
		if (sym == SDLK_w) _velocity.z = -1;
		if (sym == SDLK_s) _velocity.z = 1;
		if (sym == SDLK_a) _velocity.x = -1;
		if (sym == SDLK_d) _velocity.x = 1;
		if (sym == SDLK_LSHIFT) _speed = 66.67f;
	}
	else if (e.type == SDL_KEYUP)
	{
		if (sym == SDLK_w) _velocity.z = 0;
		if (sym == SDLK_s) _velocity.z = 0;
		if (sym == SDLK_a) _velocity.x = 0;
		if (sym == SDLK_d) _velocity.x = 0;
		if (sym == SDLK_LSHIFT) _speed = 20.f;
	}
	else if (e.type == SDL_MOUSEMOTION)
	{
		_yaw += (float)e.motion.xrel / 200.f;
		_pitch -= (float)e.motion.yrel / 200.f;
	}

	if (_pitch < glm::radians(-80.f))
		_pitch = glm::radians(-80.f);

	if (_pitch > glm::radians(70.f))
		_pitch = glm::radians(70.f);
}

#include "Engine.h"
#include <SDL2/SDL.h>


int main(int argc, char* argv[])
{
	Engine engine;
	engine.Init();
	engine.Run();
	engine.Cleanup();
	return 0;
}

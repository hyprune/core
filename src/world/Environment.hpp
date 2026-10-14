#pragma once
#include "World.hpp"
namespace hyprune::world {
std::vector<CubeLevel> prefilterEnvironment(const World &, int resolution);
// World 0.11 atmosphere.panorama: resamples an equirectangular RGBA8 image (the centre looks
// along -Z, u grows towards +X, v = 0 is straight up; turned by rotation about +Y) to
// w.panorama at maxResolution per face. With environment (default true) and no
// atmosphere.environment, 128 px copies become the environment faces that light the world.
void resamplePanorama(World &w, const unsigned char *rgba, int width, int height);
}

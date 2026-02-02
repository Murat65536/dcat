#pragma once

#include "model.hpp"
#include <vector>
#include <glm/glm.hpp>

// Generate a skydome mesh (inverted sphere)
Mesh generateSkydome(float radius = 100.0f, int segments = 32, int rings = 16);

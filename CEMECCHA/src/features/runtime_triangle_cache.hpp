#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../sdk/ue_types.hpp"

namespace ce::features::runtime_cache
{
    struct RuntimeTriangle
    {
        ce::ue::FVector world[3];
        ce::ue::FVector local[3];
        ce::ue::FVector2D uv[3];
    };

    struct Cache
    {
        bool ok = false;
        std::uintptr_t data = 0;
        int owner_offset = -1;
        int stride = 208;
        int triangle_count = 0;
        double profile_uv_avg_error = 0.0;
        std::string failure = "not_run";
        std::vector<RuntimeTriangle> triangles;
    };

    Cache resolve(std::uintptr_t component);

    bool refresh(std::uintptr_t component, Cache& cache);

    bool read_triangle(std::uintptr_t triangle_base, RuntimeTriangle& out);
}

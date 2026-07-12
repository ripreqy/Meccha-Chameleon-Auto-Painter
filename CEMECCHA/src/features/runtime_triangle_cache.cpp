#include "runtime_triangle_cache.hpp"

#include "../core/logger.hpp"
#include "../sdk/mesh_profile_paintman.hpp"

#include <Windows.h>
#include <cmath>
#include <cstring>
#include <limits>

namespace ce::features::runtime_cache
{
    namespace
    {

        template <typename T> T safe_read(std::uintptr_t addr, T fallback = T{})
        {
            __try
            {
                return *reinterpret_cast<const T*>(addr);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return fallback;
            }
        }

        bool finite_vec(const ce::ue::FVector& v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
        }
        bool finite_uv(const ce::ue::FVector2D& v)
        {
            return std::isfinite(v.X) && std::isfinite(v.Y);
        }

        ce::ue::FVector vec_sub(const ce::ue::FVector& a, const ce::ue::FVector& b)
        {
            return { a.X - b.X, a.Y - b.Y, a.Z - b.Z };
        }
        ce::ue::FVector vec_cross(const ce::ue::FVector& a, const ce::ue::FVector& b)
        {
            return { a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X };
        }
        double vec_len(const ce::ue::FVector& v)
        {
            return std::sqrt(v.X*v.X + v.Y*v.Y + v.Z*v.Z);
        }

        double triangle_uv_error_vs_profile(int triangle_index, const RuntimeTriangle& tri)
        {
            if (triangle_index < 0 || triangle_index >= ce::ue::chameleon::paintman::kTriangleCount)
                return 1e6;
            const auto& pinfo = ce::ue::chameleon::paintman::kTriangles[triangle_index];
            const double avg_u = (tri.uv[0].X + tri.uv[1].X + tri.uv[2].X) / 3.0;
            const double avg_v = (tri.uv[0].Y + tri.uv[1].Y + tri.uv[2].Y) / 3.0;
            return std::abs(pinfo.CenterU - avg_u) + std::abs(pinfo.CenterV - avg_v);
        }
    }

    bool read_triangle(std::uintptr_t base, RuntimeTriangle& out)
    {
        constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
        for (int i = 0; i < 3; ++i)
        {
            const auto wb = base + static_cast<std::uintptr_t>(i * 24);
            out.world[i].X = safe_read<double>(wb + 0, NaN);
            out.world[i].Y = safe_read<double>(wb + 8, NaN);
            out.world[i].Z = safe_read<double>(wb + 16, NaN);

            const auto lb = base + 72 + static_cast<std::uintptr_t>(i * 24);
            out.local[i].X = safe_read<double>(lb + 0, NaN);
            out.local[i].Y = safe_read<double>(lb + 8, NaN);
            out.local[i].Z = safe_read<double>(lb + 16, NaN);

            const auto ub = base + 144 + static_cast<std::uintptr_t>(i * 16);
            out.uv[i].X = safe_read<double>(ub + 0, NaN);
            out.uv[i].Y = safe_read<double>(ub + 8, NaN);

            if (!finite_vec(out.world[i]) || !finite_vec(out.local[i]) || !finite_uv(out.uv[i]))
                return false;
        }
        const auto edge0 = vec_sub(out.world[1], out.world[0]);
        const auto edge1 = vec_sub(out.world[2], out.world[0]);
        return vec_len(vec_cross(edge0, edge1)) > 0.000001;
    }

    Cache resolve(std::uintptr_t component)
    {
        Cache out{};
        constexpr int kStride = 208;
        constexpr int kExpectedTriangles = ce::ue::chameleon::paintman::kTriangleCount;
        constexpr int kValidateCount = 96;

        if (!component)
        {
            out.failure = "runtime_triangle_cache_invalid_component";
            return out;
        }

        double best_error = 1e6;
        int best_offset = -1;
        std::uintptr_t best_data = 0;
        std::vector<RuntimeTriangle> best_triangles{};

        for (int offset = 0; offset + 16 <= 0x3000; offset += 8)
        {
            const auto data = safe_read<std::uintptr_t>(component + static_cast<std::uintptr_t>(offset), 0);
            const auto num = safe_read<int> (component + static_cast<std::uintptr_t>(offset + 8), 0);
            const auto max = safe_read<int> (component + static_cast<std::uintptr_t>(offset + 12), 0);

            if (!data || num != kExpectedTriangles) continue;
            if (max < num || max > num + std::max(32, num / 2)) continue;

            if (data < 0x00010000ULL || data >= 0x00007FFF'FFFFFFFFULL) continue;

            std::vector<RuntimeTriangle> triangles(static_cast<size_t>(num));
            double uv_error_sum = 0.0;
            int checked = 0;
            bool valid = true;
            const int check_count = std::min(num, kValidateCount);

            for (int i = 0; i < check_count; ++i)
            {
                RuntimeTriangle tri{};
                if (!read_triangle(data + static_cast<std::uintptr_t>(i) * kStride, tri))
                {
                    valid = false;
                    break;
                }
                const double err = triangle_uv_error_vs_profile(i, tri);
                if (!std::isfinite(err) || err > 0.02)
                {
                    valid = false;
                    break;
                }
                uv_error_sum += err;
                triangles[static_cast<size_t>(i)] = tri;
                ++checked;
            }
            if (!valid || checked <= 0) continue;

            
            for (int i = checked; i < num; ++i)
            {
                RuntimeTriangle tri{};
                if (!read_triangle(data + static_cast<std::uintptr_t>(i) * kStride, tri))
                {
                    valid = false;
                    break;
                }
                triangles[static_cast<size_t>(i)] = tri;
            }
            if (!valid) continue;

            const double avg_error = uv_error_sum / static_cast<double>(checked);
            if (avg_error < best_error)
            {
                best_error = avg_error;
                best_offset = offset;
                best_data = data;
                best_triangles = std::move(triangles);

                ce::log::info("[rtcache][DBG] candidate offset=0x%04X data=%p num=%d max=%d avg_err=%.5f", offset, (void*)data, num, max, avg_error);
            }
        }

        if (best_offset < 0 || best_triangles.empty())
        {
            out.failure = "runtime_triangle_cache_unavailable";
            return out;
        }
        out.ok = true;
        out.failure.clear();
        out.owner_offset = best_offset;
        out.data = best_data;
        out.stride = kStride;
        out.triangle_count = static_cast<int>(best_triangles.size());
        out.profile_uv_avg_error = best_error;
        out.triangles = std::move(best_triangles);
        return out;
    }

    bool refresh(std::uintptr_t component, Cache& cache)
    {
        if (!cache.ok || !component || cache.owner_offset < 0) return false;

        
        const auto data = safe_read<std::uintptr_t>(component + static_cast<std::uintptr_t>(cache.owner_offset), 0);
        const auto num = safe_read<int> (component + static_cast<std::uintptr_t>(cache.owner_offset + 8), 0);
        if (!data || num != static_cast<int>(cache.triangles.size())) return false;
        cache.data = data;

        for (int i = 0; i < num; ++i)
        {
            if (!read_triangle(data + static_cast<std::uintptr_t>(i) * cache.stride, cache.triangles[static_cast<size_t>(i)]))
                return false;
        }
        return true;
    }
}

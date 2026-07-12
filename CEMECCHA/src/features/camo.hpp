#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include "../sdk/chameleon.hpp"
#include "runtime_triangle_cache.hpp"

namespace ce::features
{

    class CamoCapture
    {
    public:
        struct Config
        {
            int32_t downsample = 1;
        };

        bool capture_back_buffer();

        bool capture_base_color(ce::ue::chameleon::cLeonCharacter chara);

        bool capture_scene_base_color(ce::ue::chameleon::cLeonCharacter chara);

        void teardown_owned_capture();

        bool has_pixels() const { return !pixels_.empty(); }

        ce::ue::FLinearColor sample_uv(float u, float v) const;
        ce::ue::FLinearColor sample_screen(int sx, int sy, int screen_w, int screen_h) const;

        ce::ue::FLinearColor sample_box_avg(int sx, int sy, int k) const;

        ce::ue::FLinearColor sample_region_avg(float u0, float v0, float u1, float v1) const;

        ce::ue::FLinearColor sample_pixel(int x, int y) const;

        const ce::ue::chameleon::CameraSnapshot& capture_view() const { return capture_view_; }
        bool capture_view_valid() const { return capture_view_.valid; }

        const ce::features::runtime_cache::Cache& frozen_cache() const { return frozen_cache_; }

        const ce::ue::chameleon::FTransform& frozen_mesh_ctw() const { return frozen_mesh_ctw_; }
        bool frozen_mesh_ctw_valid() const { return frozen_mesh_ctw_valid_; }

        uint64_t capture_seq() const { return capture_seq_.load(); }

        void set_capture_pose_override(bool active, ce::ue::FVector location, ce::ue::FRotator rotation, float fov_deg);

        Config& config() { return cfg_; }
        const Config& config() const { return cfg_; }
        int32_t width() const { return w_; }
        int32_t height() const { return h_; }

        void teardown();

    private:
        Config cfg_{};
        mutable std::mutex mtx_;
        std::vector<uint32_t> pixels_;
        int32_t w_ = 0;
        int32_t h_ = 0;
        uint32_t fmt_ = 0;

        ce::ue::UObject* owned_capture_actor_ = nullptr;
        ce::ue::UObject* owned_capture_component_= nullptr;
        ce::ue::UObject* owned_render_target_ = nullptr;
        int32_t owned_rt_size_ = 1024;
        std::atomic<bool> spawn_in_progress_{ false };

        uintptr_t last_world_ = 0;

        ce::ue::chameleon::CameraSnapshot capture_view_{};

        struct PoseOverride {
            bool active = false;
            ce::ue::FVector location{};
            ce::ue::FRotator rotation{};
            float fov_deg = 90.0f;
        };
        PoseOverride pose_override_{};

        ce::features::runtime_cache::Cache frozen_cache_{};
        ce::ue::chameleon::FTransform frozen_mesh_ctw_{};
        bool frozen_mesh_ctw_valid_ = false;
        std::atomic<uint64_t> capture_seq_{ 0 };
    };
}

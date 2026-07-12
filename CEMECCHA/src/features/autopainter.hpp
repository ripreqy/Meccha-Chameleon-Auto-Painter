#pragma once

#include "feature.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <Windows.h>

#include "../sdk/chameleon.hpp"
#include "../sdk/mesh_profile_paintman.hpp"
#include "camo.hpp"
#include "runtime_triangle_cache.hpp"

namespace ce::features
{

    class AutoPainter final : public Feature
    {
    public:
        enum class Preset { Turbo, Fast, Balanced, Detailed, Extreme };

        struct Config
        {
            bool enabled = true;
            int hotkey_vk = VK_F1;

            float brush_radius = 4.0f / 1024.0f;
            float brush_hardness = 1.0f;
            float brush_opacity = 1.0f;
            float brush_spacing = 1.0f;

            bool burst_mode = false;
            int strokes_per_frame = 256;

            float coverage_step_texels = 4.0f;

            float z_band_world_units = 2.0f;

            bool force_paint_mode_toggle = false;

            bool multiplayer_sync = true;
            int mp_flush_chunk = 500;

            Preset preset = Preset::Balanced;
        };

        struct Status
        {
            bool running = false;
            int strokes_done = 0;
            int strokes_total = 0;
            char last_error[128] = {};
        };

        AutoPainter();
        ~AutoPainter() override;

        std::string_view name() const override { return "AutoPainter"; }
        void tick() override;
        void tick_body();
        void draw() override;

        void start();
        void stop();
        bool is_running() const { return status_.load().running; }
        void apply_preset(Preset p);

        Config& config() { return cfg_; }
        Status status() { return status_.load(); }

    private:

        struct PlanSample
        {
            int32_t triangle_index;
            float bary_a, bary_b, bary_c;
            float uv_u, uv_v;
            float world_z;
            float world_x;
            float local_x;

        };

        struct Job
        {
            ce::ue::chameleon::cLeonCharacter chara{};
            ce::ue::chameleon::RuntimePaintable rp{};

            int next_idx = 0;
            int total = 0;
            int last_flush_at = 0;
            bool prev_auto_record = true;
            bool prev_auto_flush = true;
            bool prev_paint_mode = false;

            ce::ue::chameleon::FTransform mesh_ctw{};
            bool has_mesh_ctw = false;

            ce::ue::chameleon::FRuntimeBrushSettings brush{};

            std::vector<PlanSample> samples;
        };

        bool build_job(Job& out, char (&out_err)[128]);
        void drain(int max_strokes);
        void finish_job();

        Config cfg_{};
        std::atomic<Status> status_{ Status{} };

        std::mutex job_mtx_;
        Job job_{};
        bool have_job_ = false;
        bool hotkey_prev_ = false;

        bool pending_paint_ = false;
        uint64_t last_seen_seq_ = 0;
        ULONGLONG pending_started_ms_ = 0;

        ULONGLONG last_bg_capture_ms_ = 0;
        ULONGLONG last_pending_retry_ms_ = 0;

        bool paint_mode_snapshot_taken_ = false;
        bool paint_mode_snapshot_ = false;

        CamoCapture camo_{};

        uintptr_t last_world_ = 0;
    };
}

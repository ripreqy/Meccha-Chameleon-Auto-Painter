#include "autopainter.hpp"

#include "../core/logger.hpp"
#include "../sdk/ue_object.hpp"
#include "../../third-party/imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace ce::features
{
    using namespace ce::ue::chameleon;
    using ce::ue::FVector2D;
    using ce::ue::UObject;

    static constexpr float kRoughness = 0.65f;
    static constexpr float kMetallic = 0.0f;

    AutoPainter::AutoPainter() { status_.store(Status{}); }
    AutoPainter::~AutoPainter() { stop(); }

    void AutoPainter::apply_preset(Preset p)
    {

        cfg_.preset = p;
        switch (p)
        {

            case Preset::Turbo:
                cfg_.brush_radius = 16.0f / 1024.0f;
                cfg_.coverage_step_texels = 16.0f;
                cfg_.strokes_per_frame = 4096;
                cfg_.burst_mode = false;

                break;
            case Preset::Fast:
                cfg_.brush_radius = 10.0f / 1024.0f;
                cfg_.coverage_step_texels = 10.0f;
                cfg_.strokes_per_frame = 320;
                cfg_.burst_mode = false;
                break;
            case Preset::Balanced:
                cfg_.brush_radius = 4.0f / 1024.0f;
                cfg_.coverage_step_texels = 4.0f;
                cfg_.strokes_per_frame = 256;
                cfg_.burst_mode = false;
                break;
            case Preset::Detailed:
                cfg_.brush_radius = 2.0f / 1024.0f;
                cfg_.coverage_step_texels = 2.0f;
                cfg_.strokes_per_frame = 160;
                cfg_.burst_mode = false;
                break;
            case Preset::Extreme:
                cfg_.brush_radius = 1.0f / 1024.0f;
                cfg_.coverage_step_texels = 1.0f;
                cfg_.strokes_per_frame = 96;
                cfg_.burst_mode = false;
                break;
        }
    }

    static bool tick_impl_seh(AutoPainter* self);
    void AutoPainter::tick()
    {
        __try { (void)tick_impl_seh(this); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            ce::log::warn("[paint] tick threw SEH — job aborted, next F1 rebuilds cleanly");

            have_job_ = false;
            pending_paint_ = false;
            paint_mode_snapshot_taken_ = false;
            Status s = status_.load();
            s.running = false;
            std::strncpy(s.last_error, "SEH in paint tick — reset", sizeof(s.last_error) - 1);
            status_.store(s);
        }
    }

    static bool tick_impl_seh(AutoPainter* self)
    {
        self->tick_body();
        return true;
    }

    void AutoPainter::tick_body()
    {
        if (!cfg_.enabled) return;

        const ULONGLONG now_ms = GetTickCount64();
        {
            const uintptr_t cur_world = reinterpret_cast<uintptr_t>(ce::ue::gworld());
            if (last_world_ && cur_world != last_world_)
            {
                if (have_job_)
                {
                    ce::log::warn("[paint] GWorld changed mid-paint — aborting job (no cleanup)");
                    std::lock_guard<std::mutex> lk(job_mtx_);

                    have_job_ = false;
                    Status s = status_.load();
                    s.running = false;
                    std::strncpy(s.last_error, "match transition — job dropped", sizeof(s.last_error) - 1);
                    status_.store(s);
                }
                pending_paint_ = false;
                paint_mode_snapshot_taken_ = false;
                job_ = Job{};
            }
            last_world_ = cur_world;
        }

        const bool down = (GetAsyncKeyState(cfg_.hotkey_vk) & 0x8000) != 0;
        const bool prev = hotkey_prev_;
        hotkey_prev_ = down;
        if (down && !prev)
        {
            if (is_running())
            {
                stop();
                pending_paint_ = false;
            }

            else if (pending_paint_)
            {
                pending_paint_ = false;
                if (paint_mode_snapshot_taken_ && cfg_.force_paint_mode_toggle)
                {
                    if (auto chara = local_leon_character(); chara)
                        chara.force_paint_mode(paint_mode_snapshot_);
                }
                paint_mode_snapshot_taken_ = false;
                ce::log::info("[paint] F1 cancelled pending — mouse restored");
            }
            else
            {

                pending_paint_ = true;
                last_seen_seq_ = camo_.capture_seq();
                pending_started_ms_ = now_ms;
                last_pending_retry_ms_ = 0;
                if (auto chara = local_leon_character(); chara)
                {

                    if (cfg_.force_paint_mode_toggle && !paint_mode_snapshot_taken_)
                    {
                        paint_mode_snapshot_ = chara.is_paint_mode();
                        paint_mode_snapshot_taken_ = true;
                    }
                    if (cfg_.force_paint_mode_toggle)
                        chara.force_paint_mode(true);
                    ce::log::info("[paint] F1 → pending (waiting for capture…)");
                }
                else
                {
                    ce::log::info("[paint] F1 → pending (waiting for cLeon character…)");
                }
            }
        }

        if (pending_paint_ && !have_job_)
        {
            const uint64_t seq_now = camo_.capture_seq();
            if (seq_now > last_seen_seq_ && camo_.frozen_cache().ok && camo_.frozen_mesh_ctw_valid())
            {
                pending_paint_ = false;
                start();
            }
            else if (now_ms - last_pending_retry_ms_ > 250)
            {
                if (auto chara = local_leon_character(); chara)
                {

                    if (cfg_.force_paint_mode_toggle && !paint_mode_snapshot_taken_)
                    {
                        paint_mode_snapshot_ = chara.is_paint_mode();
                        paint_mode_snapshot_taken_ = true;
                    }
                    if (cfg_.force_paint_mode_toggle)
                        chara.force_paint_mode(true);
                    camo_.set_capture_pose_override(false, {}, {}, 0);
                    (void)camo_.capture_scene_base_color(chara);
                }
                last_pending_retry_ms_ = now_ms;
            }

            if (now_ms - pending_started_ms_ > 30000)
            {
                ce::log::warn("[paint] pending timed out after 30s — press F1 again");
                pending_paint_ = false;
                paint_mode_snapshot_taken_ = false;
            }
        }

        if (have_job_)
        {

            if (!job_.chara || !job_.chara.obj)
            {
                ce::log::warn("[paint] character disappeared mid-paint — aborting");
                std::lock_guard<std::mutex> lk(job_mtx_);
                have_job_ = false;
                Status s = status_.load();
                s.running = false;
                std::strncpy(s.last_error, "character destroyed mid-paint", sizeof(s.last_error) - 1);
                status_.store(s);
            }
            else
            {
                const int per_tick = cfg_.burst_mode ? (job_.total - job_.next_idx) : cfg_.strokes_per_frame;
                drain(per_tick);
            }
        }
    }

    void AutoPainter::start()
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        if (have_job_) { ce::log::warn("AutoPainter: job already running"); return; }
        Job j{};
        char err[128] = {};
        if (!build_job(j, err))
        {
            Status s{}; std::strncpy(s.last_error, err, sizeof(s.last_error) - 1);
            status_.store(s);
            ce::log::err("AutoPainter start failed: %s", err);
            return;
        }
        job_ = j;
        have_job_ = true;
        Status s{};
        s.running = true;
        s.strokes_total = job_.total;
        status_.store(s);
        ce::log::info("[paint] job started: %d triangles brush=%.3f", job_.total, job_.brush.Radius);
    }

    void AutoPainter::stop()
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        if (have_job_) finish_job();
    }

    bool AutoPainter::build_job(Job& out, char (&out_err)[128])
    {
        out.chara = local_leon_character();
        if (!out.chara) { std::strncpy(out_err, "no local cLeon character", sizeof(out_err) - 1); return false; }

        out.rp = out.chara.runtime_paintable();
        if (!out.rp) { std::strncpy(out_err, "RuntimePaintable missing", sizeof(out_err) - 1); return false; }

        out.prev_auto_record = false;
        out.prev_auto_flush = false;
        if (cfg_.multiplayer_sync)
        {
            out.rp.set_auto_record(true);
            out.rp.set_auto_flush(false);
            out.rp.ensure_server_batch_reliable();
        }

        if (cfg_.force_paint_mode_toggle)
        {
            if (paint_mode_snapshot_taken_)
            {
                out.prev_paint_mode = paint_mode_snapshot_;
            }
            else
            {
                out.prev_paint_mode = out.chara.is_paint_mode();
                paint_mode_snapshot_ = out.prev_paint_mode;
                paint_mode_snapshot_taken_ = true;
            }
            out.chara.force_paint_mode(true);
        }
        else
        {
            out.prev_paint_mode = out.chara.is_paint_mode();
        }

        out.brush.Radius = cfg_.brush_radius;
        out.brush.Hardness = cfg_.brush_hardness;
        out.brush.Opacity = cfg_.brush_opacity;
        out.brush.Spacing = cfg_.brush_spacing;
        out.brush.Falloff = EBrushFalloff::Round;
        out.brush.BlendMode = EPaintBlendMode::Normal;
        out.brush.BrushTexture = nullptr;
        out.brush.Rotation = 0.0f;

        camo_.set_capture_pose_override(false, {}, {}, 0);
        bool got = camo_.capture_scene_base_color(out.chara);
        if (!got)
        {

            std::strncpy(out_err, "SceneCapture spawn queued — press paint again", sizeof(out_err) - 1);
            return false;
        }

        const auto& fc = camo_.frozen_cache();
        if (!fc.ok || fc.triangle_count <= 0)
        {
            std::strncpy(out_err, "runtime cache not resolved yet — press paint again", sizeof(out_err) - 1);
            ce::log::err("[paint] %s", out_err);
            return false;
        }

        if (camo_.frozen_mesh_ctw_valid())
        {
            out.mesh_ctw = camo_.frozen_mesh_ctw();
            out.has_mesh_ctw = true;
            ce::log::info("[paint][DBG] mesh_ctw (FROZEN): T=(%.1f,%.1f,%.1f) Q=(%.3f,%.3f,%.3f,%.3f) S=(%.2f,%.2f,%.2f)", out.mesh_ctw.Translation.X, out.mesh_ctw.Translation.Y, out.mesh_ctw.Translation.Z, out.mesh_ctw.Rotation.X, out.mesh_ctw.Rotation.Y, out.mesh_ctw.Rotation.Z, out.mesh_ctw.Rotation.W, out.mesh_ctw.Scale3D.X, out.mesh_ctw.Scale3D.Y, out.mesh_ctw.Scale3D.Z);
        }
        else
        {
            ce::log::warn("[paint] no frozen mesh_ctw — projection will use raw cache world[]");
        }

        {
            const float texture_size = static_cast<float>(ce::ue::chameleon::paintman::kTextureSize);
            const float step_uv = std::max(1.0f, cfg_.coverage_step_texels) / texture_size;

            out.samples.reserve(static_cast<size_t>(fc.triangle_count) * 6);

            for (int i = 0; i < fc.triangle_count; ++i)
            {
                const auto& rt = fc.triangles[static_cast<size_t>(i)];
                const double u0 = rt.uv[0].X, v0 = rt.uv[0].Y;
                const double u1 = rt.uv[1].X, v1 = rt.uv[1].Y;
                const double u2 = rt.uv[2].X, v2 = rt.uv[2].Y;
                const double denom = ((v1 - v2) * (u0 - u2)) + ((u2 - u1) * (v0 - v2));
                if (!std::isfinite(denom) || std::abs(denom) <= 1e-12) continue;

                const double min_u = std::max(0.0, std::min({u0, u1, u2}));
                const double max_u = std::min(1.0, std::max({u0, u1, u2}));
                const double min_v = std::max(0.0, std::min({v0, v1, v2}));
                const double max_v = std::min(1.0, std::max({v0, v1, v2}));
                const double start_u = std::floor(min_u / step_uv) * step_uv + step_uv * 0.5;
                const double start_v = std::floor(min_v / step_uv) * step_uv + step_uv * 0.5;

                float tri_x_sum = 0.f, tri_z_sum = 0.f, tri_local_x_sum = 0.f;
                {
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto w = out.has_mesh_ctw ? ce::ue::chameleon::transform_apply_point(out.mesh_ctw, rt.local[k]) : rt.world[k];
                        tri_x_sum += static_cast<float>(w.X);
                        tri_z_sum += static_cast<float>(w.Z);
                        tri_local_x_sum += static_cast<float>(rt.local[k].X);
                    }
                }
                const float avg_x = tri_x_sum / 3.f;
                const float avg_z = tri_z_sum / 3.f;
                const float avg_local_x = tri_local_x_sum / 3.f;

                int emitted = 0;
                for (double v = start_v; v <= max_v + step_uv * 0.25; v += step_uv)
                {
                    for (double u = start_u; u <= max_u + step_uv * 0.25; u += step_uv)
                    {
                        const double a = (((v1 - v2) * (u - u2)) + ((u2 - u1) * (v - v2))) / denom;
                        const double b = (((v2 - v0) * (u - u2)) + ((u0 - u2) * (v - v2))) / denom;
                        const double c = 1.0 - a - b;
                        constexpr double kEps = -1e-5;
                        if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(c) || a < kEps || b < kEps || c < kEps)
                            continue;

                        PlanSample s{};
                        s.triangle_index = i;
                        s.bary_a = static_cast<float>(a);
                        s.bary_b = static_cast<float>(b);
                        s.bary_c = static_cast<float>(c);
                        s.uv_u = static_cast<float>(u);
                        s.uv_v = static_cast<float>(v);
                        s.world_z = avg_z;
                        s.world_x = avg_x;
                        s.local_x = avg_local_x;
                        out.samples.push_back(s);
                        ++emitted;
                    }
                }

                if (emitted == 0)
                {
                    PlanSample s{};
                    s.triangle_index = i;
                    s.bary_a = s.bary_b = s.bary_c = 1.0f / 3.0f;
                    s.uv_u = static_cast<float>((u0 + u1 + u2) / 3.0);
                    s.uv_v = static_cast<float>((v0 + v1 + v2) / 3.0);
                    s.world_z = avg_z;
                    s.world_x = avg_x;
                    s.local_x = avg_local_x;
                    out.samples.push_back(s);
                }
            }

            const float band = std::max(0.1f, cfg_.z_band_world_units);
            std::sort(out.samples.begin(), out.samples.end(), [band](const PlanSample& a, const PlanSample& b) { const int side_a = a.local_x < 0.f ? 0 : 1; const int side_b = b.local_x < 0.f ? 0 : 1; if (side_a != side_b) return side_a < side_b; const int band_a = static_cast<int>(std::floor(a.world_z / band)); const int band_b = static_cast<int>(std::floor(b.world_z / band)); if (band_a != band_b) return band_a > band_b; return a.world_x < b.world_x; });

            ce::log::info("[paint] plan: %zu samples across %d triangles (step=%.4f UV = %.1f texels)", out.samples.size(), fc.triangle_count, step_uv, cfg_.coverage_step_texels);
        }

        out.total = static_cast<int>(out.samples.size());
        out.next_idx = 0;
        return true;
    }

    namespace
    {
        struct ProjectResult
        {
            bool visible = false;
            int px = 0;
            int py = 0;
            float depth = 0.f;
        };

        ProjectResult project_world_to_capture(const CameraSnapshot& snap, int rt_width, int rt_height, const ce::ue::FVector& world)
        {
            ProjectResult r{};
            if (!snap.valid || rt_width <= 0 || rt_height <= 0) return r;

            const ce::ue::FVector rel { world.X - snap.location.X, world.Y - snap.location.Y, world.Z - snap.location.Z };

            const double depth = rel.X * snap.forward.X + rel.Y * snap.forward.Y + rel.Z * snap.forward.Z;
            if (!std::isfinite(depth) || depth <= 0.000001) return r;
            r.depth = static_cast<float>(depth);

            const double right = rel.X * snap.right.X + rel.Y * snap.right.Y + rel.Z * snap.right.Z;
            const double up = rel.X * snap.up.X + rel.Y * snap.up.Y + rel.Z * snap.up.Z;

            constexpr double PI = 3.14159265358979323846;
            const double half_fov_rad = snap.fov_deg * PI / 360.0;
            const double tan_h = std::tan(half_fov_rad);
            const double tan_v = tan_h / std::max(0.001, static_cast<double>(snap.aspect));

            const double ndc_x = right / (depth * tan_h);
            const double ndc_y = up / (depth * tan_v);

            const double sx = (ndc_x * 0.5 + 0.5) * static_cast<double>(rt_width);
            const double sy = (0.5 - ndc_y * 0.5) * static_cast<double>(rt_height);
            if (sx < 0 || sy < 0 || sx >= rt_width || sy >= rt_height) return r;

            r.px = std::clamp(static_cast<int>(std::round(sx)), 0, rt_width - 1);
            r.py = std::clamp(static_cast<int>(std::round(sy)), 0, rt_height - 1);
            r.visible = true;
            return r;
        }
    }

    void AutoPainter::drain(int max_strokes)
    {
        std::lock_guard<std::mutex> lk(job_mtx_);
        if (!have_job_) return;

        if (!job_.rp || !job_.rp.obj)
        {
            ce::log::warn("[paint] RP disappeared mid-paint — aborting job");
            have_job_ = false;
            Status s = status_.load();
            s.running = false;
            std::strncpy(s.last_error, "RuntimePaintable destroyed mid-paint", sizeof(s.last_error) - 1);
            status_.store(s);
            return;
        }

        const int end_idx = std::min(job_.total, job_.next_idx + max_strokes);
        const auto snap = camo_.capture_view();
        const int rt_w = camo_.width();
        const int rt_h = camo_.height();
        const auto& fc = camo_.frozen_cache();

        int visible = 0, missed = 0;

        for (int i = job_.next_idx; i < end_idx; ++i)
        {
            if (i >= static_cast<int>(job_.samples.size())) break;
            const auto& s = job_.samples[static_cast<size_t>(i)];
            if (s.triangle_index >= fc.triangle_count) continue;
            const auto& rt = fc.triangles[static_cast<size_t>(s.triangle_index)];

            ce::ue::FVector w0, w1, w2;
            if (job_.has_mesh_ctw)
            {
                w0 = ce::ue::chameleon::transform_apply_point(job_.mesh_ctw, rt.local[0]);
                w1 = ce::ue::chameleon::transform_apply_point(job_.mesh_ctw, rt.local[1]);
                w2 = ce::ue::chameleon::transform_apply_point(job_.mesh_ctw, rt.local[2]);
            }
            else
            {
                w0 = rt.world[0]; w1 = rt.world[1]; w2 = rt.world[2];
            }
            const ce::ue::FVector world {
                s.bary_a * w0.X + s.bary_b * w1.X + s.bary_c * w2.X, s.bary_a * w0.Y + s.bary_b * w1.Y + s.bary_c * w2.Y, s.bary_a * w0.Z + s.bary_b * w1.Z + s.bary_c * w2.Z
            };

            const auto proj = project_world_to_capture(snap, rt_w, rt_h, world);

            FPaintChannelData ch{};
            ch.Metallic = kMetallic;
            ch.Roughness = kRoughness;
            ch.Height = 0.0f;
            ch.ApplyMode = EPaintChannelApplyMode::Override;

            if (proj.visible)
            {
                ch.AlbedoColor = camo_.sample_pixel(proj.px, proj.py);
                ++visible;
            }
            else
            {
                ++missed;
                continue;
            }

            job_.rp.paint_at_uv_with_brush({ s.uv_u, s.uv_v }, ch, job_.brush, EPaintChannel::Albedo);
        }

        if (job_.next_idx == 0)
        {
            ce::log::info("[paint] first-drain: %d visible / %d off-screen  samples=%d", visible, missed, job_.total);
        }

        job_.next_idx = end_idx;

        Status s = status_.load();
        s.strokes_done = job_.next_idx;
        status_.store(s);

        if (job_.next_idx >= job_.total)
        {
            ce::log::info("[paint] job done: %d triangles", job_.next_idx);
            finish_job();
        }
    }

    void AutoPainter::finish_job()
    {

        if (job_.rp && cfg_.multiplayer_sync)
        {
            job_.rp.set_auto_flush(false);
            job_.rp.flush_recorded_strokes_to_server();
            job_.rp.set_auto_record(false);
        }

        if (cfg_.force_paint_mode_toggle && job_.chara)
            job_.chara.force_paint_mode(job_.prev_paint_mode);

        paint_mode_snapshot_taken_ = false;

        have_job_ = false;
        Status s = status_.load();
        s.running = false;
        status_.store(s);
    }

    void AutoPainter::draw()
    {
        Status s = status_.load();

        ImGui::TextColored(ImVec4(0.f, 1.f, 0.4f, 1.f), "Auto Painter");
        ImGui::Separator();

        ImGui::Checkbox("Enabled", &cfg_.enabled);
        ImGui::InputInt("Hotkey (VK)", &cfg_.hotkey_vk);

        static const char* preset_names[] { "Turbo", "Fast", "Balanced", "Detailed", "Extreme" };
        int preset_idx = static_cast<int>(cfg_.preset);
        if (ImGui::Combo("Preset", &preset_idx, preset_names, IM_ARRAYSIZE(preset_names)))
            apply_preset(static_cast<Preset>(preset_idx));

        ImGui::Separator();
        ImGui::SliderFloat("Brush radius", &cfg_.brush_radius, 0.0005f, 0.02f, "%.5f");
        ImGui::SliderFloat("Brush hardness", &cfg_.brush_hardness, 0.0f, 1.0f);
        ImGui::SliderFloat("Brush opacity", &cfg_.brush_opacity, 0.0f, 1.0f);
        ImGui::SliderFloat("Coverage step (texels)", &cfg_.coverage_step_texels, 0.5f, 24.0f, "%.1f");
        ImGui::SliderFloat("Line height (world units)", &cfg_.z_band_world_units, 0.5f, 10.0f, "%.1f");
        ImGui::Checkbox ("Burst mode (WARNING: freezes render thread ~1s)", &cfg_.burst_mode);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip( "Runs the entire paint in one render-thread call.\n" "During the freeze your mouse input queues up in Windows\n" "and gets flushed as a giant jump when frames resume —\n" "causing the weird post-paint movement.\n" "Leave OFF and crank Strokes/frame instead for fast+smooth.");

        if (cfg_.burst_mode) ImGui::BeginDisabled();
        ImGui::SliderInt("Strokes/frame", &cfg_.strokes_per_frame, 1, 4096, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        if (cfg_.strokes_per_frame < 1) cfg_.strokes_per_frame = 1;
        if (cfg_.strokes_per_frame > 4096) cfg_.strokes_per_frame = 4096;
        if (cfg_.burst_mode) ImGui::EndDisabled();
        ImGui::TextDisabled("1 = crawl. 256 = ~15ms/frame. 2048+ = tiny stutter. Burst = ignore this.");

        ImGui::Checkbox("Force paint mode (may lock movement after)", &cfg_.force_paint_mode_toggle);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip( "Directly writes cLeon::IsPaintMode = true during the paint.\n" "Only needed if paint fails with 'runtime cache not resolved'.\n" "Off (default) = movement stays free after painting.");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.f, 0.7f, 0.f, 1.f), "Multiplayer");
        ImGui::Checkbox("Sync paint to server (Hunters see it)", &cfg_.multiplayer_sync);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip( "ON (default) — flushes the entire stroke batch to the\n" "game's ServerPaintBatch RPC once at end of job so\n" "Hunters see your camouflage.\n" "\n" "OFF — paint is purely local. Nobody else sees it.");

        ImGui::Separator();
        if (s.running)
        {
            if (ImGui::Button("Stop paint")) stop();
            const float pct = s.strokes_total ? static_cast<float>(s.strokes_done) / static_cast<float>(s.strokes_total) : 0.f;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d / %d triangles", s.strokes_done, s.strokes_total);
            ImGui::ProgressBar(pct, ImVec2(-1.f, 0.f), buf);
        }
        else
        {
            if (ImGui::Button("Paint now")) start();
        }

        if (s.last_error[0])
            ImGui::TextColored(ImVec4(1.f, 0.3f, 0.3f, 1.f), "Error: %s", s.last_error);
    }
}

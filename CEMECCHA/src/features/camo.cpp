#include "camo.hpp"

#include "../core/logger.hpp"
#include "../hooks/dx12_hook.hpp"
#include "../hooks/game_thread.hpp"
#include "../sdk/ue_object.hpp"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace ce::features
{
    using ce::ue::FLinearColor;

    namespace
    {
        using ce::ue::UObject;
        using ce::ue::UClass;
        using ce::ue::UFunction;

        struct SceneRefs
        {
            UClass* scene_capture_2d_class = nullptr;
            UClass* kismet_rendering_class = nullptr;
            UFunction* fn_CaptureScene = nullptr;
            UFunction* fn_ReadRenderTarget = nullptr;
        };

        SceneRefs& scene_refs()
        {
            static SceneRefs s;
            static std::mutex mtx;
            std::lock_guard<std::mutex> lk(mtx);
            if (!s.fn_CaptureScene)
            {
                s.scene_capture_2d_class = ce::ue::find_class_by_name("SceneCaptureComponent2D", "Class");
                s.kismet_rendering_class = ce::ue::find_class_by_name("KismetRenderingLibrary", "Class");
                if (s.scene_capture_2d_class)
                    s.fn_CaptureScene = ce::ue::find_function_by_name(s.scene_capture_2d_class, "CaptureScene");
                if (s.kismet_rendering_class)
                    s.fn_ReadRenderTarget = ce::ue::find_function_by_name(s.kismet_rendering_class, "ReadRenderTarget");
                ce::log::info("[camo] scene refs: SC2D=%p KRL=%p CaptureScene=%p ReadRT=%p", s.scene_capture_2d_class, s.kismet_rendering_class, s.fn_CaptureScene, s.fn_ReadRenderTarget);
            }
            return s;
        }
    }

    bool CamoCapture::capture_base_color(ce::ue::chameleon::cLeonCharacter chara)
    {
        namespace sc = ce::ue::chameleon;
        if (!chara) return false;

        UObject* ssbq = chara.screen_space_brush_query();
        if (!ssbq) { ce::log::warn("[camo] no ScreenSpaceBrushQuery on character"); return false; }

        UObject* capture_comp = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(ssbq) + sc::sc_offsets::SSBQ_NormalCaptureComponent);
        UObject* render_target = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(ssbq) + sc::sc_offsets::SSBQ_NormalRenderTarget);
        if (!capture_comp || !render_target)
        {
            ce::log::warn("[camo] SSBQ NormalCapture/NormalRT missing (cc=%p rt=%p)", capture_comp, render_target);
            return false;
        }

        auto& refs = scene_refs();
        if (!refs.fn_CaptureScene || !refs.fn_ReadRenderTarget)
        {
            ce::log::warn("[camo] scene reflection not resolved yet");
            return false;
        }

        uint8_t& source_field = *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(capture_comp) + sc::sc_offsets::SC_CaptureSource);
        const uint8_t prev_source = source_field;
        source_field = static_cast<uint8_t>(sc::ESceneCaptureSource::BaseColor);

        auto* hidden = reinterpret_cast<ce::ue::TArray<UObject*>*>(reinterpret_cast<uintptr_t>(capture_comp) + sc::sc_offsets::SC_HiddenActors);
        UObject* prev_hidden_data[2] { nullptr, nullptr };
        int32_t prev_hidden_count = 0;
        int32_t prev_hidden_max = 0;
        UObject** prev_hidden_ptr = nullptr;
        if (hidden)
        {
            prev_hidden_ptr = hidden->Data;
            prev_hidden_count = hidden->Count;
            prev_hidden_max = hidden->Max;
            prev_hidden_data[0] = chara.obj;
            hidden->Data = prev_hidden_data;
            hidden->Count = 1;
            hidden->Max = 2;
        }

        capture_comp->process_event(refs.fn_CaptureScene, nullptr);

        struct alignas(8) ReadParams
        {
            UObject* WorldContextObject;
            UObject* TextureRenderTarget;
            ce::ue::TArray<sc::FColor> OutSamples;
            bool bNormalize;
            bool ReturnValue;
            uint8_t Pad_22[0x6];
        };
        static_assert(sizeof(ReadParams) == 0x28);

        ReadParams rp{};
        std::memset(&rp, 0, sizeof(rp));
        rp.WorldContextObject = chara.obj;
        rp.TextureRenderTarget = render_target;
        rp.bNormalize = true;

        constexpr uint32_t UClass_ClassDefaultObject = 0x110;
        UObject* krl_default = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(refs.kismet_rendering_class) + UClass_ClassDefaultObject);
        bool ok = false;
        if (krl_default)
        {
            krl_default->process_event(refs.fn_ReadRenderTarget, &rp);
            ok = rp.ReturnValue && rp.OutSamples.Count > 0 && rp.OutSamples.Data;
        }

        if (ok)
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pixels_.resize(static_cast<size_t>(rp.OutSamples.Count));
            const int side = static_cast<int>(std::sqrt(static_cast<double>(rp.OutSamples.Count)) + 0.5);
            w_ = side;
            h_ = side;
            fmt_ = 87;
            for (int32_t i = 0; i < rp.OutSamples.Count; ++i)
            {
                const auto c = rp.OutSamples.Data[i];
                pixels_[i] = static_cast<uint32_t>(c.B)
                           | (static_cast<uint32_t>(c.G) << 8)
                           | (static_cast<uint32_t>(c.R) << 16)
                           | (static_cast<uint32_t>(c.A) << 24);
            }
            ce::log::info("[camo] base-colour capture: %d pixels (%dx%d)", rp.OutSamples.Count, w_, h_);
        }
        else
        {
            ce::log::warn("[camo] ReadRenderTarget returned no pixels (count=%d ret=%d)", rp.OutSamples.Count, static_cast<int>(rp.ReturnValue));
        }

        source_field = prev_source;
        if (hidden)
        {
            hidden->Data = prev_hidden_ptr;
            hidden->Count = prev_hidden_count;
            hidden->Max = prev_hidden_max;
        }
        return ok;
    }

    namespace
    {
        struct FullRefs
        {
            UClass* ASceneCapture2D_class = nullptr;
            UClass* UGameplayStatics_class = nullptr;
            UClass* UKismetRenderingLibrary_class = nullptr;
            UClass* AActor_class = nullptr;
            UFunction* fn_BeginDeferredActorSpawn = nullptr;
            UFunction* fn_FinishSpawningActor = nullptr;
            UFunction* fn_CreateRenderTarget2D = nullptr;
            UFunction* fn_ReadRenderTarget = nullptr;
            UFunction* fn_CaptureScene = nullptr;
            UFunction* fn_K2_TeleportTo = nullptr;
        };

        FullRefs& full_refs()
        {
            static FullRefs s;
            static std::mutex mtx;
            std::lock_guard<std::mutex> lk(mtx);
            if (!s.fn_CaptureScene)
            {
                s.ASceneCapture2D_class = ce::ue::find_class_by_name("SceneCapture2D", "Class");
                s.UGameplayStatics_class = ce::ue::find_class_by_name("GameplayStatics", "Class");
                s.UKismetRenderingLibrary_class = ce::ue::find_class_by_name("KismetRenderingLibrary", "Class");
                UClass* scc2d = ce::ue::find_class_by_name("SceneCaptureComponent2D", "Class");

                if (s.UGameplayStatics_class)
                {
                    s.fn_BeginDeferredActorSpawn = ce::ue::find_function_by_name(s.UGameplayStatics_class, "BeginDeferredActorSpawnFromClass");
                    s.fn_FinishSpawningActor = ce::ue::find_function_by_name(s.UGameplayStatics_class, "FinishSpawningActor");
                }
                if (s.UKismetRenderingLibrary_class)
                {
                    s.fn_CreateRenderTarget2D = ce::ue::find_function_by_name(s.UKismetRenderingLibrary_class, "CreateRenderTarget2D");
                    s.fn_ReadRenderTarget = ce::ue::find_function_by_name(s.UKismetRenderingLibrary_class, "ReadRenderTarget");
                }
                if (scc2d) s.fn_CaptureScene = ce::ue::find_function_by_name(scc2d, "CaptureScene");

                s.AActor_class = ce::ue::find_class_by_name("Actor", "Class");
                if (s.AActor_class)
                    s.fn_K2_TeleportTo = ce::ue::find_function_by_name(s.AActor_class, "K2_TeleportTo");

                ce::log::info("[camo-full] refs: SC2D=%p GS=%p KRL=%p Spawn=%p Finish=%p CreateRT=%p ReadRT=%p CaptureScene=%p Teleport=%p", s.ASceneCapture2D_class, s.UGameplayStatics_class, s.UKismetRenderingLibrary_class, s.fn_BeginDeferredActorSpawn, s.fn_FinishSpawningActor, s.fn_CreateRenderTarget2D, s.fn_ReadRenderTarget, s.fn_CaptureScene, s.fn_K2_TeleportTo);
            }
            return s;
        }
    }

    void CamoCapture::teardown_owned_capture()
    {
        owned_capture_actor_ = owned_capture_component_ = owned_render_target_ = nullptr;
    }

    bool CamoCapture::capture_scene_base_color(ce::ue::chameleon::cLeonCharacter chara)
    {
        namespace sc = ce::ue::chameleon;
        if (!chara) return false;

        auto& r = full_refs();
        if (!r.fn_CaptureScene || !r.fn_BeginDeferredActorSpawn || !r.fn_FinishSpawningActor || !r.fn_CreateRenderTarget2D || !r.fn_ReadRenderTarget)
        {
            ce::log::warn("[camo-full] reflections not fully resolved yet");
            return false;
        }

        {
            const auto cur_world = reinterpret_cast<uintptr_t>(ce::ue::gworld());
            if (owned_capture_actor_ && cur_world != last_world_)
            {
                ce::log::info("[camo-full] GWorld changed (%p → %p) — invalidating owned SceneCapture", (void*)last_world_, (void*)cur_world);
                owned_capture_actor_ = nullptr;
                owned_capture_component_ = nullptr;
                owned_render_target_ = nullptr;
                frozen_cache_ = {};
                frozen_mesh_ctw_valid_ = false;
                spawn_in_progress_.store(false);
            }
            last_world_ = cur_world;
        }

        bool spawn_ok = false;
        if (!owned_capture_actor_)
        {
            if (spawn_in_progress_.exchange(true))
            {
                ce::log::info("[camo-full] spawn already in flight — using fallback this frame");
                return false;
            }
            ce::log::info("[camo-full] queuing SceneCapture spawn (non-blocking, next F1 will use it)");
            ce::hooks::run_on_game_thread([this, chara, &r]{ constexpr uint32_t UClass_CDO_OFFSET = 0x110; struct alignas(8) BeginParams { const UObject* WorldContextObject; UClass* ActorClass; sc::FTransform SpawnTransform; sc::ESpawnActorCollisionHandlingMethod CollisionHandlingOverride; uint8_t Pad_71[0x7]; UObject* Owner; uint8_t TransformScaleMethod; uint8_t Pad_81[0x7]; UObject* ReturnValue; }; static_assert(sizeof(BeginParams) == 0x90); UObject* world_ctx = ce::ue::gworld(); if (!world_ctx) world_ctx = chara.obj; BeginParams bp{}; std::memset(&bp, 0, sizeof(bp)); bp.WorldContextObject = world_ctx; bp.ActorClass = r.ASceneCapture2D_class; bp.SpawnTransform.Rotation = { 0, 0, 0, 1 }; bp.SpawnTransform.Translation = { 0, 0, 0 }; bp.SpawnTransform.Scale3D = { 1, 1, 1 }; bp.CollisionHandlingOverride = sc::ESpawnActorCollisionHandlingMethod::AlwaysSpawn; bp.TransformScaleMethod = 0; UObject* gs_cdo = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(r.UGameplayStatics_class) + UClass_CDO_OFFSET); if (!gs_cdo) { ce::log::err("[camo-full] GameplayStatics CDO null"); return; } gs_cdo->process_event(r.fn_BeginDeferredActorSpawn, &bp); if (!bp.ReturnValue) { ce::log::err("[camo-full] BeginDeferredActorSpawn returned null (world_ctx=%p class=%p)", world_ctx, r.ASceneCapture2D_class); return; } owned_capture_actor_ = bp.ReturnValue; owned_capture_component_ = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(owned_capture_actor_) + sc::sc_offsets::A2D_CaptureComponent2D); if (!owned_capture_component_) { ce::log::err("[camo-full] CaptureComponent2D null on spawned actor"); return; } *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(owned_capture_component_) + sc::sc_offsets::SC_CaptureSource) = static_cast<uint8_t>(sc::ESceneCaptureSource::BaseColor); *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(owned_capture_component_) + sc::sc_offsets::SC2D_FOVAngle) = 90.0f; { auto* flags = reinterpret_cast<uint8_t*>( reinterpret_cast<uintptr_t>(owned_capture_component_) + sc::sc_offsets::SC_CaptureFlags); const uint8_t before = *flags; *flags = (before & ~static_cast<uint8_t>(0x03)) | static_cast<uint8_t>(0x04); ce::log::info("[camo-full] CaptureFlags: 0x%02X → 0x%02X (auto-capture disabled)", before, *flags); } struct alignas(8) CreateRTParams { UObject* WorldContextObject; int32_t Width; int32_t Height; sc::ETextureRenderTargetFormat Format; uint8_t Pad_11[0x3]; ce::ue::FLinearColor ClearColor; bool bAutoGenerateMipMaps; bool bSupportUAVs; uint8_t Pad_26[0x2]; UObject* ReturnValue; }; static_assert(sizeof(CreateRTParams) == 0x30); CreateRTParams cp{}; std::memset(&cp, 0, sizeof(cp)); cp.WorldContextObject = world_ctx; cp.Width = owned_rt_size_; cp.Height = owned_rt_size_; cp.Format = sc::ETextureRenderTargetFormat::RTF_RGBA8; cp.ClearColor = { 0, 0, 0, 1 }; UObject* krl_cdo = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(r.UKismetRenderingLibrary_class) + UClass_CDO_OFFSET); if (!krl_cdo) { ce::log::err("[camo-full] KRL CDO null"); return; } krl_cdo->process_event(r.fn_CreateRenderTarget2D, &cp); if (!cp.ReturnValue) { ce::log::err("[camo-full] CreateRenderTarget2D returned null (world_ctx=%p)", world_ctx); return; } owned_render_target_ = cp.ReturnValue; *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(owned_capture_component_) + sc::sc_offsets::SC2D_TextureTarget) = owned_render_target_; struct alignas(8) FinishParams { UObject* Actor; uint8_t Pad_8[0x8]; sc::FTransform SpawnTransform; uint8_t TransformScaleMethod; uint8_t Pad_71[0x7]; UObject* ReturnValue; }; static_assert(sizeof(FinishParams) == 0x80); FinishParams fp{}; std::memset(&fp, 0, sizeof(fp)); fp.Actor = owned_capture_actor_; fp.SpawnTransform.Rotation = { 0, 0, 0, 1 }; fp.SpawnTransform.Scale3D = { 1, 1, 1 }; gs_cdo->process_event(r.fn_FinishSpawningActor, &fp); if (fp.ReturnValue) owned_capture_actor_ = fp.ReturnValue; ce::log::info("[camo-full] spawned SceneCapture actor=%p component=%p RT=%p (%dx%d)", owned_capture_actor_, owned_capture_component_, owned_render_target_, owned_rt_size_, owned_rt_size_); spawn_in_progress_.store(false); }, false , 0);
            return false;
        }
        spawn_ok = true;

        static std::atomic<uint64_t> s_capture_seq{ 0 };
        const uint64_t seq_before = s_capture_seq.load();
        ce::hooks::run_on_game_thread([this, chara, &r]{ namespace sco = sc::sc_offsets; constexpr uint32_t UClass_CDO_OFFSET = 0x110; sc::CameraSnapshot snap{}; if (pose_override_.active) { snap.location = pose_override_.location; snap.rotation = pose_override_.rotation; snap.fov_deg = pose_override_.fov_deg; constexpr double PI = 3.14159265358979323846; const double cp = std::cos(snap.rotation.Pitch * PI / 180.0); const double sp = std::sin(snap.rotation.Pitch * PI / 180.0); const double cy = std::cos(snap.rotation.Yaw * PI / 180.0); const double sy = std::sin(snap.rotation.Yaw * PI / 180.0); const double cr = std::cos(snap.rotation.Roll * PI / 180.0); const double sr = std::sin(snap.rotation.Roll * PI / 180.0); snap.forward = { (float)(cp*cy), (float)(cp*sy), (float)(sp) }; snap.right = { (float)( sr*sp*cy - cr*sy), (float)( sr*sp*sy + cr*cy), (float)(-sr*cp) }; snap.up = { (float)(-(cr*sp*cy + sr*sy)),(float)(-(cr*sp*sy - sr*cy)), (float)(cr*cp) }; snap.valid = true; ce::log::info("[camo-full] using POSE OVERRIDE loc=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f fov=%.1f", snap.location.X, snap.location.Y, snap.location.Z, snap.rotation.Yaw, snap.rotation.Pitch, snap.fov_deg); } else { UObject* pc = sc::controller_of(chara); UObject* pcm = sc::camera_manager_of(pc); sc::camera_snapshot(pcm, snap); } if (!snap.valid) { UObject* char_root = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(chara.obj) + 0x01B8); if (char_root) { snap.location = *reinterpret_cast<ce::ue::FVector*>(reinterpret_cast<uintptr_t>(char_root) + 0x0140); snap.rotation = *reinterpret_cast<ce::ue::FRotator*>(reinterpret_cast<uintptr_t>(char_root) + 0x0158); sc::CameraSnapshot dummy{}; dummy.rotation = snap.rotation; UObject* pcm_ignored = nullptr; (void)pcm_ignored; constexpr double PI = 3.14159265358979323846; const double cp = std::cos(snap.rotation.Pitch * PI / 180.0); const double sp = std::sin(snap.rotation.Pitch * PI / 180.0); const double cy = std::cos(snap.rotation.Yaw * PI / 180.0); const double sy = std::sin(snap.rotation.Yaw * PI / 180.0); const double cr = std::cos(snap.rotation.Roll * PI / 180.0); const double sr = std::sin(snap.rotation.Roll * PI / 180.0); snap.forward = { (float)(cp*cy), (float)(cp*sy), (float)(sp) }; snap.right = { (float)( sr*sp*cy - cr*sy), (float)( sr*sp*sy + cr*cy), (float)(-sr*cp) }; snap.up = { (float)(-(cr*sp*cy + sr*sy)), (float)(-(cr*sp*sy - sr*cy)), (float)(cr*cp) }; snap.valid = true; } } if (snap.valid && r.fn_K2_TeleportTo) { struct alignas(8) TeleportParams { ce::ue::FVector DestLocation; ce::ue::FRotator DestRotation; bool ReturnValue; uint8_t Pad_31[0x7]; }; static_assert(sizeof(TeleportParams) == 0x38); TeleportParams tp{}; std::memset(&tp, 0, sizeof(tp)); tp.DestLocation = snap.location; tp.DestRotation = snap.rotation; owned_capture_actor_->process_event(r.fn_K2_TeleportTo, &tp); ce::log::info("[camo-full] teleport to CAM loc=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f fov=%.1f -> %s", snap.location.X, snap.location.Y, snap.location.Z, snap.rotation.Yaw, snap.rotation.Pitch, snap.fov_deg, tp.ReturnValue ? "ok" : "denied"); } if (snap.valid && snap.fov_deg >= 10.f && snap.fov_deg <= 170.f) { *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(owned_capture_component_) + sc::sc_offsets::SC2D_FOVAngle) = snap.fov_deg; } { const auto rp = reinterpret_cast<std::uintptr_t>(chara.runtime_paintable().obj); if (rp) { if (frozen_cache_.ok) { if (!ce::features::runtime_cache::refresh(rp, frozen_cache_)) { ce::log::warn("[camo-full] frozen cache went stale — re-scanning"); frozen_cache_ = {}; } } if (!frozen_cache_.ok) { frozen_cache_ = ce::features::runtime_cache::resolve(rp); if (frozen_cache_.ok) ce::log::info("[camo-full] runtime cache RESOLVED (frozen): offset=0x%04X tris=%d avg_err=%.5f", frozen_cache_.owner_offset, frozen_cache_.triangle_count, frozen_cache_.profile_uv_avg_error); else ce::log::warn("[camo-full] runtime cache resolve failed: %s", frozen_cache_.failure.c_str()); } } UObject* mesh = chara.mesh_component(); if (mesh) { frozen_mesh_ctw_ = sc::component_to_world(mesh); frozen_mesh_ctw_valid_ = true; } else { frozen_mesh_ctw_valid_ = false; } } auto* hidden = reinterpret_cast<ce::ue::TArray<UObject*>*>(reinterpret_cast<uintptr_t>(owned_capture_component_) + sco::SC_HiddenActors); static UObject* hidden_backing[2] { nullptr, nullptr }; if (hidden) { hidden_backing[0] = chara.obj; hidden->Data = hidden_backing; hidden->Count = 1; hidden->Max = 2; } owned_capture_component_->process_event(r.fn_CaptureScene, nullptr); struct alignas(8) ReadParams { UObject* WorldContextObject; UObject* TextureRenderTarget; ce::ue::TArray<sc::FColor> OutSamples; bool bNormalize; bool ReturnValue; uint8_t Pad_22[0x6]; }; static_assert(sizeof(ReadParams) == 0x28); UObject* world_ctx_read = ce::ue::gworld(); if (!world_ctx_read) world_ctx_read = chara.obj; ReadParams rp{}; std::memset(&rp, 0, sizeof(rp)); rp.WorldContextObject = world_ctx_read; rp.TextureRenderTarget = owned_render_target_; rp.bNormalize = false; UObject* krl_cdo = *reinterpret_cast<UObject**>(reinterpret_cast<uintptr_t>(r.UKismetRenderingLibrary_class) + UClass_CDO_OFFSET); if (!krl_cdo) { ce::log::warn("[camo-full] KRL CDO null on read"); return; } krl_cdo->process_event(r.fn_ReadRenderTarget, &rp); ce::log::info("[camo-full] ReadRT returned ok=%d count=%d data=%p", static_cast<int>(rp.ReturnValue), rp.OutSamples.Count, rp.OutSamples.Data); if (rp.OutSamples.Count > 0 && rp.OutSamples.Data) { std::lock_guard<std::mutex> lk(mtx_); const int32_t count = rp.OutSamples.Count; pixels_.resize(static_cast<size_t>(count)); w_ = owned_rt_size_; h_ = owned_rt_size_; fmt_ = 87; for (int32_t i = 0; i < count; ++i) { const auto c = rp.OutSamples.Data[i]; pixels_[i] = static_cast<uint32_t>(c.B) | (static_cast<uint32_t>(c.G) << 8) | (static_cast<uint32_t>(c.R) << 16) | (static_cast<uint32_t>(c.A) << 24); } if (snap.valid) { snap.aspect = 1.0f; capture_view_ = snap; } s_capture_seq.fetch_add(1); capture_seq_.store(s_capture_seq.load()); ce::log::info("[camo-full] captured %d pixels (%dx%d) via own SceneCapture", count, w_, h_); } }, false , 0);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!pixels_.empty() && w_ == owned_rt_size_ && h_ == owned_rt_size_)
            {
                return true;
            }
        }
        (void)seq_before;
        return false;
    }

    bool CamoCapture::capture_back_buffer()
    {
        int w = 0, h = 0;
        uint32_t fmt = 0;
        std::vector<uint32_t> raw;

        if (!ce::hooks::readback_last_frame(&raw, &w, &h, &fmt))
        {
            ce::log::warn("Camo: back-buffer readback failed");
            return false;
        }

        std::lock_guard<std::mutex> lk(mtx_);
        pixels_ = std::move(raw);
        w_ = w;
        h_ = h;
        fmt_ = fmt;

        static uint32_t s_last_logged_fmt = 0xFFFFFFFF;
        if (fmt != s_last_logged_fmt)
        {
            const char* fname = "unknown";
            switch (fmt) {
                case 10: fname = "R16G16B16A16_FLOAT"; break;
                case 24: fname = "R10G10B10A2_UNORM"; break;
                case 28: fname = "R8G8B8A8_UNORM"; break;
                case 29: fname = "R8G8B8A8_UNORM_SRGB";break;
                case 87: fname = "B8G8R8A8_UNORM"; break;
                case 91: fname = "B8G8R8A8_UNORM_SRGB";break;
            }
            ce::log::info("[camo] back-buffer DXGI_FORMAT=%u (%s) size=%dx%d", fmt, fname, w_, h_);
            s_last_logged_fmt = fmt;
        }
        return true;
    }

    static FLinearColor pixel_to_linear(uint32_t px, uint32_t fmt)
    {

        if (fmt == 24)
        {
            const uint32_t r = (px >> 0) & 0x3FF;
            const uint32_t g = (px >> 10) & 0x3FF;
            const uint32_t b = (px >> 20) & 0x3FF;
            return { r / 1023.0f, g / 1023.0f, b / 1023.0f, 1.0f };
        }

        uint8_t b0 = (px >> 0) & 0xFF;
        uint8_t b1 = (px >> 8) & 0xFF;
        uint8_t b2 = (px >> 16) & 0xFF;
        uint8_t b3 = (px >> 24) & 0xFF;

        uint8_t r = b0, g = b1, b = b2, a = b3;
        if (fmt == 87 || fmt == 91) { b = b0; g = b1; r = b2; a = b3; }

        auto srgb_to_lin = [](float v) { return (v <= 0.04045f) ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
        float rf = r / 255.0f;
        float gf = g / 255.0f;
        float bf = b / 255.0f;
        if (fmt == 29 || fmt == 91)
        {
            rf = srgb_to_lin(rf);
            gf = srgb_to_lin(gf);
            bf = srgb_to_lin(bf);
        }
        return { rf, gf, bf, a / 255.0f };
    }

    FLinearColor CamoCapture::sample_uv(float u, float v) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pixels_.empty() || w_ == 0 || h_ == 0) return { 0, 0, 0, 1 };

        u = std::clamp(u, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        int32_t x = static_cast<int32_t>(u * (w_ - 1));
        int32_t y = static_cast<int32_t>(v * (h_ - 1));
        const int32_t idx = y * w_ + x;
        if (idx < 0 || idx >= static_cast<int32_t>(pixels_.size())) return { 0, 0, 0, 1 };

        return pixel_to_linear(pixels_[idx], fmt_);
    }

    FLinearColor CamoCapture::sample_pixel(int x, int y) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pixels_.empty() || w_ == 0 || h_ == 0) return { 0, 0, 0, 1 };
        x = std::clamp(x, 0, w_ - 1);
        y = std::clamp(y, 0, h_ - 1);
        return pixel_to_linear(pixels_[y * w_ + x], fmt_);
    }

    FLinearColor CamoCapture::sample_screen(int sx, int sy, int screen_w, int screen_h) const
    {
        if (screen_w <= 0 || screen_h <= 0) return { 0, 0, 0, 1 };
        return sample_uv(static_cast<float>(sx) / static_cast<float>(screen_w), static_cast<float>(sy) / static_cast<float>(screen_h));
    }

    FLinearColor CamoCapture::sample_box_avg(int sx, int sy, int k) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pixels_.empty() || w_ == 0 || h_ == 0) return { 0, 0, 0, 1 };

        const int half = std::max(1, k / 2);
        const int x0 = std::clamp(sx - half, 0, w_ - 1);
        const int y0 = std::clamp(sy - half, 0, h_ - 1);
        const int x1 = std::clamp(sx + half, 0, w_ - 1);
        const int y1 = std::clamp(sy + half, 0, h_ - 1);

        double r = 0, g = 0, b = 0;
        int n = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const uint32_t px = pixels_[y * w_ + x];
                auto c = pixel_to_linear(px, fmt_);
                r += c.R; g += c.G; b += c.B;
                ++n;
            }
        if (n == 0) return { 0, 0, 0, 1 };
        return { static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n), 1.0f };
    }

    FLinearColor CamoCapture::sample_region_avg(float u0, float v0, float u1, float v1) const
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (pixels_.empty() || w_ == 0 || h_ == 0) return { 0, 0, 0, 1 };
        if (u1 < u0) std::swap(u0, u1);
        if (v1 < v0) std::swap(v0, v1);
        u0 = std::clamp(u0, 0.0f, 1.0f);
        u1 = std::clamp(u1, 0.0f, 1.0f);
        v0 = std::clamp(v0, 0.0f, 1.0f);
        v1 = std::clamp(v1, 0.0f, 1.0f);

        const int x0 = std::clamp(static_cast<int>(u0 * (w_ - 1)), 0, w_ - 1);
        const int y0 = std::clamp(static_cast<int>(v0 * (h_ - 1)), 0, h_ - 1);
        const int x1 = std::clamp(static_cast<int>(u1 * (w_ - 1)), 0, w_ - 1);
        const int y1 = std::clamp(static_cast<int>(v1 * (h_ - 1)), 0, h_ - 1);

        double r = 0, g = 0, b = 0;
        int n = 0;

        for (int y = y0; y <= y1; y += 4)
            for (int x = x0; x <= x1; x += 4)
            {
                const uint32_t px = pixels_[y * w_ + x];
                auto c = pixel_to_linear(px, fmt_);
                r += c.R; g += c.G; b += c.B;
                ++n;
            }
        if (n == 0) return { 0, 0, 0, 1 };
        return { static_cast<float>(r / n), static_cast<float>(g / n), static_cast<float>(b / n), 1.0f };
    }

    void CamoCapture::teardown()
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pixels_.clear();
        pixels_.shrink_to_fit();
        w_ = h_ = 0;
        fmt_ = 0;
    }

    void CamoCapture::set_capture_pose_override(bool active, ce::ue::FVector location, ce::ue::FRotator rotation, float fov_deg)
    {
        pose_override_.active = active;
        pose_override_.location = location;
        pose_override_.rotation = rotation;
        pose_override_.fov_deg = fov_deg;
    }
}

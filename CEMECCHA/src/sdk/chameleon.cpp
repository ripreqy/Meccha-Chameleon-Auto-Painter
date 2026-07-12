#include "chameleon.hpp"
#include "../core/logger.hpp"

#include <Windows.h>
#include <cmath>
#include <mutex>

namespace ce::ue::chameleon
{

    struct FnCache
    {
        UFunction* ServerPaintBatch = nullptr;
        UFunction* BeginStroke = nullptr;
        UFunction* EndStroke = nullptr;
        UFunction* SetBrushRadius = nullptr;
        UFunction* SetBrushSettings = nullptr;
        UFunction* ClearAllChannels = nullptr;
        UFunction* FlushRecordedStrokesToServer = nullptr;
        UFunction* InitializePaint = nullptr;
        UFunction* PaintAtUVWithBrush = nullptr;
        UFunction* PaintAtScreenPosition = nullptr;
        UFunction* GetInitializedPaintMesh = nullptr;
    };

    static FnCache s_fn;
    static std::mutex s_fn_mtx;

    static void ensure_fn_cache()
    {
        std::lock_guard<std::mutex> lk(s_fn_mtx);
        if (s_fn.ServerPaintBatch) return;

        UClass* rpc_class = find_class_by_name("RuntimePaintableComponent", "Class");
        if (!rpc_class)
        {
            ce::log::warn("URuntimePaintableComponent class not resolved yet");
            return;
        }

        auto lookup = [rpc_class](const char* n) -> UFunction* {
            if (UFunction* f = find_function_by_name(rpc_class, n); f) return f;
            return find_function_recursive(rpc_class, n);
        };

        s_fn.ServerPaintBatch = lookup("ServerPaintBatch");
        s_fn.BeginStroke = lookup("BeginStroke");
        s_fn.EndStroke = lookup("EndStroke");
        s_fn.SetBrushRadius = lookup("SetBrushRadius");
        s_fn.SetBrushSettings = lookup("SetBrushSettings");
        s_fn.ClearAllChannels = lookup("ClearAllChannels");
        s_fn.FlushRecordedStrokesToServer = lookup("FlushRecordedStrokesToServer");
        s_fn.InitializePaint = lookup("InitializePaint");
        s_fn.PaintAtUVWithBrush = lookup("PaintAtUVWithBrush");
        s_fn.PaintAtScreenPosition = lookup("PaintAtScreenPosition");
        s_fn.GetInitializedPaintMesh = lookup("GetInitializedPaintMesh");

        ce::log::info("RPC cache: ServerBatch=%p Begin=%p End=%p Flush=%p PaintUV=%p PaintScreen=%p Init=%p GetMesh=%p", s_fn.ServerPaintBatch, s_fn.BeginStroke, s_fn.EndStroke, s_fn.FlushRecordedStrokesToServer, s_fn.PaintAtUVWithBrush, s_fn.PaintAtScreenPosition, s_fn.InitializePaint, s_fn.GetInitializedPaintMesh);
    }

    static bool seh_process_event(UObject* target, UFunction* fn, void* params) noexcept
    {
        __try
        {
            target->process_event(fn, params);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool seh_memcpy(void* dst, const void* src, size_t n) noexcept
    {
        __try
        {
            auto* d = static_cast<uint8_t*>(dst);
            const auto* s = static_cast<const uint8_t*>(src);
            for (size_t i = 0; i < n; ++i) d[i] = s[i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool seh_read_u32(const uint32_t* src, uint32_t& out) noexcept
    {
        __try { out = *src; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool seh_write_u32(uint32_t* dst, uint32_t val) noexcept
    {
        __try { *dst = val; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool rp_looks_alive(UObject* obj) noexcept
    {
        if (!obj) return false;
        const uintptr_t a = reinterpret_cast<uintptr_t>(obj);
        if (a < 0x00010000ULL || a >= 0x00007FFF'FFFFFFFFULL) return false;
        
        __try
        {
            volatile void* vtbl = *reinterpret_cast<void* volatile*>(obj);
            (void)vtbl;
            volatile UClass* cls = *reinterpret_cast<UClass* volatile*>( reinterpret_cast<uintptr_t>(obj) + 0x10);
            if (!cls) return false;
            const uintptr_t ca = reinterpret_cast<uintptr_t>(const_cast<UClass*>(cls));
            if (ca < 0x00010000ULL || ca >= 0x00007FFF'FFFFFFFFULL) return false;
            volatile void* cvtbl = *reinterpret_cast<void* volatile*>(const_cast<UClass*>(cls));
            (void)cvtbl;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    UObject* RuntimePaintable::mesh_component_target() const
    {
        if (!obj) return nullptr;
        return *obj->get_ptr<UObject*>(0x0208);
    }

    void RuntimePaintable::set_auto_flush(bool enable)
    {
        if (!rp_looks_alive(obj)) return;
        auto* p = reinterpret_cast<uint8_t*>(obj) + 0x01AF;
        const uint8_t v = enable ? 1 : 0;
        (void)seh_memcpy(p, &v, 1);
    }

    void RuntimePaintable::set_auto_record(bool enable)
    {
        if (!rp_looks_alive(obj)) return;
        auto* p = reinterpret_cast<uint8_t*>(obj) + 0x01AC;
        const uint8_t v = enable ? 1 : 0;
        (void)seh_memcpy(p, &v, 1);
    }

    void RuntimePaintable::begin_stroke()
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.BeginStroke) return;
        (void)seh_process_event(obj, s_fn.BeginStroke, nullptr);
    }

    void RuntimePaintable::end_stroke()
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.EndStroke) return;
        (void)seh_process_event(obj, s_fn.EndStroke, nullptr);
    }

    void RuntimePaintable::set_brush_radius(float radius)
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.SetBrushRadius) return;
        (void)seh_process_event(obj, s_fn.SetBrushRadius, &radius);
    }

    void RuntimePaintable::set_brush_settings(const FRuntimeBrushSettings& s)
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.SetBrushSettings) return;
        (void)seh_process_event(obj, s_fn.SetBrushSettings, const_cast<FRuntimeBrushSettings*>(&s));
    }

    void RuntimePaintable::clear_all_channels()
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.ClearAllChannels) return;
        (void)seh_process_event(obj, s_fn.ClearAllChannels, nullptr);
    }

    void RuntimePaintable::flush_recorded_strokes_to_server()
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.FlushRecordedStrokesToServer) return;
        if (!seh_process_event(obj, s_fn.FlushRecordedStrokesToServer, nullptr))
            ce::log::warn("[paint] flush RPC threw AV — server may have missed tail strokes");
    }

    void RuntimePaintable::ensure_server_batch_reliable()
    {
        ensure_fn_cache();
        if (!s_fn.ServerPaintBatch)
        {
            ce::log::err("[paint] ServerPaintBatch UFunction NOT FOUND — replication won't work");
            return;
        }

        auto* flags_ptr = reinterpret_cast<uint32_t*>( reinterpret_cast<uintptr_t>(s_fn.ServerPaintBatch) + UFunction::OFF_FunctionFlags);
        uint32_t cur = 0;
        if (!seh_read_u32(flags_ptr, cur))
        {
            ce::log::err("[paint] failed to read ServerPaintBatch flags — skipping patch");
            return;
        }
        if ((cur & FUNC_NetReliable) == 0)
        {
            const uint32_t next = cur | FUNC_NetReliable;
            if (seh_write_u32(flags_ptr, next))
                ce::log::info("[paint] patched ServerPaintBatch: flags 0x%X → 0x%X (NetReliable)", cur, next);
            else
                ce::log::err("[paint] failed to write ServerPaintBatch flags");
        }
        else
        {
            ce::log::info("[paint] ServerPaintBatch already reliable (flags=0x%X)", cur);
        }
    }

    namespace
    {

        UObject* resolve_maybe_weak(UObject* raw)
        {
            const uintptr_t addr = reinterpret_cast<uintptr_t>(raw);
            if (addr == 0) return nullptr;
            if (addr >= 0x0001'0000'0000'0000ULL) return raw;
            const int32_t index = static_cast<int32_t>(addr & 0xFFFFFFFF);
            auto* arr = gobjects();
            if (!arr || index < 0 || index >= arr->NumElements) return nullptr;
            return arr->at(index);
        }
    }

    UObject* RuntimePaintable::get_initialized_paint_mesh()
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj)) return nullptr;

        UObject* raw = nullptr;

        if (s_fn.GetInitializedPaintMesh)
        {
            struct { UObject* ReturnValue; } params{ nullptr };
            if (seh_process_event(obj, s_fn.GetInitializedPaintMesh, &params))
                raw = params.ReturnValue;
        }
        if (!raw)
        {
            __try { raw = *obj->get_ptr<UObject*>(0x0208); }
            __except (EXCEPTION_EXECUTE_HANDLER) { raw = nullptr; }
        }

        return resolve_maybe_weak(raw);
    }

    bool RuntimePaintable::initialize_paint(UObject* mesh_component)
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.InitializePaint || !mesh_component) return false;

        struct { UObject* MeshComponent; bool ReturnValue; uint8_t pad[7]; } params{ mesh_component, false, {} };
        if (!seh_process_event(obj, s_fn.InitializePaint, &params)) return false;
        return params.ReturnValue;
    }

    void RuntimePaintable::paint_at_uv_with_brush(const FVector2D& uv, const FPaintChannelData& channel, const FRuntimeBrushSettings& brush, EPaintChannel target_channel)
    {
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.PaintAtUVWithBrush) return;

        struct alignas(8) Params
        {
            FVector2D Uv;
            FPaintChannelData ChannelData;
            FRuntimeBrushSettings BrushSettings;
            EPaintChannel Channel;
            uint8_t Pad_59[0x7];
        };
        static_assert(sizeof(Params) == 0x60);

        Params p{};
        p.Uv = uv;
        p.ChannelData = channel;
        p.BrushSettings = brush;
        p.Channel = target_channel;
        (void)seh_process_event(obj, s_fn.PaintAtUVWithBrush, &p);
    }

    FScreenSpacePaintResult RuntimePaintable::paint_at_screen_position(UObject* mesh, const FVector2D& screen_pos, UObject* player_controller, const FPaintChannelData& channel, EPaintChannel target_channel)
    {
        FScreenSpacePaintResult result{};
        ensure_fn_cache();
        if (!rp_looks_alive(obj) || !s_fn.PaintAtScreenPosition || !mesh || !player_controller) return result;

        
        struct alignas(8) Params
        {
            UObject* MeshComponent;
            FVector2D ScreenPosition;
            UObject* PlayerController;
            FPaintChannelData ChannelData;
            EPaintChannel Channel;
            bool bUseCachedTriangles;
            uint8_t Pad_42[0x6];
            FScreenSpacePaintResult ReturnValue;
        };
        static_assert(sizeof(Params) == 0x90);

        Params p{};
        p.MeshComponent = mesh;
        p.ScreenPosition = screen_pos;
        p.PlayerController = player_controller;
        p.ChannelData = channel;
        p.Channel = target_channel;
        p.bUseCachedTriangles = true;
        if (!seh_process_event(obj, s_fn.PaintAtScreenPosition, &p)) return result;
        return p.ReturnValue;
    }

    

    RuntimePaintable cLeonCharacter::runtime_paintable() const
    {
        if (!rp_looks_alive(obj)) return { nullptr };
        UObject* rp = nullptr;
        __try { rp = *obj->get_ptr<UObject*>(ch_offsets::cLeon_RuntimePaintable); }
        __except (EXCEPTION_EXECUTE_HANDLER) { rp = nullptr; }
        if (rp && !rp_looks_alive(rp)) rp = nullptr;
        return { rp };
    }

    UObject* cLeonCharacter::mesh_component() const
    {
        if (!rp_looks_alive(obj)) return nullptr;
        __try { return *obj->get_ptr<UObject*>(ch_offsets::Char_Mesh); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    UObject* cLeonCharacter::screen_space_brush_query() const
    {
        if (!rp_looks_alive(obj)) return nullptr;
        __try { return *obj->get_ptr<UObject*>(ch_offsets::cLeon_ScreenSpaceBrushQuery); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    bool cLeonCharacter::is_paint_mode() const
    {
        if (!rp_looks_alive(obj)) return false;
        __try { return obj->get<bool>(ch_offsets::cLeon_IsPaintMode); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void cLeonCharacter::force_paint_mode(bool enable)
    {
        if (!rp_looks_alive(obj)) return;
        auto* p = reinterpret_cast<uint8_t*>(obj) + ch_offsets::cLeon_IsPaintMode;
        const uint8_t v = enable ? 1 : 0;
        (void)seh_memcpy(p, &v, 1);
    }

    bool cLeonCharacter::is_hunter() const
    {
        if (!rp_looks_alive(obj)) return false;
        __try { return obj->get<bool>(ch_offsets::cLeon_IsHunter); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    UObject* local_player_controller() { return nullptr; }
    UObject* local_pawn() { return nullptr; }

    namespace
    {
        UClass* s_leon_class = nullptr;
        UClass* s_leon_hunter_class = nullptr;
        UClass* s_leon_survivor_class = nullptr;
        bool s_debug_dumped = false;

        bool safe_readable_local(const void* p, size_t n) noexcept
        {
            __try
            {
                volatile uint8_t sink = 0;
                const auto* b = static_cast<const uint8_t*>(p);
                for (size_t i = 0; i < n; ++i) sink ^= b[i];
                (void)sink;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void debug_dump_gobjects(int max_dump)
        {
            auto* arr = gobjects();
            if (!arr) { ce::log::err("GObjects null"); return; }

            ce::log::info("GObjects hdr: Objects=%p NumChunks=%d MaxChunks=%d NumElements=%d MaxElements=%d", arr->Objects, arr->NumChunks, arr->MaxChunks, arr->NumElements, arr->MaxElements);

            int dumped = 0;
            for (int32_t i = 0; i < arr->NumElements && dumped < max_dump; ++i)
            {
                UObject* obj = arr->at(i);
                if (!obj) continue;
                if (!safe_readable_local(obj, 0x30)) continue;

                std::string nm = obj->name();
                UClass* cls = obj->ClassPrivate;
                std::string cn = "<null>";
                if (cls && safe_readable_local(cls, 0x30)) cn = cls->name();

                ce::log::info("[%d] class=%s name=%s", i, cn.c_str(), nm.c_str());
                ++dumped;
            }
        }

        
        void debug_scan_by_substr(const char* substring, int max_hits)
        {
            auto* arr = gobjects();
            if (!arr) return;

            std::string needle = substring;
            for (auto& c : needle) c = static_cast<char>(std::tolower(c));

            int hits = 0;
            for (int32_t i = 0; i < arr->NumElements && hits < max_hits; ++i)
            {
                UObject* obj = arr->at(i);
                if (!obj || !safe_readable_local(obj, 0x30)) continue;

                std::string nm = obj->name();
                std::string lc = nm;
                for (auto& c : lc) c = static_cast<char>(std::tolower(c));
                if (lc.find(needle) == std::string::npos) continue;

                std::string fn = obj->full_name();
                ce::log::info("[scan '%s'] %s", substring, fn.c_str());
                ++hits;
            }
            ce::log::info("[scan '%s'] %d hits", substring, hits);
        }

        void ensure_leon_classes()
        {
            if (s_leon_class) return;

            
            
            static ULONGLONG s_last_try_ms = 0;
            const ULONGLONG now = GetTickCount64();
            if (s_last_try_ms && now - s_last_try_ms < 500) return;
            s_last_try_ms = now;

            s_leon_class = find_class_by_name("BP_FirstPersonCharacter_cLeon_Character_C", "BlueprintGeneratedClass");
            s_leon_hunter_class = find_class_by_name("BP_FirstPersonCharacter_cLeon_Character_Hunter_C", "BlueprintGeneratedClass");
            s_leon_survivor_class = find_class_by_name("BP_FirstPersonCharacter_cLeon_Character_Survivor_C", "BlueprintGeneratedClass");

            if (s_leon_class)
            {
                ce::log::info("cLeon classes: base=%p hunter=%p survivor=%p", s_leon_class, s_leon_hunter_class, s_leon_survivor_class);
            }
            else
            {
                
                static bool warned = false;
                if (!warned)
                {
                    warned = true;
                    ce::log::warn("cLeon Character class not loaded yet — get into a match.");
                }
            }
        }
    }

    UObject* controller_of(const cLeonCharacter& c)
    {
        if (!rp_looks_alive(c.obj)) return nullptr;
        
        UObject* pc = nullptr;
        __try { pc = *c.obj->get_ptr<UObject*>(0x0AE0); }
        __except (EXCEPTION_EXECUTE_HANDLER) { pc = nullptr; }
        if (pc && !rp_looks_alive(pc)) pc = nullptr;
        return pc;
    }

    UObject* camera_manager_of(UObject* pc)
    {
        if (!pc) return nullptr;
        auto* pcm = *pc->get_ptr<UObject*>(ch_offsets::PC_PlayerCameraManager);
        return pcm;
    }

    namespace
    {
        
        struct RefFnCache
        {
            UFunction* K2_GetActorLocation = nullptr;
            UFunction* K2_GetActorRotation = nullptr;
            UFunction* GetCameraLocation = nullptr;
            UFunction* GetCameraRotation = nullptr;
            UFunction* GetFOVAngle = nullptr;
        };
        static RefFnCache s_ref_fn;
        static std::mutex s_ref_fn_mtx;

        UFunction* resolve_actor_fn(UObject* actor, const char* name, UFunction*& slot)
        {
            std::lock_guard<std::mutex> lk(s_ref_fn_mtx);
            if (slot) return slot;
            if (!actor || !actor->ClassPrivate) return nullptr;
            slot = find_function_by_name(actor->ClassPrivate, name);
            return slot;
        }
    }

    bool camera_snapshot(UObject* pcm, CameraSnapshot& out)
    {
        if (!safe_readable_local(pcm, sizeof(UObject))) return false;

        auto* loc_fn = resolve_actor_fn(pcm, "GetCameraLocation", s_ref_fn.GetCameraLocation);
        auto* rot_fn = resolve_actor_fn(pcm, "GetCameraRotation", s_ref_fn.GetCameraRotation);
        auto* fov_fn = resolve_actor_fn(pcm, "GetFOVAngle", s_ref_fn.GetFOVAngle);
        if (!loc_fn || !rot_fn) return false;

        {
            struct { FVector ReturnValue; } p{};
            pcm->process_event(loc_fn, &p);
            out.location = p.ReturnValue;
        }
        {
            struct { FRotator ReturnValue; } p{};
            pcm->process_event(rot_fn, &p);
            out.rotation = p.ReturnValue;
        }
        if (fov_fn)
        {
            struct { float ReturnValue; } p{};
            pcm->process_event(fov_fn, &p);
            if (p.ReturnValue >= 10.f && p.ReturnValue <= 170.f) out.fov_deg = p.ReturnValue;
        } 
        constexpr double PI = 3.14159265358979323846;
        const double cp = std::cos(out.rotation.Pitch * PI / 180.0);
        const double sp = std::sin(out.rotation.Pitch * PI / 180.0);
        const double cy = std::cos(out.rotation.Yaw * PI / 180.0);
        const double sy = std::sin(out.rotation.Yaw * PI / 180.0);
        const double cr = std::cos(out.rotation.Roll * PI / 180.0);
        const double sr = std::sin(out.rotation.Roll * PI / 180.0);

        out.forward = FVector{ static_cast<float>(cp * cy), static_cast<float>(cp * sy), static_cast<float>(sp) };
        out.right = FVector{ static_cast<float>( sr * sp * cy - cr * sy), static_cast<float>( sr * sp * sy + cr * cy), static_cast<float>(-sr * cp) };
        out.up = FVector{ static_cast<float>(-(cr * sp * cy + sr * sy)), static_cast<float>(-(cr * sp * sy - sr * cy)), static_cast<float>( cr * cp) };
        out.valid = true;
        return true;
    }

    static uint8_t seh_read_role(UObject* obj) noexcept { __try { return obj->get<uint8_t>(ch_offsets::Actor_Role); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFF; } }
    static UObject* seh_read_rp(UObject* obj) noexcept { __try { return *obj->get_ptr<UObject*>(ch_offsets::cLeon_RuntimePaintable); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
    cLeonCharacter local_leon_character() { ensure_leon_classes(); if (!s_leon_class) return { nullptr }; static UObject* s_cached = nullptr; static ULONGLONG s_cache_ms = 0; static UObject* s_last_logged = nullptr; const ULONGLONG now = GetTickCount64(); if (s_cached && now - s_cache_ms < 500) { if (rp_looks_alive(s_cached)) { UObject* rp = seh_read_rp(s_cached); if (rp && rp_looks_alive(rp)) return { s_cached }; } s_cached = nullptr; } auto* arr = gobjects(); if (!arr) return { nullptr }; UObject* best_local = nullptr; UObject* rp_fallback = nullptr; UObject* any_fallback = nullptr; int leon_seen = 0; for (int32_t i = 0; i < arr->NumElements; ++i) { UObject* obj = arr->at(i); if (!obj) continue; if (!rp_looks_alive(obj)) continue; if (!obj->is_a(s_leon_class)) continue; { std::string nm = obj->name(); if (nm.rfind("Default__", 0) == 0) continue; } ++leon_seen; if (!any_fallback) any_fallback = obj; const uint8_t role = seh_read_role(obj); if (role == 0xFF) continue; UObject* rp = seh_read_rp(obj); if (role >= 2 && rp && rp_looks_alive(rp)) { best_local = obj; break; } if (!rp_fallback && rp && rp_looks_alive(rp)) rp_fallback = obj; } UObject* result = best_local ? best_local : (rp_fallback ? rp_fallback : any_fallback); if (result != s_last_logged) { ce::log::info("cLeon scan: %d instances, best_local=%p rp_fallback=%p any=%p → chose %p", leon_seen, best_local, rp_fallback, any_fallback, result); s_last_logged = result; } s_cached = result; s_cache_ms = now; return { result }; }

    FVector transform_apply_point(const FTransform& t, const FVector& p)
    {
        const double sx = p.X * t.Scale3D.X;
        const double sy = p.Y * t.Scale3D.Y;
        const double sz = p.Z * t.Scale3D.Z;
        const double qx = t.Rotation.X, qy = t.Rotation.Y, qz = t.Rotation.Z, qw = t.Rotation.W;
        const double cx1 = qy * sz - qz * sy;
        const double cy1 = qz * sx - qx * sz;
        const double cz1 = qx * sy - qy * sx;
        const double cx2 = qy * cz1 - qz * cy1;
        const double cy2 = qz * cx1 - qx * cz1;
        const double cz2 = qx * cy1 - qy * cx1;
        return { static_cast<float>(sx + 2.0 * (qw * cx1 + cx2) + t.Translation.X), static_cast<float>(sy + 2.0 * (qw * cy1 + cy2) + t.Translation.Y), static_cast<float>(sz + 2.0 * (qw * cz1 + cz2) + t.Translation.Z) };
    }

    FTransform component_to_world(UObject* scene_component)
    {
        FTransform out{};
        out.Rotation = { 0.0, 0.0, 0.0, 1.0 };
        out.Scale3D = { 1.0, 1.0, 1.0 };
        if (!scene_component) return out;
        constexpr uintptr_t kKnownOffsets[] = { 0x01C0, 0x01D0, 0x01E0, 0x01F0, 0x0200, 0x0210, 0x0220 };
        for (auto off : kKnownOffsets)
        {
            const auto p = reinterpret_cast<uintptr_t>(scene_component) + off;
            FTransform c{};
            __try { c = *reinterpret_cast<const FTransform*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
            const double qm = c.Rotation.X * c.Rotation.X + c.Rotation.Y * c.Rotation.Y + c.Rotation.Z * c.Rotation.Z + c.Rotation.W * c.Rotation.W;
            if (qm > 0.9 && qm < 1.1 && c.Scale3D.X > 0.01 && c.Scale3D.X < 100.0 && std::isfinite(c.Translation.X) && std::isfinite(c.Translation.Y) && std::isfinite(c.Translation.Z)) return c;
        }
        return out;
    }
}

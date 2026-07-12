#include "esp.hpp"

#include "../core/logger.hpp"
#include "../sdk/ue_object.hpp"
#include "../../third-party/imgui/imgui.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <string>

namespace ce::features
{
    using ce::ue::FVector;
    using ce::ue::FVector2D;
    using ce::ue::FRotator;
    using ce::ue::FName;
    using ce::ue::UObject;
    using ce::ue::UClass;
    using ce::ue::UFunction;

    namespace
    {

        inline bool valid_ptr(const void* p)
        {
            const uintptr_t a = reinterpret_cast<uintptr_t>(p);
            return a >= 0x00010000ULL && a < 0x00007FFF'FFFFFFFFULL;
        }

        
        inline bool safe_readable_esp(const void* p, size_t n) noexcept
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

        struct Fns
        {
            UFunction* PC_ProjectWorldLocationToScreen = nullptr;
            UFunction* PC_LineOfSightTo = nullptr;
            UFunction* Actor_K2_GetActorLocation = nullptr;
            UFunction* SMC_GetBoneName = nullptr;
            UFunction* SC_GetSocketLocation = nullptr;
            UFunction* Pawn_GetPlayerState = nullptr;
            UFunction* PS_GetPlayerName = nullptr;
        };

        static Fns s_fn;
        static bool s_fn_ready = false;

        void ensure_fns_for(UObject* obj)
        {
            if (s_fn_ready || !obj || !obj->ClassPrivate) return;
            
            s_fn.PC_ProjectWorldLocationToScreen = ce::ue::find_function_recursive(obj->ClassPrivate, "ProjectWorldLocationToScreen");
            s_fn.PC_LineOfSightTo = ce::ue::find_function_recursive(obj->ClassPrivate, "LineOfSightTo");
            s_fn.Actor_K2_GetActorLocation = ce::ue::find_function_recursive(obj->ClassPrivate, "K2_GetActorLocation");
            if (s_fn.PC_ProjectWorldLocationToScreen || s_fn.PC_LineOfSightTo)
            {
                s_fn_ready = true;
                ce::log::info("[esp] fn cache: W2S=%p LOS=%p ActorLoc=%p", s_fn.PC_ProjectWorldLocationToScreen, s_fn.PC_LineOfSightTo, s_fn.Actor_K2_GetActorLocation);
            }
        }

        UFunction* ensure_mesh_fn(UClass** slot_class, UFunction** slot_fn, UObject* mesh, const char* name)
        {
            if (*slot_fn) return *slot_fn;
            if (!valid_ptr(mesh)) return nullptr;
            if (!safe_readable_esp(mesh, sizeof(UObject))) return nullptr;
            if (!valid_ptr(mesh->ClassPrivate)) return nullptr;
            *slot_fn = ce::ue::find_function_recursive(mesh->ClassPrivate, name);
            if (*slot_fn) *slot_class = mesh->ClassPrivate;
            return *slot_fn;
        }

        
        FVector actor_world_location(UObject* actor)
        {
            if (!actor) return {};
            auto* root = *actor->get_ptr<UObject*>(0x01B8);
            if (!root) return {};
            return *reinterpret_cast<FVector*>(reinterpret_cast<uintptr_t>(root) + 0x0140);
        }

        std::string to_lower(std::string s)
        {
            for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            return s;
        }

        
        struct BonePair
        {
            std::initializer_list<const char*> a;
            std::initializer_list<const char*> b;
        };
        static const BonePair kBoneConnections[] = {
            { {"head","Head"}, {"neck","neck_01","Neck"} }, { {"neck","neck_01","Neck"}, {"spine3","spine_03","spine_05","Spine2","Spine1","spine"} }, { {"spine3","spine_03","Spine2"}, {"spine2","spine_02","Spine1"} }, { {"spine2","spine_02","Spine1"}, {"spine1","spine_01","spine","pelvis","Hips"} }, { {"shoulder_L","clavicle_l","LeftShoulder"}, {"spine3","spine_03","spine"} }, { {"upper_arm_L","upperarm_l","LeftArm"}, {"shoulder_L","clavicle_l","LeftShoulder"} }, { {"lower_arm_L","lowerarm_l","LeftForeArm"}, {"upper_arm_L","upperarm_l","LeftArm"} }, { {"hand_L","hand_l","LeftHand"}, {"lower_arm_L","lowerarm_l","LeftForeArm"} }, { {"shoulder_R","clavicle_r","RightShoulder"}, {"spine3","spine_03","spine"} }, { {"upper_arm_R","upperarm_r","RightArm"}, {"shoulder_R","clavicle_r","RightShoulder"} }, { {"lower_arm_R","lowerarm_r","RightForeArm"}, {"upper_arm_R","upperarm_r","RightArm"} }, { {"hand_R","hand_r","RightHand"}, {"lower_arm_R","lowerarm_r","RightForeArm"} }, { {"hip_L","thigh_l","LeftUpLeg"}, {"spine1","spine_01","pelvis","Hips"} }, { {"upper_leg_L","thigh_l","LeftUpLeg"}, {"hip_L","spine1","spine_01"} }, { {"lower_leg_L","calf_l","LeftLeg"}, {"upper_leg_L","thigh_l","LeftUpLeg"} }, { {"foot_l","foot_L","LeftFoot"}, {"lower_leg_L","calf_l","LeftLeg"} }, { {"hip_R","thigh_r","RightUpLeg"}, {"spine1","spine_01","pelvis","Hips"} }, { {"upper_leg_R","thigh_r","RightUpLeg"}, {"hip_R","spine1","spine_01"} }, { {"lower_leg_R","calf_r","RightLeg"}, {"upper_leg_R","thigh_r","RightUpLeg"} }, { {"foot_r","foot_R","RightFoot"}, {"lower_leg_R","calf_r","RightLeg"} }, };

        int find_bone(const ESP::SkeletonInfo* info, std::initializer_list<const char*> names)
        {
            if (!info) return -1;
            for (auto* n : names)
            {
                auto it = info->name_to_index.find(to_lower(n));
                if (it != info->name_to_index.end()) return it->second;
            }
            return -1;
        }
    }

    ESP::ESP() = default;

    
    
    
    
    namespace paintman_bones {
        constexpr int spine1 = 2;
        constexpr int spine2 = 3;
        constexpr int spine3 = 4;
        constexpr int neck = 5;
        constexpr int head = 6;
        constexpr int shoulder_L = 8;
        constexpr int upper_arm_L = 9;
        constexpr int lower_arm_L = 10;
        constexpr int hand_L = 11;
        constexpr int shoulder_R = 13;
        constexpr int upper_arm_R = 14;
        constexpr int lower_arm_R = 15;
        constexpr int hand_R = 16;
        constexpr int hip_L = 18;
        constexpr int upper_leg_L = 19;
        constexpr int lower_leg_L = 20;
        constexpr int foot_L = 21;
        constexpr int hip_R = 23;
        constexpr int upper_leg_R = 24;
        constexpr int lower_leg_R = 25;
        constexpr int foot_R = 26;
    }

    ESP::SkeletonInfo& ESP::ensure_skeleton(UObject* mesh)
    {
        auto& info = skel_cache_[mesh];
        if (info.dumped) return info;
        info.dumped = true;

        
        using namespace paintman_bones;
        info.head_idx = head;
        info.foot_idx = foot_L;
        info.pair_indices = {
            { head, neck }, { neck, spine3 }, { spine3, spine2 }, { spine2, spine1 }, { shoulder_L, spine3 }, { upper_arm_L, shoulder_L }, { lower_arm_L, upper_arm_L }, { hand_L, lower_arm_L }, { shoulder_R, spine3 }, { upper_arm_R, shoulder_R }, { lower_arm_R, upper_arm_R }, { hand_R, lower_arm_R }, { hip_L, spine1 }, { upper_leg_L, hip_L }, { lower_leg_L, upper_leg_L }, { foot_L, lower_leg_L }, { hip_R, spine1 }, { upper_leg_R, hip_R }, { lower_leg_R, upper_leg_R }, { foot_R, lower_leg_R }, };
        return info;
    }

    void ESP::tick()
    {
        if (!cfg_.enabled) return;

        namespace sc = ce::ue::chameleon;

        
        
        
        
        
        
        
        static UObject* s_pc_cache = nullptr;
        static uint64_t s_pc_cache_ms = 0;
        static uint64_t s_pc_last_scan_ms = 0;

        UObject* pc = nullptr;
        const uint64_t now_pc = GetTickCount64();
        if (valid_ptr(s_pc_cache) && safe_readable_esp(s_pc_cache, sizeof(UObject)) && now_pc - s_pc_cache_ms < 3000)
        {
            pc = s_pc_cache;
        }
        if (!pc)
        {
            
            auto chara = sc::local_leon_character();
            if (chara && valid_ptr(chara.obj))
                pc = sc::controller_of(chara);
            if (!valid_ptr(pc) || !safe_readable_esp(pc, sizeof(UObject)))
                pc = nullptr;
        }
        if (!pc && now_pc - s_pc_last_scan_ms > 200)
        {
            
            
            s_pc_last_scan_ms = now_pc;
            auto* arr2 = ce::ue::gobjects();
            if (arr2)
            {
                for (int32_t i = 0; i < arr2->NumElements; ++i)
                {
                    UObject* obj = arr2->at(i);
                    if (!valid_ptr(obj) || !safe_readable_esp(obj, sizeof(UObject))) continue;
                    UClass* cls = obj->ClassPrivate;
                    if (!valid_ptr(cls) || !safe_readable_esp(cls, sizeof(UObject))) continue;
                    std::string cn = cls->name();
                    if (cn != "PlayerController" && cn.find("PlayerController") == std::string::npos) continue;
                    std::string nm = obj->name();
                    if (nm.rfind("Default__", 0) == 0) continue;
                    pc = obj;
                    break;
                }
            }
        }

        
        
        static uint64_t s_hb_ms = 0;
        auto heartbeat = [&](const char* what) {
            if (now_pc - s_hb_ms > 5000)
            {
                s_hb_ms = now_pc;
                ce::log::info("[esp][HB] %s (pc=%p actors=%zu)", what, pc, actor_cache_.size());
            }
        };

        if (!pc) { heartbeat("no PlayerController"); return; }
        s_pc_cache = pc;
        s_pc_cache_ms = now_pc;

        ensure_fns_for(pc);
        if (!s_fn.PC_ProjectWorldLocationToScreen) { heartbeat("no PC_ProjectWorldLocationToScreen"); return; }

        
        UObject* pcm = sc::camera_manager_of(pc);
        sc::CameraSnapshot cam{};
        sc::camera_snapshot(pcm, cam);
        if (!cam.valid)
        {
            heartbeat("camera_snapshot invalid — invalidating PC cache");
            s_pc_cache = nullptr;
            s_pc_cache_ms = 0;
            actor_cache_.clear();
            skel_cache_.clear();
            last_actor_scan_ms_ = 0;
            return;
        }

        
        
        if (std::abs(cam.location.X) > 1e6 || std::abs(cam.location.Y) > 1e6 || std::abs(cam.location.Z) > 1e6)
        {
            heartbeat("camera location implausible");
            s_pc_cache = nullptr;
            s_pc_cache_ms = 0;
            actor_cache_.clear();
            skel_cache_.clear();
            last_actor_scan_ms_ = 0;
            return;
        }

        heartbeat("ok");

        UObject* self_actor = nullptr;
        {
            auto chara2 = sc::local_leon_character();
            if (chara2 && valid_ptr(chara2.obj)) self_actor = chara2.obj;
        }

        const ImU32 col_vis = ImGui::GetColorU32(ImVec4(cfg_.col_visible[0], cfg_.col_visible[1], cfg_.col_visible[2], cfg_.col_visible[3]));
        const ImU32 col_hid = ImGui::GetColorU32(ImVec4(cfg_.col_hidden [0], cfg_.col_hidden [1], cfg_.col_hidden [2], cfg_.col_hidden [3]));
        const ImU32 col_txt = ImGui::GetColorU32(ImVec4(cfg_.col_text [0], cfg_.col_text [1], cfg_.col_text [2], cfg_.col_text [3]));

        auto* dl = ImGui::GetForegroundDrawList();

        auto line_of_sight = [&](UObject* target) -> bool
        {
            if (!cfg_.use_los_check || !s_fn.PC_LineOfSightTo || !pc || !target) return true;

            struct alignas(8) Params
            {
                UObject* ViewTarget;
                FVector ViewPoint;
                bool bAlternateChecks;
                bool ReturnValue;
            };
            static_assert(sizeof(Params) == 40);
            Params p{};
            p.ViewTarget = target;
            p.ViewPoint = {};
            p.bAlternateChecks = false;
            pc->process_event(s_fn.PC_LineOfSightTo, &p);
            return p.ReturnValue;
        };

        ImGuiIO& io = ImGui::GetIO();
        const int vp_w = static_cast<int>(io.DisplaySize.x);
        const int vp_h = static_cast<int>(io.DisplaySize.y);
        const double vp_aspect = (vp_h > 0) ? (double)vp_w / (double)vp_h : 16.0/9.0;
        const double half_fov_rad = cam.fov_deg * 3.14159265358979323846 / 360.0;
        const double tan_half_h = std::tan(half_fov_rad);
        const double tan_half_v = tan_half_h / std::max(0.001, vp_aspect);

        auto project_w2s = [&](const FVector& world, FVector2D& out_screen) -> bool
        {
            const double rx = world.X - cam.location.X;
            const double ry = world.Y - cam.location.Y;
            const double rz = world.Z - cam.location.Z;
            const double depth = rx*cam.forward.X + ry*cam.forward.Y + rz*cam.forward.Z;
            if (!std::isfinite(depth) || depth <= 1.0) return false;
            const double right_d = rx*cam.right.X + ry*cam.right.Y + rz*cam.right.Z;
            const double up_d = rx*cam.up.X + ry*cam.up.Y + rz*cam.up.Z;
            const double ndc_x = right_d / (depth * tan_half_h);
            const double ndc_y = up_d / (depth * tan_half_v);
            const double sx = (ndc_x * 0.5 + 0.5) * vp_w;
            const double sy = (0.5 - ndc_y * 0.5) * vp_h;
            if (sx < -vp_w || sx > 2*vp_w) return false;
            if (sy < -vp_h || sy > 2*vp_h) return false;
            out_screen = { sx, sy };
            return true;
        };

        struct FString { wchar_t* Data; int32_t Count; int32_t Max; };

        auto read_ptr = [](uintptr_t addr, UObject*& out) noexcept -> bool {
            __try { out = *reinterpret_cast<UObject**>(addr); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        };
        auto read_fstring = [](uintptr_t addr, FString& out) noexcept -> bool {
            __try { out = *reinterpret_cast<FString*>(addr); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        };
        auto read_wchar_buf = [](wchar_t* src, int32_t count, wchar_t* dst) noexcept -> bool {
            __try {
                for (int32_t i = 0; i < count; ++i) { dst[i] = src[i]; if (!src[i]) return true; }
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        };

        auto resolve_player_name = [&](UObject* actor_obj) -> std::string
        {
            if (!valid_ptr(actor_obj) || !safe_readable_esp(actor_obj, 0x300)) return {};
            UObject* ps = nullptr;
            if (!read_ptr(reinterpret_cast<uintptr_t>(actor_obj) + 0x02C8, ps)) return {};
            if (!valid_ptr(ps) || !safe_readable_esp(ps, 0x350)) return {};

            FString name_str{};
            if (!read_fstring(reinterpret_cast<uintptr_t>(ps) + 0x0340, name_str)) return {};
            if (!valid_ptr(name_str.Data) || name_str.Count <= 0 || name_str.Count > 512) return {};

            wchar_t buf[513]{};
            const int32_t n = std::min<int32_t>(name_str.Count, 512);
            if (!read_wchar_buf(name_str.Data, n, buf)) return {};

            std::string out;
            out.reserve(static_cast<size_t>(n));
            for (int32_t i = 0; i < n; ++i)
            {
                if (!buf[i]) break;
                out.push_back(static_cast<char>(buf[i]));
            }
            return out;
        };

        struct FTArrayHdr { void* Data; int32_t Count; int32_t Max; };

        static std::unordered_map<UObject*, uint32_t> s_bone_arr_off;

        auto probe_bone_arr_off = [&](UObject* mesh) -> uint32_t {
            if (auto it = s_bone_arr_off.find(mesh); it != s_bone_arr_off.end()) return it->second;
            constexpr uint32_t kMinOff = 0x0400;
            constexpr uint32_t kMaxOff = 0x0C00;
            for (uint32_t off = kMinOff; off + 16 <= kMaxOff; off += 8)
            {
                FTArrayHdr hdr{};
                __try { hdr = *reinterpret_cast<FTArrayHdr*>(reinterpret_cast<uintptr_t>(mesh) + off); }
                __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
                const uintptr_t data = reinterpret_cast<uintptr_t>(hdr.Data);
                if (data < 0x00010000ULL || data >= 0x00007FFF'FFFFFFFFULL) continue;
                if (hdr.Count < 20 || hdr.Count > 500) continue;
                if (hdr.Max < hdr.Count || hdr.Max > hdr.Count + 128) continue;
                
                double z0 = 0.0;
                __try { z0 = *reinterpret_cast<double*>(data + 0x20 + 16); }
                __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
                if (!std::isfinite(z0)) continue;
                s_bone_arr_off[mesh] = off;
                ce::log::info("[esp] bone-array offset for mesh %p resolved to 0x%X (bones=%d)", mesh, off, hdr.Count);
                return off;
            }
            s_bone_arr_off[mesh] = 0;
            return 0;
        };

        auto socket_location_fast = [&](UObject* mesh, int bone_idx, const ce::ue::chameleon::FTransform& mesh_ctw) -> FVector
        {
            if (!mesh || bone_idx < 0) return {};
            const uint32_t off = probe_bone_arr_off(mesh);
            if (off == 0) return {};
            FTArrayHdr hdr{};
            __try { hdr = *reinterpret_cast<FTArrayHdr*>(reinterpret_cast<uintptr_t>(mesh) + off); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
            if (!hdr.Data || bone_idx >= hdr.Count) return {};
            FVector component_space{};
            __try
            {
                
                const uintptr_t bone_ft = reinterpret_cast<uintptr_t>(hdr.Data) + static_cast<uintptr_t>(bone_idx) * 0x60;
                component_space = *reinterpret_cast<FVector*>(bone_ft + 0x20);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return {}; }
            return ce::ue::chameleon::transform_apply_point(mesh_ctw, component_space);
        };

        
        auto socket_location_slow = [&](UObject* mesh, int bone_idx) -> FVector
        {
            if (!mesh || bone_idx < 0) return {};
            UClass* smc_cls = nullptr;
            static UFunction* fn_socket = nullptr;
            static UFunction* fn_bone_name_by_idx = nullptr;
            ensure_mesh_fn(&smc_cls, &fn_socket, mesh, "GetSocketLocation");
            ensure_mesh_fn(&smc_cls, &fn_bone_name_by_idx, mesh, "GetBoneName");
            if (!fn_socket || !fn_bone_name_by_idx) return {};

            struct BoneParams { int32_t BoneIndex; FName ReturnValue; };
            static_assert(sizeof(BoneParams) == 12);
            BoneParams bp{};
            bp.BoneIndex = bone_idx;
            mesh->process_event(fn_bone_name_by_idx, &bp);

            struct alignas(8) SocketParams { FName InSocketName; FVector ReturnValue; };
            static_assert(sizeof(SocketParams) == 32);
            SocketParams sp{};
            sp.InSocketName = bp.ReturnValue;
            mesh->process_event(fn_socket, &sp);
            return sp.ReturnValue;
        };
    }

    void ESP::draw()
    {
        ImGui::Checkbox("Enabled", &cfg_.enabled);
        ImGui::Separator();
        ImGui::Checkbox("Skeleton", &cfg_.draw_skeleton);
        ImGui::Checkbox("Box", &cfg_.draw_box);
        ImGui::Checkbox("Name", &cfg_.draw_name);
        ImGui::Checkbox("Distance", &cfg_.draw_distance);
        ImGui::Separator();
        ImGui::Checkbox("Show Hiders", &cfg_.esp_hiders);
        ImGui::Checkbox("Show Hunters", &cfg_.esp_hunters);
        ImGui::Checkbox("Hide dead", &cfg_.hide_dead);
        ImGui::Checkbox("Use LOS check", &cfg_.use_los_check);
        ImGui::Separator();
        ImGui::SliderFloat("Line thickness", &cfg_.line_thickness, 0.5f, 5.0f);
        ImGui::SliderFloat("Box thickness", &cfg_.box_thickness, 0.5f, 5.0f);
        ImGui::SliderFloat("Max distance (m)", &cfg_.max_distance_m, 10.0f, 500.0f);
    }
}

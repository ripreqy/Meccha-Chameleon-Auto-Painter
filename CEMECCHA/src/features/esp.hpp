#pragma once

#include "feature.hpp"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include "../sdk/chameleon.hpp"

namespace ce::features
{

    class ESP final : public Feature
    {
    public:
        struct Config
        {
            bool enabled = true;
            bool draw_skeleton = true;
            bool draw_box = true;
            bool draw_name = true;
            bool draw_distance = true;
            bool esp_hiders = true;
            bool esp_hunters = true;
            bool hide_dead = true;

            float col_visible[4] { 0.0f, 1.0f, 0.0f, 1.0f };
            float col_hidden [4] { 1.0f, 0.0f, 0.0f, 1.0f };
            float col_text [4] { 1.0f, 1.0f, 1.0f, 1.0f };
            float line_thickness = 1.5f;
            float box_thickness = 1.5f;

            bool use_los_check = true;

            float max_distance_m = 200.0f;
        };

        ESP();
        ~ESP() override = default;

        std::string_view name() const override { return "ESP"; }
        void tick() override;
        void draw() override;

        Config& config() { return cfg_; }

        struct SkeletonInfo
        {
            bool dumped = false;
            std::unordered_map<std::string, int> name_to_index;

            int head_idx = -1;
            int foot_idx = -1;
            std::vector<std::pair<int,int>> pair_indices;
        };

    private:
        SkeletonInfo& ensure_skeleton(ce::ue::UObject* mesh);

        Config cfg_{};

        std::unordered_map<ce::ue::UObject*, SkeletonInfo> skel_cache_;

        struct CachedActor
        {
            ce::ue::UObject* actor = nullptr;
            bool is_hider = false;
            ce::ue::UObject* player_state = nullptr;
            std::string player_name;
            uint64_t name_refresh_ms = 0;

            bool los_visible = true;
            uint64_t los_refresh_ms = 0;
        };
        std::vector<CachedActor> actor_cache_;
        uint64_t last_actor_scan_ms_ = 0;
    };
}

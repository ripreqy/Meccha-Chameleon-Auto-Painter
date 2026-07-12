#pragma once

#include "feature.hpp"

#include <memory>
#include <vector>

namespace ce::features
{
    class Manager
    {
    public:
        static Manager& get();

        template <typename T, typename... Args> T& add(Args&&... args)
        {
            auto p = std::make_unique<T>(std::forward<Args>(args)...);
            T& ref = *p;
            features_.emplace_back(std::move(p));
            return ref;
        }

        void tick();
        void draw();

        template <typename T> T* get_by_name(std::string_view name)
        {
            for (auto& f : features_)
                if (f->name() == name) return static_cast<T*>(f.get());
            return nullptr;
        }

    private:
        std::vector<std::unique_ptr<Feature>> features_;
    };
}

#pragma once

#include <string_view>

namespace ce::features
{
    class Feature
    {
    public:
        virtual ~Feature() = default;

        virtual std::string_view name() const = 0;
        virtual void tick() = 0;
        virtual void draw() = 0;
    };
}

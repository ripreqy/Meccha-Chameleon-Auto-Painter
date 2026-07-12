#include "manager.hpp"

namespace ce::features
{
    Manager& Manager::get()
    {
        static Manager s;
        return s;
    }

    void Manager::tick()
    {
        for (auto& f : features_) f->tick();
    }

    void Manager::draw()
    {
        for (auto& f : features_) f->draw();
    }
}

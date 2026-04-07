#include "SystemScheduler.h"

void SystemScheduler::addSystem(std::unique_ptr<ISystem> system)
{
    systems_.push_back(std::move(system));
}

void SystemScheduler::updateAll(RuntimeContext& ctx)
{
    for (auto& sys : systems_)
        sys->update(ctx);
}

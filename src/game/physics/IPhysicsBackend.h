#pragma once

namespace dash::physics {

class IPhysicsBackend {
public:
    virtual ~IPhysicsBackend() = default;
    virtual const char* name() const = 0;
};

class BuiltinPhysicsBackend final : public IPhysicsBackend {
public:
    const char* name() const override { return "builtin"; }
};

} // namespace dash::physics

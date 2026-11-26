#pragma once
// Native (C++) gameplay scripts. For most game code prefer Luau scripts;
// native scripts are for performance-critical or engine-level behaviour.
//
//   class Spinner : public ScriptableEntity {
//       void OnUpdate(float dt) override { Transform().Rotation = ...; }
//   };
//   entity.Add<NativeScriptComponent>().Bind<Spinner>();

#include <functional>

#include "Lapis/Scene/Entity.h"

namespace Lapis {

class ScriptableEntity {
public:
    virtual ~ScriptableEntity() = default;

    virtual void OnCreate() {}
    virtual void OnUpdate(float dt) { LAPIS_UNUSED(dt); }
    virtual void OnDestroy() {}

    Entity Self;

protected:
    template <typename T> T& Get() { return Self.Get<T>(); }
    TransformComponent& Transform() { return Self.Transform(); }
};

struct NativeScriptComponent {
    std::shared_ptr<ScriptableEntity> Instance;
    std::function<std::shared_ptr<ScriptableEntity>()> Instantiate;

    template <typename T, typename... Args>
    NativeScriptComponent& Bind(Args... args) {
        Instantiate = [args...]() { return std::make_shared<T>(args...); };
        return *this;
    }
};

} // namespace Lapis

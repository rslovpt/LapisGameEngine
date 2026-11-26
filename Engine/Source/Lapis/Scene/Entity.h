#pragma once
// Convenience handle = EntityID + owning scene.

#include <string>

#include "Lapis/Scene/Components.h"
#include "Lapis/Scene/Registry.h"

namespace Lapis {

class Scene;

class Entity {
public:
    Entity() = default;
    Entity(EntityID id, Scene* scene) : m_ID(id), m_Scene(scene) {}

    template <typename T, typename... Args> T& Add(Args&&... args);
    template <typename T> T&   Get();
    template <typename T> T*   TryGet();
    template <typename T> bool Has() const;
    template <typename T> void Remove();

    bool IsValid() const;
    explicit operator bool() const { return IsValid(); }

    EntityID GetID() const { return m_ID; }
    Scene*   GetScene() const { return m_Scene; }

    const std::string& GetName() const;
    TransformComponent& Transform() { return Get<TransformComponent>(); }

    bool operator==(const Entity& o) const { return m_ID == o.m_ID && m_Scene == o.m_Scene; }
    bool operator!=(const Entity& o) const { return !(*this == o); }

private:
    EntityID m_ID;
    Scene*   m_Scene = nullptr;
};

} // namespace Lapis

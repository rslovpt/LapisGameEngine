#pragma once
// Entity-component registry built on sparse sets.
//
//   EntityID e = registry.Create();
//   registry.Add<TransformComponent>(e);
//   registry.Each<TransformComponent, MeshRendererComponent>(
//       [](EntityID id, TransformComponent& t, MeshRendererComponent& m) { ... });
//
// Entity IDs carry a generation so stale handles to destroyed entities are
// detected. References returned by Add/Get stay valid until another component
// of the same type is added or removed.

#include <limits>
#include <type_traits>
#include <vector>

#include "Lapis/Core/Assert.h"
#include "Lapis/Core/Base.h"

namespace Lapis {

struct EntityID {
    static constexpr u32 InvalidIndex = std::numeric_limits<u32>::max();
    u32 Index = InvalidIndex;
    u32 Generation = 0;

    bool IsNull() const { return Index == InvalidIndex; }
    explicit operator bool() const { return !IsNull(); }
    bool operator==(const EntityID& o) const { return Index == o.Index && Generation == o.Generation; }
    bool operator!=(const EntityID& o) const { return !(*this == o); }
    u64  Packed() const { return (u64(Generation) << 32) | Index; }
};

namespace Detail {
    inline u32 NextComponentTypeID() { static u32 next = 0; return next++; }
    template <typename T> u32 ComponentTypeID() { static const u32 id = NextComponentTypeID(); return id; }
}

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual bool Has(u32 index) const = 0;
    virtual void Remove(u32 index) = 0;
    virtual size_t Size() const = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool {
public:
    bool Has(u32 index) const override { return index < m_Sparse.size() && m_Sparse[index] != 0; }

    template <typename... Args>
    T& Add(u32 index, Args&&... args) {
        if (Has(index)) { Get(index) = T{std::forward<Args>(args)...}; return Get(index); }
        if (index >= m_Sparse.size()) m_Sparse.resize(size_t(index) + 1, 0);
        if constexpr (std::is_aggregate_v<T>) m_Dense.push_back(T{std::forward<Args>(args)...});
        else m_Dense.emplace_back(std::forward<Args>(args)...);
        m_Owners.push_back(index);
        m_Sparse[index] = (u32)m_Dense.size(); // dense position + 1
        return m_Dense.back();
    }

    T& Get(u32 index) { LAPIS_ASSERT(Has(index), "component missing"); return m_Dense[m_Sparse[index] - 1]; }
    T* TryGet(u32 index) { return Has(index) ? &m_Dense[m_Sparse[index] - 1] : nullptr; }

    void Remove(u32 index) override {
        if (!Has(index)) return;
        const u32 pos = m_Sparse[index] - 1;
        const u32 last = (u32)m_Dense.size() - 1;
        if (pos != last) {
            m_Dense[pos] = std::move(m_Dense[last]);
            m_Owners[pos] = m_Owners[last];
            m_Sparse[m_Owners[pos]] = pos + 1;
        }
        m_Dense.pop_back();
        m_Owners.pop_back();
        m_Sparse[index] = 0;
    }

    size_t Size() const override { return m_Dense.size(); }
    const std::vector<u32>& Owners() const { return m_Owners; }

private:
    std::vector<u32> m_Sparse;  // entity index -> dense position + 1 (0 = absent)
    std::vector<T>   m_Dense;
    std::vector<u32> m_Owners;  // dense position -> entity index
};

class Registry : NonCopyable {
public:
    EntityID Create() {
        u32 index;
        if (!m_FreeList.empty()) {
            index = m_FreeList.back();
            m_FreeList.pop_back();
        } else {
            index = (u32)m_Generations.size();
            m_Generations.push_back(0);
            m_Alive.push_back(false);
        }
        m_Alive[index] = true;
        ++m_AliveCount;
        return {index, m_Generations[index]};
    }

    void Destroy(EntityID e) {
        if (!IsValid(e)) return;
        for (auto& pool : m_Pools) if (pool) pool->Remove(e.Index);
        m_Alive[e.Index] = false;
        ++m_Generations[e.Index];
        m_FreeList.push_back(e.Index);
        --m_AliveCount;
    }

    bool IsValid(EntityID e) const {
        return !e.IsNull() && e.Index < m_Generations.size() && m_Alive[e.Index] && m_Generations[e.Index] == e.Generation;
    }

    // Current ID for a raw index (or a null ID if nothing lives there).
    EntityID FromIndex(u32 index) const {
        if (index >= m_Generations.size() || !m_Alive[index]) return {};
        return {index, m_Generations[index]};
    }

    size_t AliveCount() const { return m_AliveCount; }

    template <typename T, typename... Args>
    T& Add(EntityID e, Args&&... args) {
        LAPIS_ASSERT(IsValid(e), "Add on invalid entity");
        return Pool<T>().Add(e.Index, std::forward<Args>(args)...);
    }

    template <typename T> bool Has(EntityID e) const {
        const IComponentPool* p = FindPool<T>();
        return IsValid(e) && p && p->Has(e.Index);
    }

    template <typename T> T& Get(EntityID e) { LAPIS_ASSERT(Has<T>(e), "Get: missing component"); return Pool<T>().Get(e.Index); }

    template <typename T> T* TryGet(EntityID e) {
        if (!IsValid(e)) return nullptr;
        auto* p = static_cast<ComponentPool<T>*>(FindPoolMutable<T>());
        return p ? p->TryGet(e.Index) : nullptr;
    }

    template <typename T> void Remove(EntityID e) {
        if (auto* p = FindPoolMutable<T>(); p && IsValid(e)) p->Remove(e.Index);
    }

    template <typename T> size_t Count() const {
        const IComponentPool* p = FindPool<T>();
        return p ? p->Size() : 0;
    }

    // Calls fn(EntityID, Ts&...) for every entity that has all of Ts.
    // Safe against adding/removing components or entities inside fn
    // (entities created during iteration are not visited).
    template <typename First, typename... Rest, typename F>
    void Each(F&& fn) {
        auto* first = static_cast<ComponentPool<First>*>(FindPoolMutable<First>());
        if (!first) return;
        const std::vector<u32> owners = first->Owners(); // snapshot
        for (u32 index : owners) {
            const EntityID e = FromIndex(index);
            if (!Has<First>(e) || !(Has<Rest>(e) && ...)) continue;
            fn(e, Get<First>(e), Get<Rest>(e)...);
        }
    }

    // Calls fn(EntityID) for every live entity.
    template <typename F>
    void EachEntity(F&& fn) const {
        for (u32 i = 0; i < m_Generations.size(); ++i)
            if (m_Alive[i]) fn(EntityID{i, m_Generations[i]});
    }

    void Clear() {
        m_Pools.clear();
        m_Generations.clear();
        m_Alive.clear();
        m_FreeList.clear();
        m_AliveCount = 0;
    }

private:
    template <typename T> ComponentPool<T>& Pool() {
        const u32 id = Detail::ComponentTypeID<T>();
        if (id >= m_Pools.size()) m_Pools.resize(size_t(id) + 1);
        if (!m_Pools[id]) m_Pools[id] = CreateScope<ComponentPool<T>>();
        return *static_cast<ComponentPool<T>*>(m_Pools[id].get());
    }
    template <typename T> const IComponentPool* FindPool() const {
        const u32 id = Detail::ComponentTypeID<T>();
        return id < m_Pools.size() ? m_Pools[id].get() : nullptr;
    }
    template <typename T> IComponentPool* FindPoolMutable() {
        const u32 id = Detail::ComponentTypeID<T>();
        return id < m_Pools.size() ? m_Pools[id].get() : nullptr;
    }

    std::vector<Scope<IComponentPool>> m_Pools;
    std::vector<u32>  m_Generations;
    std::vector<bool> m_Alive;
    std::vector<u32>  m_FreeList;
    size_t m_AliveCount = 0;
};

} // namespace Lapis

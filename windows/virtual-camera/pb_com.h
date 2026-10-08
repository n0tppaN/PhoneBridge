// Tiny COM smart pointer (no WRL dependency, works in MSVC and MinGW).
#pragma once
#include <utility>

namespace pb {

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(T* p) : m_p(p) { if (m_p) m_p->AddRef(); }          // AddRefs
    ComPtr(const ComPtr& o) : m_p(o.m_p) { if (m_p) m_p->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : m_p(o.m_p) { o.m_p = nullptr; }
    ~ComPtr() { reset(); }
    ComPtr& operator=(const ComPtr& o) { if (this != &o) { ComPtr t(o); swap(t); } return *this; }
    ComPtr& operator=(ComPtr&& o) noexcept { if (this != &o) { reset(); m_p = o.m_p; o.m_p = nullptr; } return *this; }

    T* Get() const { return m_p; }
    T* operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }
    T** put() { reset(); return &m_p; }                          // for out-params
    void reset() { if (m_p) { T* t = m_p; m_p = nullptr; t->Release(); } }
    void attach(T* p) { reset(); m_p = p; }                      // takes ownership, no AddRef
    T* detach() { T* t = m_p; m_p = nullptr; return t; }
    void swap(ComPtr& o) { std::swap(m_p, o.m_p); }

private:
    T* m_p = nullptr;
};

}  // namespace pb

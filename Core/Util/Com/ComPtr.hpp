#pragma once

#include <cstdlib>

/// Minimal RAII wrapper for COM pointers.
/// Automatically calls AddRef() on copy and Release() on destruction.
template<typename T>
class ComPtr
{
public:
    ComPtr() noexcept : m_ptr(nullptr) {}
    explicit ComPtr(T* a_ptr) noexcept : m_ptr(a_ptr) {}
    ComPtr(std::nullptr_t) noexcept : m_ptr(nullptr) {}

    ComPtr(const ComPtr& a_other) noexcept : m_ptr(a_other.m_ptr)
    {
        internalAddRef();
    }

    ComPtr(ComPtr&& a_other) noexcept : m_ptr(a_other.m_ptr)
    {
        a_other.m_ptr = nullptr;
    }

    ComPtr& operator=(const ComPtr& a_other) noexcept
    {
        if (this != &a_other)
        {
            internalRelease();
            m_ptr = a_other.m_ptr;
            internalAddRef();
        }
        return *this;
    }

    ComPtr& operator=(ComPtr&& a_other) noexcept
    {
        if (this != std::addressof(a_other))
        {
            internalRelease();

            m_ptr = a_other.m_ptr;
            a_other.m_ptr = nullptr;
        }

        return *this;
    }

    ~ComPtr() noexcept
    {
        internalRelease();
    }

    /// Access raw pointer (no AddRef).
    T* get() const noexcept { return m_ptr; }

    T* operator->() const noexcept { return m_ptr; }

    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    bool operator==(std::nullptr_t) const noexcept
    {
        return m_ptr == nullptr;
    }

    bool operator!=(std::nullptr_t) const noexcept
    {
        return m_ptr != nullptr;
    }

    /// For COM output parameters: releases current pointer, returns address.
    T** operator&() noexcept
    {
        internalRelease();
        return &m_ptr;
    }

    /// Release and set to null.
    void Release() noexcept
    {
        internalRelease();
        m_ptr = nullptr;
    }

    /// Take ownership of a raw pointer (assumes caller's ref, no AddRef).
    void Attach(T* a_ptr) noexcept
    {
        internalRelease();
        m_ptr = a_ptr;
    }

    /// Release ownership without decrementing refcount.
    T* Detach() noexcept
    {
        T* _ptr = m_ptr;
        m_ptr = nullptr;
        return _ptr;
    }

    /// Release and re-query.
    template<typename U>
    HRESULT As(IID a_iid, ComPtr<U>& aout) const noexcept
    {
        return m_ptr ? m_ptr->QueryInterface(a_iid, reinterpret_cast<void**>(&aout)) : E_POINTER;
    }

protected:
    void internalAddRef() noexcept
    {
        if (m_ptr) { m_ptr->AddRef(); }
    }

    void internalRelease() noexcept
    {
        if (m_ptr) 
        { 
            m_ptr->Release(); 
            m_ptr = nullptr;
        }
    }

private:
    T* m_ptr;
};
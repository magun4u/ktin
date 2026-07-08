#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// win_handle.h
class UniqueHandle
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.handle_);
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    bool IsValid() const
    {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Get() const { return handle_; }

    HANDLE Release()
    {
        HANDLE out = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return out;
    }

    void Reset(HANDLE handle = INVALID_HANDLE_VALUE)
    {
        if (IsValid())
            CloseHandle(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

inline void ResetWinHandleRef(HANDLE& handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    handle = nullptr;
}

// win_gdi.h
class UniqueGdiObject
{
public:
    UniqueGdiObject() = default;
    explicit UniqueGdiObject(HGDIOBJ object) : object_(object) {}
    ~UniqueGdiObject() { Reset(); }

    UniqueGdiObject(const UniqueGdiObject&) = delete;
    UniqueGdiObject& operator=(const UniqueGdiObject&) = delete;

    UniqueGdiObject(UniqueGdiObject&& other) noexcept : object_(other.object_)
    {
        other.object_ = nullptr;
    }

    UniqueGdiObject& operator=(UniqueGdiObject&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.object_);
            other.object_ = nullptr;
        }
        return *this;
    }

    bool IsValid() const { return object_ != nullptr; }
    HGDIOBJ Get() const { return object_; }

    HGDIOBJ Release()
    {
        HGDIOBJ out = object_;
        object_ = nullptr;
        return out;
    }

    void Reset(HGDIOBJ object = nullptr)
    {
        if (object_)
            DeleteObject(object_);
        object_ = object;
    }

private:
    HGDIOBJ object_ = nullptr;
};

class ScopedSelectObject
{
public:
    ScopedSelectObject(HDC hdc, HGDIOBJ object) : hdc_(hdc)
    {
        if (!hdc_ || !object)
            return;
        old_ = SelectObject(hdc_, object);
        if (old_ == HGDI_ERROR)
            old_ = nullptr;
    }

    ~ScopedSelectObject()
    {
        if (hdc_ && old_)
            SelectObject(hdc_, old_);
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

    bool IsSelected() const { return old_ != nullptr; }

private:
    HDC hdc_ = nullptr;
    HGDIOBJ old_ = nullptr;
};

template <typename T>
inline void ResetGdiObjectRef(T& object)
{
    if (object)
    {
        DeleteObject((HGDIOBJ)object);
        object = nullptr;
    }
}

inline bool FillSolidRect(HDC hdc, const RECT& rc, COLORREF color)
{
    if (!hdc)
        return false;

    UniqueGdiObject brush(CreateSolidBrush(color));
    if (!brush.IsValid())
        return false;

    return FillRect(hdc, &rc, static_cast<HBRUSH>(brush.Get())) != 0;
}

// win_dc.h
class ScopedWindowDC
{
public:
    explicit ScopedWindowDC(HWND hwnd) : hwnd_(hwnd), hdc_(GetDC(hwnd)) {}
    ~ScopedWindowDC()
    {
        if (hdc_)
            ReleaseDC(hwnd_, hdc_);
    }

    ScopedWindowDC(const ScopedWindowDC&) = delete;
    ScopedWindowDC& operator=(const ScopedWindowDC&) = delete;

    HDC Get() const { return hdc_; }
    explicit operator bool() const { return hdc_ != nullptr; }

private:
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
};

class UniqueMemoryDC
{
public:
    UniqueMemoryDC() = default;
    explicit UniqueMemoryDC(HDC compatibleDc)
        : hdc_(compatibleDc ? CreateCompatibleDC(compatibleDc) : nullptr)
    {
    }

    ~UniqueMemoryDC()
    {
        if (hdc_)
            DeleteDC(hdc_);
    }

    UniqueMemoryDC(const UniqueMemoryDC&) = delete;
    UniqueMemoryDC& operator=(const UniqueMemoryDC&) = delete;

    UniqueMemoryDC(UniqueMemoryDC&& other) noexcept : hdc_(other.hdc_)
    {
        other.hdc_ = nullptr;
    }

    UniqueMemoryDC& operator=(UniqueMemoryDC&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.hdc_);
            other.hdc_ = nullptr;
        }
        return *this;
    }

    HDC Get() const { return hdc_; }
    explicit operator bool() const { return hdc_ != nullptr; }

    void Reset(HDC hdc = nullptr)
    {
        if (hdc_)
            DeleteDC(hdc_);
        hdc_ = hdc;
    }

private:
    HDC hdc_ = nullptr;
};

// win_paint.h
class ScopedPaintDC
{
public:
    explicit ScopedPaintDC(HWND hwnd) : hwnd_(hwnd), hdc_(BeginPaint(hwnd_, &ps_)) {}
    ~ScopedPaintDC()
    {
        if (hdc_)
            EndPaint(hwnd_, &ps_);
    }

    ScopedPaintDC(const ScopedPaintDC&) = delete;
    ScopedPaintDC& operator=(const ScopedPaintDC&) = delete;

    HDC Get() const { return hdc_; }
    PAINTSTRUCT& PaintStruct() { return ps_; }
    explicit operator bool() const { return hdc_ != nullptr; }

private:
    HWND hwnd_ = nullptr;
    PAINTSTRUCT ps_{};
    HDC hdc_ = nullptr;
};

// win_menu.h
class UniqueMenu
{
public:
    UniqueMenu() = default;
    explicit UniqueMenu(HMENU menu) : menu_(menu) {}
    ~UniqueMenu() { Reset(); }

    UniqueMenu(const UniqueMenu&) = delete;
    UniqueMenu& operator=(const UniqueMenu&) = delete;

    UniqueMenu(UniqueMenu&& other) noexcept : menu_(other.menu_)
    {
        other.menu_ = nullptr;
    }

    UniqueMenu& operator=(UniqueMenu&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.menu_);
            other.menu_ = nullptr;
        }
        return *this;
    }

    bool IsValid() const { return menu_ != nullptr; }
    HMENU Get() const { return menu_; }

    HMENU Release()
    {
        HMENU out = menu_;
        menu_ = nullptr;
        return out;
    }

    void Reset(HMENU menu = nullptr)
    {
        if (menu_)
            DestroyMenu(menu_);
        menu_ = menu;
    }

private:
    HMENU menu_ = nullptr;
};

inline void ResetMenuRef(HMENU& menu)
{
    if (menu)
    {
        DestroyMenu(menu);
        menu = nullptr;
    }
}
inline bool ReplaceWindowMenu(HWND hwnd, HMENU menu)
{
    if (!hwnd)
    {
        if (menu)
            DestroyMenu(menu);
        return false;
    }

    HMENU oldMenu = GetMenu(hwnd);
    if (!SetMenu(hwnd, menu))
    {
        if (menu)
            DestroyMenu(menu);
        return false;
    }

    if (oldMenu && oldMenu != menu)
        DestroyMenu(oldMenu);

    DrawMenuBar(hwnd);
    return true;
}

// win_find.h
class UniqueFindHandle
{
public:
    UniqueFindHandle() = default;
    explicit UniqueFindHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueFindHandle() { Reset(); }

    UniqueFindHandle(const UniqueFindHandle&) = delete;
    UniqueFindHandle& operator=(const UniqueFindHandle&) = delete;

    UniqueFindHandle(UniqueFindHandle&& other) noexcept : handle_(other.handle_)
    {
        other.handle_ = INVALID_HANDLE_VALUE;
    }

    UniqueFindHandle& operator=(UniqueFindHandle&& other) noexcept
    {
        if (this != &other)
        {
            Reset(other.handle_);
            other.handle_ = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    bool IsValid() const
    {
        return handle_ && handle_ != INVALID_HANDLE_VALUE;
    }

    HANDLE Get() const { return handle_; }

    HANDLE Release()
    {
        HANDLE out = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return out;
    }

    void Reset(HANDLE handle = INVALID_HANDLE_VALUE)
    {
        if (IsValid())
            FindClose(handle_);
        handle_ = handle;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

// win_rect.h
inline int RectWidth(const RECT& rc)
{
    return static_cast<int>(rc.right - rc.left);
}

inline int RectHeight(const RECT& rc)
{
    return static_cast<int>(rc.bottom - rc.top);
}

inline int RectWidthAtLeast(const RECT& rc, int minValue)
{
    const int width = RectWidth(rc);
    return width < minValue ? minValue : width;
}

inline int RectHeightAtLeast(const RECT& rc, int minValue)
{
    const int height = RectHeight(rc);
    return height < minValue ? minValue : height;
}

// win_proc_attr.h
class UniqueProcThreadAttributeList
{
public:
    UniqueProcThreadAttributeList() = default;
    ~UniqueProcThreadAttributeList() { Reset(); }

    UniqueProcThreadAttributeList(const UniqueProcThreadAttributeList&) = delete;
    UniqueProcThreadAttributeList& operator=(const UniqueProcThreadAttributeList&) = delete;

    UniqueProcThreadAttributeList(UniqueProcThreadAttributeList&& other) noexcept
        : list_(other.list_)
    {
        other.list_ = nullptr;
    }

    UniqueProcThreadAttributeList& operator=(UniqueProcThreadAttributeList&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            list_ = other.list_;
            other.list_ = nullptr;
        }
        return *this;
    }

    bool Allocate(DWORD count)
    {
        Reset();

        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        if (size == 0)
            return false;

        auto* list = static_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
            HeapAlloc(GetProcessHeap(), 0, size));
        if (!list)
            return false;

        if (!InitializeProcThreadAttributeList(list, count, 0, &size))
        {
            HeapFree(GetProcessHeap(), 0, list);
            return false;
        }

        list_ = list;
        return true;
    }

    PPROC_THREAD_ATTRIBUTE_LIST Get() const { return list_; }
    bool IsValid() const { return list_ != nullptr; }

    void Reset()
    {
        if (list_)
        {
            DeleteProcThreadAttributeList(list_);
            HeapFree(GetProcessHeap(), 0, list_);
            list_ = nullptr;
        }
    }

private:
    PPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

// win_timer.h
inline void KillWinTimer(HWND hwnd, UINT_PTR timerId)
{
    if (hwnd)
        KillTimer(hwnd, timerId);
}

inline bool StartWinTimer(HWND hwnd, UINT_PTR timerId, UINT intervalMs, TIMERPROC proc = nullptr)
{
    return hwnd && SetTimer(hwnd, timerId, intervalMs, proc) != 0;
}

inline bool RestartWinTimer(HWND hwnd, UINT_PTR timerId, UINT intervalMs, TIMERPROC proc = nullptr)
{
    KillWinTimer(hwnd, timerId);
    return StartWinTimer(hwnd, timerId, intervalMs, proc);
}


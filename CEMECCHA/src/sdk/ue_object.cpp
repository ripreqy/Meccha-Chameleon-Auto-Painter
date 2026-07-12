#include "ue_object.hpp"

#include "../core/globals.hpp"
#include "../core/logger.hpp"

#include <Windows.h>
#include <mutex>
#include <unordered_map>
#include <string>

namespace
{
    std::mutex g_reflection_mtx;
    std::unordered_map<std::string, ce::ue::UObject*> g_object_cache;
    std::unordered_map<std::string, ce::ue::UClass*> g_class_cache;
    std::unordered_map<std::string, ce::ue::UFunction*> g_function_cache;

    using AppendStringFn = void (*)(const ce::ue::FName*, wchar_t*);

    bool safe_readable(const void* p, size_t n) noexcept
    {
        __try
        {
            volatile uint8_t sink = 0;
            const auto* b = static_cast<const uint8_t*>(p);
            for (size_t i = 0; i < n; ++i) sink ^= b[i];
            (void)sink;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
}

namespace ce::ue
{
    TUObjectArray* gobjects()
    {
        return reinterpret_cast<TUObjectArray*>(ce::globals::g_gobjects_addr);
    }

    UObject* gworld()
    {
        return *reinterpret_cast<UObject**>(ce::globals::g_gworld_addr);
    }

    namespace
    {
        struct FString { wchar_t* data; int32_t count; int32_t max; };

        bool safe_append_string(AppendStringFn fn, FName* tmp, FString* s) noexcept
        {
            __try { fn(tmp, reinterpret_cast<wchar_t*>(s)); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    std::string name_from_index(int32_t index)
    {
        FName tmp{ index, 0 };
        wchar_t buf[512] = {};
        auto fn = reinterpret_cast<AppendStringFn>(ce::globals::g_append_string_addr);
        if (!fn) return {};

        FString s{ buf, 0, 512 };
        if (!safe_append_string(fn, &tmp, &s)) return {};

        std::string out;
        out.reserve(static_cast<size_t>(s.count));
        for (int32_t i = 0; i < s.count; ++i)
        {
            wchar_t c = s.data[i];
            if (!c) break;
            out.push_back(static_cast<char>(c));
        }
        return out;
    }

    std::string UObject::name() const
    {
        return name_from_index(NamePrivate.ComparisonIndex);
    }

    std::string UObject::full_name() const
    {
        std::string result;
        if (ClassPrivate) result = ClassPrivate->name() + " ";

        const UObject* cur = OuterPrivate;
        std::string trail;
        while (cur)
        {
            trail = cur->name() + (trail.empty() ? "" : ".") + trail;
            cur = cur->OuterPrivate;
        }
        if (!trail.empty()) result += trail + ".";
        result += name();
        return result;
    }

    bool UObject::is_a(UClass* target) const
    {
        UStruct* cur = reinterpret_cast<UStruct*>(ClassPrivate);
        while (cur)
        {
            if (cur == reinterpret_cast<UStruct*>(target)) return true;
            cur = cur->SuperStruct;
        }
        return false;
    }

    namespace
    {
        using ProcessEventFn = void (*)(UObject*, UFunction*, void*);

        bool safe_process_event(UObject* self, ProcessEventFn pe, UFunction* fn, void* params) noexcept
        {
            __try
            {
                pe(self, fn, params);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
    }

    void UObject::process_event(UFunction* fn, void* params)
    {
        if (!fn || !Vtable) return;
        constexpr int PROCESS_EVENT_IDX = 0x4C;

        if (!safe_readable(this, sizeof(UObject))) return;
        auto vtbl = *reinterpret_cast<void***>(this);
        if (!vtbl) return;
        if (!safe_readable(vtbl, (PROCESS_EVENT_IDX + 1) * sizeof(void*))) return;

        auto pe = reinterpret_cast<ProcessEventFn>(vtbl[PROCESS_EVENT_IDX]);
        if (!pe) return;

        (void)safe_process_event(this, pe, fn, params);
    }

    UObject* find_object(std::string_view full_name)
    {
        std::lock_guard<std::mutex> lk(g_reflection_mtx);

        std::string key(full_name);
        if (auto it = g_object_cache.find(key); it != g_object_cache.end()) return it->second;

        auto* arr = gobjects();
        if (!arr) return nullptr;

        for (int32_t i = 0; i < arr->NumElements; ++i)
        {
            UObject* obj = arr->at(i);
            if (!obj) continue;
            if (!safe_readable(obj, sizeof(UObject))) continue;

            if (obj->full_name() == full_name)
            {
                g_object_cache.emplace(key, obj);
                return obj;
            }
        }
        return nullptr;
    }

    UClass* find_class(std::string_view full_name)
    {
        std::string key(full_name);
        {
            std::lock_guard<std::mutex> lk(g_reflection_mtx);
            if (auto it = g_class_cache.find(key); it != g_class_cache.end()) return it->second;
        }

        std::string prefixed = "Class " + key;
        UObject* o = find_object(prefixed);
        if (!o)
        {
            prefixed = "BlueprintGeneratedClass " + key;
            o = find_object(prefixed);
        }

        auto* c = reinterpret_cast<UClass*>(o);
        std::lock_guard<std::mutex> lk(g_reflection_mtx);
        g_class_cache.emplace(std::move(key), c);
        return c;
    }

    UFunction* find_function(std::string_view full_name)
    {
        std::string key(full_name);
        {
            std::lock_guard<std::mutex> lk(g_reflection_mtx);
            if (auto it = g_function_cache.find(key); it != g_function_cache.end()) return it->second;
        }
        std::string prefixed = "Function " + key;
        auto* fn = reinterpret_cast<UFunction*>(find_object(prefixed));
        std::lock_guard<std::mutex> lk(g_reflection_mtx);
        g_function_cache.emplace(std::move(key), fn);
        return fn;
    }

    UFunction* find_function(UClass* owner, std::string_view fn_name)
    {
        if (!owner) return nullptr;
        std::string full = owner->full_name();
        auto pos = full.find(' ');
        if (pos != std::string::npos) full.erase(0, pos + 1);
        full += ".";
        full += fn_name;
        return find_function(full);
    }

    UClass* find_class_by_name(std::string_view name, std::string_view expected_class_kind)
    {
        std::lock_guard<std::mutex> lk(g_reflection_mtx);

        std::string key = std::string(expected_class_kind) + "|" + std::string(name);
        if (auto it = g_class_cache.find(key); it != g_class_cache.end()) return it->second;

        auto* arr = gobjects();
        if (!arr) return nullptr;

        for (int32_t i = 0; i < arr->NumElements; ++i)
        {
            UObject* obj = arr->at(i);
            if (!obj) continue;
            if (!safe_readable(obj, sizeof(UObject))) continue;

            if (obj->name() != name) continue;

            UClass* cls = obj->ClassPrivate;
            if (!cls) continue;
            if (!safe_readable(cls, sizeof(UObject))) continue;

            if (!expected_class_kind.empty())
            {
                if (cls->name() != expected_class_kind) continue;
            }

            auto* uc = reinterpret_cast<UClass*>(obj);
            g_class_cache.emplace(std::move(key), uc);
            return uc;
        }
        return nullptr;
    }

    UFunction* find_function_by_name(UClass* owner, std::string_view fn_name)
    {
        if (!owner) return nullptr;
        if (!safe_readable(owner, sizeof(UObject))) return nullptr;

        std::lock_guard<std::mutex> lk(g_reflection_mtx);

        std::string owner_name = owner->name();
        std::string key = owner_name + "::" + std::string(fn_name);
        if (auto it = g_function_cache.find(key); it != g_function_cache.end()) return it->second;

        auto* arr = gobjects();
        if (!arr) return nullptr;

        for (int32_t i = 0; i < arr->NumElements; ++i)
        {
            UObject* obj = arr->at(i);
            if (!obj) continue;
            if (!safe_readable(obj, sizeof(UObject))) continue;

            if (obj->name() != fn_name) continue;

            UObject* outer = obj->OuterPrivate;
            if (outer != owner) continue;

            UClass* cls = obj->ClassPrivate;
            if (!cls) continue;
            if (cls->name() != "Function") continue;

            auto* uf = reinterpret_cast<UFunction*>(obj);
            g_function_cache.emplace(std::move(key), uf);
            return uf;
        }
        return nullptr;
    }

    UFunction* find_function_recursive(UClass* owner, std::string_view fn_name)
    {
        UClass* cur = owner;
        int hop_guard = 32;
        while (cur && hop_guard-- > 0)
        {
            const uintptr_t a = reinterpret_cast<uintptr_t>(cur);
            if (a < 0x00010000ULL || a >= 0x00007FFF'FFFFFFFFULL) break;
            if (!safe_readable(cur, sizeof(UStruct))) break;
            if (UFunction* fn = find_function_by_name(cur, fn_name); fn) return fn;
            cur = reinterpret_cast<UClass*>(cur->SuperStruct);
        }
        return nullptr;
    }

    uint32_t resolve_bool_property_offset(UClass* owner, std::string_view prop_name)
    {
        if (!owner) return 0xFFFFFFFF;
        if (!safe_readable(owner, sizeof(UObject))) return 0xFFFFFFFF;

        UClass* chain[32]{};
        int chain_len = 0;
        {
            UClass* cur = owner;
            int hop_guard = 32;
            while (cur && hop_guard-- > 0 && chain_len < 32)
            {
                const uintptr_t a = reinterpret_cast<uintptr_t>(cur);
                if (a < 0x00010000ULL || a >= 0x00007FFF'FFFFFFFFULL) break;
                if (!safe_readable(cur, sizeof(UStruct))) break;
                chain[chain_len++] = cur;
                cur = reinterpret_cast<UClass*>(cur->SuperStruct);
            }
        }

        auto* arr = gobjects();
        if (!arr) return 0xFFFFFFFF;

        for (int32_t i = 0; i < arr->NumElements; ++i)
        {
            UObject* obj = arr->at(i);
            if (!obj) continue;
            if (!safe_readable(obj, sizeof(UObject))) continue;

            if (obj->name() != prop_name) continue;

            UObject* outer = obj->OuterPrivate;
            bool match = false;
            for (int k = 0; k < chain_len; ++k)
                if (outer == chain[k]) { match = true; break; }
            if (!match) continue;

            
            UClass* cls = obj->ClassPrivate;
            if (!cls) continue;
            const std::string cn = cls->name();
            if (cn != "BoolProperty" && cn != "Bool16Property" && cn != "Bool32Property" && cn != "Bool64Property")
                continue;

            const uintptr_t base = reinterpret_cast<uintptr_t>(obj);
            if (!safe_readable(reinterpret_cast<void*>(base + 0x4C), 4)) continue;
            const uint32_t off = *reinterpret_cast<const uint32_t*>(base + 0x4C);
            if (off == 0 || off > 0x100000) continue;
            return off;
        }
        return 0xFFFFFFFF;
    }

    void init_reflection_caches()
    {
        std::lock_guard<std::mutex> lk(g_reflection_mtx);
        g_object_cache.clear();
        g_class_cache.clear();
        g_function_cache.clear();
    }
}

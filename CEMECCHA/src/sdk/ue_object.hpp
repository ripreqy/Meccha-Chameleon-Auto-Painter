#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "ue_types.hpp"

namespace ce::ue
{
    class UObject;
    class UClass;
    class UFunction;

    struct FName
    {
        int32_t ComparisonIndex;
        int32_t Number;

        bool operator==(const FName& o) const { return ComparisonIndex == o.ComparisonIndex && Number == o.Number; }
    };

    struct FUObjectItem
    {
        UObject* Object;
        uint8_t Pad[0x10];
    };
    static_assert(sizeof(FUObjectItem) == 0x18, "FUObjectItem must be 24 bytes (verified via Dumper-7)");

    struct TUObjectArray
    {
        static constexpr int32_t ElementsPerChunk = 0x10000;

        FUObjectItem** Objects;
        uint8_t Pad_8[0x8];
        int32_t MaxElements;
        int32_t NumElements;
        int32_t MaxChunks;
        int32_t NumChunks;

        UObject* at(int32_t idx) const
        {
            if (idx < 0 || idx >= NumElements) return nullptr;
            const int32_t chunk = idx / ElementsPerChunk;
            const int32_t off = idx % ElementsPerChunk;
            if (chunk >= NumChunks) return nullptr;
            FUObjectItem* c = Objects[chunk];
            if (!c) return nullptr;
            return c[off].Object;
        }
    };
    static_assert(sizeof(TUObjectArray) == 0x20, "TUObjectArray must be 32 bytes");

    class UObject
    {
    public:
        void* Vtable;
        int32_t ObjectFlags;
        int32_t InternalIndex;
        UClass* ClassPrivate;
        FName NamePrivate;
        UObject* OuterPrivate;

        std::string name() const;
        std::string full_name() const;
        bool is_a(UClass* target) const;

        template <typename T> T get(uint32_t off) const { return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + off); }

        template <typename T> T* get_ptr(uint32_t off) const { return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + off); }

        template <typename T> void set(uint32_t off, T v) { *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(this) + off) = v; }

        void process_event(UFunction* fn, void* params);
    };

    class UField : public UObject
    {
    public:
        UField* Next;
    };

    class UStruct : public UField
    {
    public:
        uint8_t Pad_30[0x10];
        UStruct* SuperStruct;
    };

    class UClass : public UStruct
    {
    };

    class UFunction : public UStruct
    {
    public:

        static constexpr uint32_t OFF_FunctionFlags = 0xB0;

        uint32_t function_flags() const { return *reinterpret_cast<const uint32_t*>(reinterpret_cast<uintptr_t>(this) + OFF_FunctionFlags); }
        void set_function_flags(uint32_t v) { *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(this) + OFF_FunctionFlags) = v; }
    };

    enum EFunctionFlags : uint32_t
    {
        FUNC_NetReliable = 0x00000400, };

    TUObjectArray* gobjects();
    UObject* gworld();
    std::string name_from_index(int32_t index);

    UObject* find_object(std::string_view full_name);
    UClass* find_class (std::string_view full_name);
    UFunction* find_function(std::string_view full_name);
    UFunction* find_function(UClass* owner, std::string_view fn_name);

    UClass* find_class_by_name(std::string_view name, std::string_view expected_class_kind = {});
    UFunction* find_function_by_name(UClass* owner, std::string_view fn_name);

    uint32_t resolve_bool_property_offset(UClass* owner, std::string_view prop_name);

    UFunction* find_function_recursive(UClass* owner, std::string_view fn_name);

    void init_reflection_caches();
}

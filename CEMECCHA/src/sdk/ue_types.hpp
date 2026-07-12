#pragma once

#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace ce::ue
{
    struct FVector2D { double X, Y; };
    struct FVector { double X, Y, Z; };
    struct FRotator { double Pitch, Yaw, Roll; };
    struct FQuat { double X, Y, Z, W; };

    struct FLinearColor
    {
        float R, G, B, A;
        constexpr FLinearColor(float r = 0, float g = 0, float b = 0, float a = 1) : R(r), G(g), B(b), A(a) {}
    };

    struct FGuid { uint32_t A, B, C, D; };

    template <typename T> struct TArray
    {
        T* Data = nullptr;
        int32_t Count = 0;
        int32_t Max = 0;

        void reset()
        {
            if (Data) std::free(Data);
            Data = nullptr;
            Count = 0;
            Max = 0;
        }

        void reserve(int32_t n)
        {
            if (n <= Max) return;
            int32_t grow = Max ? Max : 16;
            while (grow < n) grow *= 2;
            Data = static_cast<T*>(std::realloc(Data, static_cast<size_t>(grow) * sizeof(T)));
            Max = grow;
        }

        void push_back(const T& v)
        {
            reserve(Count + 1);
            std::memcpy(&Data[Count], &v, sizeof(T));
            ++Count;
        }
    };

    template <typename T> struct OwnedTArray : public TArray<T> {
        ~OwnedTArray() { this->reset(); }
    };

    static_assert(sizeof(FVector) == 0x18, "FVector must be 24 bytes (LWC double)");
    static_assert(sizeof(FVector2D) == 0x10, "FVector2D must be 16 bytes");
    static_assert(sizeof(FLinearColor) == 0x10, "FLinearColor must be 16 bytes");
    static_assert(sizeof(FGuid) == 0x10, "FGuid must be 16 bytes");
    static_assert(sizeof(TArray<int>) == 0x10, "TArray must be 16 bytes");
}

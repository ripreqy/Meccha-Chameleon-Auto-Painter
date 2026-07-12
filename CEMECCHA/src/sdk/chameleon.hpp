#pragma once

#include <cstdint>

#include "ue_types.hpp"
#include "ue_object.hpp"

namespace ce::ue::chameleon
{

    struct FColor { uint8_t B, G, R, A; };
    static_assert(sizeof(FColor) == 4);

    struct FTransform
    {
        FQuat Rotation;
        FVector Translation;
        uint8_t Pad_38[0x8];
        FVector Scale3D;
        uint8_t Pad_58[0x8];
    };
    static_assert(sizeof(FTransform) == 0x60, "FTransform");

    enum class ESceneCaptureSource : uint8_t
    {
        SceneColorHDR = 0, SceneColorHDRNoAlpha = 1, FinalColorLDR = 2, SceneColorSceneDepth = 3, SceneDepth = 4, DeviceDepth = 5, Normal = 6, BaseColor = 7, FinalColorHDR = 8, FinalToneCurveHDR = 9, };

    enum class ETextureRenderTargetFormat : uint8_t
    {
        RTF_R8 = 0, RTF_RG8 = 1, RTF_RGBA8 = 2, RTF_RGBA8_SRGB= 3, RTF_R16f = 4, RTF_RG16f = 5, RTF_RGBA16f = 6, RTF_R32f = 7, RTF_RG32f = 8, RTF_RGBA32f = 9, RTF_RGB10A2 = 10, };

    enum class ESpawnActorCollisionHandlingMethod : uint8_t
    {
        Undefined = 0, AlwaysSpawn = 1, AdjustIfPossibleButAlwaysSpawn = 2, AdjustIfPossibleButDontSpawnIfColliding = 3, DontSpawnIfColliding = 4, };

    namespace sc_offsets
    {

        constexpr uint32_t A2D_CaptureComponent2D = 0x02B8;

        constexpr uint32_t SC_CaptureSource = 0x0241;
        constexpr uint32_t SC_HiddenActors = 0x0258;

        constexpr uint32_t SC_CaptureFlags = 0x0244;

        constexpr uint32_t SC2D_ProjectionType = 0x0328;
        constexpr uint32_t SC2D_FOVAngle = 0x032C;
        constexpr uint32_t SC2D_TextureTarget = 0x0350;

        constexpr uint32_t SSBQ_DepthCaptureComponent = 0x01E8;
        constexpr uint32_t SSBQ_DepthRenderTarget = 0x01F0;
        constexpr uint32_t SSBQ_NormalCaptureComponent = 0x01F8;
        constexpr uint32_t SSBQ_NormalRenderTarget = 0x0200;
        constexpr uint32_t SSBQ_StencilCaptureComponent = 0x0208;
        constexpr uint32_t SSBQ_StencilRenderTarget = 0x0210;
    }

    enum class EPaintChannel : uint8_t
    {
        Albedo = 0, Metallic = 1, Roughness = 2, Height = 3, All = 4, AlbedoMetallicRoughness = 5, };

    enum class EPaintBlendMode : uint8_t
    {
        Normal = 0, Additive = 1, Multiply = 2, };

    enum class EPaintChannelApplyMode : uint8_t
    {
        Override = 0, AlphaBlend = 1, Additive = 2, };

    enum class EBrushFalloff : uint8_t
    {
        Linear = 0, Smooth = 1, Round = 2, Sharp = 3, };

    struct FRuntimeBrushSettings
    {
        float Radius;
        float Hardness;
        float Opacity;
        float Spacing;
        EBrushFalloff Falloff;
        EPaintBlendMode BlendMode;
        uint8_t Pad_12[0x6];
        void* BrushTexture;
        float Rotation;
        uint8_t Pad_24[0x4];
    };
    static_assert(sizeof(FRuntimeBrushSettings) == 0x28, "FRuntimeBrushSettings");

    struct FPaintChannelData
    {
        FLinearColor AlbedoColor;
        float Metallic;
        float Roughness;
        float Height;
        EPaintChannelApplyMode ApplyMode;
        uint8_t Pad_1D[0x3];
    };
    static_assert(sizeof(FPaintChannelData) == 0x20, "FPaintChannelData");

    struct FPaintStroke
    {
        FVector2D Uv;
        FVector WorldPosition;
        bool bHasWorldPosition;
        uint8_t Pad_29[0x7];
        FVector LocalPosition;
        bool bHasLocalPosition;
        bool bHasSkeletalTriangleAnchor;
        uint8_t Pad_4A[0x2];
        int32_t SkeletalTriangleIndex;
        FVector SkeletalTriangleBarycentric;
        FRuntimeBrushSettings BrushSettings;
        FPaintChannelData ChannelData;
        EPaintChannel TargetChannel;
        uint8_t Pad_B1[0x3];
        float EffectiveBrushWorldRadius;
        int32_t EffectiveSubdivisionLevel;
        float EffectiveSubdivisionPixelSize;
        int32_t EffectiveTemplateResolution;
        int32_t EffectiveMaxGeneratedBrushTriangles;
        int32_t EffectiveGutterExpandPixels;
        FGuid ReplicationSourceId;
        uint8_t Pad_DC[0x4];
    };
    static_assert(sizeof(FPaintStroke) == 0xE0, "FPaintStroke must be 224 bytes");

    struct FPaintStrokeBatch
    {
        TArray<FPaintStroke> Strokes;
    };
    static_assert(sizeof(FPaintStrokeBatch) == 0x10, "FPaintStrokeBatch");

    namespace ch_offsets
    {

        constexpr uint32_t Actor_bHidden = 0x0058;
        constexpr uint32_t Actor_Role = 0x0168;
        constexpr uint32_t Char_Mesh = 0x0418;
        constexpr uint32_t Char_HandBone = 0x0490;
        constexpr uint32_t Char_FirstPersonMesh = 0x04C8;
        constexpr uint32_t Char_CharacterMovement = 0x0330;
        constexpr uint32_t Actor_bEnableCollision = 0x005D;

        constexpr uint32_t Human_Dead = 0x05AA;
        constexpr uint32_t Human_Invincible = 0x05AB;
        constexpr uint32_t Human_InputLock = 0x05A8;
        constexpr uint32_t Human_HaveActor_R = 0x0508;
        constexpr uint32_t Human_EnableShotActors = 0x0588;
        constexpr uint32_t Human_Health = 0x0640;

        constexpr uint32_t cLeon_RuntimePaintable = 0x0B68;
        constexpr uint32_t cLeon_ScreenSpaceBrushQuery = 0x0B70;
        constexpr uint32_t cLeon_IsPaintMode = 0x0B79;
        constexpr uint32_t cLeon_IsBrushing = 0x0BF8;
        constexpr uint32_t cLeon_IsHunter = 0x0C3A;
        constexpr uint32_t cLeon_BodyVisibility = 0x0C50;

        constexpr uint32_t cLeonBase_DecoyCoolTimes = 0x0CA0;
        constexpr uint32_t cLeonBase_DecoyCoolTimeDefault = 0x0CB0;

        constexpr uint32_t Hunter_GunCoolTime = 0x0D20;
        constexpr uint32_t Hunter_InfinityBullet = 0x0DA4;
        constexpr uint32_t Hunter_GunCoolTimeDefault = 0x0DA8;
        constexpr uint32_t Hunter_IsChater = 0x0DB0;
        constexpr uint32_t Hunter_CheatCheck = 0x0DB4;

        constexpr uint32_t Survivor_OverlapCheckCapsules = 0x0CE0;

        constexpr uint32_t PC_PlayerCameraManager = 0x0360;
        constexpr uint32_t PCM_POV_Location = 0x1540;
        constexpr uint32_t PCM_POV_Rotation = 0x1558;
    }

    struct FScreenSpacePaintResult
    {
        bool bSuccess;
        uint8_t Pad_1[0x7];
        FVector2D HitUV;
        FVector HitWorldPosition;
        FVector HitNormal;
    };
    static_assert(sizeof(FScreenSpacePaintResult) == 0x48);

    struct RuntimePaintable
    {
        UObject* obj;
        operator bool() const { return obj != nullptr; }

        UObject* mesh_component_target() const;

        void set_auto_flush(bool enable);
        void set_auto_record(bool enable);

        void begin_stroke();
        void end_stroke();
        void set_brush_radius(float radius);
        void set_brush_settings(const FRuntimeBrushSettings& s);
        void clear_all_channels();
        bool initialize_paint(UObject* mesh_component);

        void flush_recorded_strokes_to_server();
        void ensure_server_batch_reliable();

        UObject* get_initialized_paint_mesh();

        void paint_at_uv_with_brush(const FVector2D& uv, const FPaintChannelData& channel, const FRuntimeBrushSettings& brush, EPaintChannel target_channel);

        FScreenSpacePaintResult paint_at_screen_position(UObject* mesh, const FVector2D& screen_pos, UObject* player_controller, const FPaintChannelData& channel, EPaintChannel target_channel);
    };

    struct cLeonCharacter
    {
        UObject* obj;
        operator bool() const { return obj != nullptr; }

        RuntimePaintable runtime_paintable() const;
        UObject* mesh_component() const;
        UObject* screen_space_brush_query() const;
        bool is_paint_mode() const;
        void force_paint_mode(bool enable);
        bool is_hunter() const;
    };

    UObject* local_player_controller();
    UObject* local_pawn();
    cLeonCharacter local_leon_character();

    UObject* controller_of(const cLeonCharacter& c);

    UObject* camera_manager_of(UObject* player_controller);

    FVector actor_location(UObject* actor);
    FRotator actor_rotation(UObject* actor);

    FTransform component_to_world(UObject* scene_component);

    FVector transform_apply_point(const FTransform& t, const FVector& point);

    struct CameraSnapshot
    {
        FVector location{};
        FRotator rotation{};
        FVector forward{};
        FVector right{};
        FVector up{};
        float fov_deg{90.f};
        float aspect{16.f/9.f};
        bool valid{false};
    };

    bool camera_snapshot(UObject* player_camera_manager, CameraSnapshot& out);
}

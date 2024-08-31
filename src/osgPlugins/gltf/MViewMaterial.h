#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>

class MViewMaterial {
public:
    struct AnisoParams {
        float integral;
        float strength;
        std::vector<float> tangent;

        AnisoParams() : integral(0.0), strength(0.0) {};
        AnisoParams(const nlohmann::json& json);
    };

    struct RefractionParams {
        float IOR;
        float IORActual;
        bool distantBackground;
        bool newRefraction;
        std::vector<float> tint;
        bool useAlbedoTint;

        RefractionParams() : IOR(1.5), IORActual(1.0),
            distantBackground(false), newRefraction(false),
            useAlbedoTint(false) {};
        RefractionParams(const nlohmann::json& json);
    };

    struct MicrofiberParams {
        std::vector<float> fresnelColor;
        float fresnelGlossMask;
        float fresnelOcc;

        MicrofiberParams() = default;
        MicrofiberParams(const nlohmann::json& json);
    };

    struct SkinParams {
        std::vector<float> fresnelColor;
        float fresnelGlossMask;
        float fresnelOcc;
        float millimeterScale;
        float normalSmooth;
        float scaleAdjust;
        float shadowBlur;
        std::vector<float> subdermisColor;
        std::vector<float> transColor;
        float transDepth;
        float transScatter;
        float transSky;
        int version;

        SkinParams() = default;
        SkinParams(const nlohmann::json& json);
    };

    // Atributos da classe
    std::string name;
    std::map<std::string, std::vector<double>> extrasTexCoordRanges;
    std::string blend;

    std::string albedoTex;
    std::string alphaTex;
    std::string glossTex;
    std::string reflectivityTex;
    std::string normalTex;
    std::string extrasTex;
    std::string extrasTexA;

    float alphaTest;

    bool usesBlending;
    bool usesRefraction;
    bool useSkin;
    bool useAniso;
    bool useMicroFiber;

    bool ggxSpecular;

    float shadowAlphaTest;
    bool castShadows;

    float horizonOcclude;

    std::vector<float> fresnel;
    float emissiveIntensity;
    AnisoParams anisoParams;
    RefractionParams refractionParams;
    MicrofiberParams microfiberParams;
    SkinParams skinParams;

    bool tangentGenerateBitangent;
    bool tangentNormalize;
    bool tangentOrthogonalize;

    bool textureFilterNearest;
    bool textureWrapClamp;

    bool aoSecondaryUV;
    bool emissiveSecondaryUV;

    float horizonSmoothing;

    float vOffset, uOffset;

    // Construtor
    MViewMaterial() {};
    MViewMaterial(const nlohmann::json& jsonConfig);
};

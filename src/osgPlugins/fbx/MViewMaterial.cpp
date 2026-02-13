
#include "json.hpp"
#include "MViewMaterial.h"

static bool getBooleanValue(const nlohmann::json& json, const std::string& key, bool defaultValue) 
{
	if (json.contains(key)) {
		if (json[key].is_boolean()) {
			return json[key].get<bool>();
		}
		else if (json[key].is_number_integer()) {
			return json[key].get<int>() != 0;
		}
	}
	return defaultValue;
}


// Construtor para AnisoParams
MViewMaterial::AnisoParams::AnisoParams(const nlohmann::json& json) {
    integral = json.value("integral", -1.0f);
    strength = json.value("strength", -1.0f);
    tangent = json.value("tangent", std::vector<float>());
}

// Construtor para RefractionParams
MViewMaterial::RefractionParams::RefractionParams(const nlohmann::json& json) {
    IOR = json.value("IOR", -1.0f);
    IORActual = json.value("IORActual", -1.0f);
    distantBackground = getBooleanValue(json, "distantBackground", false);
    newRefraction = getBooleanValue(json, "newRefraction", false);
    tint = json.value("tint", std::vector<float>());
    useAlbedoTint = getBooleanValue(json, "useAlbedoTint", false);
}

// Construtor para MicrofiberParams
MViewMaterial::MicrofiberParams::MicrofiberParams(const nlohmann::json& json) {
    fresnelColor = json.value("fresnelColor", std::vector<float>());
    fresnelGlossMask = json.value("fresnelGlossMask", -1.0f);
    fresnelOcc = json.value("fresnelOcc", -1.0f);
}

MViewMaterial::SkinParams::SkinParams(const nlohmann::json& json) {
    fresnelColor = json.value("fresnelColor", std::vector<float>());
    fresnelGlossMask = json.value("fresnelGlossMask", -1.0f);
    fresnelOcc = json.value("fresnelOcc", -1.0f);
    millimeterScale = json.value("millimeterScale", -1.0f);
    normalSmooth = json.value("normalSmooth", -1.0f);
    scaleAdjust = json.value("scaleAdjust", -1.0f);
    shadowBlur = json.value("shadowBlur", -1.0f);
    subdermisColor = json.value("subdermisColor", std::vector<float>());
    transColor = json.value("transColor", std::vector<float>());
    transDepth = json.value("transDepth", -1.0f);
    transScatter = json.value("transScatter", -1.0f);
    transSky = json.value("transSky", -1.0f);
    version = json.value("version", 0);
}

// Construtor para MViewMaterial
MViewMaterial::MViewMaterial(const nlohmann::json& jsonConfig) 
{
    // Atribuindo valores básicos
    name = jsonConfig.value("name", "");
    alphaTest = jsonConfig.value("alphaTest", -1.0f);
    blend = jsonConfig.value("blend", "none");
    
    albedoTex = jsonConfig.value("albedoTex", "");
    alphaTex = jsonConfig.value("alphaTex", "");
    glossTex = jsonConfig.value("glossTex", "");
    reflectivityTex = jsonConfig.value("reflectivityTex", "");
    normalTex = jsonConfig.value("normalTex", "");
    extrasTex = jsonConfig.value("extrasTex", "");
    extrasTexA = jsonConfig.value("extrasTexA", "");

    usesBlending = (blend != "none");
    usesRefraction = getBooleanValue(jsonConfig, "refraction", false);
    useSkin = getBooleanValue(jsonConfig, "useSkin", false);
    useMicroFiber = getBooleanValue(jsonConfig, "microfiber", false);
    useAniso = getBooleanValue(jsonConfig, "aniso", false);
    ggxSpecular = getBooleanValue(jsonConfig, "ggxSpecular", false);

    shadowAlphaTest = alphaTest;
    castShadows = (blend != "add");
    horizonOcclude = jsonConfig.value("horizonOcclude", -1.0f);

    fresnel = jsonConfig.value("fresnel", std::vector<float>());
    emissiveIntensity = jsonConfig.value("emissiveIntensity", -1.0f);

    tangentGenerateBitangent = getBooleanValue(jsonConfig, "tangentGenerateBitangent", false);
    tangentNormalize = getBooleanValue(jsonConfig, "tangentNormalize", false);
    tangentOrthogonalize = getBooleanValue(jsonConfig, "tangentOrthogonalize", false);

    textureFilterNearest = getBooleanValue(jsonConfig, "textureFilterNearest", false);
    textureWrapClamp = getBooleanValue(jsonConfig, "textureWrapClamp", false);

    aoSecondaryUV = getBooleanValue(jsonConfig, "aoSecondaryUV", false);
    emissiveSecondaryUV = getBooleanValue(jsonConfig, "emissiveSecondaryUV", false);

    horizonSmoothing = jsonConfig.value("horizonSmoothing", -1.0f);
    vOffset = uOffset = 0.0f;

    // ExtrasTexCoordRanges
    if (jsonConfig.contains("extrasTexCoordRanges")) {
        for (const auto& item : jsonConfig["extrasTexCoordRanges"].items()) {
            const std::string& key = item.key();
            const auto& value = item.value();

            if (value.contains("scaleBias") && value["scaleBias"].is_array()) {
                std::vector<double> scaleBiasValues = value["scaleBias"].get<std::vector<double>>();
                extrasTexCoordRanges[key] = scaleBiasValues;
            }
            else {
                extrasTexCoordRanges[key] = std::vector<double>();
            }
        }
    }

    // Inicialização de AnisoParams
    if (jsonConfig.contains("anisoParams")) {
        anisoParams = AnisoParams(jsonConfig["anisoParams"]);
    }

    // Inicialização de RefractionParams
    if (jsonConfig.contains("refractionParams")) {
        refractionParams = RefractionParams(jsonConfig["refractionParams"]);
    }

    // Inicialização de MicrofiberParams
    if (jsonConfig.contains("microfiberParams")) {
        microfiberParams = MicrofiberParams(jsonConfig["microfiberParams"]);
    }

    // Inicialização de SkinParams
    if (jsonConfig.contains("skinParams")) {
        skinParams = SkinParams(jsonConfig["skinParams"]);
    }
}

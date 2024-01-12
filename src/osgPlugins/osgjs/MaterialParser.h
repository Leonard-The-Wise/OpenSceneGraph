#pragma once

namespace osgJSONParser
{
	struct TextureInfo {
		std::string UID;
		std::string Name;
		std::string WrapS;
		std::string WrapT;
		std::string MagFilter;
		std::string MinFilter;
		int TexCoordUnit;
		std::string TextureTarget;
		std::string InternalFormat;

		TextureInfo() :
			TexCoordUnit(0)
		{};
	};

	struct ChannelInfo {
		bool Enable;
		bool FlipY;
		double Factor;
		std::vector<double> Color;
		TextureInfo Texture;

		ChannelInfo() :
			Enable(false),
			FlipY(false),
			Factor(0)
		{};
	};

	struct MaterialInfo {
		std::string ID;
		std::string Name;
		int Version;
		std::unordered_map<std::string, ChannelInfo> Channels;
	};

	class MaterialFile
	{
	public:

		typedef std::map<std::string, MaterialInfo> Materials;

		MaterialFile():
			knownChannelNames{ 
			"AOPBR", 
			"Sheen",
			"Matcap",
			"BumpMap",
			"Opacity",
			"AlbedoPBR",
			"AlphaMask",
			"CavityPBR",
			"ClearCoat",
			"EmitColor",
			"NormalMap",
			"Anisotropy",
			"DiffusePBR",
			"SpecularF0",
			"SpecularPBR",
			"DiffuseColor",
			"Displacement",
			"MetalnessPBR",
			"RoughnessPBR",
			"GlossinessPBR",
			"SpecularColor",
			"SheenRoughness",
			"DiffuseIntensity",
			"SpecularHardness",
			"ClearCoatNormalMap",
			"ClearCoatRoughness",
			"SubsurfaceScattering",
			"SubsurfaceTranslucency",
		}
		{};

		bool readMaterialFile(const std::string& viewerInfoFileName, const std::string& textureInfoFileName);

		inline const Materials getMaterials() {
			return _materials;
		}

	private:

		const std::set<std::string> knownChannelNames;
		Materials _materials;

		bool parseViewerInfo(const nlohmann::json& viewerInfoDoc);

		bool parseTextureInfo(const nlohmann::json& textureInfoDoc);


	};
}
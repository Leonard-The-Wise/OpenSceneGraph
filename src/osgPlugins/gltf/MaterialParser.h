#pragma once

namespace osgJSONParser
{
	struct TextureInfo2 {
		std::string UID;
		std::string Name;
		std::string WrapS;
		std::string WrapT;
		std::string MagFilter;
		std::string MinFilter;
		int TexCoordUnit;
		std::string TextureTarget;
		std::string InternalFormat;

		TextureInfo2() :
			TexCoordUnit(0)
		{};
	};

	struct ChannelInfo2 {
		bool Enable;
		bool FlipY;
		float Factor;
		float IOR;
		float Thickness;
		bool ThinLayer;
		float RoughnessFactor;
		float Direction;
		float Rotation;
		std::string Type;
		std::vector<float> Color;
		std::vector<float> ColorFactor;
		std::vector<float> Tint;
		TextureInfo2 Texture;

		ChannelInfo2() :
			Enable(false),
			FlipY(false),
			Factor(0.0),
			IOR(-1.0),
			Thickness(0.0),
			ThinLayer(false),
			RoughnessFactor(0.0),
			Direction(0.0),
			Rotation(0.0)
		{};
	};

	struct MaterialInfo2 {
		std::string ID;
		std::string Name;
		int StateSetID;
		int Version;
		std::unordered_map<std::string, ChannelInfo2> Channels;
		bool BackfaceCull;
		bool UsePBR;

		MaterialInfo2() :
			StateSetID(0),
			Version(0),
			BackfaceCull(false),
			UsePBR(false)
		{};
	};

	class MaterialFile2
	{
	public:

		typedef std::unordered_map<std::string, MaterialInfo2> Materials;

		MaterialFile2():
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

		inline std::map<std::string, TextureInfo2> getTextureMap()
		{
			return _textureMap;
		}

		inline const std::map<int, std::string> getMaterialStateSetIDs()
		{
			return _stateSetIDMaterial;
		}

		void renameTexture(const std::string& originalFile, const std::string& modifiedFile);

	private:

		const std::set<std::string> knownChannelNames;
		Materials _materials;
		std::map<std::string, TextureInfo2> _textureMap;
		std::map<int, std::string> _stateSetIDMaterial;

		void makeTextureMap();

		bool parseViewerInfo(const nlohmann::json& viewerInfoDoc);

		bool parseTextureInfo(const nlohmann::json& textureInfoDoc);

	};
}
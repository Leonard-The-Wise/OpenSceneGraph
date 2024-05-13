#pragma once

namespace osgJSONParser
{
	struct MeshInfo
	{
		int UniqueID;
		std::string MaterialName;
	};

	struct MaterialInfo
	{
		std::string ID;
		std::unordered_map<std::string, std::string> KnownLayerNames =
		{
			{"Albedo", ""},
			{"AO", ""},
			{"Opacity", ""},
			{"Bump map", "" },
			{"Emission", ""},
			{"Normal", ""},
			{"Diffuse", ""},
			{"Roughness", ""},
			{"Specular", ""},
			{"SpecularPBR", ""},
			{"Specular F0", ""},
			{"Displacement", ""},
			{"Metalness", ""},
			{"Diffuse colour", ""},
			{"Glossiness", ""},
			{"Specular colour", ""},
			{"Diffuse intensity", ""},
			{"Specular hardness", ""},
			{"Clear coat normal", ""},
			{"Clear coat roughness", ""},
		};

	public:
		const std::string getImageName(std::string layerName) const;
		const osg::Vec4 getVector(std::string layerName) const;
		double getDouble(std::string layerName) const;
	};

	class MaterialFile
	{
	public:

		MaterialFile() {};

		bool readMaterialFile(const std::string& filePath);

		inline const std::map<std::string, MeshInfo> getMeshes() const {
			return Meshes;
		}
		inline const std::map<std::string, MaterialInfo> getMaterials() const {
			return Materials;
		}

	private:

		std::map<std::string, MeshInfo> Meshes;
		std::map<std::string, MaterialInfo> Materials;
	};

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
		std::vector<float> Color;
		TextureInfo2 Texture;

		ChannelInfo2() :
			Enable(false),
			FlipY(false),
			Factor(0)
		{};
	};

	struct MaterialInfo2 {
		std::string ID;
		std::string Name;
		int Version;
		std::unordered_map<std::string, ChannelInfo2> Channels;
		bool BackfaceCull;
	};

	class MaterialFile2
	{
	public:

		typedef std::map<std::string, MaterialInfo2> Materials;

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

		void renameTexture(const std::string& originalFile, const std::string& modifiedFile);

	private:

		const std::set<std::string> knownChannelNames;
		Materials _materials;
		MaterialFile _materialFile1;
		std::map<std::string, TextureInfo2> _textureMap;

		void makeTextureMap();

		bool parseViewerInfo(const nlohmann::json& viewerInfoDoc);

		bool parseTextureInfo(const nlohmann::json& textureInfoDoc);

		void mergeWithMaterial1(const std::string& fileName);
	};
}
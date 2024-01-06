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

		inline const std::map<std::string, MeshInfo>& getMeshes() const {
			return Meshes;
		}
		inline const std::map<std::string, MaterialInfo>& getMaterials() const {
			return Materials;
		}

	private:

		std::map<std::string, MeshInfo> Meshes;
		std::map<std::string, MaterialInfo> Materials;
	};
}
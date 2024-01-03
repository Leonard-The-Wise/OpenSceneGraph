#pragma once


namespace osgJSONParser
{

	struct MeshInfo
	{
		int UniqueID;
		std::string MeshName;
		std::string MaterialName;
	};

	struct MaterialInfo
	{
		std::string ID;
		std::string Name;

		std::unordered_map<std::string, std::string> KnownLayerNames = 
		{
			{"Albedo", ""},
			{"AO", ""},
			{"Opacity", ""},
			{"Emission", ""},
			{"Normal", ""},
			{"Diffuse", ""},
			{"SpecularPBR", ""},
			{"Diffuse colour", ""},
			{"Glossiness", ""},
			{"Specular colour", ""},
			{"Diffuse intensity", ""},
			{"Specular hardness", ""},
			{"Clear coat normal", ""},
			{"Clear coat roughness", ""},
		};
	};

	class MaterialFile
	{
	public:

		MaterialFile() {};

		bool readMaterialFile(const std::string& filePath);

		inline const std::vector<MeshInfo>& getMeshes() const {
			return Meshes;
		}
		inline const std::vector<MaterialInfo>& getMaterials() const {
			return Materials;
		}

	private:

		std::vector<MeshInfo> Meshes;
		std::vector<MaterialInfo> Materials;
	};
}
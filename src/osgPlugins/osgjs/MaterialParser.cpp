
#include "pch.h"
#include "json.hpp"

#include "OsgjsParserHelper.h"
#include "MaterialParser.h"

#include "jpcre2.hpp"

const std::string COMMENT = R"(^\/\/.+)";
const std::string MESHNAME = R"(^Mesh \"(?'MeshName'\w+)\" uses material \"(?'MaterialName'\w+)\" and has UniqueID \"(?'UniqueID'\d+)\")";
const std::string MATERIALNAME = R"(^Material \"(?'MaterialName'\w+)\" has ID (?'ID'[\w-]+))";
const std::string MATERIALLINE = R"(^\t(?'TextureLayerName'[\w\s]*?)(\s*+(\((?'FlipAxis'Flipped\s*\w+)\)))?(\s*+(\((?'TexCoord'UV\d+)\)))?(\s*+(\((?'Parameter'[\w\s\d=,]*)\)))*+:\s(?'FileOrParam'[\w.,+-|()]*))";

const jpcre2::select<char>::Regex commentRegEx(COMMENT, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
const jpcre2::select<char>::Regex meshNameRegEx(MESHNAME, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
const jpcre2::select<char>::Regex materialNameRegEx(MATERIALNAME, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);
const jpcre2::select<char>::Regex materialLineRegEx(MATERIALLINE, PCRE2_MULTILINE, jpcre2::JIT_COMPILE);

typedef jpcre2::select<char> pcre2;

using namespace osgJSONParser;
using namespace nlohmann;

constexpr auto MATERIALINFO_FILE = "materialInfo.txt";


bool MaterialFile::readMaterialFile(const std::string& filePath)
{
	std::string fileName = osgDB::findDataFile(filePath);
	pcre2::VecNas captureGroup;

	pcre2::RegexMatch regexMatch;
	regexMatch.addModifier("gm");
	regexMatch.setNamedSubstringVector(&captureGroup);

	osgDB::ifstream ifs(fileName.c_str());

	if (ifs.is_open())
	{
		std::string line;
		while (std::getline(ifs, line))
		{
			if (line.empty())
				continue;

			regexMatch.setSubject(line);

			// First try parse as comment
			regexMatch.setRegexObject(&commentRegEx);
			size_t count = regexMatch.match();

			if (count > 0)
				continue;

			// Try parse as mesh
			regexMatch.setRegexObject(&meshNameRegEx);
			count = regexMatch.match();

			if (count > 0)
			{
				MeshInfo newMesh;
				std::string meshName = captureGroup[0]["MeshName"];
				newMesh.MaterialName = captureGroup[0]["MaterialName"];
				newMesh.UniqueID = stoi(captureGroup[0]["UniqueID"]);

				Meshes[meshName] = newMesh;

				continue;
			}

			// Try parse as Material
			regexMatch.setRegexObject(&materialNameRegEx);
			count = regexMatch.match();

			if (count > 0)
			{
				MaterialInfo newMaterial;
				newMaterial.ID = captureGroup[0]["ID"];
				std::string materialName = captureGroup[0]["MaterialName"];

				// Try to build materialList
				regexMatch.setRegexObject(&materialLineRegEx);

				// Doing double read, but since file has an empty line between objects this is not a problem.
				while (count > 0 && std::getline(ifs, line))
				{
					regexMatch.setSubject(line);
					count = regexMatch.match();

					if (count > 0)
					{
						// Try to find parameter in parameters map.
						if (newMaterial.KnownLayerNames.find(captureGroup[0]["TextureLayerName"]) != newMaterial.KnownLayerNames.end())
						{
							newMaterial.KnownLayerNames.at(captureGroup[0]["TextureLayerName"]) = captureGroup[0]["FileOrParam"];
						}
						//else
						//{
						//	OSG_WARN << "WARNING: Found unknown texture parameter: " << captureGroup[0]["TextureLayerName"] << std::endl;
						//}
					}
				}

				Materials[materialName] = newMaterial;
			}
		}
	}
	else
	{
		return false;
	}

	return true;
}

const std::string MaterialInfo::getImageName(std::string layerName) const
{
	if (KnownLayerNames.find(layerName) == KnownLayerNames.end())
		return std::string();

	std::string ext = osgDB::getLowerCaseFileExtension(KnownLayerNames.at(layerName));
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "tiff" || ext == "bmp" || ext == "gif"
		|| ext == "dds" || ext == "pic" || ext == "rgb")
		return KnownLayerNames.at(layerName);

	return std::string();
}

const osg::Vec4 MaterialInfo::getVector(std::string layerName) const
{
	if (KnownLayerNames.find(layerName) == KnownLayerNames.end())
		return osg::Vec4();

	std::stringstream strVec;
	strVec << KnownLayerNames.at(layerName);
	std::string strPart;
	std::vector<double> dvec;

	while (std::getline(strVec, strPart, '|'))
	{
		double d;
		if (ParserHelper::getSafeDouble(strPart, d))
			dvec.push_back(d);
	}

	if (dvec.size() == 3)
		return osg::Vec4(dvec[0], dvec[1], dvec[2], 1);

	return osg::Vec4();
}

double MaterialInfo::getDouble(std::string layerName) const
{
	if (KnownLayerNames.find(layerName) == KnownLayerNames.end())
		return -1.0;

	double d;
	if (!ParserHelper::getSafeDouble(KnownLayerNames.at(layerName), d))
		return -1.0;

	return d;
}


TextureInfo2 parseTexture(const json& textureInfoDoc)
{
	TextureInfo2 returnTexture;

	if (textureInfoDoc.contains("uid"))
		returnTexture.UID = textureInfoDoc["uid"].get<std::string>();

	if (textureInfoDoc.contains("wrapS"))
		returnTexture.WrapS = textureInfoDoc["wrapS"].get<std::string>();

	if (textureInfoDoc.contains("wrapT"))
		returnTexture.WrapT = textureInfoDoc["wrapT"].get<std::string>();

	if (textureInfoDoc.contains("magFilter"))
		returnTexture.MagFilter = textureInfoDoc["magFilter"].get<std::string>();

	if (textureInfoDoc.contains("minFilter"))
		returnTexture.MinFilter = textureInfoDoc["minFilter"].get<std::string>();

	if (textureInfoDoc.contains("texCoordUnit"))
		returnTexture.TexCoordUnit = textureInfoDoc["texCoordUnit"].get<int>();

	if (textureInfoDoc.contains("textureTarget"))
		returnTexture.TextureTarget = textureInfoDoc["textureTarget"].get<std::string>();

	if (textureInfoDoc.contains("internalFormat"))
		returnTexture.InternalFormat = textureInfoDoc["internalFormat"].get<std::string>();

	return returnTexture;
}

static ChannelInfo2 parseChannel(const json& channelValue)
{
	ChannelInfo2 returnInfo;

	if (channelValue.contains("enable"))
	{
		returnInfo.Enable = channelValue["enable"].get<bool>();
	}

	if (channelValue.contains("factor"))
	{
		returnInfo.Factor = channelValue["factor"].get<double>();
	}

	if (channelValue.contains("color") && channelValue["color"].is_array())
	{
		returnInfo.Color.resize(3);
		for (int i = 0; i < 3 && i < channelValue["color"].size(); ++i)
		{
			returnInfo.Color[i] = channelValue["color"][i].get<double>();
		}
	}

	if (channelValue.contains("texture") && channelValue["texture"].is_object())
	{
		returnInfo.Texture = parseTexture(channelValue["texture"]);

	}

	return returnInfo;
}

bool MaterialFile2::readMaterialFile(const std::string& viewerInfoFileName, const std::string& textureInfoFileName)
{

	std::string viewerInfoName = osgDB::findDataFile(viewerInfoFileName);
	std::string textureInfoName = osgDB::findDataFile(textureInfoFileName);

	if (viewerInfoName.empty() || textureInfoName.empty())
		return false;

	std::ifstream viewerStream(viewerInfoName.c_str());
	std::ifstream textureStream(textureInfoName.c_str());
	json viewerInfoDoc;
	json textureInfoDoc;

	if (!viewerStream.is_open() || !textureStream.is_open())
		return false;

	viewerStream >> viewerInfoDoc;
	textureStream >> textureInfoDoc;

	if (!parseViewerInfo(viewerInfoDoc))
		return false;

	if (!parseTextureInfo(textureInfoDoc))
		return false;

	std::string material1Path = osgDB::getFilePath(viewerInfoFileName);
	if (!material1Path.empty())
		material1Path.push_back('/');

	material1Path += MATERIALINFO_FILE;

	mergeWithMaterial1(material1Path);

	makeTextureMap();

	return true;
}

bool MaterialFile2::parseViewerInfo(const json& viewerInfoDoc)
{
	if (!viewerInfoDoc.contains("options"))
		return false;

	if (!viewerInfoDoc["options"].is_object())
		return false;

	const json& options = viewerInfoDoc["options"];
	if (options.contains("materials") && options["materials"].is_object())
	{
		const json& materials = options["materials"];

		for (auto& materialItem : materials.items())
		{
			if (materialItem.value().is_object())
			{
				MaterialInfo2 material;
				std::string materialName;
				auto& itemValue = materialItem.value();
				if (itemValue.contains("name"))
				{
					materialName = itemValue["name"].get<std::string>();
					material.Name = materialName;
				}
				else
					return false;

				if (itemValue.contains("version"))
					material.Version = itemValue["version"].get<int>();

				if (itemValue.contains("id"))
					material.ID = itemValue["id"].get<std::string>();

				if (itemValue.contains("channels") && itemValue["channels"].is_object())
				{
					for (auto& channel : itemValue["channels"].items())
					{
						if (knownChannelNames.find(channel.key()) != knownChannelNames.end())
						{
							ChannelInfo2 channelInfo = parseChannel(channel.value());
							material.Channels[channel.key()] = channelInfo;

						}
						else
						{
							OSG_WARN << "WARNING: Unknown material layer name: " << channel.key() << std::endl;
						}
					}
				}

				_materials[materialName] = material;
			}
		}
	}

	return true;
}

bool MaterialFile2::parseTextureInfo(const json& textureInfoDoc)
{
	if (!textureInfoDoc.contains("results"))
		return false;

	if (!textureInfoDoc["results"].is_array())
		return false;

	// Recover texture names
	for (auto& texture : textureInfoDoc["results"])
	{
		if (texture.is_object())
		{
			std::string textureName = texture["name"];
			std::string textureUID = texture["uid"];

			for (auto& material : _materials)
			{
				MaterialInfo2& materialInfo = material.second;
				for (auto& materialChannel : materialInfo.Channels)
				{
					ChannelInfo2& channelInfo = materialChannel.second;
					if (channelInfo.Texture.UID == textureUID)
					{
						channelInfo.Texture.Name = textureName;
					}
				}
			}
		}
	}

	return true;;
}

void osgJSONParser::MaterialFile2::mergeWithMaterial1(const std::string& fileName)
{
	MaterialFile materialFile;

	if (!materialFile.readMaterialFile(fileName))
	{
		// OSG_NOTICE << "INFO: Could not read" << fileName << ". Models may miss materials and textures." << std::endl;
		return;
	}

	std::map<std::string, MeshInfo> meshInfo1 = materialFile.getMeshes();

	for (auto& mesh : meshInfo1)
	{
		std::string meshName1 = mesh.first;
		std::string materialName1 = mesh.second.MaterialName;

		// find existing material in _materials
		if (_materials.find(materialName1) != _materials.end())
		{
			// Make a copy of material and save new mesh
			_materials[meshName1] = _materials.at(materialName1);
		}
	}
}

void osgJSONParser::MaterialFile2::renameTexture(const std::string& originalFile, const std::string& modifiedFile)
{
	// Rename texture on material map
	for (auto& material : _materials)
	{
		// Run through all materials and rename matching textures.
		MaterialInfo2& mInfo = material.second;
		for (auto& channel : mInfo.Channels)
		{
			// Make a copy
			if (channel.second.Texture.Name == originalFile)
			{
				TextureInfo2 texture = channel.second.Texture;
				texture.Name = modifiedFile;
				channel.second.Texture = texture;
			}
		}
	}

	// Rename texture on texture map
	if (_textureMap.find(originalFile) != _textureMap.end())
	{
		TextureInfo2 newTexture = _textureMap[originalFile];
		_textureMap[modifiedFile] = newTexture;
		_textureMap.erase(originalFile);
	}


}

void MaterialFile2::makeTextureMap()
{
	for (auto& material : _materials)
	{
		// Get all textures.
		MaterialInfo2& mInfo = material.second;
		for (auto& channel : mInfo.Channels)
		{
			if (!channel.second.Enable)
				continue;

			TextureInfo2& texture = channel.second.Texture;
			if (!texture.Name.empty())
				_textureMap[texture.Name] = texture;
		}
	}
}
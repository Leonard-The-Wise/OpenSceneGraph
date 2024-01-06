
#include "pch.h"

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
						else
						{
							OSG_WARN << "WARNING: Found unknown texture parameter: " << captureGroup[0]["TextureLayerName"] << std::endl;
						}
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

const std::string osgJSONParser::MaterialInfo::getImageName(std::string layerName) const
{
	if (KnownLayerNames.find(layerName) == KnownLayerNames.end())
		return std::string();

	std::string ext = osgDB::getLowerCaseFileExtension(KnownLayerNames.at(layerName));
	if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "tiff" || ext == "bmp" || ext == "gif"
		|| ext == "dds" || ext == "pic" || ext == "rgb")
		return KnownLayerNames.at(layerName);

	return std::string();
}

const osg::Vec4 osgJSONParser::MaterialInfo::getVector(std::string layerName) const
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

double osgJSONParser::MaterialInfo::getDouble(std::string layerName) const
{
	if (KnownLayerNames.find(layerName) == KnownLayerNames.end())
		return -1.0;

	double d;
	if (!ParserHelper::getSafeDouble(KnownLayerNames.at(layerName), d))
		return -1.0;

	return d;
}

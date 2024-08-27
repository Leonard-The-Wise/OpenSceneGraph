#pragma once

#include <osg/MatrixTransform>

#include "json.hpp"
#include "MViewFile.h"
#include "ParserHelper.h"

namespace MViewParser
{
	struct Bounds 
	{
		osg::Vec3 min;
		osg::Vec3 max;
		float maxExtent;
		float averageExtent;
	};


	class SubMesh
	{
	public:
		std::string materialName;
		int firstIndex;
		int indexCount;
		int firstWireIndex;
		int wireIndexCount;

		SubMesh(const nlohmann::json& description);
	};


	class Mesh
	{

	public:

		std::string name;
		std::string meshMaterial;

		std::vector<SubMesh> subMeshes;

		Mesh(const nlohmann::json& description, const MViewFile::ArchiveFile& archiveFile);

		osg::ref_ptr<osg::Geometry> asGeometry();

		osg::ref_ptr<osg::MatrixTransform> asGeometryInMatrix();

		inline void setAnimated(bool animated) {
			isAnimated = animated;
		}

	private:

		std::string file;

		nlohmann::json desc;
		std::string descDump;

		int indexCount;
		int indexTypeSize;
		int wireCount;
		int vertexCount;
		bool isDynamicMesh;
		bool cullBackFaces;

		int stride;
		osg::Vec3 origin;


		bool hasVertexColor;
		bool hasSecondaryTexCoord;
		bool isAnimated;

		osg::ref_ptr<osg::Vec3Array> vertex;
		osg::ref_ptr<osg::Vec2Array> texCoords;
		osg::ref_ptr<osg::Vec2Array> texCoords2;
		osg::ref_ptr<osg::Vec3Array> normals;
		osg::ref_ptr<osg::Vec3Array> tangents;
		osg::ref_ptr<osg::Vec4ubArray> colors;
		osg::ref_ptr<osg::DrawElementsUInt> indices;

		osg::ref_ptr<osg::MatrixTransform> meshMatrix;

		Bounds bounds;
		// Método para descompactar os vetores unitários (normais, tangentes, etc.)
		void unpackUnitVectors(osg::ref_ptr<osg::FloatArray>& a, const uint16_t* c, int b, int d);
	};

	class MViewReader
	{
	public:

		MViewReader() : _archive(nullptr), 
			_modelName("Imported MVIEW Scene"), 
			_modelVersion(0)
		{}

		osgDB::ReaderWriter::ReadResult readMViewFile(const std::string& fileName);

	private:

		osg::ref_ptr<osg::Node> parseScene(const nlohmann::json& sceneData);

		void fillMetaData(const nlohmann::json& sceneData);

		MViewFile::Archive* _archive;
		std::vector<Mesh> _meshes;

		std::string _modelName;
		std::string _modelAuthor;
		std::string _modelLink;
		int _modelVersion;
	};
}
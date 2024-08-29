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

	class AnimatedProperty {
	public:
		float currentValue;
		size_t keyframeBufferStartIndexFloat;
		float lastValue;
		int interpolationOffsetUShort;
		int frameIndexOffsetUShort;
		int weighOutOffsetFloat;
		int weighInOffsetFloat;
		int valueOffsetFloat;
		int indexUShortSkip;
		int indexFloatSkip;
		int interpolationType;
		int bytesPerKeyFrame;
		int keyframePackingType;
		float lastFramePercent;
		bool enable;
		std::string name;
		bool debugMe;
		std::string debugString;
		int lastSearchIndex;
		int savedSearchIndex;
		int numKeyframes;

		// Construtor
		AnimatedProperty()
			: currentValue(0.0f),
			keyframeBufferStartIndexFloat(static_cast<size_t>(-1)),
			lastValue(0.0f),
			interpolationOffsetUShort(0),
			frameIndexOffsetUShort(0),
			weighOutOffsetFloat(0),
			weighInOffsetFloat(0),
			valueOffsetFloat(0),
			indexUShortSkip(0),
			indexFloatSkip(0),
			interpolationType(0),
			bytesPerKeyFrame(0),
			keyframePackingType(0),
			lastFramePercent(-10.0f),
			enable(true),
			name("NONE"),
			debugMe(true),
			debugString(""),
			lastSearchIndex(1),
			savedSearchIndex(0),
			numKeyframes(0) {
		}
	};

	class AnimatedObject {
	public:
		std::string partName;
		std::string sceneObjectType;
		int skinningRigIndex;
		int id;
		int modelPartIndex;
		int parentIndex;
		int modelPartFPS;

		std::vector<AnimatedProperty> animatedProperties;
		std::map<std::string, AnimatedProperty*> animatedPropertiesMap;

		osg::ref_ptr<osgAnimation::Vec3LinearChannel> translation;
		osg::ref_ptr<osgAnimation::Vec3LinearChannel> scale;
		osg::ref_ptr<osgAnimation::Vec3LinearChannel> rotationEuler;
		osg::ref_ptr<osgAnimation::QuatSphericalLinearChannel> rotation;

		AnimatedObject(const MViewFile::Archive& archive, const nlohmann::json& description, int ID);

	private:

		MViewFile::ByteStream* keyFramesByteStream;
		std::vector<float> keyframesSharedBufferFloats;
		std::vector<uint32_t> keyframesSharedBufferUShorts;
		std::vector<uint16_t> keyframesSharedBufferShorts;
		std::vector<uint8_t> keyframesSharedBufferBytes;

		void unPackKeyFrames();

		std::vector<std::pair<int, float>> extractKeyframes(const AnimatedProperty& property);

		void assembleKeyFrames();

		void copyFromExtractedKeys(const std::string& keyName, osg::ref_ptr<osg::FloatArray>& timesArray, osg::ref_ptr<osg::FloatArray>& keyArray);

		osg::ref_ptr<osgAnimation::Vec3LinearChannel> makeVec3LinearFromArrays(const std::string channelName,
			osg::ref_ptr<osg::FloatArray>& timesArray, osg::ref_ptr<osg::FloatArray>& keyXArray, osg::ref_ptr<osg::FloatArray>& keyYArray, 
			osg::ref_ptr<osg::FloatArray>& keyZArray);

		osg::ref_ptr<osgAnimation::QuatSphericalLinearChannel> makeQuatLinearFromArrays(const std::string channelName,
			osg::ref_ptr<osg::FloatArray>& timesArray, osg::ref_ptr<osg::FloatArray>& keyXArray, osg::ref_ptr<osg::FloatArray>& keyYArray,
			osg::ref_ptr<osg::FloatArray>& keyZArray);

	};

	class Animation {
	public:
		std::string name;
		int expectedNumAnimatedObjects;
		std::vector<AnimatedObject> animatedObjects;
		osg::Matrix sceneTransform;

		Animation(const MViewFile::Archive& archive, const nlohmann::json& description);

		const osg::ref_ptr<osgAnimation::Animation> asAnimation();

	};

	class SkinningCluster {
	public:
		int linkMode;
		int linkObjectIndex;
		int associateObjectIndex;
		osg::Matrix defaultClusterWorldTransform;
		osg::Matrix defaultClusterWorldTransformInvert;
		osg::Matrix defaultClusterBaseTransform;
		osg::Matrix defaultAssociateWorldTransform;
		osg::Matrix defaultAssociateWorldTransformInvert;
	};

	class SkinningRig {
	public:
		std::string debugString;
		std::vector<SkinningCluster> skinningClusters;
		std::string srcVFile;
		int expectedNumClusters;
		int expectedNumVertices;
		int numClusterLinks;
		int originalObjectIndex;
		bool isRigidSkin;
		int tangentMethod;
		std::vector<uint8_t> linkMapCount;
		std::vector<uint16_t> linkMapClusterIndices;
		std::vector<float> linkMapWeights;
		bool isRigValid;

		SkinningRig(MViewFile::Archive& a, const nlohmann::json& c, MViewFile::ByteStream& b);
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
		bool isAnimated;
		osg::ref_ptr<osg::MatrixTransform> meshMatrix;

		std::vector<SubMesh> subMeshes;

		Mesh(const nlohmann::json& description, const MViewFile::ArchiveFile& archiveFile);

		const osg::ref_ptr<osg::Geometry> asGeometry();

		const osg::ref_ptr<osg::MatrixTransform> asGeometryInMatrix();

		inline void setAnimated(bool animated) {
			isAnimated = animated;
		}

		void setAnimatedTransform(const AnimatedObject& referenceNode);

		void createInfluenceMap(const SkinningRig& skinningRig, const std::map<int, std::string>& modelBonePartNames);

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

		osg::ref_ptr<osgAnimation::VertexInfluenceMap> influenceMap;

		bool hasVertexColor;
		bool hasSecondaryTexCoord;

		osg::ref_ptr<osg::Vec3Array> vertex;
		osg::ref_ptr<osg::Vec2Array> texCoords;
		osg::ref_ptr<osg::Vec2Array> texCoords2;
		osg::ref_ptr<osg::Vec3Array> normals;
		osg::ref_ptr<osg::Vec3Array> tangents;
		osg::ref_ptr<osg::Vec4ubArray> colors;
		osg::ref_ptr<osg::DrawElementsUInt> indices;

		Bounds bounds;
		// M�todo para descompactar os vetores unit�rios (normais, tangentes, etc.)
		void unpackUnitVectors(osg::ref_ptr<osg::FloatArray>& a, const uint16_t* c, int b, int d);
	};


	class MViewReader
	{
	public:

		MViewReader() : _archive(nullptr), 
			_modelName("Imported MVIEW Scene"), 
			_modelVersion(0),
			_numMatricesInTable(0)
		{}

		osgDB::ReaderWriter::ReadResult readMViewFile(const std::string& fileName);

	private:

		osg::ref_ptr<osg::Node> parseScene(const nlohmann::json& sceneData);

		void fillMetaData(const nlohmann::json& sceneData);

		void getMeshes(const nlohmann::json& sceneData);

		bool parseAnimations(const nlohmann::json& sceneData);

		int getMeshIndexFromID(int id);

		osg::ref_ptr<osgAnimation::Skeleton> buildBones();

		osg::ref_ptr<osgAnimation::BasicAnimationManager> buildAnimationManager();

		MViewFile::Archive* _archive;
		std::vector<Mesh> _meshes;
		std::vector<SkinningRig> _skinningRigs;
		std::vector<Animation> _animations;
		std::map<int, std::string> _modelBonePartNames;

		std::string _modelName;
		std::string _modelAuthor;
		std::string _modelLink;
		int _modelVersion;

		std::vector<int> _meshIDs;
		std::vector<int> _materialIDs;
		int _numMatricesInTable;
	};
}
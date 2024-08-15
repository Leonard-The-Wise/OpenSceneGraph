#include "pch.h"

#include <unordered_map>

#include <osg/Node>
#include <osg/Geometry>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osgDB/FileNameUtils>
#include <osgDB/ReaderWriter>
#include <osgDB/FileUtils>
#include <osgDB/WriteFile>
#include <stack>
#include <iomanip>

#include <osgAnimation/RigGeometry>
#include <osgAnimation/MorphGeometry>

#include <osgAnimation/BasicAnimationManager>
#include <osgAnimation/Animation>
#include <osgAnimation/UpdateBone>
#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/StackedTranslateElement>
#include <osgAnimation/StackedQuaternionElement>
#include <osgAnimation/StackedRotateAxisElement>
#include <osgAnimation/StackedMatrixElement>
#include <osgAnimation/StackedScaleElement>

#include <osg/Image>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include "tiny_gltf.h"

#include "Stringify.h"
#include "json.hpp"

#include "MaterialParser.h"

using namespace osgJSONParser;

#include "OSGtoGLTF.h"

#include "stb_image.h"
#include "stb_image_write.h"

// Avoid spamming missing targets
static std::set<std::string> missingTargets;
static MaterialFile2 meshMaterials;
static bool meshMaterialsParsed = false;

#pragma region Utility functions

const osg::ref_ptr<osg::Callback> getRealUpdateCallback(const osg::ref_ptr<osg::Callback>& callback)
{
	if (!callback)
		return nullptr;

	// Try to cast callback to a supported type
	if (osg::dynamic_pointer_cast<osgAnimation::BasicAnimationManager>(callback))
		return callback;
	if (osg::dynamic_pointer_cast<osgAnimation::UpdateBone>(callback))
		return callback;
	if (osg::dynamic_pointer_cast<osgAnimation::UpdateMatrixTransform>(callback))
		return callback;
	if (osg::dynamic_pointer_cast<osgAnimation::UpdateMorph>(callback))
		return callback;

	return getRealUpdateCallback(callback->getNestedCallback());
}

static osg::Matrix getAnimatedMatrixTransform(const osg::ref_ptr<osg::Callback> callback)
{
	const osg::ref_ptr<osgAnimation::UpdateMatrixTransform> umt = osg::dynamic_pointer_cast<osgAnimation::UpdateMatrixTransform>(callback);

	osg::Matrix nodeMatrix;

	if (!umt)
		return nodeMatrix;

	auto& stackedTransforms = umt->getStackedTransforms();

	osg::Vec3d pos, scl;
	osg::Quat rot, so;

	// Should have only 1 of each or a matrix...
	for (auto& stackedTransform : stackedTransforms)
	{
		if (auto translateElement = osg::dynamic_pointer_cast<osgAnimation::StackedTranslateElement>(stackedTransform))
		{
			nodeMatrix.preMultTranslate(translateElement->getTranslate());
		}
		else if (auto rotateElement = osg::dynamic_pointer_cast<osgAnimation::StackedQuaternionElement>(stackedTransform))
		{
			nodeMatrix.preMultRotate(rotateElement->getQuaternion());
		}
		else if (auto scaleElement = osg::dynamic_pointer_cast<osgAnimation::StackedScaleElement>(stackedTransform))
		{
			nodeMatrix.preMultScale(scaleElement->getScale());
		}
		else if (auto rotateAxisElement = osg::dynamic_pointer_cast<osgAnimation::StackedRotateAxisElement>(stackedTransform))
		{
			osg::Vec3 axis = rotateAxisElement->getAxis();
			float angle = rotateAxisElement->getAngle();
			osg::Quat rotQuat;
			rotQuat.makeRotate(angle, axis);
			nodeMatrix.preMultRotate(rotQuat);
		}
		else if (auto matrixElement = osg::dynamic_pointer_cast<osgAnimation::StackedMatrixElement>(stackedTransform))
		{
			nodeMatrix = matrixElement->getMatrix() * nodeMatrix;
			break;
		}
	}

	return nodeMatrix;
}

static osg::Matrix getMatrixFromSkeletonToNode(const osg::Node& node)
{
	osg::Matrix retMatrix;
	if (dynamic_cast<const osgAnimation::Skeleton*>(&node))
	{
		return retMatrix; // dynamic_cast<const osgAnimation::Skeleton*>(&node)->getMatrix();
	}
	else if (dynamic_cast<const osg::MatrixTransform*>(&node))
	{
		osg::Matrix nodeMatrix = dynamic_cast<const osg::MatrixTransform*>(&node)->getMatrix();

		// Check to see if it is animated.
		osg::ref_ptr<osg::Callback> callback = const_cast<osg::Callback*>(node.getUpdateCallback());
		osg::ref_ptr<osg::Callback> nodeCallback = getRealUpdateCallback(callback);

		//if (!_ignoreAnimations && nodeCallback)
		if (nodeCallback)
		{
			nodeMatrix = getAnimatedMatrixTransform(nodeCallback);
		}

		if (node.getNumParents() > 0)
			return nodeMatrix * getMatrixFromSkeletonToNode(*node.getParent(0));
		else
			return nodeMatrix;
	}
	else if (node.getNumParents() > 0)
		return getMatrixFromSkeletonToNode(*node.getParent(0));

	return retMatrix;
}

/// <summary>
/// Transforms a vector with a matrix. For Vec3Array, we can transform vertices and normals. For tangents
/// we always use Vec4Array.
/// </summary>
/// <param name="array">Input array</param>
/// <param name="transform">Transform matrix</param>
/// <param name="normalize">If set to true, then treats Vec3Array as normals and Vec4Array as tangents</param>
/// <returns></returns>
template <typename T>
osg::ref_ptr<T> transformArray(osg::ref_ptr<T>& array, osg::Matrix& transform, bool normalize)
{
	osg::ref_ptr<osg::Array> returnArray;
	osg::Matrix transposeInverse = transform;
	transposeInverse.transpose(transposeInverse);
	transposeInverse = osg::Matrix::inverse(transposeInverse);

	switch (array->getType())
	{
	case osg::Array::Vec4ArrayType:
	{
		returnArray = new osg::Vec4Array();
		returnArray->reserveArray(array->getNumElements());
		for (auto& vec : *osg::dynamic_pointer_cast<const osg::Vec4Array>(array))
		{
			osg::Vec4 v;
			if (normalize)
			{
				osg::Vec3 tangentVec3;
				if (vec.x() == 0.0f && vec.y() == 0.0f && vec.z() == 0.0f) // Fix non-direction vectors (paliative)
					tangentVec3 = osg::Vec3(1.0f, 0.0f, 0.0f);
				else
					tangentVec3 = osg::Vec3(vec.x(), vec.y(), vec.z());
				tangentVec3 = tangentVec3 * transposeInverse;
				tangentVec3.normalize();
				v = osg::Vec4(tangentVec3.x(), tangentVec3.y(), tangentVec3.z(), vec.w());
			}
			else
				v = vec * transform;
			osg::dynamic_pointer_cast<osg::Vec4Array>(returnArray)->push_back(v);
		}
		break;
	}
	case osg::Array::Vec3ArrayType:
	{
		returnArray = new osg::Vec3Array();
		returnArray->reserveArray(array->getNumElements());
		for (auto& vec : *osg::dynamic_pointer_cast<const osg::Vec3Array>(array))
		{
			osg::Vec3 v; 
			if (normalize)
			{
				v = vec * transposeInverse;
				if (v.x() == 0.0f && v.y() == 0.0f && v.z() == 0.0f)  // Fix non-direction vector (paliative)
					v = osg::Vec3(1.0f, 0.0f, 0.0f);
				v.normalize();
			}
			else
				v = vec * transform;
				
			osg::dynamic_pointer_cast<osg::Vec3Array>(returnArray)->push_back(v);
		}
		break;
	}
	default:
		OSG_WARN << "WARNING: Unsuported array to transform." << std::endl;
	}

	return osg::dynamic_pointer_cast<T>(returnArray);
}

static osg::ref_ptr<osg::Vec2Array> flipUVs(const osg::ref_ptr<osg::Vec2Array>& texCoords)
{
	osg::ref_ptr<osg::Vec2Array> returnArray = new osg::Vec2Array(*texCoords);

	for (auto& v : *returnArray)
	{
		v = osg::Vec2(v.x(), 1 - v.y());
	}

	return returnArray;
}

template <typename T>
osg::ref_ptr<T> doubleToFloatArray(const osg::Array* array)
{
	osg::ref_ptr<osg::Array> returnArray;

	switch (array->getType())
	{
	case osg::Array::Vec4dArrayType:
		returnArray = new osg::Vec4Array();
		returnArray->reserveArray(array->getNumElements());
		for (unsigned int i = 0; i < array->getNumElements(); ++i)
		{
			osg::Vec4d vec = (*dynamic_cast<const osg::Vec4dArray*>(array))[i];
			osg::dynamic_pointer_cast<osg::Vec4Array>(returnArray)->push_back(osg::Vec4(vec.x(), vec.y(), vec.z(), vec.w()));
		}	
		break;
	case osg::Array::Vec3dArrayType:
		returnArray = new osg::Vec3Array();
		returnArray->reserveArray(array->getNumElements());
		for (unsigned int i = 0; i < array->getNumElements(); ++i)
		{
			osg::Vec3d vec = (*dynamic_cast<const osg::Vec3dArray*>(array))[i];
			osg::dynamic_pointer_cast<osg::Vec3Array>(returnArray)->push_back(osg::Vec3(vec.x(), vec.y(), vec.z()));
		}
		break;
	case osg::Array::Vec2dArrayType:
		returnArray = new osg::Vec2Array();
		returnArray->reserveArray(array->getNumElements());
		for (unsigned int i = 0; i < array->getNumElements(); ++i)
		{
			osg::Vec2d vec = (*dynamic_cast<const osg::Vec2dArray*>(array))[i];
			osg::dynamic_pointer_cast<osg::Vec2Array>(returnArray)->push_back(osg::Vec2(vec.x(), vec.y()));
		}
		break;
	case osg::Array::DoubleArrayType:
		returnArray = new osg::FloatArray();
		returnArray->reserveArray(array->getNumElements());
		for (unsigned int i = 0; i < array->getNumElements(); ++i)
		{
			float f = (*dynamic_cast<const osg::DoubleArray*>(array))[i];
			osg::dynamic_pointer_cast<osg::FloatArray>(returnArray)->push_back(f);
		}
		break;
	default:
		OSG_WARN << "Unsuported float array." << std::endl;
		break;
	}

	return osg::dynamic_pointer_cast<T>(returnArray);
}

static osg::ref_ptr<osg::Vec4Array> colorsByteToFloat(const osg::ref_ptr<osg::Vec4ubArray>& colors)
{
	osg::ref_ptr<osg::Vec4Array> returnArray = new osg::Vec4Array;
	for (auto& color : *colors)
	{
		returnArray->push_back(osg::Vec4(color.r() / 255.0f, color.g() / 255.0f, color.b() / 255.0f, color.a() / 255.0f));
	}

	return returnArray;
}

inline static bool isEmptyRig(osgAnimation::RigGeometry* rigGeometry)
{
	osg::Geometry* geometry = rigGeometry->getSourceGeometry();
	return !geometry->getVertexArray();
}

static bool isEmptyGeometry(osg::Node* node)
{
	osg::Geometry* geometry = dynamic_cast<osg::Geometry*>(node);
	osgAnimation::RigGeometry* rigGeometry = dynamic_cast<osgAnimation::RigGeometry*>(node);

	if (!geometry)
		return true;

	if (rigGeometry)
		return true; // geometry = rigGeometry->getSourceGeometry();

	return !geometry->getVertexArray();
}

static bool isEmptyNode(osg::Node* node)
{
	if (!node)
		return true;

	osg::Group* group = dynamic_cast<osg::Group*>(node);
	osg::Geometry* geometry = dynamic_cast<osg::Geometry*>(node);
	osgAnimation::Skeleton* skeleton = dynamic_cast<osgAnimation::Skeleton*>(node);
	osgAnimation::Bone* bone = dynamic_cast<osgAnimation::Bone*>(node);

	if (skeleton || bone)
		return false;

	if (geometry)
		return isEmptyGeometry(node);

	if (group)
	{
		for (unsigned int i = 0; i < group->getNumChildren(); ++i)
		{
			if (!isEmptyNode(group->getChild(i)))
				return false;
		}
	}
	
	return true;
}

unsigned getBytesInDataType(GLenum dataType)
{
	return
		dataType == TINYGLTF_PARAMETER_TYPE_BYTE || dataType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE ? 1 :
		dataType == TINYGLTF_PARAMETER_TYPE_SHORT || dataType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT ? 2 :
		dataType == TINYGLTF_PARAMETER_TYPE_INT || dataType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT || dataType == TINYGLTF_PARAMETER_TYPE_FLOAT ? 4 :
		0;
}

unsigned getBytesPerElement(const osg::Array* data)
{
	return data->getDataSize() * getBytesInDataType(data->getDataType());
}

unsigned getBytesPerElement(const osg::DrawElements* data)
{
	return
		dynamic_cast<const osg::DrawElementsUByte*>(data) ? 1 :
		dynamic_cast<const osg::DrawElementsUShort*>(data) ? 2 :
		4;
}

static std::string getLastNamePart(const std::string& input)
{
	size_t pos = input.find_last_of('|');
	if (pos != std::string::npos) {
		return input.substr(pos + 1);
	}
	return input;
}

bool hasMatrixParent(const osg::Node& object)
{
	if (dynamic_cast<const osg::MatrixTransform*>(&object))
		return true;

	if (object.getNumParents() == 0)
		return false;

	return hasMatrixParent(*object.getParent(0));
}

void buildMaterials()
{
	std::string viewerInfoFile = std::string("viewer_info.json");
	std::string textureInfoFile = std::string("texture_info.json");
	std::ignore = meshMaterials.readMaterialFile(viewerInfoFile, textureInfoFile);
}

osg::Texture::FilterMode getFilterModeFromString(const std::string& filterMode)
{
	if (filterMode == "LINEAR") return osg::Texture::FilterMode::LINEAR;
	if (filterMode == "LINEAR_MIPMAP_LINEAR") return osg::Texture::FilterMode::LINEAR_MIPMAP_LINEAR;
	if (filterMode == "LINEAR_MIPMAP_NEAREST") return osg::Texture::FilterMode::LINEAR_MIPMAP_NEAREST;
	if (filterMode == "NEAREST") return osg::Texture::FilterMode::NEAREST;
	if (filterMode == "NEAREST_MIPMAP_LINEAR") return osg::Texture::FilterMode::NEAREST_MIPMAP_LINEAR;
	if (filterMode == "NEAREST_MIPMAP_NEAREST") return osg::Texture::FilterMode::NEAREST_MIPMAP_NEAREST;

	return osg::Texture::FilterMode::LINEAR;
}

osg::Texture::WrapMode getWrapModeFromString(const std::string& wrapMode)
{
	if (wrapMode == "CLAMP_TO_EDGE") return osg::Texture::WrapMode::CLAMP_TO_EDGE;
	if (wrapMode == "CLAMP_TO_BORDER") return osg::Texture::WrapMode::CLAMP_TO_BORDER;
	if (wrapMode == "REPEAT") return osg::Texture::WrapMode::REPEAT;
	if (wrapMode == "MIRROR") return osg::Texture::WrapMode::MIRROR;

	return osg::Texture::WrapMode::REPEAT;
}

std::string stripAllExtensions(const std::string& filename)
{
	std::string finalName = filename;
	while (!osgDB::getFileExtension(finalName).empty())
	{
		std::string ext = osgDB::getFileExtension(finalName);

		// Only remove known extensions
		if (ext != "png" && ext != "gz" && ext != "bin" && ext != "binz" && ext != "zip" && ext != "bmp" && ext != "tiff" && ext != "tga" && ext != "jpg" && ext != "jpeg"
			&& ext != "gif" && ext != "tgz" && ext != "pic" && ext != "pnm" && ext != "dds")
			break;

		finalName = osgDB::getStrippedName(finalName);
	}

	return finalName;
}

std::string combineTextures(const std::string& rgbFile, const std::string& redChannelFile) 
{
	std::string outputFileName = stripAllExtensions(rgbFile) + ".a-combined.png";

	if (osgDB::fileExists("textures\\" + outputFileName))
		return outputFileName;

	int width, height, channels;
	unsigned char* rgbData = stbi_load(std::string("textures\\" + stripAllExtensions(rgbFile) + ".png").c_str(), &width, &height, &channels, 3);
	if (!rgbData) {
		OSG_WARN << "Error loading RGB texture " << rgbFile << " to combine!" << std::endl;
		return rgbFile;
	}

	int rWidth, rHeight, rChannels;
	unsigned char* redData = stbi_load(std::string("textures\\" + stripAllExtensions(redChannelFile) + ".png").c_str(), &rWidth, &rHeight, &rChannels, 1);
	if (!redData || rWidth != width || rHeight != height) {
		OSG_WARN << "Error loading R channel texture " << redChannelFile << " to combine or incompatible channels!" << std::endl;
		stbi_image_free(rgbData);
		return rgbFile;
	}

	unsigned char* combinedData = new unsigned char[width * height * 4];

	for (int i = 0; i < width * height; ++i) {
		combinedData[i * 4] = rgbData[i * 3];         // R
		combinedData[i * 4 + 1] = rgbData[i * 3 + 1]; // G
		combinedData[i * 4 + 2] = rgbData[i * 3 + 2]; // B
		combinedData[i * 4 + 3] = redData[i];         // A (usando o canal R da segunda textura)
	}

	stbi_write_png(std::string("textures\\" + outputFileName).c_str(), width, height, 4, combinedData, width * 4);

	stbi_image_free(rgbData);
	stbi_image_free(redData);
	delete[] combinedData;

	OSG_NOTICE << "Created new texture " << outputFileName << " to combine both Opacity and Albedo/Diffuse colors in one image as required by GLTF 2.0 standard." << std::endl;
	OSG_NOTICE << "You may manually remove " << rgbFile << " and " << redChannelFile << " later if you want." << std::endl;

	return outputFileName;
}

std::string combineMetallicRoughnessTextures(const std::string& roughnessFile, const std::string& metallicFile) 
{
	std::string outputFileName = stripAllExtensions(roughnessFile) + ".m-combined.png";

	if (osgDB::fileExists("textures\\" + outputFileName))
		return outputFileName;

	int width, height, channels;
	unsigned char* roughnessData = stbi_load(std::string("textures\\" + stripAllExtensions(roughnessFile) + ".png").c_str(), &width, &height, &channels, 1);
	if (!roughnessData) {
		OSG_WARN << "Error loading roughness texture " << roughnessFile << "!" << std::endl;
		return roughnessFile;
	}

	int mWidth, mHeight, mChannels;
	unsigned char* metallicData = stbi_load(std::string("textures\\" + stripAllExtensions(metallicFile) + ".png").c_str(), &mWidth, &mHeight, &mChannels, 1);
	if (!metallicData || mWidth != width || mHeight != height) {
		OSG_WARN << "Error loading metallic texture or incompatible size! [" << metallicFile << "]" << std::endl;
		stbi_image_free(roughnessData);
		return roughnessFile;
	}

	unsigned char* combinedData = new unsigned char[width * height * 3];

	for (int i = 0; i < width * height; ++i) {
		combinedData[i * 3] = 0;                    // R (deixado em 0)
		combinedData[i * 3 + 1] = roughnessData[i]; // G (usando o canal R da textura de roughness)
		combinedData[i * 3 + 2] = metallicData[i];  // B (usando o canal R da textura de metallic)
	}

	stbi_write_png(std::string("textures\\" + outputFileName).c_str(), width, height, 3, combinedData, width * 3);

	stbi_image_free(roughnessData);
	stbi_image_free(metallicData);
	delete[] combinedData;

	OSG_NOTICE << "Created new texture " << outputFileName << " to combine both Roughness (G) and Metallic (B) colors in one image as required by GLTF 2.0 standard." << std::endl;
	OSG_NOTICE << "You may manually remove " << roughnessFile << " and " << metallicFile << " later if you want." << std::endl;
	// std::remove(std::string("textures\\" + stripAllExtensions(roughnessFile) + ".png").c_str());
	// std::remove(std::string("textures\\" + stripAllExtensions(metallicFile) + ".png").c_str());

	return outputFileName;
}


#pragma endregion


#pragma region Buffers and Accessors


int OSGtoGLTF::getOrCreateBuffer(const osg::BufferData* data, GLenum type)
{
	ArraySequenceMap::iterator a = _buffers.find(data);
	if (a != _buffers.end())
		return a->second;

	_model.buffers.push_back(tinygltf::Buffer());
	tinygltf::Buffer& buffer = _model.buffers.back();
	int id = _model.buffers.size() - 1;
	_buffers[data] = id;

	buffer.data.resize(data->getTotalDataSize());

	//TODO: account for endianess
	unsigned char* ptr = (unsigned char*)(data->getDataPointer());
	for (unsigned i = 0; i < data->getTotalDataSize(); ++i)
		buffer.data[i] = *ptr++;

	return id;
}

int OSGtoGLTF::getOrCreateBufferView(const osg::BufferData* data, GLenum type, GLenum target)
{
	ArraySequenceMap::iterator a = _bufferViews.find(data);
	if (a != _bufferViews.end())
		return a->second;

	int bufferId = -1;
	ArraySequenceMap::iterator buffersIter = _buffers.find(data);
	if (buffersIter != _buffers.end())
		bufferId = buffersIter->second;
	else
		bufferId = getOrCreateBuffer(data, type);

	_model.bufferViews.push_back(tinygltf::BufferView());
	tinygltf::BufferView& bv = _model.bufferViews.back();
	int id = _model.bufferViews.size() - 1;
	// _bufferViews[data] = id;

	bv.buffer = bufferId;
	bv.byteLength = data->getTotalDataSize();
	bv.byteOffset = 0;
	bv.byteStride = 0;

	if (target != 0)
		bv.target = target;

	return id;
}

int OSGtoGLTF::getOrCreateGeometryAccessor(const osg::Array* data, osg::PrimitiveSet* pset, tinygltf::Primitive& prim, const std::string& attr)
{
	//osg::ref_ptr<const osg::BufferData> arrayData = data;
	ArraySequenceMap::iterator a = _accessors.find(data);
	if (a != _accessors.end())
		return a->second;

	ArraySequenceMap::iterator bv = _bufferViews.find(data);
	int bvID(-1);
	if (bv == _bufferViews.end())
		bvID = getOrCreateBufferView(data, TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TARGET_ARRAY_BUFFER);
	else
		bvID = bv->second;

	_model.accessors.push_back(tinygltf::Accessor());
	tinygltf::Accessor& accessor = _model.accessors.back();
	int accessorId = _model.accessors.size() - 1;
	prim.attributes[attr] = accessorId;

	accessor.type =
		data->getDataSize() == 1 ? TINYGLTF_TYPE_SCALAR :
		data->getDataSize() == 2 ? TINYGLTF_TYPE_VEC2 :
		data->getDataSize() == 3 ? TINYGLTF_TYPE_VEC3 :
		data->getDataSize() == 4 ? TINYGLTF_TYPE_VEC4 :
		TINYGLTF_TYPE_SCALAR;

	accessor.bufferView = bvID;
	accessor.byteOffset = 0;
	accessor.componentType = data->getDataType();
	accessor.count = data->getNumElements();

	const osg::DrawArrays* da = dynamic_cast<const osg::DrawArrays*>(pset);
	if (da)
	{
		accessor.byteOffset = da->getFirst() * getBytesPerElement(data);
		accessor.count = da->getCount();
	}

	//TODO: indexed elements
	osg::DrawElements* de = dynamic_cast<osg::DrawElements*>(pset);
	if (de)
	{
		_model.accessors.push_back(tinygltf::Accessor());
		tinygltf::Accessor& idxAccessor = _model.accessors.back();
		prim.indices = _model.accessors.size() - 1;

		idxAccessor.type = TINYGLTF_TYPE_SCALAR;
		idxAccessor.byteOffset = 0;
		idxAccessor.componentType = de->getDataType();
		idxAccessor.count = de->getNumIndices();

		getOrCreateBuffer(de, idxAccessor.componentType);
		int idxBV = getOrCreateBufferView(de, idxAccessor.componentType, TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER);

		idxAccessor.bufferView = idxBV;
	}

	return accessorId;
}

int OSGtoGLTF::createBindMatrixAccessor(const BindMatrices& matrix, int componentType)
{
	osg::ref_ptr<osg::FloatArray> matrixData = convertMatricesToFloatArray(matrix);
	int bufferViewId = getOrCreateBufferView(matrixData, componentType, 0);

	_model.accessors.push_back(tinygltf::Accessor());
	tinygltf::Accessor& accessor = _model.accessors.back();
	int accessorId = _model.accessors.size() - 1;

	accessor.bufferView = bufferViewId;
	accessor.byteOffset = 0;
	accessor.componentType = componentType;
	accessor.count = matrixData->getNumElements() / 16;
	accessor.type = TINYGLTF_TYPE_MAT4;

	return accessorId;
}

int OSGtoGLTF::getOrCreateAccessor(const osg::Array* data, int numElements, int componentType, int accessorType, int bufferTarget)
{
	ArraySequenceMap::iterator a = _accessors.find(data);
	if (a != _accessors.end())
		return a->second;

	int bufferViewId = getOrCreateBufferView(data, componentType, bufferTarget);

	_model.accessors.push_back(tinygltf::Accessor());
	tinygltf::Accessor& accessor = _model.accessors.back();
	int accessorId = _model.accessors.size() - 1;

	accessor.bufferView = bufferViewId;
	accessor.byteOffset = 0;
	accessor.componentType = componentType;
	accessor.count = numElements;
	accessor.type = accessorType;

	return accessorId;
}


#pragma endregion


#pragma region Class Helpers and class utilities

int OSGtoGLTF::findBoneId(const std::string& boneName, const BoneIDNames& boneIdMap) {
	auto it = boneIdMap.find(boneName);
	if (it != boneIdMap.end())
	{
		return it->second;
	}
	return -1;
}

osg::ref_ptr<osg::FloatArray> OSGtoGLTF::convertMatricesToFloatArray(const BindMatrices& matrix)
{
	osg::ref_ptr<osg::FloatArray> floatArray = new osg::FloatArray(16 * matrix.size());

	size_t floatIndex(0);
	for (auto& invMatrix : matrix)
	{
		for (unsigned int i = 0; i < 4; ++i) {
			for (unsigned int j = 0; j < 4; ++j) {
				(*floatArray)[floatIndex] = (*invMatrix.second)(i, j);
				floatIndex++;
			}
		}
	}
	return floatArray;
}

void OSGtoGLTF::BuildSkinWeights(const RiggedMeshStack& rigStack, const BoneIDNames& gltfBoneIDNames)
{
	for (auto& riggedMesh : rigStack)
	{
		tinygltf::Mesh& mesh = _model.meshes[riggedMesh.first];
		const osgAnimation::VertexInfluenceMap* vim = riggedMesh.second->getInfluenceMap();

		if (!vim)
			continue;

		osg::ref_ptr<osg::UShortArray> jointIndices = new osg::UShortArray(riggedMesh.second->getSourceGeometry()->getVertexArray()->getNumElements() * 4);
		osg::ref_ptr<osg::FloatArray> vertexWeights = new osg::FloatArray(riggedMesh.second->getSourceGeometry()->getVertexArray()->getNumElements() * 4);

		// Build influence map
		for (const auto& influenceEntry : *vim)
		{
			const std::string& boneName = influenceEntry.first;
			const osgAnimation::VertexInfluence& influence = influenceEntry.second;

			// Find bone ID in bone map
			int boneId = findBoneId(boneName, gltfBoneIDNames);

			// Convert bone ID to joint ID (the order the bone was added to eht joint instead).
			int boneOrder(0);
			for (auto& joint : _gltfSkeletons.top().second->joints)
			{
				if (boneId == joint)
				{
					boneId = boneOrder;
					break;
				}
				boneOrder++;
			}

			for (const auto& weightEntry : influence)
			{
				int vertexIndex = weightEntry.first;
				float weight = weightEntry.second;

				// Find first free position on vector to put weight
				for (int i = 0; i < 4; ++i)
				{
					int index = vertexIndex * 4 + i;
					if ((*vertexWeights)[index] == 0.0f)
					{
						(*jointIndices)[index] = boneId;
						(*vertexWeights)[index] = weight;
						break;
					}
				}
			}
		}

		// Create JOINTS_0 and WEIGHTS_0 accessors
		int joints = getOrCreateAccessor(jointIndices, jointIndices->getNumElements() / 4, TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT,
			TINYGLTF_TYPE_VEC4, TINYGLTF_TARGET_ARRAY_BUFFER);
		int weights = getOrCreateAccessor(vertexWeights, vertexWeights->getNumElements() / 4, TINYGLTF_PARAMETER_TYPE_FLOAT,
			TINYGLTF_TYPE_VEC4, TINYGLTF_TARGET_ARRAY_BUFFER);

		// Set Accessors to mesh primitives
		for (auto& primitive : mesh.primitives)
		{
			primitive.attributes["JOINTS_0"] = joints;
			primitive.attributes["WEIGHTS_0"] = weights;
		}
	}
}

void OSGtoGLTF::getOrphanedChildren(osg::Node* childNode, std::vector<osg::Node*>& output, bool getMatrix)
{
	osg::MatrixTransform* matrix = dynamic_cast<osg::MatrixTransform*>(childNode);
	osg::Group* group = dynamic_cast<osg::Group*>(childNode);

	if (matrix)
	{
		if (getMatrix)
			output.push_back(childNode);
		return;
	}

	if (group)
	{
		for (unsigned int i = 0; i < group->getNumChildren(); ++i)
		{
			getOrphanedChildren(group->getChild(i), output, true);
		}
	}
	else
		output.push_back(childNode);
}

bool OSGtoGLTF::isMatrixAnimated(const osg::MatrixTransform* node)
{
	if (!node)
		return false;

	// Search node callback
	const osg::ref_ptr<osg::Callback> callback = const_cast<osg::Callback*>(node->getUpdateCallback());
	osg::ref_ptr<osg::Callback> nodeCallback = getRealUpdateCallback(callback);
	if (!nodeCallback)
		return false;

	if (dynamic_cast<const osgAnimation::Skeleton*>(node) || dynamic_cast<const osgAnimation::Bone*>(node))
		return false;

	// Search for UpdateMatrix callback
	const osg::ref_ptr<osgAnimation::UpdateMatrixTransform> umt = osg::dynamic_pointer_cast<osgAnimation::UpdateMatrixTransform>(nodeCallback);
	if (!umt)
		return false;

	// Look into animations list to see if this node is target of animations
	std::string nodeName = umt->getName();
	if (_animationTargetNames.find(nodeName) != _animationTargetNames.end())
		return true;

	return false;
}

#pragma endregion

#pragma region Morph Geometry processing

static osg::ref_ptr<osg::Vec3Array> calculateDisplacement(const osg::ref_ptr<const osg::Vec3Array>& vertices,
	const osg::ref_ptr<const osg::Vec3Array>& originalVertices, const std::string& morphTargetName)
{

	osg::ref_ptr<osg::Vec3Array> returnArray = new osg::Vec3Array;

	// Sanity check
	if (vertices->size() != originalVertices->size())
	{
		OSG_WARN << "WARNING: Morph target '" << morphTargetName << "' has inconsistent size. Skipping...";
		return returnArray;
	}

	for (int i = 0; i < vertices->size(); i++)
	{
		returnArray->push_back((*vertices)[i] - (*originalVertices)[i]);
	}

	return returnArray;
}

void OSGtoGLTF::createMorphTargets(const osg::Geometry* geometry, tinygltf::Mesh& mesh, int meshNodeId, bool isRigMorph, 
	osg::ref_ptr<const osg::Vec3Array> originalVertices)
{
	const osgAnimation::MorphGeometry* morph = isRigMorph ?
		dynamic_cast<const osgAnimation::MorphGeometry*>(dynamic_cast<const osgAnimation::RigGeometry*>(geometry)->getSourceGeometry()) :
		dynamic_cast<const osgAnimation::MorphGeometry*>(geometry);

	if (!morph)
		return;

	// Calculate transforms for geometry
	// Always use parent geometry to calculate matrices because morph can be nested inside geometry.
	osg::Matrix transformMatrix;
	if (isRigMorph)
		transformMatrix = getMatrixFromSkeletonToNode(*geometry); 

	std::string morphName = morph->getName();

	// Refrain from re-creating the same array accessors for different primitives
	// (because same pointers will result in reusing the same buffer)
	std::map<std::string, osg::ref_ptr<const osg::Vec3Array>> morphVerticesMap;
	std::map<std::string, osg::ref_ptr<const osg::Vec3Array>> morphNormalsMap;
	std::map<std::string, osg::ref_ptr<osg::Vec3Array>> morphTangentsMap;

	for (auto& primitive : mesh.primitives)
	{
		for (auto& morphTargetItem : morph->getMorphTargetList())
		{
			const osg::Geometry* morphTarget = morphTargetItem.getGeometry();
			std::string morphTargetName = morphTarget->getName();

			// Create vertices accessor
			osg::ref_ptr<const osg::Vec3Array> vertices = dynamic_cast<const osg::Vec3Array*>(morphTarget->getVertexArray());
			if (morphVerticesMap.find(morphTargetName) == morphVerticesMap.end())
			{
				const osg::Vec3dArray* verticesd = dynamic_cast<const osg::Vec3dArray*>(morphTarget->getVertexArray());
				if (verticesd)
					vertices = doubleToFloatArray<osg::Vec3Array>(verticesd);

				if (!vertices)
				{
					OSG_WARN << "WARNING: Morph target contains no vertices: " << morphTargetName << std::endl;
					continue;
				}

				if (!transformMatrix.isIdentity())
					vertices = transformArray(vertices, transformMatrix, false);

				// Calculate vertices displacement (cause OSG gives absolute coordinates instead of differences)
				vertices = calculateDisplacement(vertices, originalVertices, morphTargetName);

				morphVerticesMap.emplace(morphTargetName, vertices);
			}
			else
				vertices = morphVerticesMap[morphTargetName];

			osg::Vec3f verticesMin(FLT_MAX, FLT_MAX, FLT_MAX);
			osg::Vec3f verticesMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

			for (unsigned i = 0; i < vertices->size(); ++i)
			{
				const osg::Vec3f& v = (*vertices)[i];
				verticesMin.x() = osg::minimum(verticesMin.x(), v.x());
				verticesMin.y() = osg::minimum(verticesMin.y(), v.y());
				verticesMin.z() = osg::minimum(verticesMin.z(), v.z());
				verticesMax.x() = osg::maximum(verticesMax.x(), v.x());
				verticesMax.y() = osg::maximum(verticesMax.y(), v.y());
				verticesMax.z() = osg::maximum(verticesMax.z(), v.z());
			}

			int vertexAccessorIndex = getOrCreateAccessor(vertices, vertices->getNumElements(),
				TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, TINYGLTF_TARGET_ARRAY_BUFFER);

			tinygltf::Accessor& vertexAccessor = _model.accessors[vertexAccessorIndex];
			vertexAccessor.minValues.push_back(verticesMin.x());
			vertexAccessor.minValues.push_back(verticesMin.y());
			vertexAccessor.minValues.push_back(verticesMin.z());
			vertexAccessor.maxValues.push_back(verticesMax.x());
			vertexAccessor.maxValues.push_back(verticesMax.y());
			vertexAccessor.maxValues.push_back(verticesMax.z());

			std::map<std::string, int> morphTargetAttributes;
			morphTargetAttributes["POSITION"] = vertexAccessorIndex;

			// Create normals accesssor
			osg::ref_ptr<const osg::Vec3Array> normals = dynamic_cast<const osg::Vec3Array*>(morphTarget->getNormalArray());
			if (morphNormalsMap.find(morphTargetName) == morphNormalsMap.end())
			{
				const osg::Vec3dArray* normalsd = dynamic_cast<const osg::Vec3dArray*>(morphTarget->getNormalArray());
				if (normalsd)
					normals = doubleToFloatArray<osg::Vec3Array>(normalsd);

				if (normals)
				{
					if (!transformMatrix.isIdentity())
						normals = transformArray(normals, transformMatrix, true);
				}

				morphNormalsMap.emplace(morphTargetName, normals);
			}
			else
				normals = morphNormalsMap[morphTargetName];

			if (normals)
			{
				int normalAccessorIndex = getOrCreateAccessor(normals, normals->getNumElements(),
					TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, TINYGLTF_TARGET_ARRAY_BUFFER);
				morphTargetAttributes["NORMAL"] = normalAccessorIndex;
			}

			// Create tangents accessor
			osg::ref_ptr<const osg::Vec4Array> tangents;
			osg::ref_ptr<osg::Vec3Array> tangentsRefactor;
			const osg::Vec4dArray* tangentsd(nullptr);
			if (morphTangentsMap.find(morphTargetName) == morphTangentsMap.end())
			{
				for (auto& attrib : morphTarget->getVertexAttribArrayList())
				{
					bool isTangent = false;
					if (attrib->getUserValue("tangent", isTangent))
					{
						if (isTangent)
						{
							tangents = osg::dynamic_pointer_cast<osg::Vec4Array>(attrib);
							tangentsd = osg::dynamic_pointer_cast<osg::Vec4dArray>(attrib);
							if (tangentsd)
								tangents = doubleToFloatArray<osg::Vec4Array>(tangentsd);
							break;
						}
					}
				}
				if (tangents)
				{
					if (!transformMatrix.isIdentity())
						tangents = transformArray(tangents, transformMatrix, true);

					// Tangents on morph target is expected to be a Vec3Array, not Vec4, so we discard the 4th element.
					tangentsRefactor = new osg::Vec3Array();
					tangentsRefactor->reserveArray(tangents->size());
					for (auto& v : *tangents)
						tangentsRefactor->push_back(osg::Vec3(v.x(), v.y(), v.z()));
				}

				morphTangentsMap.emplace(morphTargetName, tangentsRefactor);
			}
			else
				tangentsRefactor = morphTangentsMap[morphTargetName];

			if (tangentsRefactor)
			{
				int tangentAccessorIndex = getOrCreateAccessor(tangentsRefactor, tangentsRefactor->getNumElements(),
					TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, TINYGLTF_TARGET_ARRAY_BUFFER);

				morphTargetAttributes["TANGENT"] = tangentAccessorIndex;
			}

			primitive.targets.push_back(morphTargetAttributes);

			_gltfMorphTargets[morphTargetName] = meshNodeId;
		}
	}
}

#pragma endregion


#pragma region Animations Processing


void OSGtoGLTF::createVec3Sampler(tinygltf::Animation& gltfAnimation, int targetId, osgAnimation::Vec3LinearChannel* vec3Channel)
{
	std::string transformType = vec3Channel->getName();
	std::string targetPath;

	if (transformType == "translate")
		targetPath = "translation";
	else if (transformType == "scale" || transformType == "ScalingCompensation")
		targetPath = "scale";
	else
	{
		OSG_WARN << "WARNING: Unknown animation channel target: " << transformType << std::endl;
		return;
	}

	osgAnimation::Vec3KeyframeContainer* keyframes = vec3Channel->getOrCreateSampler()->getOrCreateKeyframeContainer();
#ifndef NDEBUG
	osg::ref_ptr<osg::FloatArray> oldTimesArray = new osg::FloatArray;
#endif
	osg::ref_ptr<osg::FloatArray> timesArray = new osg::FloatArray;
	osg::ref_ptr<osg::Vec3Array> keysArray = new osg::Vec3Array;

	float timeMin(FLT_MAX);
	float timeMax(-FLT_MAX);

#ifndef NDEBUG
	oldTimesArray->reserve(keyframes->size());
#endif
	timesArray->reserve(keyframes->size());
	keysArray->reserve(keyframes->size());

	// Check wether we have a Stacked Matrix transform for this channel.
	osg::Matrix stackedTranslate;
	if (_gltfStackedMatrices.find(targetId) != _gltfStackedMatrices.end())
	{
		stackedTranslate = _gltfStackedMatrices.at(targetId);
	}

	int i = -1; // Keep track of time and try to correct equal times
	for (osgAnimation::Vec3Keyframe& keyframe : *keyframes) 
	{
		i++;

		float timeValue = keyframe.getTime();
#ifndef NDEBUG
		oldTimesArray->push_back(timeValue);
#endif
		if (i > 0)
		{
			float oldTime = (*keyframes)[i - 1].getTime();
			float delta = timeValue - oldTime;
			if (delta <= 0.0) // can't have equal time or unordered. Can break animations, but they would be broken anyway...
			{
				timeValue += std::abs(delta) + 0.001; // 0.1 milissecond at a time
				keyframe.setTime(timeValue);
			}
		}
		timesArray->push_back(timeValue);
		if (targetPath == "translation")
			keysArray->push_back(keyframe.getValue() * stackedTranslate);
		else
			keysArray->push_back(keyframe.getValue());

		timeMin = osg::minimum(timeMin, static_cast<float>(timeValue));
		timeMax = osg::maximum(timeMax, static_cast<float>(timeValue));
	}

	// Sanity check
	if (timesArray->size() == 0)
		return;

	tinygltf::AnimationSampler sampler;
	sampler.input = getOrCreateAccessor(timesArray, timesArray->size(), TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TYPE_SCALAR, 0);
	sampler.output = getOrCreateAccessor(keysArray, keysArray->size(), TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, 0);
	sampler.interpolation = "LINEAR";

	tinygltf::Accessor& timesAccessor = _model.accessors[sampler.input];
	timesAccessor.minValues.push_back(timeMin);
	timesAccessor.maxValues.push_back(timeMax);

	int samplerIndex = gltfAnimation.samplers.size();
	gltfAnimation.samplers.push_back(sampler);

	tinygltf::AnimationChannel channel;
	channel.sampler = samplerIndex;
	channel.target_node = targetId;
	channel.target_path = targetPath;

	gltfAnimation.channels.push_back(channel);
}

void OSGtoGLTF::createQuatSampler(tinygltf::Animation& gltfAnimation, int targetId, osgAnimation::QuatSphericalLinearChannel* quatChannel)
{
	// Assumindo que o channel de quaternions é sempre para rotação
	std::string targetPath = "rotation";

	osgAnimation::QuatKeyframeContainer* keyframes = quatChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();
	osg::ref_ptr<osg::FloatArray> timesArray = new osg::FloatArray;
	osg::ref_ptr<osg::Vec4Array> keysArray = new osg::Vec4Array; // QuatArray is double, we need float!

	float timeMin(FLT_MAX);
	float timeMax(-FLT_MAX);

	timesArray->reserve(keyframes->size());
	keysArray->reserve(keyframes->size());

	// Check wether we have a Stacked Matrix transform for this channel.
	osg::Quat stackedRotation;
	if (_gltfStackedMatrices.find(targetId) != _gltfStackedMatrices.end())
	{
		osg::Vec3f translation, scale;
		osg::Quat so;
		_gltfStackedMatrices.at(targetId).decompose(translation, stackedRotation, scale, so);
	}

	int i = -1; // Keep track of time and try to correct equal times
	for (osgAnimation::QuatKeyframe& keyframe : *keyframes)
	{
		i++;

		float timeValue = keyframe.getTime();
		if (i > 0)
		{
			float oldTime = (*keyframes)[i - 1].getTime();
			float delta = timeValue - oldTime;
			if (delta <= 0.0) // can't have equal time or unordered. Can break animations, but they would be broken anyway...
			{
				timeValue += std::abs(delta) + 0.001; // 0.1 milissecond at a time
				keyframe.setTime(timeValue);
			}
		}
		timesArray->push_back(timeValue);

		osg::Quat quat(keyframe.getValue().x(), keyframe.getValue().y(), keyframe.getValue().z(), keyframe.getValue().w());
		if (quat.x() == 0.0f && quat.y() == 0.0f && quat.z() == 0.0 && quat.w() == 0.0f) //fix broken quat
			quat = osg::Vec4(0.0f, 0.0f, 0.0f, 1.0f);

		quat *= stackedRotation;
		quat.asVec4().normalize();

		keysArray->push_back(quat.asVec4());

		timeMin = osg::minimum(timeMin, static_cast<float>(timeValue));
		timeMax = osg::maximum(timeMax, static_cast<float>(timeValue));
	}

	// Sanity check
	if (timesArray->size() == 0)
		return;

	// Criar os accessors para os tempos e valores (quaternions)
	tinygltf::AnimationSampler sampler;
	sampler.input = getOrCreateAccessor(timesArray, timesArray->size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_SCALAR, 0);
	sampler.output = getOrCreateAccessor(keysArray, keysArray->size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC4, 0);
	sampler.interpolation = "LINEAR";

	tinygltf::Accessor& timesAccessor = _model.accessors[sampler.input];
	timesAccessor.minValues.push_back(timeMin);
	timesAccessor.maxValues.push_back(timeMax);

	int samplerIndex = gltfAnimation.samplers.size();
	gltfAnimation.samplers.push_back(sampler);

	tinygltf::AnimationChannel channel;
	channel.sampler = samplerIndex;
	channel.target_node = targetId;
	channel.target_path = targetPath;

	gltfAnimation.channels.push_back(channel);
#ifndef NDEBUG
	int ChannelID = gltfAnimation.channels.size() - 1;
	ChannelID = ChannelID;
#endif
}

void OSGtoGLTF::gatherFloatKeys(osgAnimation::FloatLinearChannel* floatChannel, const std::string& morphTarget)
{
	osgAnimation::FloatKeyframeContainer* keyframes = floatChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

	// Map keyframe times for a given morph target weights
	for (const osgAnimation::FloatKeyframe& keyframe : *keyframes)
	{
		float currentTime = keyframe.getTime();

		if (_morphTargetTimeWeights.find(currentTime) != _morphTargetTimeWeights.end())
		{
			std::map<std::string, float>& morphWeight = _morphTargetTimeWeights.at(keyframe.getTime());
			morphWeight[morphTarget] = keyframe.getValue();
		}
		else
		{
			std::map<std::string, float> newMorphKey;
			newMorphKey[morphTarget] = keyframe.getValue();
			_morphTargetTimeWeights[currentTime] = newMorphKey;
		}
	}

	_currentMorphTargets.push_back(morphTarget);

}

void OSGtoGLTF::flushWeightsKeySampler(tinygltf::Animation& gltfAnimation, int targetId)
{

	if (_morphTargetTimeWeights.size() == 0)
		return;

	osg::ref_ptr<osg::FloatArray> timesArrayTmp = new osg::FloatArray;
	osg::ref_ptr<osg::FloatArray> keysArray = new osg::FloatArray;

	// Create times and keys array
	for (auto& timeKey : _morphTargetTimeWeights)
	{
		float currentTime = timeKey.first;
		timesArrayTmp->push_back(currentTime);

		std::map<std::string, float>& currentMorphWeights = timeKey.second;

		for (auto& morphTarget : _currentMorphTargets)
		{
			float weight(0.0f);

			//Try to look for the weight in the array. If not found, still place a 0 on keys array
			if (currentMorphWeights.find(morphTarget) != currentMorphWeights.end())
			{
				weight = currentMorphWeights.at(morphTarget);
			}
			
			keysArray->push_back(weight);
		}
	}

	_currentMorphTargets.clear();
	_morphTargetTimeWeights.clear();

	// Proceed with normal sampler creation
	osg::ref_ptr<osg::FloatArray> timesArray = new osg::FloatArray;
	float timeMin(FLT_MAX);
	float timeMax(-FLT_MAX);

	timesArray->reserveArray(timesArrayTmp->size());
	size_t i = 0; // Keep track of time and try to correct equal times
	for (auto& timeValue : *timesArrayTmp)
	{
		if (i > 0)
		{
			double oldTime = (*timesArrayTmp)[i - 1];
			double delta = timeValue - oldTime;
			if (delta <= 0.0) // can't have equal time or unordered. Can break animations, but they would be broken anyway...
			{
				timeValue += std::abs(delta) + 0.001; // 0.1 milissecond at a time
			}
		}
		timesArray->push_back(timeValue);
		timeMin = osg::minimum(timeMin, static_cast<float>(timeValue));
		timeMax = osg::maximum(timeMax, static_cast<float>(timeValue));
		++i;
	}

	// Sanity check
	if (timesArray->size() == 0)
		return;

	// Criar os accessors para os tempos e valores
	tinygltf::AnimationSampler sampler;
	sampler.input = getOrCreateAccessor(timesArray, timesArray->size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_SCALAR, 0);
	sampler.output = getOrCreateAccessor(keysArray, keysArray->size(), TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_SCALAR, 0);
	sampler.interpolation = "LINEAR";

	tinygltf::Accessor& timesAccessor = _model.accessors[sampler.input];
	timesAccessor.minValues.push_back(timeMin);
	timesAccessor.maxValues.push_back(timeMax);

	int samplerIndex = gltfAnimation.samplers.size();
	gltfAnimation.samplers.push_back(sampler);

	tinygltf::AnimationChannel channel;
	channel.sampler = samplerIndex;
	channel.target_node = targetId;
	channel.target_path = "weights";

	gltfAnimation.channels.push_back(channel);
}

void OSGtoGLTF::createAnimation(const osg::ref_ptr<osgAnimation::Animation> osgAnimation)
{
	std::string animationName = osgAnimation->getName().c_str();
	animationName = getLastNamePart(animationName);

	tinygltf::Animation gltfAnimation;
	gltfAnimation.name = animationName;

	int oldTargetId(-1);
	int targetId(-1);
	int realTarget(-1);
	for (auto& channel : osgAnimation->getChannels())
	{
		std::string targetName = channel->getTargetName();

		// TODO: Morph
		// Get target ID from name
		if (_gltfValidAnimationTargets.find(targetName) != _gltfValidAnimationTargets.end()
			|| _gltfMorphTargets.find(targetName) != _gltfMorphTargets.end())
		{
			if (_gltfValidAnimationTargets.find(targetName) != _gltfValidAnimationTargets.end())
				targetId = _gltfValidAnimationTargets.at(targetName);
			else
				targetId = _gltfMorphTargets.at(targetName);
		}
		else
		{
			// Check to see if missing target was a dummy (empty) node before
			if (_gltfAllTargets.find(targetName) != _gltfAllTargets.end())
				continue;

			// Warn user of missing animation target
			if (missingTargets.find(targetName) == missingTargets.end() && 
				_discardedAnimationTargetNames.find(targetName) == _discardedAnimationTargetNames.end())
			{
				OSG_WARN << "WARNING: Animation target " << targetName << " not found." << std::endl;
				missingTargets.emplace(targetName);
				continue;
			}
		}

		if (auto vec3Channel = dynamic_cast<osgAnimation::Vec3LinearChannel*>(channel.get()))
		{
			createVec3Sampler(gltfAnimation, targetId, vec3Channel);
		}
		else if (auto quatChannel = dynamic_cast<osgAnimation::QuatSphericalLinearChannel*>(channel.get())) 
		{
			createQuatSampler(gltfAnimation, targetId, quatChannel);
		}

		// Float samplers are treated different. It is animating node weights and so it needs to first
		// gather all keys from different morph targets of the same node and then align them in a single channel, then flush it all
		// to the animation stack. For this we need static structures as helpers.
		else if (auto floatChannel = dynamic_cast<osgAnimation::FloatLinearChannel*>(channel.get())) 
		{
			if (oldTargetId == -1)
				oldTargetId = targetId;

			if (targetId == oldTargetId)
			{
				gatherFloatKeys(floatChannel, targetName);
				realTarget = targetId;
			}
			else
			{
				flushWeightsKeySampler(gltfAnimation, oldTargetId);
				gatherFloatKeys(floatChannel, targetName);
				realTarget = targetId;
				oldTargetId = targetId;
			}			
		}
#ifndef NDEBUG
		int ChannelID = gltfAnimation.channels.size() - 1;
		ChannelID = ChannelID;
#endif
	}

	// Ensure all float animations are saved
	flushWeightsKeySampler(gltfAnimation, realTarget);

	_model.animations.push_back(gltfAnimation);

#ifndef NDEBUG
	int ChannelID = gltfAnimation.channels.size() - 1;
	ChannelID = ChannelID;
#endif

}

void OSGtoGLTF::applyBasicAnimation(const osg::ref_ptr<osg::Callback>& callback)
{
	if (!callback)
		return;

	auto bam = osg::dynamic_pointer_cast<osgAnimation::BasicAnimationManager>(callback);
	if (!bam)
		return;

	OSG_NOTICE << "Processing " << bam->getAnimationList().size() << " animation(s)..." << std::endl;

	// Run through all animations
	for (auto& animation : bam->getAnimationList())
	{
		createAnimation(animation);
	}
}

void OSGtoGLTF::addAnimationTarget(int gltfNodeId, const osg::ref_ptr<osg::Callback>& nodeCallback)
{
	const osg::ref_ptr<osgAnimation::UpdateMatrixTransform> umt = osg::dynamic_pointer_cast<osgAnimation::UpdateMatrixTransform>(nodeCallback);

	if (!umt)
		return;

	std::string updateMatrixName = umt->getName();
	_gltfValidAnimationTargets[updateMatrixName] = gltfNodeId;
	
	auto& stackedTransforms = umt->getStackedTransforms();

	for (auto& stackedTransform : stackedTransforms)
	{
		if (auto matrixElement = osg::dynamic_pointer_cast<osgAnimation::StackedMatrixElement>(stackedTransform))
		{
			_gltfStackedMatrices[gltfNodeId] = matrixElement->getMatrix();
			break;
		}
	}
}

void OSGtoGLTF::addDummyTarget(const osg::ref_ptr<osg::Callback>& nodeCallback)
{
	const osg::ref_ptr<osgAnimation::UpdateMatrixTransform> umt = osg::dynamic_pointer_cast<osgAnimation::UpdateMatrixTransform>(nodeCallback);

	if (!umt)
		return;

	std::string updateMatrixName = umt->getName();
	_gltfAllTargets.emplace(updateMatrixName);
}

#pragma endregion


#pragma region Materials Processing

OSGtoGLTF::MaterialSurfaceLayer OSGtoGLTF::getTexMaterialLayer(const osg::Material* material, const osg::Texture* texture)
{
	if (!texture || !material)
		return MaterialSurfaceLayer::None;

	std::string textureFile = osgDB::getSimpleFileName(texture->getImage(0)->getFileName());
	std::string layerName;

	// Run through all known layer names and try to match textureFile
	for (auto& knownLayer : _knownMaterialLayerNames)
	{
		std::string materialFile;
		std::ignore = material->getUserValue(std::string("textureLayer_") + knownLayer, materialFile);
		if (materialFile == textureFile)
		{
			if (knownLayer == "AOPBR")
				return MaterialSurfaceLayer::AmbientOcclusion;
			else if (knownLayer == "AlbedoPBR" || knownLayer == "DiffusePBR" || knownLayer == "DiffuseColor")
				return MaterialSurfaceLayer::Albedo;
			else if (knownLayer == "ClearCoat" || knownLayer == "Matcap")
				return MaterialSurfaceLayer::ClearCoat;
			else if (knownLayer == "NormalMap" || knownLayer == "BumpMap")
				return MaterialSurfaceLayer::NormalMap;
			else if (knownLayer == "ClearCoatNormalMap")
				return MaterialSurfaceLayer::ClearCoatNormal;
			else if (knownLayer == "ClearCoatRoughness")
				return MaterialSurfaceLayer::ClearCoatRoughness;
			else if (knownLayer == "SpecularPBR" || knownLayer == "SpecularF0" || knownLayer == "SpecularColor")
				return MaterialSurfaceLayer::Specular;
			else if (knownLayer == "Displacement" || knownLayer == "CavityPBR")
				return MaterialSurfaceLayer::DisplacementColor;
			else if (knownLayer == "EmitColor")
				return MaterialSurfaceLayer::Emissive;
			else if (knownLayer == "GlossinessPBR" || knownLayer == "RoughnessPBR")
				return MaterialSurfaceLayer::Roughness;
			else if (knownLayer == "Opacity" || knownLayer == "AlphaMask")
				return MaterialSurfaceLayer::Transparency;
			else if (knownLayer == "MetalnessPBR")
				return MaterialSurfaceLayer::Metallic;
			else if (knownLayer == "Sheen")
				return MaterialSurfaceLayer::Sheen;
			else if (knownLayer == "SheenRoughness")
				return MaterialSurfaceLayer::SheenRoughness;
		}
	}

	return MaterialSurfaceLayer::None;
}

int OSGtoGLTF::createTexture(const osg::Texture* texture)
{
	const osg::Image* osgImage = texture->getImage(0);
	std::string fileName = osgImage->getFileName();

	// Replace backslash
	for (auto& c : fileName)
	{
		if (c == '\\')
			c = '/';
	}

	if (_gltfTextures.find(fileName) != _gltfTextures.end())
		return _gltfTextures[fileName];

	tinygltf::Image gltfImage;
	gltfImage.uri = fileName;
	int imageIndex = _model.images.size();
	_model.images.push_back(gltfImage);

	tinygltf::Sampler sampler;
	sampler.magFilter = texture->getFilter(osg::Texture::MAG_FILTER);
	sampler.minFilter = texture->getFilter(osg::Texture::MIN_FILTER);
	sampler.wrapS = texture->getWrap(osg::Texture::WRAP_S);
	sampler.wrapT = texture->getWrap(osg::Texture::WRAP_T);
	int samplerIndex = _model.samplers.size();
	_model.samplers.push_back(sampler);

	tinygltf::Texture gltfTexture;
	gltfTexture.sampler = samplerIndex;
	gltfTexture.source = imageIndex;

	int textureIndex = _model.textures.size();
	_model.textures.push_back(gltfTexture);

	_gltfTextures.emplace(fileName, textureIndex);

	return textureIndex;
}

int OSGtoGLTF::getCurrentMaterial(osg::Geometry* geometry)
{
	int materialIndex(-1);

	osg::Vec4 diffuse(0, 0, 0, 1),
		ambient(0, 0, 0, 1),
		specular(0, 0, 0, 1),
		emission(0, 0, 0, 1);

	float shininess(0);
	float transparency(0);

	// Parse rig geometry
	osgAnimation::RigGeometry* rigGeometry = dynamic_cast<osgAnimation::RigGeometry*>(geometry);

	// Push material and textures from OSG
	osg::ref_ptr<osg::StateSet> stateSet = rigGeometry ? rigGeometry->getSourceGeometry()->getOrCreateStateSet() : geometry->getOrCreateStateSet();
	const osg::Material* mat = dynamic_cast<const osg::Material*>(stateSet->getAttribute(osg::StateAttribute::MATERIAL));
	if (!mat)
		return -1;

	std::string materialName = mat->getName();
	if (_gltfMaterials.find(materialName) != _gltfMaterials.end())
		return _gltfMaterials[materialName];

	std::vector<const osg::Texture*> texArray;
	for (unsigned int i = 0; i < stateSet->getNumTextureAttributeLists(); i++)
	{
		texArray.push_back(dynamic_cast<const osg::Texture*>(stateSet->getTextureAttribute(i, osg::StateAttribute::TEXTURE)));
	}

	diffuse = mat->getDiffuse(osg::Material::FRONT);
	ambient = mat->getAmbient(osg::Material::FRONT);
	specular = mat->getSpecular(osg::Material::FRONT);
	shininess = mat->getShininess(osg::Material::FRONT);
	emission = mat->getEmission(osg::Material::FRONT);
	transparency = 1 - diffuse.w();

	tinygltf::Material material;
	material.name = materialName;

	material.pbrMetallicRoughness.baseColorFactor = { diffuse.r(), diffuse.g(), diffuse.b(), diffuse.a() };
	// material.emissiveFactor = { emission.r(), emission.g(), emission.b() };

	bool backfaceCull(false);
	std::ignore = mat->getUserValue("backfaceCull", backfaceCull);
	material.doubleSided = !backfaceCull;

	// Declare use of specular extension
	//if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_specular") == _model.extensionsUsed.end()) 
	//{
	//	_model.extensionsUsed.emplace_back("KHR_materials_specular");
	//}

	//tinygltf::Value specularColorFactorValue;
	//for (float component : {specular.x(), specular.y(), specular.z()}) 
	//{
	//	specularColorFactorValue.Get<tinygltf::Value::Array>().emplace_back(component);
	//}

	//tinygltf::Value specularExtensionValue;
	//specularExtensionValue.Get<tinygltf::Value::Object>().emplace("specularColorFactor", specularColorFactorValue);
	//material.extensions.emplace("KHR_materials_specular", specularExtensionValue);

	std::set<MaterialSurfaceLayer> usedMaterials;
	for (auto& tex : texArray)
	{
		MaterialSurfaceLayer textureLayer = getTexMaterialLayer(mat, tex);

		// Don't overwrite materials.
		if (usedMaterials.find(textureLayer) != usedMaterials.end())
			continue;
		usedMaterials.emplace(textureLayer);

		int textureIndex = createTexture(tex);

		switch (textureLayer)
		{
		case MaterialSurfaceLayer::Albedo:
		{
			material.pbrMetallicRoughness.baseColorTexture.index = textureIndex;
			break;
		}
		case MaterialSurfaceLayer::Transparency:
		{
			material.alphaMode = "BLEND";
			break;
		}
		case MaterialSurfaceLayer::AmbientOcclusion:
		{
			material.occlusionTexture.index = textureIndex;
			break;
		}
		case MaterialSurfaceLayer::ClearCoat:
		{
			// Declare use of clear coat extension
			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_clearcoat") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_clearcoat");
			}

			if (material.extensions.find("KHR_materials_clearcoat") == material.extensions.end()) 
			{
				material.extensions["KHR_materials_clearcoat"] = tinygltf::Value();
			}
			auto& clearCoatExtension = material.extensions["KHR_materials_clearcoat"];
			tinygltf::Value clearcoatTextureValue;
			clearcoatTextureValue.Get< tinygltf::Value::Object>()["index"] = tinygltf::Value(textureIndex);
			clearCoatExtension.Get<tinygltf::Value::Object>().emplace("clearcoatTexture", clearcoatTextureValue);								
			break;
		}
		case MaterialSurfaceLayer::ClearCoatNormal:
		{
			// Declare use of clear coat extension
			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_clearcoat") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_clearcoat");
			}

			if (material.extensions.find("KHR_materials_clearcoat") == material.extensions.end())
			{
				material.extensions["KHR_materials_clearcoat"] = tinygltf::Value();
			}
			tinygltf::Value& clearCoatExtension = material.extensions["KHR_materials_clearcoat"];
			tinygltf::Value clearcoatNormalTexture;
			clearcoatNormalTexture.Get<tinygltf::Value::Object>()["index"] = tinygltf::Value(textureIndex);
			clearCoatExtension.Get<tinygltf::Value::Object>().emplace("clearcoatNormalTexture", clearcoatNormalTexture);
			break;
		}
		case MaterialSurfaceLayer::ClearCoatRoughness:
		{
			// Declare use of clear coat extension
			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_clearcoat") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_clearcoat");
			}

			if (material.extensions.find("KHR_materials_clearcoat") == material.extensions.end())
			{
				material.extensions["KHR_materials_clearcoat"] = tinygltf::Value();
			}
			tinygltf::Value& clearCoatExtension = material.extensions["KHR_materials_clearcoat"];
			tinygltf::Value clearcoatRoughnessTexture;
			clearcoatRoughnessTexture.Get<tinygltf::Value::Object>()["index"] = tinygltf::Value(textureIndex);
			clearCoatExtension.Get<tinygltf::Value::Object>().emplace("clearcoatRoughnessTexture", clearcoatRoughnessTexture);

			break;
		}
		case MaterialSurfaceLayer::Emissive:
		{
			material.emissiveTexture.index = textureIndex;
			break;
		}
		case MaterialSurfaceLayer::Metallic:
		{
			material.pbrMetallicRoughness.metallicRoughnessTexture.index = textureIndex;
			break;
		}
		case MaterialSurfaceLayer::NormalMap:
		{
			material.normalTexture.index = textureIndex;
			break;
		}
		case MaterialSurfaceLayer::Roughness: // Conflicts with metallic. Must study how to solve this
		{
			material.pbrMetallicRoughness.metallicRoughnessTexture.index = textureIndex;
			break;
		}
		case MaterialSurfaceLayer::Sheen:
		{
			// Declare use of sheen extension
			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_sheen") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_sheen");
			}

			if (material.extensions.find("KHR_materials_sheen") == material.extensions.end())
			{
				material.extensions["KHR_materials_sheen"] = tinygltf::Value();
			}
			tinygltf::Value& sheenExtension = material.extensions["KHR_materials_sheen"];
			tinygltf::Value sheenTexture;
			sheenTexture.Get<tinygltf::Value::Object>()["index"] = tinygltf::Value(textureIndex);
			sheenExtension.Get<tinygltf::Value::Object>().emplace("sheenColorTexture", sheenTexture);

			break;
		}
		case MaterialSurfaceLayer::SheenRoughness:
		{
			// Declare use of sheen extension
			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_sheen") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_sheen");
			}

			if (material.extensions.find("KHR_materials_sheen") == material.extensions.end())
			{
				material.extensions["KHR_materials_sheen"] = tinygltf::Value();
			}

			tinygltf::Value& sheenExtension = material.extensions["KHR_materials_sheen"];
			tinygltf::Value sheenTexture;
			sheenTexture.Get<tinygltf::Value::Object>()["index"] = tinygltf::Value(textureIndex);
			sheenExtension.Get<tinygltf::Value::Object>().emplace("sheenRoughnessTexture", sheenTexture);

			break;
		}
		case MaterialSurfaceLayer::Specular:
		{
			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_specular") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_specular");
			}

			if (material.extensions.find("KHR_materials_specular") == material.extensions.end())
			{
				material.extensions["KHR_materials_specular"] = tinygltf::Value();
			}
			tinygltf::Value& clearCoatExtension = material.extensions["KHR_materials_specular"];
			tinygltf::Value specularColorTexture;
			specularColorTexture.Get<tinygltf::Value::Object>()["index"] = tinygltf::Value(textureIndex);
			clearCoatExtension.Get<tinygltf::Value::Object>().emplace("specularColorTexture", specularColorTexture);

			break;
		}
		default:
		{
			OSG_DEBUG << "Missing texture placement for: " << osgDB::getSimpleFileName(tex->getImage(0)->getFileName()) << std::endl;
		}
		}
	}

	materialIndex = _model.materials.size();
	_model.materials.push_back(material);

	_gltfMaterials.emplace(materialName, materialIndex);
	

	return materialIndex;
}

int OSGtoGLTF::createTextureV2(const TextureInfo2& texInfo, const std::string& textureNameOverride)
{
	std::string fileName = stripAllExtensions(texInfo.Name) + ".png";
	if (textureNameOverride != "")
		fileName = stripAllExtensions(textureNameOverride) + ".png";

	if (_gltfTextures.find(fileName) != _gltfTextures.end())
		return _gltfTextures[fileName];

	tinygltf::Image gltfImage;
	gltfImage.uri = "textures/" + fileName;
	int imageIndex = _model.images.size();
	_model.images.push_back(gltfImage);

	tinygltf::Sampler sampler;
	sampler.magFilter = getFilterModeFromString(texInfo.MagFilter);
	sampler.minFilter = getFilterModeFromString(texInfo.MinFilter);
	sampler.wrapS = getWrapModeFromString(texInfo.WrapS);
	sampler.wrapT = getWrapModeFromString(texInfo.WrapT);
	int samplerIndex = _model.samplers.size();
	_model.samplers.push_back(sampler);

	tinygltf::Texture gltfTexture;
	gltfTexture.sampler = samplerIndex;
	gltfTexture.source = imageIndex;

	int textureIndex = _model.textures.size();
	_model.textures.push_back(gltfTexture);

	_gltfTextures.emplace(fileName, textureIndex);

	return textureIndex;
}


int OSGtoGLTF::getCurrentMaterialV2(osg::Geometry* geometry)
{
	// Parse materials file
	if (!meshMaterialsParsed)
	{
		buildMaterials();
	}

	// Parse rig geometry
	osgAnimation::RigGeometry* rigGeometry = dynamic_cast<osgAnimation::RigGeometry*>(geometry);

	// Push material and textures from OSG. If not found, try the default one (first in load order).
	osg::ref_ptr<osg::StateSet> stateSet = rigGeometry ? rigGeometry->getSourceGeometry()->getStateSet() : geometry->getStateSet();
	const osg::Material* mat = stateSet ? dynamic_cast<const osg::Material*>(stateSet->getAttribute(osg::StateAttribute::MATERIAL)) : NULL;
	auto& knownMaterials = meshMaterials.getMaterials();
	if (!mat && knownMaterials.size() == 0)
	{
		return -1;
	}

	std::string stateSetName = stateSet ? stateSet->getName() : "";
	std::string materialName = mat ? mat->getName() : !stateSetName.empty() ? stateSetName : knownMaterials.begin()->second.Name;
	if (_gltfMaterials.find(materialName) != _gltfMaterials.end())
		return _gltfMaterials[materialName];

	// Get current material
	if (knownMaterials.find(materialName) == knownMaterials.end())
		return -1;

	auto& knownMaterial = knownMaterials.at(materialName);

	if (knownMaterial.UsePBR)
	{
		return getMaterialPBR(knownMaterial);
	}
	else
	{
		return getMaterialClassic(knownMaterial);
	}

	return -1;

}

int OSGtoGLTF::getMaterialPBR(const MaterialInfo2& materialInfo)
{
	tinygltf::Material material;
	material.name = materialInfo.Name;

	material.doubleSided = !materialInfo.BackfaceCull;

	// Decide which color / factor to use.
	// Priority: AlbedoPBR, DiffusePBR, DiffuseColor
	ChannelInfo2 activeColor;
	if (materialInfo.Channels.find("AlbedoPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("AlbedoPBR").Enable)
		activeColor = materialInfo.Channels.at("AlbedoPBR");
	else if (materialInfo.Channels.find("DiffusePBR") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffusePBR").Enable)
		activeColor = materialInfo.Channels.at("DiffusePBR");
	else if (materialInfo.Channels.find("DiffuseColor") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffuseColor").Enable)
		activeColor = materialInfo.Channels.at("DiffuseColor");

	if (activeColor.Enable)
	{
		if (activeColor.Color.size() == 3)
			material.pbrMetallicRoughness.baseColorFactor = { activeColor.Color[0], activeColor.Color[1], activeColor.Color[2], activeColor.Factor };
		else
			material.pbrMetallicRoughness.baseColorFactor = { activeColor.Factor, activeColor.Factor, activeColor.Factor, activeColor.Factor };
	}

	// Decide which texture to use.
	// Priority: AlbedoPBR, DiffusePBR, DiffuseColor
	ChannelInfo2 activeTexture;
	std::string activeTextureName;
	if (materialInfo.Channels.find("AlbedoPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("AlbedoPBR").Enable &&
		materialInfo.Channels.at("AlbedoPBR").Texture.Name != "")
		activeTexture = materialInfo.Channels.at("AlbedoPBR");
	else if (materialInfo.Channels.find("DiffusePBR") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffusePBR").Enable &&
		materialInfo.Channels.at("DiffusePBR").Texture.Name != "")
		activeTexture = materialInfo.Channels.at("DiffusePBR");
	else if (materialInfo.Channels.find("DiffuseColor") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffuseColor").Enable &&
		materialInfo.Channels.at("DiffuseColor").Texture.Name != "")
		activeTexture = materialInfo.Channels.at("DiffuseColor");

	if (activeTexture.Enable)
		activeTextureName = activeTexture.Texture.Name;

	// Calculate Opacity
	if (materialInfo.Channels.find("Opacity") != materialInfo.Channels.end() && materialInfo.Channels.at("Opacity").Enable)
	{
		const ChannelInfo2& opacity = materialInfo.Channels.at("Opacity");
		if (material.pbrMetallicRoughness.baseColorFactor.size() == 4)
			material.pbrMetallicRoughness.baseColorFactor[3] = opacity.Factor;
		else if (material.pbrMetallicRoughness.baseColorFactor.size() == 0 && opacity.Color.size() == 3)
			material.pbrMetallicRoughness.baseColorFactor = { opacity.Color[0], opacity.Color[1], opacity.Color[2], opacity.Factor };
		else
			material.pbrMetallicRoughness.baseColorFactor = { opacity.Factor, opacity.Factor, opacity.Factor, opacity.Factor };

//		if (opacity.Texture.Name != "")
		{
			if (opacity.Type == "alphaBlend" && opacity.Factor < 1.0)
				material.alphaMode = "BLEND";
			else if (opacity.Type == "dithering" || opacity.Type == "alphaBlend" && opacity.Factor == 1.0)
				material.alphaMode = "MASK";
			else
				material.alphaMode = "OPAQUE";
		}

		if (opacity.Type == "additive" && opacity.Factor == 0.0)
			material.alphaMode = "MASK";

		// Calculate refraction
		if (opacity.Type == "refraction" && opacity.Texture.Name != "")
		{
			material.alphaMode = "BLEND";

			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_ior") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_ior");
			}

			if (material.extensions.find("KHR_materials_ior") == material.extensions.end())
			{
				material.extensions["KHR_materials_ior"] = tinygltf::Value();
			}
			tinygltf::Value& iorExtension = material.extensions["KHR_materials_ior"];
			iorExtension.Get<tinygltf::Value::Object>().emplace("ior", opacity.IOR);
		}

		// Calculate and combine textures for blending if names are different.
		if (opacity.Texture.Name != "" && opacity.Texture.Name != activeTexture.Texture.Name)
		{
			activeTextureName = combineTextures(activeTexture.Texture.Name, opacity.Texture.Name);
		}

	}

	// Create standalone or Opacity combined texture for AlbedoPBR/Diffuse/DiffuseColor
	material.pbrMetallicRoughness.baseColorTexture.index = createTextureV2(activeTexture.Texture, activeTextureName);


	// Ambient Occlusion
	ChannelInfo2 aoPBR;
	if (materialInfo.Channels.find("AOPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("AOPBR").Enable)
		aoPBR = materialInfo.Channels.at("AOPBR");
	if (aoPBR.Enable && aoPBR.Texture.Name != "")
		material.occlusionTexture.index = createTextureV2(aoPBR.Texture);

	// Emissive color and texture
	ChannelInfo2 emissive;
	if (materialInfo.Channels.find("EmitColor") != materialInfo.Channels.end() && materialInfo.Channels.at("EmitColor").Enable)
		emissive = materialInfo.Channels.at("EmitColor");

	if (emissive.Enable && emissive.Texture.Name != "")
	{
		if (emissive.Color.size() == 3)
			material.emissiveFactor = { emissive.Color[0], emissive.Color[1], emissive.Color[2] };
		else
			material.emissiveFactor = { emissive.Factor, emissive.Factor, emissive.Factor };

		if (emissive.Texture.Name != "")
			material.emissiveTexture.index = createTextureV2(emissive.Texture);
	}

	// Metallic factor
	ChannelInfo2 metallic;
	std::string metallicTexture;
	if (materialInfo.Channels.find("MetalnessPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("MetalnessPBR").Enable)
		metallic = materialInfo.Channels.at("MetalnessPBR");
	if (metallic.Enable)
	{
		material.pbrMetallicRoughness.metallicFactor = metallic.Factor;
		if (metallic.Texture.Name != "")
			metallicTexture = metallic.Texture.Name;
	}

	// Roughness factor
	ChannelInfo2 roughness;
	std::string roughnessTexture;
	if (materialInfo.Channels.find("RoughnessPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("RoughnessPBR").Enable)
		roughness = materialInfo.Channels.at("RoughnessPBR");
	if (roughness.Enable)
	{
		material.pbrMetallicRoughness.roughnessFactor = roughness.Factor;
		if (roughness.Texture.Name != "")
			roughnessTexture = roughness.Texture.Name;
	}

	// Solve conflict between metallic and roughness - must combine if both are set with different images
	if (metallicTexture != "" && roughnessTexture == "")
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(metallic.Texture);
	else if (metallicTexture == "" && roughnessTexture != "")
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(roughness.Texture);
	else if (metallicTexture != "" && roughnessTexture != "" && metallicTexture == roughnessTexture)
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(roughness.Texture);
	else if (metallicTexture != "" && roughnessTexture != "" && metallicTexture != roughnessTexture)
	{
		std::string combinedTexture = combineMetallicRoughnessTextures(roughnessTexture, metallicTexture);
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(roughness.Texture, combinedTexture);
	}

	// Normal map texture
	ChannelInfo2 normal;
	if (materialInfo.Channels.find("NormalMap") != materialInfo.Channels.end() && materialInfo.Channels.at("NormalMap").Enable)
		normal = materialInfo.Channels.at("NormalMap");
	if (normal.Enable && normal.Texture.Name != "")
		material.normalTexture.index = createTextureV2(normal.Texture);


	// Emplace material on collection
	int materialIndex = _model.materials.size();
	_model.materials.push_back(material);

	_gltfMaterials.emplace(materialInfo.Name, materialIndex);

	return materialIndex;
}

int OSGtoGLTF::getMaterialClassic(const MaterialInfo2& materialInfo)
{
	tinygltf::Material material;
	material.name = materialInfo.Name;

	material.doubleSided = !materialInfo.BackfaceCull;

	// Decide which color / factor to use.
	// Priority: DiffuseColor, DiffusePBR, AlbedoPBR
	ChannelInfo2 activeColor;
	if (materialInfo.Channels.find("DiffuseColor") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffuseColor").Enable)
		activeColor = materialInfo.Channels.at("DiffuseColor");
	else if (materialInfo.Channels.find("DiffusePBR") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffusePBR").Enable)
		activeColor = materialInfo.Channels.at("DiffusePBR");
	else if (materialInfo.Channels.find("AlbedoPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("AlbedoPBR").Enable)
		activeColor = materialInfo.Channels.at("AlbedoPBR");

	if (activeColor.Enable)
	{
		if (activeColor.Color.size() == 3)
			material.pbrMetallicRoughness.baseColorFactor = { activeColor.Color[0], activeColor.Color[1], activeColor.Color[2], activeColor.Factor };
		else
			material.pbrMetallicRoughness.baseColorFactor = { activeColor.Factor, activeColor.Factor, activeColor.Factor, activeColor.Factor };
	}

	// Decide which texture to use.
	// Priority: DiffuseColor, DiffusePBR, AlbedoPBR
	ChannelInfo2 activeTexture;
	std::string activeTextureName;
	if (materialInfo.Channels.find("DiffuseColor") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffuseColor").Enable &&
		materialInfo.Channels.at("DiffuseColor").Texture.Name != "")
		activeTexture = materialInfo.Channels.at("DiffuseColor");
	else if (materialInfo.Channels.find("DiffusePBR") != materialInfo.Channels.end() && materialInfo.Channels.at("DiffusePBR").Enable &&
		materialInfo.Channels.at("DiffusePBR").Texture.Name != "")
		activeTexture = materialInfo.Channels.at("DiffusePBR");
	else if (materialInfo.Channels.find("AlbedoPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("AlbedoPBR").Enable &&
			materialInfo.Channels.at("AlbedoPBR").Texture.Name != "")
		activeTexture = materialInfo.Channels.at("AlbedoPBR");

	if (activeTexture.Enable)
		activeTextureName = activeTexture.Texture.Name;

	// Calculate Opacity
	if (materialInfo.Channels.find("Opacity") != materialInfo.Channels.end() && materialInfo.Channels.at("Opacity").Enable)
	{
		const ChannelInfo2& opacity = materialInfo.Channels.at("Opacity");
		if (material.pbrMetallicRoughness.baseColorFactor.size() == 4)
			material.pbrMetallicRoughness.baseColorFactor[3] = opacity.Factor;
		else if (material.pbrMetallicRoughness.baseColorFactor.size() == 0 && opacity.Color.size() == 3)
			material.pbrMetallicRoughness.baseColorFactor = { opacity.Color[0], opacity.Color[1], opacity.Color[2], opacity.Factor };
		else
			material.pbrMetallicRoughness.baseColorFactor = { opacity.Factor, opacity.Factor, opacity.Factor, opacity.Factor };

		if (opacity.Texture.Name != "")
		{
			if (opacity.Type == "alphaBlend" && (opacity.Factor < 1.0 || opacity.Texture.Name != activeTexture.Texture.Name))
				material.alphaMode = "BLEND";
			else if (opacity.Type == "dithering")
				material.alphaMode = "MASK";
			else
				material.alphaMode = "OPAQUE";
		}

		// Calculate refraction
		if (opacity.Type == "refraction")
		{
			material.alphaMode = "BLEND";

			if (std::find(_model.extensionsUsed.begin(), _model.extensionsUsed.end(), "KHR_materials_ior") == _model.extensionsUsed.end())
			{
				_model.extensionsUsed.emplace_back("KHR_materials_ior");
			}

			if (material.extensions.find("KHR_materials_ior") == material.extensions.end())
			{
				material.extensions["KHR_materials_ior"] = tinygltf::Value();
			}
			tinygltf::Value& iorExtension = material.extensions["KHR_materials_ior"];
			iorExtension.Get<tinygltf::Value::Object>().emplace("ior", opacity.IOR);
		}

		// Calculate and combine textures for blending if names are different.
		if (opacity.Texture.Name != "" && opacity.Texture.Name != activeTexture.Texture.Name)
		{
			activeTextureName = combineTextures(activeTexture.Texture.Name, opacity.Texture.Name);
		}

	}

	// Create standalone or Opacity combined texture for AlbedoPBR/Diffuse/DiffuseColor
	material.pbrMetallicRoughness.baseColorTexture.index = createTextureV2(activeTexture.Texture, activeTextureName);


	// Ambient Occlusion
	ChannelInfo2 aoPBR;
	if (materialInfo.Channels.find("AOPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("AOPBR").Enable)
		aoPBR = materialInfo.Channels.at("AOPBR");
	if (aoPBR.Enable && aoPBR.Texture.Name != "")
		material.occlusionTexture.index = createTextureV2(aoPBR.Texture);

	// Emissive color and texture
	ChannelInfo2 emissive;
	if (materialInfo.Channels.find("EmitColor") != materialInfo.Channels.end() && materialInfo.Channels.at("EmitColor").Enable)
		emissive = materialInfo.Channels.at("EmitColor");

	if (emissive.Enable && emissive.Texture.Name != "")
	{
		if (emissive.Color.size() == 3)
			material.emissiveFactor = { emissive.Color[0], emissive.Color[1], emissive.Color[2] };
		else
			material.emissiveFactor = { emissive.Factor, emissive.Factor, emissive.Factor };

		if (emissive.Texture.Name != "")
			material.emissiveTexture.index = createTextureV2(emissive.Texture);
	}

	// Metallic factor
	ChannelInfo2 metallic;
	std::string metallicTexture;
	if (materialInfo.Channels.find("MetalnessPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("MetalnessPBR").Enable)
		metallic = materialInfo.Channels.at("MetalnessPBR");
	if (metallic.Enable)
	{
		material.pbrMetallicRoughness.metallicFactor = metallic.Factor;

		if (metallic.Texture.Name != "")
			metallicTexture = metallic.Texture.Name;
	}

	// Roughness factor
	ChannelInfo2 roughness;
	std::string roughnessTexture;
	if (materialInfo.Channels.find("RoughnessPBR") != materialInfo.Channels.end() && materialInfo.Channels.at("RoughnessPBR").Enable)
		roughness = materialInfo.Channels.at("RoughnessPBR");
	if (roughness.Enable)
	{
		material.pbrMetallicRoughness.roughnessFactor = roughness.Factor;

		if (roughness.Texture.Name != "")
			roughnessTexture = roughness.Texture.Name;
	}

	// Solve conflict between metallic and roughness - must combine if both are set with different images
	if (metallicTexture != "" && roughnessTexture == "")
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(metallic.Texture);
	else if (metallicTexture == "" && roughnessTexture != "")
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(roughness.Texture);
	else if (metallicTexture != "" && roughnessTexture != "" && metallicTexture == roughnessTexture)
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(roughness.Texture);
	else if (metallicTexture != "" && roughnessTexture != "" && metallicTexture != roughnessTexture)
	{
		std::string combinedTexture = combineMetallicRoughnessTextures(roughnessTexture, metallicTexture);
		material.pbrMetallicRoughness.metallicRoughnessTexture.index = createTextureV2(roughness.Texture, combinedTexture);
	}

	// Normal map texture
	ChannelInfo2 normal;
	if (materialInfo.Channels.find("NormalMap") != materialInfo.Channels.end() && materialInfo.Channels.at("NormalMap").Enable)
		normal = materialInfo.Channels.at("NormalMap");
	if (normal.Enable && normal.Texture.Name != "")
		material.normalTexture.index = createTextureV2(normal.Texture);


	// Emplace material on collection
	int materialIndex = _model.materials.size();
	_model.materials.push_back(material);

	_gltfMaterials.emplace(materialInfo.Name, materialIndex);

	return materialIndex;
}


#pragma endregion


#pragma region Main functions (public)


void OSGtoGLTF::apply(osg::Node& node)
{
	// Determine the nature of the node
	osg::Geometry* geometry = dynamic_cast<osg::Geometry*>(&node);
	osgAnimation::RigGeometry* rigGeometry = dynamic_cast<osgAnimation::RigGeometry*>(&node);
	osg::MatrixTransform* matrix = dynamic_cast<osg::MatrixTransform*>(&node);
	osgAnimation::Skeleton* skeleton = dynamic_cast<osgAnimation::Skeleton*>(&node);
	osgAnimation::Bone* bone = dynamic_cast<osgAnimation::Bone*>(&node);

	bool emptyNode = isEmptyNode(&node);
	std::string NodeName = node.getName();
	if (skeleton)
		NodeName = NodeName.empty() ? "Skeleton" : NodeName;

	// Grab the model name from first matrix to use later (or now if it coincides)
	if (_firstNamedMatrix && matrix)
	{
		_modelName = NodeName;
		_firstNamedMatrix = false;
	}

	// First matrix: GLTF uses a +X=right +y=up -z=forward coordinate system
	// so we fix it here
	if (_firstMatrix && matrix && !emptyNode)
	{
		osg::Matrix transform = osg::Matrixd::rotate(osg::Z_AXIS, osg::Y_AXIS);
		osg::Matrix original = matrix->getMatrix();
		transform = transform * original;
		matrix->setMatrix(transform);
		_firstMatrix = false;
		_firstMatrixNode = &node;
		NodeName = _modelName;
	}

	bool isRoot = _model.scenes[_model.defaultScene].nodes.empty();
	if (isRoot && matrix && !emptyNode)
	{
		// put a placeholder here just to prevent any other nodes
		// from thinking they are the root
		_model.scenes[_model.defaultScene].nodes.push_back(-1);
	}

	bool pushedStateSet = false;
	osg::ref_ptr< osg::StateSet > ss = node.getStateSet();
	if (ss)
	{
		pushedStateSet = pushStateSet(ss.get());
	}

	// Don't let 2 skeletons overlap (create only 1 skin)
	if (skeleton && _gltfSkeletons.size() == 0)
	{
		_model.skins.push_back(tinygltf::Skin());
		_gltfSkeletons.push(std::make_pair(_model.skins.size() - 1, &_model.skins.back()));
	}
	else if (skeleton && _gltfSkeletons.size() > 0)
	{
		// Mark a placeholder just to let the system know there are 2 or more skeletons
		_gltfSkeletons.push(std::make_pair(-1, &_model.skins.back()));
	}

	traverse(node);

	if (ss && pushedStateSet)
	{
		popStateSet();
	}

	// TODO: Create matrices only if animated. Recalculate transforms
	// We only create relevant nodes like geometries and transform matrices
	if (!emptyNode && (geometry || matrix) || (rigGeometry && !isEmptyRig(rigGeometry)))
	{
		_model.nodes.push_back(tinygltf::Node());
		tinygltf::Node& gnode = _model.nodes.back();
		int id = _model.nodes.size() - 1;
		gnode.name = ::Stringify() << (NodeName.empty() ? (Stringify() << "_gltfNode_" << id) : NodeName);

		// For rig geometries, they are not children of any nodes.
		if (!rigGeometry)
			_osgNodeSeqMap[&node] = id;
		else
		{
			_model.scenes[_model.defaultScene].nodes.push_back(id);
			isRoot = false; // Mark root as false just to prevent unwanted modifications bellow
		}

		// For geometries without parent, we put them on root
		if (geometry && !hasMatrixParent(*geometry))
		{
			_model.scenes[_model.defaultScene].nodes.push_back(id);
			isRoot = false; // Mark root as false just to prevent unwanted modifications bellow
		}

		if (isRoot)
		{
			// replace the placeholder with the actual root id.
			_model.scenes[_model.defaultScene].nodes[0] = id;
		}

		if (bone)
		{
			// The same as above
			int boneID = _model.nodes.size() - 1;

			_gltfSkeletons.top().second->joints.push_back(boneID);
			_skeletonInvBindMatrices[boneID] = &bone->getInvBindMatrixInSkeletonSpace();
			_gltfBoneIDNames[gnode.name] = boneID;
		}

		// See if this is an animation target
		addAnimationTarget(id, getRealUpdateCallback(node.getUpdateCallback()));
	}

	addDummyTarget(getRealUpdateCallback(node.getUpdateCallback()));
}

void OSGtoGLTF::apply(osg::Group& group)
{
	apply(static_cast<osg::Node&>(group));

	// Determine nature of group
	osg::MatrixTransform* matrix = dynamic_cast<osg::MatrixTransform*>(&group);

	// Only aply children for matrices since we are skipping normal groups
	if (matrix && !isEmptyNode(&group))
	{
		for (unsigned i = 0; i < group.getNumChildren(); ++i)
		{
			if (_osgNodeSeqMap.find(group.getChild(i)) != _osgNodeSeqMap.end())
			{
				int id = _osgNodeSeqMap.at(group.getChild(i));
				_model.nodes.back().children.push_back(id);
			}

			// Get orphaned children of groups that were nested on this matrix
			std::vector<osg::Node*> output;
			getOrphanedChildren(group.getChild(i), output);
			for (auto& node : output)
			{
				if (_osgNodeSeqMap.find(node) != _osgNodeSeqMap.end())
				{
					int id = _osgNodeSeqMap.at(node);
					_model.nodes.back().children.push_back(id);
				}
			}
		}
	}

	osg::ref_ptr<osg::Callback> nodeCallback = group.getUpdateCallback();
	if (nodeCallback)
		applyBasicAnimation(getRealUpdateCallback(nodeCallback));
}

void OSGtoGLTF::apply(osg::Transform& xform)
{
	apply(static_cast<osg::Group&>(xform));

	// Compute local matrices
	osg::Matrix matrix;
	xform.computeLocalToWorldMatrix(matrix, this);

	if (!matrix.isIdentity() && !isEmptyNode(&xform))
	{
		osg::Vec3 translation, scale;
		osg::Quat rotation, so;
		matrix.decompose(translation, rotation, scale, so);
		_model.nodes.back().translation = { translation.x(), translation.y(), translation.z() };
		_model.nodes.back().rotation = { rotation.x(), rotation.y(), rotation.z(), rotation.w() };
		_model.nodes.back().scale = { scale.x(), scale.y(), scale.z() };
	}

	// Post-process skeleton... create inverse bind matrices accessor and skin weights
	// Only for last skeleton
	osgAnimation::Skeleton* skeleton = dynamic_cast<osgAnimation::Skeleton*>(&xform);
	if (skeleton && _gltfSkeletons.size() == 1)
	{
		int MatrixAccessor = createBindMatrixAccessor(_skeletonInvBindMatrices);
		_gltfSkeletons.top().second->inverseBindMatrices = MatrixAccessor;

		// Build skin weights and clear rigged mesh map, so we don't create duplicates
		BuildSkinWeights(_riggedMeshMap, _gltfBoneIDNames);

		// Clear queue and pop skeleton so other skeletons may be processed
		_skeletonInvBindMatrices.clear();
		_gltfSkeletons.pop();
		_riggedMeshMap.clear();
		_gltfBoneIDNames.clear();
	}
	else if (skeleton && _gltfSkeletons.size() > 0)
	{
		_gltfSkeletons.pop();
	}
}

void OSGtoGLTF::apply(osg::Geometry& drawable)
{
	//Early checks to valid geometry
	osg::Geometry* geom = drawable.asGeometry();
	if (!geom)
		return;

	apply(static_cast<osg::Node&>(drawable));

	osg::ref_ptr< osg::StateSet > ss = drawable.getStateSet();
	bool pushedStateSet = false;
	if (ss.valid())
	{
		pushedStateSet = pushStateSet(ss.get());
	}

	const osgAnimation::MorphGeometry* morph = dynamic_cast<const osgAnimation::MorphGeometry*>(geom);
	osgAnimation::RigGeometry* rigGeometry = dynamic_cast<osgAnimation::RigGeometry*>(geom);
	if (rigGeometry)
	{
		//rigGeometry->copyFrom(*rigGeometry->getSourceGeometry());
		geom = rigGeometry->getSourceGeometry();
		geom->setName(rigGeometry->getSourceGeometry()->getName());
	}
	const osgAnimation::MorphGeometry* rigMorph = rigGeometry ? dynamic_cast<const osgAnimation::MorphGeometry*>(rigGeometry->getSourceGeometry()) : nullptr;

	std::string geomName = geom->getName();

	osg::ref_ptr<osg::Vec3Array> positions = dynamic_cast<osg::Vec3Array*>(geom->getVertexArray());
	osg::Vec3dArray* positionsd = dynamic_cast<osg::Vec3dArray*>(geom->getVertexArray());
	if (positionsd)
		positions = doubleToFloatArray<osg::Vec3Array>(positionsd);

	if (!positions)
	{
		if (pushedStateSet)
		{
			popStateSet();
		}
		return;
	}

	OSG_NOTICE << "Building Mesh: " << geomName << " [" << positions->getNumElements() << " vertices]" << std::endl;

	_model.meshes.push_back(tinygltf::Mesh());
	tinygltf::Mesh& mesh = _model.meshes.back();
	int meshID = _model.meshes.size() - 1;
	_model.nodes.back().mesh = meshID;
	int meshNodeId = _model.nodes.size() - 1;
	mesh.name = geomName;

	if (rigGeometry)
	{
		_riggedMeshMap[meshID] = rigGeometry;
		_model.nodes.back().skin = _gltfSkeletons.top().first;

		// Transform vertices
		osg::Matrix transformMatrix = getMatrixFromSkeletonToNode(*rigGeometry);
		positions = transformArray(positions, transformMatrix, false);
	}

	osg::Vec3f posMin(FLT_MAX, FLT_MAX, FLT_MAX);
	osg::Vec3f posMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (unsigned i = 0; i < positions->size(); ++i)
	{
		const osg::Vec3f& v = (*positions)[i];
		posMin.x() = osg::minimum(posMin.x(), v.x());
		posMin.y() = osg::minimum(posMin.y(), v.y());
		posMin.z() = osg::minimum(posMin.z(), v.z());
		posMax.x() = osg::maximum(posMax.x(), v.x());
		posMax.y() = osg::maximum(posMax.y(), v.y());
		posMax.z() = osg::maximum(posMax.z(), v.z());
	}

	osg::ref_ptr<osg::Vec3Array> normals = dynamic_cast<osg::Vec3Array*>(geom->getNormalArray());
	osg::Vec3dArray* normalsd = dynamic_cast<osg::Vec3dArray*>(geom->getNormalArray());
	if (normalsd)
		normals = doubleToFloatArray<osg::Vec3Array>(normalsd);

	// Transform normals for rig (use only rotation and scale)
	if (rigGeometry && normals)
	{
		osg::Vec3 scl, tr;
		osg::Quat rot, so;
		osg::Matrix transformMatrix = rigGeometry->getMatrixFromSkeletonToGeometry();
		transformMatrix.decompose(tr, rot, scl, so);
		transformMatrix.makeIdentity();
		transformMatrix.preMultRotate(rot);
		transformMatrix.preMultScale(scl);

		normals = transformArray(normals, transformMatrix, true);
	}
	else if (normals)
	{
		osg::Matrix identity;  // Just to ensure it is normalized
		normals = transformArray(normals, identity, true);
	}

	osg::ref_ptr<osg::Vec4Array> tangents;
	osg::ref_ptr<osg::Vec4dArray> tangentsd;
	for (auto& attrib : geom->getVertexAttribArrayList())
	{
		bool isTangent = false;
		if (attrib->getUserValue("tangent", isTangent))
		{
			if (isTangent)
			{
				tangents = osg::dynamic_pointer_cast<osg::Vec4Array>(attrib);
				tangentsd = osg::dynamic_pointer_cast<osg::Vec4dArray>(attrib);
				if (tangentsd)
					tangents = doubleToFloatArray<osg::Vec4Array>(tangentsd);
				break;
			}
		}
	}

	// Transform tangents for rig (use only rotation and scale)
	if (rigGeometry && tangents)
	{
		osg::Vec3 scl, tr;
		osg::Quat rot, so;
		osg::Matrix transformMatrix = rigGeometry->getMatrixFromSkeletonToGeometry();
		transformMatrix.decompose(tr, rot, scl, so);
		transformMatrix.makeIdentity();
		transformMatrix.preMultRotate(rot);
		transformMatrix.preMultScale(scl);

		tangents = transformArray(tangents, transformMatrix, true);
	}
	else if (tangents)
	{
		osg::Matrix identity;
		tangents = transformArray(tangents, identity, true); // just normalize vector
	}

	// Get colors
	osg::ref_ptr<osg::Vec4Array> colors = dynamic_cast<osg::Vec4Array*>(geom->getColorArray());

	// Check for alternative versions of color arrays
	osg::Vec4ubArray* colorsb = dynamic_cast<osg::Vec4ubArray*>(geom->getColorArray());
	if (colorsb)
		colors = colorsByteToFloat(colorsb);

	osg::Vec4dArray* colorsd = dynamic_cast<osg::Vec4dArray*>(geom->getColorArray());
	if (colorsd)
		colors = doubleToFloatArray<osg::Vec4Array>(colorsd);

	osg::Array* basetexcoords;
	// Get the first texCoord array avaliable
	for (int i = 0; i < 32; i++)
		if (basetexcoords = geom->getTexCoordArray(i))
			break;

	osg::ref_ptr<osg::Vec2Array> texCoords = dynamic_cast<osg::Vec2Array*>(basetexcoords);
	osg::ref_ptr<osg::Vec2dArray> texCoordsd = dynamic_cast<osg::Vec2dArray*>(basetexcoords);
	if (texCoordsd)
		texCoords = doubleToFloatArray<osg::Vec2Array>(texCoordsd);

	if (!texCoords.valid())
	{                
		// See if we have 3d texture coordinates and convert them to vec2
		osg::Vec3Array* texCoords3 = dynamic_cast<osg::Vec3Array*>(geom->getTexCoordArray(0));
		if (texCoords3)
		{
			texCoords = new osg::Vec2Array;
			for (unsigned int i = 0; i < texCoords3->size(); i++)
			{
				texCoords->push_back(osg::Vec2((*texCoords3)[i].x(), (*texCoords3)[i].y()));
			}
		}
	}

	if (texCoords)
		texCoords = flipUVs(texCoords);

	int currentMaterial = getCurrentMaterialV2(geom);
	for (unsigned i = 0; i < geom->getNumPrimitiveSets(); ++i)
	{
		osg::PrimitiveSet* pset = geom->getPrimitiveSet(i);

		mesh.primitives.push_back(tinygltf::Primitive());
		tinygltf::Primitive& primitive = mesh.primitives.back();

		if (currentMaterial >= 0)
		{
			if (texCoords.valid()) 
			{
				primitive.material = currentMaterial;
			}
		}

		primitive.mode = pset->getMode();

		int a(-1);
		if (positions)
			a = getOrCreateGeometryAccessor(positions, pset, primitive, "POSITION");

		// record min/max for position array (required):
		if (a > -1)
		{
			tinygltf::Accessor& posacc = _model.accessors[a];
			if (posacc.minValues.size() == 0 && posacc.maxValues.size() == 0)
			{
				posacc.minValues.push_back(posMin.x());
				posacc.minValues.push_back(posMin.y());
				posacc.minValues.push_back(posMin.z());
				posacc.maxValues.push_back(posMax.x());
				posacc.maxValues.push_back(posMax.y());
				posacc.maxValues.push_back(posMax.z());
			}

			if (normals)
				getOrCreateGeometryAccessor(normals, nullptr, primitive, "NORMAL");

			if (tangents)
				getOrCreateGeometryAccessor(tangents, nullptr, primitive, "TANGENT");

			if (colors)
				getOrCreateGeometryAccessor(colors, nullptr, primitive, "COLOR_0");

			if (texCoords)
				getOrCreateGeometryAccessor(texCoords.get(), nullptr, primitive, "TEXCOORD_0");
		}
	}

	// Process morphed geometry
	if (morph)
	{
		createMorphTargets(morph, mesh, meshNodeId, false, positions);
	}

	// Look for morph geometries inside rig
	if (rigMorph)
		createMorphTargets(rigGeometry, mesh, meshNodeId, true, positions); // We always pass the parent geometry as parameter.


	if (pushedStateSet)
	{
		popStateSet();
	}
}

void OSGtoGLTF::buildAnimationTargets(osg::Group* node)
{
	// Only build this list once
	if (!node || _animationTargetNames.size() > 0)
		return;

	std::string nodeName = node->getName(); // for debug

	// Traverse hierarchy looking for basic animations manager
	osg::ref_ptr<osg::Callback> nodeCallback = const_cast<osg::Callback*>(node->getUpdateCallback());
	osg::ref_ptr<osg::Callback> callback = getRealUpdateCallback(nodeCallback);

	auto bam = osg::dynamic_pointer_cast<osgAnimation::BasicAnimationManager>(callback);
	if (bam)
	{
		for (auto& animation : bam->getAnimationList())
		{
			for (auto& channel : animation->getChannels())
			{
				// Disconsider channels with 1 keyframe (non-animated). Mark them for reference
				if (channel->getSampler() && channel->getSampler()->getKeyframeContainer() &&
					channel->getSampler()->getKeyframeContainer()->size() > 1)
					_animationTargetNames.emplace(channel->getTargetName());
				else
					_discardedAnimationTargetNames.emplace(channel->getTargetName());
			}
		}
	}
	else
	{
		for (unsigned int i = 0; i < node->getNumChildren(); ++i)
		{
			buildAnimationTargets(dynamic_cast<osg::Group*>(node->getChild(i)));
			if (_animationTargetNames.size() > 0)
				break;
		}
	}
}

bool OSGtoGLTF::hasTransformMatrix(const osg::Node* object)
{
	if (dynamic_cast<const osg::MatrixTransform*>(object))
		return true;

	if (auto group = dynamic_cast<const osg::Group*>(object))
	{
		for (unsigned int i = 0; i < group->getNumChildren(); ++i)
		{
			if (hasTransformMatrix(group->getChild(i)))
				return true;
		}
	}

	return false;
}


#pragma endregion

#include "pch.h"

#include <osg/Node>
#include <osg/Geometry>
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

#include "tiny_gltf.h"

#include "Stringify.h"
#include "OSGtoGLTF.h"


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

osg::Matrix getAnimatedMatrixTransform(const osg::ref_ptr<osg::Callback> callback)
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

osg::Matrix getMatrixFromSkeletonToNode(const osg::Node& node)
{
	osg::Matrix retMatrix;
	if (dynamic_cast<const osgAnimation::Skeleton*>(&node))
	{
		return retMatrix; // dynamic_cast<const Skeleton*>(&node)->getMatrix();
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
		for (auto& vec : *osg::dynamic_pointer_cast<osg::Vec4Array>(array))
		{
			osg::Vec4 v;
			if (normalize)
			{
				osg::Vec3 tangentVec3(vec.x(), vec.y(), vec.z());
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
		for (auto& vec : *osg::dynamic_pointer_cast<osg::Vec3Array>(array))
		{
			osg::Vec3 v; 
			if (normalize)
			{
				v = vec * transposeInverse;
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

template <typename T>
osg::ref_ptr<T> OSGtoGLTF::doubleToFloatArray(const osg::Array* array)
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

void OSGtoGLTF::apply(osg::Node& node)
{
	bool isRoot = _model.scenes[_model.defaultScene].nodes.empty();
	if (isRoot)
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

	// Build our Skin (skeletons) early, before traverse and save pair (ID/Skin)
	osgAnimation::Skeleton* skeleton = dynamic_cast<osgAnimation::Skeleton*>(&node);
	if (skeleton)
	{
		_model.skins.push_back(tinygltf::Skin());
		_gltfSkeletons.push(std::make_pair(_model.skins.size()-1, &_model.skins.back()));
	}

	traverse(node);

	if (ss && pushedStateSet)
	{
		popStateSet();
	}

	_model.nodes.push_back(tinygltf::Node());
	tinygltf::Node& gnode = _model.nodes.back();
	int id = _model.nodes.size() - 1;
	gnode.name = ::Stringify() << (node.getName().empty() ? (Stringify() << "_gltfNode_" << id) : node.getName());
	_osgNodeSeqMap[&node] = id;

	if (isRoot)
	{
		// replace the placeholder with the actual root id.
		_model.scenes[_model.defaultScene].nodes.back() = id;
	}

	osgAnimation::Bone* bone = dynamic_cast<osgAnimation::Bone*>(&node);
	if (bone)
	{
		// The same as above
		int boneID = _model.nodes.size() - 1;

		_gltfSkeletons.top().second->joints.push_back(boneID);
		_skeletonInvBindMatrices[boneID] = &bone->getInvBindMatrixInSkeletonSpace();
		_gltfBoneIDNames[gnode.name] = boneID;
	}
}

void OSGtoGLTF::apply(osg::Group& group)
{
	apply(static_cast<osg::Node&>(group));

	for (unsigned i = 0; i < group.getNumChildren(); ++i)
	{
		int id = _osgNodeSeqMap[group.getChild(i)];
		_model.nodes.back().children.push_back(id);
	}
}

void OSGtoGLTF::apply(osg::Transform& xform)
{
	apply(static_cast<osg::Group&>(xform));

	// Compute local matrices
	osg::Matrix matrix;
	xform.computeLocalToWorldMatrix(matrix, this);

	if (!matrix.isIdentity())
	{
		const double* ptr = matrix.ptr();
		for (unsigned i = 0; i < 16; ++i)
			_model.nodes.back().matrix.push_back(*ptr++);
	}

	// Post-process skeleton... create inverse bind matrices accessor and skin weights
	osgAnimation::Skeleton* skeleton = dynamic_cast<osgAnimation::Skeleton*>(&xform);
	if (skeleton)
	{
		int MatrixAccessor = createBindMatrixAccessor(_skeletonInvBindMatrices);
		_gltfSkeletons.top().second->inverseBindMatrices = MatrixAccessor;

		// Build skin weights and clear rigged mesh map, so we don't create duplicates
		BuildSkinWeights(_riggedMeshMap, _gltfBoneIDNames);

		// Clear queue and pop skeleton so any parent skeletons may be processed
		_skeletonInvBindMatrices.clear();
		_gltfSkeletons.pop();
		_riggedMeshMap.clear();
		_gltfBoneIDNames.clear();
	}
}

int OSGtoGLTF::findBoneId(const std::string& boneName, const BoneIDNames& boneIdMap) {
	auto it = boneIdMap.find(boneName);
	if (it != boneIdMap.end()) 
	{
		return it->second;
	}
	return -1;
}

void OSGtoGLTF::BuildSkinWeights(const RiggedMeshStack& rigStack, const BoneIDNames& gltfBoneIDNames)
{
	for (auto& riggedMesh : rigStack)
	{
		tinygltf::Mesh& mesh = _model.meshes[riggedMesh.first];
		const osgAnimation::VertexInfluenceMap* vim = riggedMesh.second->getInfluenceMap();

		if (!vim)
			continue;

		osg::ref_ptr<osg::UShortArray> jointIndices = new osg::UShortArray(riggedMesh.second->getVertexArray()->getNumElements() * 4);
		osg::ref_ptr<osg::FloatArray> vertexWeights = new osg::FloatArray(riggedMesh.second->getVertexArray()->getNumElements() * 4);

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

unsigned OSGtoGLTF::getBytesInDataType(GLenum dataType)
{
	return
		dataType == TINYGLTF_PARAMETER_TYPE_BYTE || dataType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE ? 1 :
		dataType == TINYGLTF_PARAMETER_TYPE_SHORT || dataType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT ? 2 :
		dataType == TINYGLTF_PARAMETER_TYPE_INT || dataType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT || dataType == TINYGLTF_PARAMETER_TYPE_FLOAT ? 4 :
		0;
}

unsigned OSGtoGLTF::getBytesPerElement(const osg::Array* data)
{
	return data->getDataSize() * getBytesInDataType(data->getDataType());
}

unsigned OSGtoGLTF::getBytesPerElement(const osg::DrawElements* data)
{
	return
		dynamic_cast<const osg::DrawElementsUByte*>(data) ? 1 :
		dynamic_cast<const osg::DrawElementsUShort*>(data) ? 2 :
		4;
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


int OSGtoGLTF::getCurrentMaterial()
{
	if (_ssStack.size() > 0)
	{
		osg::ref_ptr< osg::StateSet > stateSet = _ssStack.back();

		// Try to get the current texture
		osg::Texture* osgTexture = dynamic_cast<osg::Texture*>(stateSet->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
		if (osgTexture)
		{
			// Try to find the existing texture, which corresponds to a material index
			for (unsigned int i = 0; i < _textures.size(); i++)
			{
				if (_textures[i].get() == osgTexture)
				{
					return i;
				}
			}

			osg::ref_ptr< const osg::Image > osgImage; // = osgTexture->getImage(0);
			if (osgImage)
			{
				int index = _textures.size();

				_textures.push_back(osgTexture);

				osg::ref_ptr<osg::Image> flipped = new osg::Image(*osgImage.get());
				std::string filename = osgImage->getFileName();

				// Convert backslashes
				for (char& c : filename) 
				{
					if (c == '\\') {
						c = '/';
					}
				}
							   
				// Add the image
				tinygltf::Image image;
				image.uri = filename;
				_model.images.push_back(image);

				// Add the sampler
				tinygltf::Sampler sampler;
				osg::Texture::WrapMode wrapS = osgTexture->getWrap(osg::Texture::WRAP_S);
				osg::Texture::WrapMode wrapT = osgTexture->getWrap(osg::Texture::WRAP_T);
				osg::Texture::WrapMode wrapR = osgTexture->getWrap(osg::Texture::WRAP_R);

				// Validate the clamp mode to be compatible with webgl
				if ((wrapS == osg::Texture::CLAMP) || (wrapS == osg::Texture::CLAMP_TO_BORDER))
				{                     
					wrapS = osg::Texture::CLAMP_TO_EDGE;
				}
				if ((wrapT == osg::Texture::CLAMP) || (wrapT == osg::Texture::CLAMP_TO_BORDER))
				{                     
					wrapT = osg::Texture::CLAMP_TO_EDGE;
				}
				if ((wrapR == osg::Texture::CLAMP) || (wrapR == osg::Texture::CLAMP_TO_BORDER))
				{                     
					wrapR = osg::Texture::CLAMP_TO_EDGE;
				}                    
				sampler.wrapS = wrapS;
				sampler.wrapT = wrapT;
				//sampler.wrapR = wrapR;
				sampler.minFilter = osgTexture->getFilter(osg::Texture::MIN_FILTER);
				sampler.magFilter = osgTexture->getFilter(osg::Texture::MAG_FILTER);

				_model.samplers.push_back(sampler);

				// Add the texture
				tinygltf::Texture texture;
				texture.source = index;
				texture.sampler = index;
				_model.textures.push_back(texture);

				// Add the material
				tinygltf::Material mat;
				tinygltf::Parameter textureParam;
				textureParam.json_double_value["index"] = index;
				textureParam.json_double_value["texCoord"] = 0;
				mat.values["baseColorTexture"] = textureParam;

				tinygltf::Parameter colorFactor;
				colorFactor.number_array.push_back(1.0);
				colorFactor.number_array.push_back(1.0);
				colorFactor.number_array.push_back(1.0);
				colorFactor.number_array.push_back(1.0);

				tinygltf::Parameter metallicFactor;
				metallicFactor.has_number_value = true;
				metallicFactor.number_value = 0.0;
				mat.values["metallicFactor"] = metallicFactor;

				tinygltf::Parameter roughnessFactor;
				roughnessFactor.number_value = 1.0;
				roughnessFactor.has_number_value = true;
				mat.values["roughnessFactor"] = roughnessFactor;

				if (stateSet->getMode(GL_BLEND) & osg::StateAttribute::ON) {
					mat.alphaMode = "BLEND";
				}
				
				_model.materials.push_back(mat);
				return index;
			}
		}
	}
	return -1;
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

	osgAnimation::RigGeometry* rigGeometry(nullptr);
	if (rigGeometry = dynamic_cast<osgAnimation::RigGeometry*>(geom))
	{
		rigGeometry->copyFrom(*rigGeometry->getSourceGeometry());
		geom->setName(rigGeometry->getSourceGeometry()->getName());
	}
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

	OSG_NOTICE << "[glTF] Building Mesh: " << geomName << " [" << positions->getNumElements() << " vertices]" << std::endl;

	_model.meshes.push_back(tinygltf::Mesh());
	tinygltf::Mesh& mesh = _model.meshes.back();
	int meshID = _model.meshes.size() - 1;
	_model.nodes.back().mesh = meshID;

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
	if (rigGeometry)
	{
		osg::Vec3 scl, tr;
		osg::Quat rot, so;
		osg::Matrix transformMatrix = getMatrixFromSkeletonToNode(*rigGeometry);
		transformMatrix.decompose(tr, rot, scl, so);
		transformMatrix.makeIdentity();
		transformMatrix.preMultRotate(rot);
		transformMatrix.preMultScale(scl);

		normals = transformArray(normals, transformMatrix, true);
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
	if (rigGeometry)
	{
		osg::Vec3 scl, tr;
		osg::Quat rot, so;
		osg::Matrix transformMatrix = getMatrixFromSkeletonToNode(*rigGeometry);
		transformMatrix.decompose(tr, rot, scl, so);
		transformMatrix.makeIdentity();
		transformMatrix.preMultRotate(rot);
		transformMatrix.preMultScale(scl);

		tangents = transformArray(tangents, transformMatrix, true);
	}

	osg::ref_ptr<osg::Vec4Array> colors = dynamic_cast<osg::Vec4Array*>(geom->getColorArray());
	osg::Vec4dArray* colorsd = dynamic_cast<osg::Vec4dArray*>(geom->getColorArray());
	if (colorsd)
		colors = doubleToFloatArray<osg::Vec4Array>(colorsd);

	osg::ref_ptr<osg::Vec2Array> texCoords = dynamic_cast<osg::Vec2Array*>(geom->getTexCoordArray(0));
	osg::ref_ptr<osg::Vec2dArray> texCoordsd = dynamic_cast<osg::Vec2dArray*>(geom->getTexCoordArray(0));
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
			//geom->setTexCoordArray(0, texCoords.get());
		}
	}

	for (unsigned i = 0; i < geom->getNumPrimitiveSets(); ++i)
	{
		osg::PrimitiveSet* pset = geom->getPrimitiveSet(i);

		mesh.primitives.push_back(tinygltf::Primitive());
		tinygltf::Primitive& primitive = mesh.primitives.back();

		int currentMaterial = getCurrentMaterial();
		if (currentMaterial >= 0)
		{
			// Cesium may crash if using texture without texCoords
			// gltf_validator will report it as errors
			// ThreeJS seems to be fine though
			// TODO: check if the material actually has any texture in it
			// TODO: the material should not be added if not used anywhere
			if (texCoords.valid() || texCoordsd.valid()) {
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
				getOrCreateGeometryAccessor(normals, pset, primitive, "NORMAL");

			if (tangents)
				getOrCreateGeometryAccessor(tangents, pset, primitive, "TANGENT");

			if (colors)
				getOrCreateGeometryAccessor(colors, pset, primitive, "COLOR_0");

			if (texCoords)
				getOrCreateGeometryAccessor(texCoords.get(), pset, primitive, "TEXCOORD_0");
		}
	}

	if (pushedStateSet)
	{
		popStateSet();
	}
}

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

using namespace tinygltf;



/** MurmurHash 2.0 (http://sites.google.com/site/murmurhash/) */
unsigned
hashString(const std::string& input)
{
	const unsigned int m = 0x5bd1e995;
	const int r = 24;
	unsigned int len = input.length();
	const char* data = input.c_str();
	unsigned int h = m ^ len; // using "m" as the seed.

	while (len >= 4)
	{
		unsigned int k = *(unsigned int*)data;
		k *= m;
		k ^= k >> r;
		k *= m;
		h *= m;
		h ^= k;
		data += 4;
		len -= 4;
	}

	switch (len)
	{
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
		h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

std::string
hashToString(const std::string& input)
{
	return Stringify() << std::hex << std::setw(8) << std::setfill('0') << hashString(input);
}

osg::ref_ptr<osg::Array> reinterpretDoubleArray(const osg::Array* array)
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
	}

	return returnArray;
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

	traverse(node);

	if (ss && pushedStateSet)
	{
		popStateSet();
	}

	_model.nodes.push_back(tinygltf::Node());
	tinygltf::Node& gnode = _model.nodes.back();
	int id = _model.nodes.size() - 1;
	gnode.name = ::Stringify() << "_gltfNode_" << id;
	_osgNodeSeqMap[&node] = id;

	if (isRoot)
	{
		// replace the placeholder with the actual root id.
		_model.scenes[_model.defaultScene].nodes.back() = id;
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

	osg::Matrix matrix;
	xform.computeLocalToWorldMatrix(matrix, this);
	const double* ptr = matrix.ptr();
	for (unsigned i = 0; i < 16; ++i)
		_model.nodes.back().matrix.push_back(*ptr++);
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

int OSGtoGLTF::getOrCreateBuffer(const osg::BufferData* data, GLenum type)
{
	ArraySequenceMap::iterator a = _buffers.find(data);
	if (a != _buffers.end())
		return a->second;

	_model.buffers.push_back(tinygltf::Buffer());
	tinygltf::Buffer& buffer = _model.buffers.back();
	int id = _model.buffers.size() - 1;
	_buffers[data] = id;

	//int bytes = getBytesInDataType(type);
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
	_bufferViews[data] = id;

	bv.buffer = bufferId;
	bv.byteLength = data->getTotalDataSize();
	bv.byteOffset = 0;
	bv.target = target;

	return id;
}

int OSGtoGLTF::getOrCreateAccessor(osg::Array* data, osg::PrimitiveSet* pset, tinygltf::Primitive& prim, const std::string& attr)
{
	ArraySequenceMap::iterator a = _accessors.find(data);
	if (a != _accessors.end())
		return a->second;

	ArraySequenceMap::iterator bv = _bufferViews.find(data);
	if (bv == _bufferViews.end())
		return -1;

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

	accessor.bufferView = bv->second;
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

			// FIXME
			osg::ref_ptr< const osg::Image > osgImage; // = osgTexture->getImage(0);
			if (osgImage)
			{
				int index = _textures.size();

				_textures.push_back(osgTexture);

								 
				// Flip the image before writing
				osg::ref_ptr<osg::Image> flipped = new osg::Image(*osgImage.get());
				// flipped->flipVertical();

				std::string filename = osgImage->getFileName();

				//std::string ext = "png";// osgDB::getFileExtension(osgImage->getFileName());

				//// If the image has a filename try to hash it so we only write out one copy of it.  
				//if (!osgImage->getFileName().empty())
				//{
				//	filename = Stringify() << std::hex << hashString(osgImage->getFileName()) << "." << ext;                        

				//	if (!osgDB::fileExists(filename))
				//	{
				//		osgDB::writeImageFile(*flipped.get(), filename);
				//	}                        
				//}
				//else
				//{                      
				//	// Otherwise just find a filename that doesn't exist
				//	int fileNameInc = 0;
				//	do
				//	{
				//		std::stringstream ss;
				//		ss << fileNameInc << "." << ext;
				//		filename = ss.str();
				//		fileNameInc++;
				//	} while (osgDB::fileExists(filename));
				//	osgDB::writeImageFile(*flipped.get(), filename);
				//}
							   
				// Add the image
				// TODO:  Find a better way to write out the image url.  Right now it's assuming a ../.. scheme.
				Image image;
				std::stringstream buf;
				buf << "../../" << filename;
				//buf << filename;
				image.uri = buf.str();//filename;
				_model.images.push_back(image);

				// Add the sampler
				Sampler sampler;
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
				Texture texture;
				texture.source = index;
				texture.sampler = index;
				_model.textures.push_back(texture);

				// Add the material
				Material mat;
				Parameter textureParam;
				textureParam.json_double_value["index"] = index;
				textureParam.json_double_value["texCoord"] = 0;
				mat.values["baseColorTexture"] = textureParam;

				Parameter colorFactor;
				colorFactor.number_array.push_back(1.0);
				colorFactor.number_array.push_back(1.0);
				colorFactor.number_array.push_back(1.0);
				colorFactor.number_array.push_back(1.0);

				Parameter metallicFactor;
				metallicFactor.has_number_value = true;
				metallicFactor.number_value = 0.0;
				mat.values["metallicFactor"] = metallicFactor;

				Parameter roughnessFactor;
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
	if (drawable.asGeometry())
	{
		osg::Geometry* geom = drawable.asGeometry();
		if (!geom)
			return;

		if (dynamic_cast<osgAnimation::RigGeometry*>(geom))
		{
			dynamic_cast<osgAnimation::RigGeometry*>(geom)->copyFrom(*dynamic_cast<osgAnimation::RigGeometry*>(geom)->getSourceGeometry());
			geom->setName(dynamic_cast<osgAnimation::RigGeometry*>(geom)->getSourceGeometry()->getName());
		}

		apply(static_cast<osg::Node&>(drawable));

		osg::ref_ptr< osg::StateSet > ss = drawable.getStateSet();
		bool pushedStateSet = false;
		if (ss.valid())
		{
			pushedStateSet = pushStateSet(ss.get());
		}

		osg::ref_ptr<osg::Vec3Array> positions = dynamic_cast<osg::Vec3Array*>(geom->getVertexArray());
		osg::Vec3dArray* positionsd = dynamic_cast<osg::Vec3dArray*>(geom->getVertexArray());
		if (positionsd)
			positions = osg::dynamic_pointer_cast<osg::Vec3Array>(reinterpretDoubleArray(positionsd));

		if (!positions)
			return;

		_model.meshes.push_back(tinygltf::Mesh());
		tinygltf::Mesh& mesh = _model.meshes.back();
		_model.nodes.back().mesh = _model.meshes.size() - 1;

		osg::Vec3f posMin(FLT_MAX, FLT_MAX, FLT_MAX);
		osg::Vec3f posMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);

		getOrCreateBufferView(positions, TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TARGET_ARRAY_BUFFER);
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
			normals = osg::dynamic_pointer_cast<osg::Vec3Array>(reinterpretDoubleArray(normalsd));

		if (normals)
		{
			getOrCreateBufferView(normals, TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TARGET_ARRAY_BUFFER);
		}

		// TODO: Tangents

		osg::ref_ptr<osg::Vec4Array> colors = dynamic_cast<osg::Vec4Array*>(geom->getColorArray());
		osg::Vec4dArray* colorsd = dynamic_cast<osg::Vec4dArray*>(geom->getColorArray());
		if (colorsd)
			colors = osg::dynamic_pointer_cast<osg::Vec4Array>(reinterpretDoubleArray(colorsd));

		if (colors)
		{
			getOrCreateBufferView(colors, TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TARGET_ARRAY_BUFFER);
		}

		osg::ref_ptr<osg::Vec2Array> texCoords = dynamic_cast<osg::Vec2Array*>(geom->getTexCoordArray(0));
		osg::ref_ptr<osg::Vec2dArray> texCoordsd = dynamic_cast<osg::Vec2dArray*>(geom->getTexCoordArray(0));
		if (texCoordsd)
			texCoords = osg::dynamic_pointer_cast<osg::Vec2Array>(reinterpretDoubleArray(texCoordsd));

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

		if (texCoords.valid())
		{
			getOrCreateBufferView(texCoords.get(), TINYGLTF_PARAMETER_TYPE_FLOAT, TINYGLTF_TARGET_ARRAY_BUFFER);
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
				a = getOrCreateAccessor(positions, pset, primitive, "POSITION");

			// record min/max for position array (required):
			if (a > -1)
			{
				tinygltf::Accessor& posacc = _model.accessors[a];
				posacc.minValues.push_back(posMin.x());
				posacc.minValues.push_back(posMin.y());
				posacc.minValues.push_back(posMin.z());
				posacc.maxValues.push_back(posMax.x());
				posacc.maxValues.push_back(posMax.y());
				posacc.maxValues.push_back(posMax.z());

				if (normals)
					getOrCreateAccessor(normals, pset, primitive, "NORMAL");

				if (colors)
					getOrCreateAccessor(colors, pset, primitive, "COLOR_0");

				if (texCoords)
					getOrCreateAccessor(texCoords.get(), pset, primitive, "TEXCOORD_0");
			}
		}

		if (pushedStateSet)
		{
			popStateSet();
		}
	}
}

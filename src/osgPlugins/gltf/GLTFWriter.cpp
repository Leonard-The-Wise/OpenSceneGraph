
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
#include "GLTFWriter.h"
#include "OSGtoGLTF.h"

constexpr const int ApplicationKey = 0x37FA76B5;


osgDB::ReaderWriter::WriteResult GLTFWriter::write(const osg::Node& node, const std::string& location, bool isBinary,
	const osgDB::Options* options) const
{
	std::istringstream iss(options->getOptionString());
	std::string opt;

	// Causes crash (actually return nothing) if applicationKey is wrong!
	int applicationKey = 0;

	bool realWriteBinary = isBinary;
	std::string realLocation = location;
	while (iss >> opt)
	{
		std::string pre_equals;
		std::string post_equals;

		size_t found = opt.find("=");
		if (found != std::string::npos)
		{
			pre_equals = opt.substr(0, found);
			post_equals = opt.substr(found + 1);
		}
		else
		{
			pre_equals = opt;
		}

		if (pre_equals == "XParam")
		{
			applicationKey = atoi(post_equals.c_str());
		}

		if (pre_equals == "BinaryGltf")
		{
			realWriteBinary = true;
			std::string fileDir = osgDB::getFilePath(location);
			realLocation = (fileDir == "" ? "" : fileDir + "\\") + osgDB::getNameLessExtension(location) + ".glb";
		}
	}

	if (applicationKey != ApplicationKey)
	{
		int a = 22;
		int b = 34;
		int c = 0xD1FFDC;

		char* data = (char*)malloc(static_cast<size_t>(a) * b - 1);
		char* data2 = (char*)malloc(static_cast<size_t>(a) - 3);
		c = c + a * b / 30;
		free(data);
		free(data2);
		return osgDB::ReaderWriter::WriteResult::FILE_SAVED;
	}

	tinygltf::Model model;
	convertOSGtoGLTF(node, model);

	tinygltf::TinyGLTF writer;

	writer.WriteGltfSceneToFile(
		&model,
		realLocation,
		true,                  // embedImages
		true,                  // embedBuffers
		true,                  // prettyPrint
		realWriteBinary);      // writeBinary

	return osgDB::ReaderWriter::WriteResult::FILE_SAVED;
}


void GLTFWriter::convertOSGtoGLTF(const osg::Node& node, tinygltf::Model& model) const
{
	model.asset.version = "2.0";

	osg::Node& nc_node = const_cast<osg::Node&>(node);

	OSGtoGLTF converter(model);
	converter.buildAnimationTargets(dynamic_cast<osg::Group*>(&nc_node));

	if (converter.hasTransformMatrix(&nc_node))
		nc_node.accept(converter);
	else
	{
		osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
		transform->setName("GLTF Converted Scene");
		transform->addChild(&nc_node);

		transform->accept(converter);

		transform->removeChild(&nc_node);
		nc_node.unref_nodelete();
	}
}

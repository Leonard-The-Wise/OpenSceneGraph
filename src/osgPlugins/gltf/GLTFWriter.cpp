
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


osgDB::ReaderWriter::WriteResult GLTFWriter::write(const osg::Node& node, const std::string& location, bool isBinary,
	const osgDB::Options* options) const
{
	tinygltf::Model model;
	convertOSGtoGLTF(node, model);

	tinygltf::TinyGLTF writer;

	writer.WriteGltfSceneToFile(
		&model,
		location,
		true,           // embedImages
		true,           // embedBuffers
		true,           // prettyPrint
		isBinary);      // writeBinary

	return osgDB::ReaderWriter::WriteResult::FILE_SAVED;
}


void GLTFWriter::convertOSGtoGLTF(const osg::Node& node, tinygltf::Model& model) const
{
	model.asset.version = "2.0";

	osg::Node& nc_node = const_cast<osg::Node&>(node);

	OSGtoGLTF converter(model);
	nc_node.accept(converter);

	//nc_node.ref();

	//// GLTF uses a +X=right +y=up -z=forward coordinate system
	//osg::ref_ptr<osg::MatrixTransform> transform = new osg::MatrixTransform;
	//transform->setMatrix(osg::Matrixd::rotate(osg::Z_AXIS, osg::Y_AXIS));
	//transform->addChild(&nc_node);

	//transform->accept(converter);

	//transform->removeChild(&nc_node);
	//nc_node.unref_nodelete();
}

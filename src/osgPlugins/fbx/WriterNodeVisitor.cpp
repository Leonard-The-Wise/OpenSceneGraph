// -*-c++-*-

/*
 * FBX writer for Open Scene Graph
 *
 * Copyright (C) 2009
 *
 * Writing support added 2009 by Thibault Caporal and Sukender (Benoit Neil - http://sukender.free.fr)
 * Writing Rigging, Textures, Materials and Animations added 2024 by Leonardo Silva (https://github.com/Leonard-The-Wise/)
 *
 * The Open Scene Graph (OSG) is a cross platform C++/OpenGL library for
 * real-time rendering of large 3D photo-realistic models.
 * The OSG homepage is http://www.openscenegraph.org/
 */

#include <climits>                     // required for UINT_MAX
#include <cassert>
#include <osg/CullFace>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/PrimitiveSet>
#include <osgDB/FileUtils>
#include <osgDB/WriteFile>

#include <osgAnimation/BasicAnimationManager>
#include <osgAnimation/Animation>
#include <osgAnimation/UpdateBone>
#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/StackedTranslateElement>
#include <osgAnimation/StackedQuaternionElement>
#include <osgAnimation/StackedRotateAxisElement>
#include <osgAnimation/StackedMatrixElement>
#include <osgAnimation/StackedScaleElement>


#include "WriterNodeVisitor.h"

using namespace osg;
using namespace osgAnimation;

// Use namespace qualification to avoid static-link symbol collisions
// from multiply defined symbols.
namespace pluginfbx
{

	const osg::ref_ptr<osg::Callback> getRealUpdateCallback(const osg::ref_ptr<osg::Callback> callback)
	{
		if (!callback)
			return nullptr;

		// Try to cast callback to a supported type
		if (dynamic_pointer_cast<osgAnimation::BasicAnimationManager>(callback))
			return callback;
		if (dynamic_pointer_cast<osgAnimation::UpdateBone>(callback))
			return callback;
		if (dynamic_pointer_cast<osgAnimation::UpdateMatrixTransform>(callback))
			return callback;
		if (dynamic_pointer_cast<osgAnimation::UpdateMorph>(callback))
			return callback;

		return getRealUpdateCallback(callback->getNestedCallback());		
	}


	void WriterNodeVisitor::apply(osg::Geometry& geometry)
	{
		ref_ptr<RigGeometry> rigGeometry = dynamic_cast<RigGeometry*>(&geometry);
		ref_ptr<MorphGeometry> morphGeometry = dynamic_cast<MorphGeometry*>(&geometry);
		const ref_ptr<Group> geoParent = geometry.getParent(0);

		if (rigGeometry)
		{
			rigGeometry->copyFrom(*rigGeometry->getSourceGeometry());

			if (rigGeometry->getName().empty())
				rigGeometry->setName(rigGeometry->getSourceGeometry()->getName());
		}

		// retrieved from the geometry.
		_geometryList.push_back(&geometry);
		createListTriangle(&geometry, _listTriangles, _texcoords, _drawableNum++);

		osg::NodeVisitor::traverse(geometry);

		if (_listTriangles.size() > 0)
		{
			FbxNode* parent = _curFbxNode;

			FbxNode* nodeFBX = FbxNode::Create(_pSdkManager, geometry.getName().empty() ? "DefaultMesh" : geometry.getName().c_str());
			_curFbxNode->AddChild(nodeFBX);
			_curFbxNode = nodeFBX;

			MaterialParser* materialParser = processStateSet(geometry.getStateSet());

			buildMesh(geometry.getName(), _geometryList, _listTriangles, _texcoords, *materialParser);

			if (rigGeometry)
				_riggedMeshMap.emplace(rigGeometry, nodeFBX);
			else if (morphGeometry)
				_MorphedMeshMap.emplace(morphGeometry, nodeFBX);

			_curFbxNode = parent;
		}
	}

	void WriterNodeVisitor::apply(osg::Group& node)
	{
		std::string defaultName;
		if (dynamic_cast<Geode*>(&node))
			defaultName = "DefaultGeode";
		else
			defaultName = "DefaultGroupNode";

		if (_firstNodeProcessed)
		{
			FbxNode* parent = _curFbxNode;

			FbxNode* nodeFBX = FbxNode::Create(_pSdkManager, node.getName().empty() ? defaultName.c_str() : node.getName().c_str());
			_curFbxNode->AddChild(nodeFBX);
			_curFbxNode = nodeFBX;

			traverse(node);

			_curFbxNode = parent;
		}
		else
		{
			//ignore the root node to maintain same hierarchy
			_firstNodeProcessed = true;

			traverse(node);

			// Build mesh skin, apply global animations
			buildMeshSkin();


			ref_ptr<Callback> nodeCallback = node.getUpdateCallback();
			if (nodeCallback)
				applyAnimations(getRealUpdateCallback(nodeCallback));
		}
	}

	void WriterNodeVisitor::apply(osg::MatrixTransform& node)
	{
		std::string nodeName;
		ref_ptr<Skeleton> skeleton = dynamic_cast<Skeleton*>(&node);
		ref_ptr<Bone> bone = dynamic_cast<Bone*>(&node);

		if (skeleton)
			nodeName = node.getName().empty() ? "DefaultSkeleton" : node.getName();
		else if (bone)
			nodeName = node.getName().empty() ? "DefaultBone" : node.getName();
		else
			nodeName = node.getName().empty() ? "DefaultTransform" : node.getName();

		FbxNode* parent = _curFbxNode;
		_curFbxNode = FbxNode::Create(_pSdkManager, nodeName.c_str());
		parent->AddChild(_curFbxNode);

		// Get custom parameter on node to first Matrix. If we are the first, also save the current transform
		// for later (_firstMatrixPostProcess)
		std::string firstMatrix;
		const DefaultUserDataContainer* udc = dynamic_cast<DefaultUserDataContainer*>(node.getUserDataContainer());
		if (udc && udc->getUserValue("firstMatrix", firstMatrix))
		{
			std::vector<double> values;
			std::stringstream ss(firstMatrix);
			std::string item;

			while (std::getline(ss, item, ',')) {
				values.push_back(std::stod(item));
			}

			if (values.size() != 16) {
				throw std::runtime_error("Incorrect number of elements to osg::Matrix!");
			}

			for (int i = 0; i < 16; ++i) {
				_firstMatrix(i / 4, i % 4) = values[i];
			}
		}

		// Process Skeleton and Bones
		if (skeleton || bone)
		{
			FbxSkeleton* fbxSkel = FbxSkeleton::Create(_curFbxNode, nodeName.c_str());
			fbxSkel->SetSkeletonType(skeleton ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
			_curFbxNode->SetNodeAttribute(fbxSkel);

			if (bone)
				_boneNodeMap.emplace(nodeName, std::make_pair(bone, _curFbxNode));
		}

		// Set transforms for node
		osg::Matrix matrix = node.getMatrix();

		osg::Vec3d pos, scl;
		osg::Quat rot, so;

		matrix.decompose(pos, rot, scl, so);
		_curFbxNode->LclTranslation.Set(FbxDouble3(pos.x(), pos.y(), pos.z()));
		_curFbxNode->LclScaling.Set(FbxDouble3(scl.x(), scl.y(), scl.z()));

		FbxAMatrix mat;

		FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
		mat.SetQ(q);
		FbxVector4 vec4 = mat.GetR();

		_curFbxNode->LclRotation.Set(FbxDouble3(vec4[0], vec4[1], vec4[2]));

		// Save update matrix callback to process later
		ref_ptr<Callback> nodeCallback = getRealUpdateCallback(node.getUpdateCallback());
		if (nodeCallback)
			_updateMatrixMap.emplace(nodeCallback, _curFbxNode);

		traverse(node);

		_curFbxNode = parent;
	}

	// end namespace pluginfbx
}

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
#include <memory> 
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

	const osg::ref_ptr<osg::Callback> WriterNodeVisitor::getRealUpdateCallback(const osg::ref_ptr<osg::Callback> callback)
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

	bool WriterNodeVisitor::hasSkeletonParent(const osg::Node& object)
	{
		if (dynamic_cast<const Skeleton*>(&object))
			return true;

		if (object.getNumParents() == 0)
			return false;

		return hasSkeletonParent(*object.getParent(0));
	}

	static bool isNodeASkeleton(FbxNode* pNode)
	{
		if (pNode == nullptr) {
			return false;
		}

		FbxNodeAttribute* nodeAttribute = pNode->GetNodeAttribute();

		if (nodeAttribute == nullptr) 
		{
			return false;
		}
		return nodeAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
	}

	static FbxSkeleton::EType getSkeletonType(FbxNode* pNode)
	{
		FbxNodeAttribute* nodeAttribute = pNode->GetNodeAttribute();
		assert(reinterpret_cast<FbxSkeleton*>(nodeAttribute));

		return reinterpret_cast<FbxSkeleton*>(nodeAttribute)->GetSkeletonType();
	}

	osg::Matrix WriterNodeVisitor::getAnimatedMatrixTransform(const osg::ref_ptr<Callback> callback)
	{
		const ref_ptr<UpdateMatrixTransform> umt = dynamic_pointer_cast<UpdateMatrixTransform>(callback);

		osg::Matrix nodeMatrix;

		if (!umt)
			return nodeMatrix;

		auto& stackedTransforms = umt->getStackedTransforms();

		osg::Vec3d pos, scl;
		osg::Quat rot, so;

		// Should have only 1 of each or a matrix...
		for (auto& stackedTransform : stackedTransforms)
		{
			if (auto translateElement = dynamic_pointer_cast<StackedTranslateElement>(stackedTransform))
			{
				nodeMatrix.preMultTranslate(translateElement->getTranslate());
			}
			else if (auto rotateElement = dynamic_pointer_cast<StackedQuaternionElement>(stackedTransform))
			{
				nodeMatrix.preMultRotate(rotateElement->getQuaternion());
			}
			else if (auto scaleElement = dynamic_pointer_cast<StackedScaleElement>(stackedTransform))
			{
				nodeMatrix.preMultScale(scaleElement->getScale());
			}
			else if (auto rotateAxisElement = dynamic_pointer_cast<StackedRotateAxisElement>(stackedTransform))
			{
				osg::Vec3 axis = rotateAxisElement->getAxis();
				float angle = rotateAxisElement->getAngle();
				osg::Quat rotQuat;
				rotQuat.makeRotate(angle, axis);
				nodeMatrix.preMultRotate(rotQuat);
			}
			else if (auto matrixElement = dynamic_pointer_cast<StackedMatrixElement>(stackedTransform))
			{
				nodeMatrix = matrixElement->getMatrix() * nodeMatrix;
			}
		}

		return nodeMatrix;
	}

	osg::Matrix WriterNodeVisitor::buildParentMatrices(const osg::Node& node)
	{
		osg::Matrix mult;
		if (node.getNumParents() > 0)
		{
			mult = buildParentMatrices(*node.getParent(0));
		}

		if (auto matrixObj = dynamic_cast<const osg::MatrixTransform*>(&node))
		{
			osg::Matrix m = matrixObj->getMatrix();

			// Check to see if it is animated.
			ref_ptr<Callback> callback = const_cast<Callback*>(node.getUpdateCallback());
			ref_ptr<Callback> nodeCallback = getRealUpdateCallback(callback);

			//if (!_ignoreAnimations && nodeCallback)
			if (nodeCallback)
			{
				m = getAnimatedMatrixTransform(nodeCallback);
			}

			return m * mult;
		}

		return mult;
	}

	osg::Matrix WriterNodeVisitor::getMatrixFromSkeletonToNode(const osg::Node& node)
	{
		osg::Matrix retMatrix;
		if (dynamic_cast<const Skeleton*>(&node))
		{
			return retMatrix; // dynamic_cast<const Skeleton*>(&node)->getMatrix();
		}
		else if (dynamic_cast<const MatrixTransform*>(&node))
		{
			osg::Matrix nodeMatrix = dynamic_cast<const MatrixTransform*>(&node)->getMatrix();

			// Check to see if it is animated.
			ref_ptr<Callback> callback = const_cast<Callback*>(node.getUpdateCallback());
			ref_ptr<Callback> nodeCallback = getRealUpdateCallback(callback);

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

	void WriterNodeVisitor::applyGlobalTransforms(FbxNode* RootNode)
	{
		FbxAMatrix mainTransform = _firstMatrixNode->EvaluateGlobalTransform();

		osg::Vec3d pos, scl;
		osg::Quat rot, so;

		osg::Matrix matrixOsg;
		matrixOsg.makeRotate(osg::DegreesToRadians(_rotateXAxis), X_AXIS);
		matrixOsg.postMultScale(Vec3(_scaleModel, _scaleModel, _scaleModel));
		matrixOsg.decompose(pos, rot, scl, so);

		FbxQuaternion rotationQuat(rot.x(), rot.y(), rot.z(), rot.w());
		FbxVector4 translate(0, 0, 0);
		FbxVector4 scale(scl.x(), scl.y(), scl.z());

		FbxAMatrix matMultiply;
		matMultiply.SetTQS(translate, rotationQuat, scale);

		mainTransform = mainTransform * matMultiply;

		FbxVector4 rotationFinal = mainTransform.GetR();
		FbxVector4 positionFinal = mainTransform.GetT();
		FbxVector4 scaleFinal = mainTransform.GetS();

		_firstMatrixNode->LclTranslation.Set(FbxDouble3(positionFinal[0], positionFinal[1], positionFinal[2]));
		_firstMatrixNode->LclRotation.Set(FbxDouble3(rotationFinal[0], rotationFinal[1], rotationFinal[2]));
		_firstMatrixNode->LclScaling.Set(FbxDouble3(scaleFinal[0], scaleFinal[1], scaleFinal[2]));
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

		if (_listTriangles.size() > 0)
		{
			OSG_NOTICE << "Building Mesh: " << geometry.getName() << " [" << _listTriangles.size() << " triangles]" << std::endl;

			MaterialParser* materialParser = processStateSet(geometry.getStateSet());

			FbxNode* nodeFBX = buildMesh(geometry, materialParser);

			if (rigGeometry)
				_riggedMeshMap.emplace(rigGeometry, nodeFBX);
			else if (morphGeometry)
				_MorphedMeshMap.emplace(morphGeometry, nodeFBX);

			_geometryList.clear();
			_listTriangles.clear();
			_texcoords = false;
			_drawableNum = 0;
		}

		osg::NodeVisitor::traverse(geometry);

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

			if (_exportFullHierarchy)
			{
				FbxNode* nodeFBX = FbxNode::Create(_pSdkManager, node.getName().empty() ? defaultName.c_str() : node.getName().c_str());
				_curFbxNode->AddChild(nodeFBX);
				_curFbxNode = nodeFBX;
			}

			traverse(node);

			if (!_ignoreBones && !_ignoreAnimations)
			{
				ref_ptr<Callback> nodeCallback = node.getUpdateCallback();
				if (nodeCallback)
					applyAnimations(getRealUpdateCallback(nodeCallback));
			}

			_curFbxNode = parent;
		}
		else
		{
			//ignore the root node to maintain same hierarchy
			_firstNodeProcessed = true;

			FbxNode* RootNode = _curFbxNode;
			_MeshesRoot = _curFbxNode;
			
			traverse(node);

			// Build mesh skin, apply global animations
			if (!_ignoreBones)
			{
				buildMeshSkin();

				if (!_ignoreAnimations)
				{
					ref_ptr<Callback> nodeCallback = node.getUpdateCallback();
					if (nodeCallback)
						applyAnimations(getRealUpdateCallback(nodeCallback));
				}
			}

			applyGlobalTransforms(RootNode);
		}
	}

	void WriterNodeVisitor::apply(osg::MatrixTransform& node)
	{
		std::string nodeName;
		ref_ptr<Skeleton> skeleton = dynamic_cast<Skeleton*>(&node);
		ref_ptr<Bone> bone = dynamic_cast<Bone*>(&node);

		FbxNode* parent = _curFbxNode;
		// bool NodeHasBoneParent = isNodeASkeleton(parent);

		if (skeleton)
			nodeName = node.getName().empty() ? "Armature" : node.getName();
		else if (bone)
			nodeName = node.getName().empty() ? "DefaultBone" : node.getName();
		else
			nodeName = node.getName().empty() ? "DefaultTransform" : node.getName();

		// Get custom parameter on node to first Matrix. If we are the first, also save the current transform
		// for later (_firstMatrixPostProcess)
		const DefaultUserDataContainer* udc = dynamic_cast<DefaultUserDataContainer*>(node.getUserDataContainer());

		bool firstMatrixGet;
		bool isFirstMatrix = (udc && udc->getUserValue("firstMatrix", firstMatrixGet));

		// Set transforms for node
		osg::Matrix matrix = node.getMatrix();
		_matrixStack.push_back(std::make_pair(nodeName, matrix));

		// Fix for sketchfab coordinates
		if (isFirstMatrix)
		{
			matrix.makeIdentity();
			//osg::Matrix matrix2;
			//matrix2.makeRotate(osg::DegreesToRadians(-90.0), X_AXIS);
			//matrix.preMult(matrix2);
			node.setMatrix(matrix);
		}

		// Create groups for nodes if they are bones or if we are ignoring bones
		// so we can see matrix groups when no bone is present.
		osg::Vec3d pos, scl;
		osg::Quat rot, so;
		FbxAMatrix mat;

		if (isFirstMatrix || _ignoreBones || _exportFullHierarchy || skeleton || bone)
		{
			_curFbxNode = FbxNode::Create(_pSdkManager, nodeName.c_str());
			parent->AddChild(_curFbxNode);

			if (skeleton || bone)
			{
				_skeletonNodes.push_back(_curFbxNode);
			}

			// Need to reconstruct skeleton transforms for non-full hierarchy
			if (skeleton && !_exportFullHierarchy
				&& (skeleton->getNumParents() == 0 || skeleton->getNumParents() > 0 && !hasSkeletonParent(*skeleton->getParent(0)))) 
			{
				Matrix matrixSkeletonTransform;
				if (skeleton->getNumParents() > 0)
					matrixSkeletonTransform = buildParentMatrices(*skeleton->getParent(0));
				matrix = matrixSkeletonTransform * matrix;
			}

			matrix.decompose(pos, rot, scl, so);
			FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
			mat.SetQ(q);
			FbxVector4 vec4 = mat.GetR();

			_curFbxNode->LclTranslation.Set(FbxDouble3(pos.x(), pos.y(), pos.z()));
			_curFbxNode->LclRotation.Set(FbxDouble3(vec4[0], vec4[1], vec4[2]));
			_curFbxNode->LclScaling.Set(FbxDouble3(scl.x(), scl.y(), scl.z()));
		}

		if (isFirstMatrix)
		{
			_firstMatrixNode = _curFbxNode;
			_MeshesRoot = _curFbxNode;
			_firstMatrix = matrix;
		}

		// Process Skeleton and Bones and create nodes before continuing
		if (!_ignoreBones && (skeleton || bone))
		{
			FbxSkeleton* fbxSkel = FbxSkeleton::Create(_curFbxNode, skeleton ? "RootNode" : nodeName.c_str());
			fbxSkel->SetSkeletonType(skeleton ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
			_curFbxNode->SetNodeAttribute(fbxSkel);

			//if (NodeHasBoneParent && getSkeletonType(parent) == FbxSkeleton::eEffector)
			//{
			//	reinterpret_cast<FbxSkeleton*>(parent->GetNodeAttribute())->SetSkeletonType(FbxSkeleton::eLimb);
			//}

			if (bone)
				_boneNodeSkinMap.emplace(nodeName, std::make_pair(bone, _curFbxNode));
		}

		// Process UpdateMatrixTransform and UpdateBone Callbacks last
//		if (!_ignoreAnimations && !skeleton)
		if (!skeleton)
		{
			ref_ptr<Callback> nodeCallback = getRealUpdateCallback(node.getUpdateCallback());
			if (nodeCallback)
				applyUpdateMatrixTransform(nodeCallback, _curFbxNode, node);
		}

		traverse(node);

		_curFbxNode = parent;
	}
}

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

	static bool isNodeASkeleton(FbxNode* pNode)
	{
		if (pNode == nullptr)
		{
			return false;
		}

		FbxNodeAttribute* nodeAttribute = pNode->GetNodeAttribute();

		if (nodeAttribute == nullptr)
		{
			return false;
		}

		return nodeAttribute->GetAttributeType() == FbxNodeAttribute::eSkeleton;
	}

	bool WriterNodeVisitor::isMatrixAnimated(const osg::MatrixTransform* node)
	{
		if (!node)
			return false;

		// Search node callback
		const ref_ptr<Callback> callback = const_cast<Callback*>(node->getUpdateCallback());
		ref_ptr<Callback> nodeCallback = getRealUpdateCallback(callback);
		if (!nodeCallback)
			return false;

		if (dynamic_cast<const Skeleton*>(node) || dynamic_cast<const Bone*>(node))
			return false;

		// Search for UpdateMatrix callback
		const ref_ptr<UpdateMatrixTransform> umt = dynamic_pointer_cast<UpdateMatrixTransform>(nodeCallback);
		if (!umt)
			return false;

		// Look into animations list to see if this node is target of animations
		std::string nodeName = umt->getName();
		if (_animationTargetNames.find(nodeName) != _animationTargetNames.end())
			return true;

		return false;
	}

	const osg::ref_ptr<osg::Callback> WriterNodeVisitor::getRealUpdateCallback(const osg::ref_ptr<osg::Callback>& callback)
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

	bool WriterNodeVisitor::hasAnimatedMatrixParent(const osg::Node* node)
	{
		if (!node)
			return false;

		if (dynamic_cast<const MatrixTransform*>(node) && isMatrixAnimated(dynamic_cast<const MatrixTransform*>(node)))
			return true;

		if (node->getNumParents() == 0)
			return false;

		return hasAnimatedMatrixParent(node->getParent(0));
	}

	bool WriterNodeVisitor::firstBoneInHierarchy(FbxNode* boneParent)
	{
		if (!boneParent)
			return true;

		if (isNodeASkeleton(boneParent))
			return false;

		if (!boneParent->GetParent())
			return true;

		return firstBoneInHierarchy(boneParent->GetParent());
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
				break;
			}
		}

		return nodeMatrix;
	}

	osg::Matrix WriterNodeVisitor::buildParentMatrices(const osg::Node& node, int& numParents, bool useAllParents)
	{
		osg::Matrix mult;
		std::string nodeName = node.getName();

		if (!useAllParents)
		{
			if (isMatrixAnimated(dynamic_cast<const MatrixTransform*>(&node)) || dynamic_cast<const Skeleton*>(&node))
			{
				numParents++;
				return mult;
			}
		}

		if (node.getNumParents() > 0)
		{
			mult = buildParentMatrices(*node.getParent(0), numParents, useAllParents);
		}

		if (auto matrixObj = dynamic_cast<const osg::MatrixTransform*>(&node))
		{
			// 
			osg::Matrix m = matrixObj->getMatrix();
			numParents++;

			ref_ptr<Callback> callback = const_cast<Callback*>(node.getUpdateCallback());
			ref_ptr<Callback> nodeCallback = getRealUpdateCallback(callback);

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

	std::string WriterNodeVisitor::buildNodePath(FbxNode* currentNode)
	{
		if (currentNode->GetParent())
			return buildNodePath(currentNode->GetParent()) + currentNode->GetName() + std::string("/");
		else
			return std::string(currentNode->GetName()) + std::string("/");
	}

	void WriterNodeVisitor::applyGlobalTransforms()
	{
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

		while (_animatedMatrices.size() > 0)
		{
			MatrixTransform* node = _animatedMatrices.top().first;
			FbxNode* matrixAnimated = _animatedMatrices.top().second;

			FbxAMatrix mainTransform = matrixAnimated->EvaluateGlobalTransform();
			mainTransform = mainTransform * matMultiply;

			std::string matrixName = matrixAnimated->GetName(); // for debug

			if (!hasAnimatedMatrixParent(node))
			{
				FbxVector4 rotationFinal = mainTransform.GetR();
				FbxVector4 positionFinal = mainTransform.GetT();
				FbxVector4 scaleFinal = mainTransform.GetS();

				matrixAnimated->LclTranslation.Set(FbxDouble3(positionFinal[0], positionFinal[1], positionFinal[2]));
				matrixAnimated->LclRotation.Set(FbxDouble3(rotationFinal[0], rotationFinal[1], rotationFinal[2]));
				matrixAnimated->LclScaling.Set(FbxDouble3(scaleFinal[0], scaleFinal[1], scaleFinal[2]));
			}
			_animatedMatrices.pop();
		}

		while (_riggedMeshesRoot.size() > 0)
		{
			Skeleton* node = _riggedMeshesRoot.top().first;
			FbxNode* skeleton = _riggedMeshesRoot.top().second;

			std::string skeletonName = skeleton->GetName(); // for debug

			if (!hasAnimatedMatrixParent(node)) // Don't apply transformations twice!
			{
				FbxAMatrix mainTransform = skeleton->EvaluateGlobalTransform();
				mainTransform = mainTransform * matMultiply;

				FbxVector4 rotationFinal = mainTransform.GetR();
				FbxVector4 positionFinal = mainTransform.GetT();
				FbxVector4 scaleFinal = mainTransform.GetS();

				skeleton->LclTranslation.Set(FbxDouble3(positionFinal[0], positionFinal[1], positionFinal[2]));
				skeleton->LclRotation.Set(FbxDouble3(rotationFinal[0], rotationFinal[1], rotationFinal[2]));
				skeleton->LclScaling.Set(FbxDouble3(scaleFinal[0], scaleFinal[1], scaleFinal[2]));
			}

			_riggedMeshesRoot.pop();
		}

		while (_normalMeshesNodes.size() > 0)
		{
			FbxNode* meshRoot = _normalMeshesNodes.top();

			std::string meshName = meshRoot->GetName();

			FbxAMatrix mainTransform = meshRoot->EvaluateGlobalTransform();
			mainTransform = mainTransform * matMultiply;

			FbxVector4 rotationFinal = mainTransform.GetR();
			FbxVector4 positionFinal = mainTransform.GetT();
			FbxVector4 scaleFinal = mainTransform.GetS();

			meshRoot->LclTranslation.Set(FbxDouble3(positionFinal[0], positionFinal[1], positionFinal[2]));
			meshRoot->LclRotation.Set(FbxDouble3(rotationFinal[0], rotationFinal[1], rotationFinal[2]));
			meshRoot->LclScaling.Set(FbxDouble3(scaleFinal[0], scaleFinal[1], scaleFinal[2]));

			_normalMeshesNodes.pop();
		}
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

			if (_exportFull)
			{
				_curFbxNode = FbxNode::Create(_pSdkManager, node.getName().c_str());
				parent->AddChild(_curFbxNode);
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
			_firstMatrixNode = _curFbxNode; // Temporary in case we don't find a first matrix

			// Build animations targets list (needed to see which nodes we will create or not)
			buildAnimationTargets(&node);
			
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

			applyGlobalTransforms();
		}
	}

	void WriterNodeVisitor::apply(osg::MatrixTransform& node)
	{
		std::string nodeName;
		ref_ptr<Skeleton> skeleton = dynamic_cast<Skeleton*>(&node);
		ref_ptr<Bone> bone = dynamic_cast<Bone*>(&node);
		int numMatrixParents(0);

		FbxNode* parent = _curFbxNode;

		if (skeleton)
			nodeName = node.getName().empty() ? "Skeleton" : node.getName();
		else if (bone)
			nodeName = node.getName().empty() ? "DefaultBone" : node.getName();
		else
			nodeName = node.getName().empty() ? "DefaultTransform" : node.getName();

		// Get custom parameter on node to first Matrix. If we are the first, also save the current transform
		// for later (_firstMatrixPostProcess)
		const DefaultUserDataContainer* udc = dynamic_cast<DefaultUserDataContainer*>(node.getUserDataContainer());

		bool firstMatrixGet;
		bool isFirstMatrix = (udc && udc->getUserValue("firstMatrix", firstMatrixGet));
		bool animatedMatrix = isMatrixAnimated(&node);

		// Set transforms for node
		osg::Matrix matrix = node.getMatrix();
		osg::Vec3d pos, scl;
		osg::Quat rot, so;
		FbxAMatrix mat;
		FbxAMatrix currentNodeMatrix;
		_matrixStack.push_back(std::make_pair(nodeName, matrix));

		// Only build the appropriate nodes that apply
		if (_exportFull || isFirstMatrix || skeleton || bone || animatedMatrix)
		{
			_curFbxNode = FbxNode::Create(_pSdkManager, nodeName.c_str());
			parent->AddChild(_curFbxNode);
			std::string currentNodePath = buildNodePath(_curFbxNode);		// for debug

			if (skeleton || bone)
			{
				_skeletonNodes.emplace(_curFbxNode);
			}

			// Need to reconstruct skeleton and animated matrices transforms.
			if (!_exportFull && (skeleton || animatedMatrix))
			{
				Matrix matrixTransform;
				if (node.getNumParents() > 0)
					matrixTransform = buildParentMatrices(*node.getParent(0), numMatrixParents, false);
				matrix = matrixTransform * matrix;

				// Fix for matrices without a first parent
				if (numMatrixParents == 0)
				{
					osg::Matrix matrixMult;
					matrixMult.makeRotate(osg::DegreesToRadians(-90.0), X_AXIS);
					matrix.decompose(pos, rot, scl, so);
					matrix = matrixMult;
				}
			}

			if (isFirstMatrix && !skeleton)
			{
				osg::Matrix matrixMult;
				matrixMult.makeRotate(osg::DegreesToRadians(-90.0), X_AXIS);
				matrix = matrix * matrixMult;
				node.setMatrix(matrix);
				nodeName = "First Matrix";

				_firstMatrixNode = _curFbxNode;
			}
			else if (skeleton)
			{
				_riggedMeshesRoot.push(std::make_pair(skeleton, _curFbxNode));
			}
			else if (animatedMatrix)
			{
				_animatedMatrices.push(std::make_pair(&node, _curFbxNode));
			}

			matrix.decompose(pos, rot, scl, so);
			FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
			mat.SetQ(q);
			FbxVector4 vec4 = mat.GetR();

			_curFbxNode->LclTranslation.Set(FbxDouble3(pos.x(), pos.y(), pos.z()));
			_curFbxNode->LclRotation.Set(FbxDouble3(vec4[0], vec4[1], vec4[2]));
			_curFbxNode->LclScaling.Set(FbxDouble3(scl.x(), scl.y(), scl.z()));
			currentNodeMatrix = _curFbxNode->EvaluateLocalTransform();	// for debug
		}

		// Process Skeleton and Bones and create nodes before continuing
		if (!_ignoreBones && (skeleton || bone))
		{
			FbxSkeleton* fbxSkel = FbxSkeleton::Create(_curFbxNode, skeleton ? "RootNode" : nodeName.c_str());
			fbxSkel->SetSkeletonType(skeleton ? FbxSkeleton::eRoot : FbxSkeleton::eLimbNode);
			_curFbxNode->SetNodeAttribute(fbxSkel);

			if (bone)
				_boneNodeSkinMap.emplace(nodeName, std::make_pair(bone, _curFbxNode));
		}

		// Process UpdateMatrixTransform and UpdateBone Callbacks last
		if (!skeleton)
		{
			ref_ptr<Callback> nodeCallback = getRealUpdateCallback(node.getUpdateCallback());
			if (nodeCallback)
				applyUpdateMatrixTransform(nodeCallback, _curFbxNode, node);
		}

		// Traverse Hierarchy
		traverse(node);

		_curFbxNode = parent;
	}
}

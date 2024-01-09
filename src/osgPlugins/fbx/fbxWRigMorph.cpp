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


namespace pluginfbx
{

	void WriterNodeVisitor::applyUpdateMatrixTransform(const osg::ref_ptr<osg::Callback>& callback,
		FbxNode* fbxNode, osg::MatrixTransform& matrixTransform)
	{
		const ref_ptr<UpdateMatrixTransform> umt = dynamic_pointer_cast<UpdateMatrixTransform>(callback);

		if (!umt)
			return;

		auto& stackedTransforms = umt->getStackedTransforms();

		// For any matrixTransform that is a bone, creates FbxAnimCurveNodes and map to it
		ref_ptr<Bone> bone = dynamic_cast<Bone*>(&matrixTransform);
		if (bone)
		{
			std::shared_ptr<UpdateBoneNodes> newBoneAnim = std::make_shared<UpdateBoneNodes>();
			std::string updateBoneName = umt->getName();
			newBoneAnim->bone = bone;
			newBoneAnim->fbxNode = fbxNode;

			_boneAnimCurveMap.emplace(updateBoneName, newBoneAnim);
		}

		// Should have only 1 of each or a matrix...
		for (auto& stackedTransform : stackedTransforms)
		{
			if (auto translateElement = dynamic_pointer_cast<StackedTranslateElement>(stackedTransform))
			{
				FbxDouble3 translation(translateElement->getTranslate().x(),
					translateElement->getTranslate().y(),
					translateElement->getTranslate().z());
				fbxNode->LclTranslation.Set(translation);
			}
			else if (auto rotateElement = dynamic_pointer_cast<StackedQuaternionElement>(stackedTransform))
			{
				osg::Quat rot = rotateElement->getQuaternion();
				FbxAMatrix mat;
				FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
				mat.SetQ(q);
				FbxVector4 vec4 = mat.GetR();
				fbxNode->LclRotation.Set(vec4);
			}
			else if (auto scaleElement = dynamic_pointer_cast<StackedScaleElement>(stackedTransform))
			{
				// Associe scaleElement com scaleCurveNode
				FbxDouble3 scale(scaleElement->getScale().x(),
					scaleElement->getScale().y(),
					scaleElement->getScale().z());
				fbxNode->LclScaling.Set(scale);
			}
			else if (auto rotateAxisElement = dynamic_pointer_cast<StackedRotateAxisElement>(stackedTransform))
			{
				osg::Vec3 axis = rotateAxisElement->getAxis();
				float angle = rotateAxisElement->getAngle();

				osg::Quat rot;
				rot.makeRotate(angle, axis);

				FbxAMatrix mat;
				FbxQuaternion q(rot.x(), rot.y(), rot.z(), rot.w());
				mat.SetQ(q);
				FbxVector4 vec4 = mat.GetR();
				fbxNode->LclRotation.Set(vec4);
			}
			else if (auto matrixElement = dynamic_pointer_cast<StackedMatrixElement>(stackedTransform))
			{
				osg::Matrix matrix = matrixElement->getMatrix();
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
			}
		}
	}

	void WriterNodeVisitor::createMorphTargets(const osgAnimation::MorphGeometry* morphGeometry, FbxMesh* mesh, const osg::Matrix& rotateMatrix)
	{
		FbxBlendShape* fbxBlendShape = FbxBlendShape::Create(_pSdkManager, morphGeometry->getName().c_str());
		mesh->AddDeformer(fbxBlendShape);

		bool vertexFailedNotice(false);
		for (unsigned int i = 0; i < morphGeometry->getMorphTargetList().size(); ++i)
		{
			const osg::Geometry* osgMorphTarget = morphGeometry->getMorphTarget(i).getGeometry();
			FbxBlendShapeChannel* fbxChannel = FbxBlendShapeChannel::Create(_pSdkManager, osgMorphTarget->getName().c_str());

			std::stringstream ss;
			ss << osgMorphTarget->getName().c_str() << "_" << i;
			FbxShape* fbxShape = FbxShape::Create(_pSdkManager, ss.str().c_str());
			fbxChannel->AddTargetShape(fbxShape);

			// Create vertices
			const osg::Array* vertices = osgMorphTarget->getVertexArray();
			if (!vertices)
				continue;

			fbxShape->InitControlPoints(vertices->getNumElements());
			FbxVector4* fbxControlPoints = fbxShape->GetControlPoints();

			bool vertexFailed(false);
			for (unsigned int j = 0; j < vertices->getNumElements(); j++)
			{
				switch (vertices->getType())
				{
				case Array::Vec4dArrayType:
				{
					osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(vertices)->at(j);
					osg::Vec4d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case Array::Vec4ArrayType:
				{
					osg::Vec4 vec = dynamic_cast<const Vec4Array*>(vertices)->at(j);
					osg::Vec4d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w());
					break;
				}
				case Array::Vec3dArrayType:
				{
					osg::Vec3d vec = dynamic_cast<const Vec3dArray*>(vertices)->at(j);
					osg::Vec3d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				case Array::Vec3ArrayType:
				{
					osg::Vec3 vec = dynamic_cast<const Vec3Array*>(vertices)->at(j);
					osg::Vec3d vecf = vec * rotateMatrix;
					fbxControlPoints[j] = FbxVector4(vecf.x(), vecf.y(), vecf.z());
					break;
				}
				default:
				{
					if (!vertexFailedNotice)
						OSG_WARN << "Error creating 1 or more targets for Mesh. Vertex Array is not floats. Name = " << mesh->GetName() << std::endl;
					vertexFailed = true;
					vertexFailedNotice = true;
				}
				}

				if (vertexFailed)
					break;
			}

			if (vertexFailed)
				continue;

			// Create Normals
			const osg::Array* normals = osgMorphTarget->getNormalArray();

			if (normals)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementNormal* layerElementNormal = FbxLayerElementNormal::Create(fbxShape, "Normals");
				layerElementNormal->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerElementNormal->SetReferenceMode(FbxLayerElement::eDirect);
				layerElementNormal->GetDirectArray().SetCount(normals->getNumElements());

				for (unsigned int j = 0; j < normals->getNumElements(); ++j)
				{
					switch (normals->getType())
					{
					case Array::Vec4dArrayType:
					{
						osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(normals)->at(j);
						osg::Vec4d vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec4ArrayType:
					{
						osg::Vec4 vec = dynamic_cast<const Vec4Array*>(normals)->at(j);
						osg::Vec4 vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec3dArrayType:
					{
						osg::Vec3d vec = dynamic_cast<const Vec3dArray*>(normals)->at(j);
						osg::Vec3 vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					case Array::Vec3ArrayType:
					{
						osg::Vec3 vec = dynamic_cast<const Vec3Array*>(normals)->at(j);
						osg::Vec3 vecf = vec * rotateMatrix;
						layerElementNormal->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					}
				}

				layer->SetNormals(layerElementNormal);
			}

			// Create Colors
			const osg::Array* colors = osgMorphTarget->getColorArray();

			if (colors)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementVertexColor* layerVertexColors = FbxLayerElementVertexColor::Create(fbxShape, "VertexColors");
				layerVertexColors->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerVertexColors->SetReferenceMode(FbxLayerElement::eDirect);
				layerVertexColors->GetDirectArray().SetCount(colors->getNumElements());

				for (unsigned int j = 0; j < colors->getNumElements(); ++j)
				{
					switch (colors->getType())
					{
					case Array::Vec4dArrayType:
					{
						osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r(), vec.g(), vec.b(), vec.a()));
						break;
					}
					case Array::Vec4ArrayType:
					{
						osg::Vec4 vec = dynamic_cast<const Vec4Array*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r(), vec.g(), vec.b(), vec.a()));
						break;
					}
					case Array::Vec4ubArrayType:
					{
						osg::Vec4ub vec = dynamic_cast<const Vec4ubArray*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r() / 255.0, vec.g() / 255.0, vec.b() / 255.0, vec.a() / 255.0));
						break;
					}
					case Array::Vec4bArrayType:
					{
						osg::Vec4b vec = dynamic_cast<const Vec4bArray*>(colors)->at(j);
						layerVertexColors->GetDirectArray().SetAt(j, FbxVector4(vec.r() / 255.0, vec.g() / 255.0, vec.b() / 255.0, vec.a() / 255.0));
						break;
					}
					}
				}

				layer->SetVertexColors(layerVertexColors);
			}

			// Create tangents
			const osg::Array* tangents = nullptr;
			for (auto& attrib : osgMorphTarget->getVertexAttribArrayList())
			{
				bool isTangent = false;
				if (attrib->getUserValue("tangent", isTangent))
					if (isTangent)
					{
						tangents = attrib;
						break;
					}
			}

			if (tangents)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementTangent* layerTangents = FbxLayerElementTangent::Create(fbxShape, "Tangents");
				layerTangents->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerTangents->SetReferenceMode(FbxLayerElement::eDirect);
				layerTangents->GetDirectArray().SetCount(tangents->getNumElements());

				for (unsigned int j = 0; j < tangents->getNumElements(); ++j)
				{
					switch (tangents->getType())
					{
					case Array::Vec4dArrayType:
					{
						osg::Vec4d vec = dynamic_cast<const Vec4dArray*>(tangents)->at(j);
						osg::Vec4d vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec4ArrayType:
					{
						osg::Vec4 vec = dynamic_cast<const Vec4Array*>(tangents)->at(j);
						osg::Vec4 vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z(), vecf.w()));
						break;
					}
					case Array::Vec3dArrayType:
					{
						osg::Vec3d vec = dynamic_cast<const Vec3dArray*>(tangents)->at(j);
						osg::Vec3d vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					case Array::Vec3ArrayType:
					{
						osg::Vec3 vec = dynamic_cast<const Vec3Array*>(tangents)->at(j);
						osg::Vec3 vecf = vec * rotateMatrix;
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vecf.x(), vecf.y(), vecf.z()));
						break;
					}
					case Array::Vec2dArrayType:
					{
						osg::Vec2d vec = dynamic_cast<const Vec2dArray*>(tangents)->at(j);
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vec.x(), vec.y(), 0.0));
						break;
					}
					case Array::Vec2ArrayType:
					{
						osg::Vec2 vec = dynamic_cast<const Vec2Array*>(tangents)->at(j);
						layerTangents->GetDirectArray().SetAt(j, FbxVector4(vec.x(), vec.y(), 0.0));
						break;
					}
					}
				}

				layer->SetTangents(layerTangents);
			}

			// Create TexCoords
			const osg::Array* texCoord = osgMorphTarget->getTexCoordArray(0);

			if (texCoord)
			{
				FbxLayer* layer = fbxShape->GetLayer(0);
				int layerNum(0);
				if (!layer) {
					layerNum = fbxShape->CreateLayer();
					layer = fbxShape->GetLayer(0);
				}

				FbxLayerElementUV* layerDiffuseUV = FbxLayerElementUV::Create(fbxShape, "UVDiffuse");
				layerDiffuseUV->SetMappingMode(FbxLayerElement::eByControlPoint);
				layerDiffuseUV->SetReferenceMode(FbxLayerElement::eDirect);
				layerDiffuseUV->GetDirectArray().SetCount(texCoord->getNumElements());

				for (unsigned int j = 0; j < texCoord->getNumElements(); ++j)
				{
					switch (texCoord->getType())
					{
					case Array::Vec2dArrayType:
					{
						osg::Vec2d vec = dynamic_cast<const Vec2dArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2ArrayType:
					{
						osg::Vec2 vec = dynamic_cast<const Vec2Array*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2ubArrayType:
					{
						osg::Vec2ub vec = dynamic_cast<const Vec2ubArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2usArrayType:
					{
						osg::Vec2us vec = dynamic_cast<const Vec2usArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2uiArrayType:
					{
						osg::Vec2ui vec = dynamic_cast<const Vec2uiArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2bArrayType:
					{
						osg::Vec2b vec = dynamic_cast<const Vec2bArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2sArrayType:
					{
						osg::Vec2s vec = dynamic_cast<const Vec2sArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					case Array::Vec2iArrayType:
					{
						osg::Vec2i vec = dynamic_cast<const Vec2iArray*>(texCoord)->at(j);
						layerDiffuseUV->GetDirectArray().SetAt(j, FbxVector2(vec.x(), vec.y()));
						break;
					}
					}
				}

				layer->SetUVs(layerDiffuseUV);
			}
		}
	}

	void WriterNodeVisitor::applySkinning(const osgAnimation::VertexInfluenceMap& vim, FbxMesh* fbxMesh)
	{
		FbxSkin* skinDeformer = FbxSkin::Create(_pSdkManager, "");

		for (const auto& influence : vim)
		{
			const std::string& boneName = influence.first;

			BonePair bonePair;
			if (_boneNodeSkinMap.find(boneName) != _boneNodeSkinMap.end())
				bonePair = _boneNodeSkinMap.at(boneName);

			ref_ptr<osgAnimation::Bone> bone = bonePair.first;
			FbxNode* fbxBoneNode = bonePair.second;

			if (!bone)
			{
				OSG_WARN << "WARNING: FBX Mesh " << fbxMesh->GetName() << " has a missing bone: " << boneName << std::endl;
				continue;
			}

			std::stringstream clusterName;
			clusterName << bone->getName() << "_cluster";
			FbxCluster* cluster = FbxCluster::Create(_pSdkManager, clusterName.str().c_str());
			cluster->SetLink(fbxBoneNode);
			cluster->SetLinkMode(FbxCluster::eNormalize);

			for (const auto& weightPair : influence.second)
			{
				int vertexIndex = weightPair.first;
				double weight = weightPair.second;
				cluster->AddControlPointIndex(vertexIndex, weight);
			}

			skinDeformer->AddCluster(cluster);

			osg::Matrixd osgInvBindMatrix = Matrix::inverse(bone->getInvBindMatrixInSkeletonSpace());

			FbxAMatrix fbxInvBindMatrix;
			for (int row = 0; row < 4; ++row) {
				for (int col = 0; col < 4; ++col) {
					fbxInvBindMatrix[row][col] = osgInvBindMatrix(row, col);
				}
			}

			cluster->SetTransformLinkMatrix(fbxInvBindMatrix);
		}

		fbxMesh->AddDeformer(skinDeformer);
	}

	void WriterNodeVisitor::buildMeshSkin()
	{
		if (_riggedMeshMap.size() > 0)
			OSG_NOTICE << "Processing rig and skinning... " << std::endl;

		for (auto& entry : _riggedMeshMap)
		{
			const osgAnimation::VertexInfluenceMap* vim = entry.first->getInfluenceMap();
			if (!vim)
				continue;

			FbxNode* meshNode = entry.second;
			FbxMesh* mesh = nullptr;

			int attributeCount = meshNode->GetNodeAttributeCount();

			for (int index = 0; index < attributeCount; index++) {
				FbxNodeAttribute* attribute = meshNode->GetNodeAttributeByIndex(index);

				if (attribute && attribute->GetAttributeType() == FbxNodeAttribute::eMesh) {
					mesh = FbxCast<FbxMesh>(attribute);
					break;
				}
			}

			if (mesh)
				applySkinning(*vim, mesh);
		}
	}

}

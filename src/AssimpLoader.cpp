/*
 Copyright (C) 2011 Gabor Papp

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "cinder/app/App.h"
#include "cinder/ImageIo.h"
#include "cinder/CinderMath.h"
#include "cinder/Utilities.h"

#include "AssimpLoader.h"

using namespace std;
using namespace ci;

namespace mndl { namespace assimp {

static void fromAssimp( const aiMesh *aim, TriMesh *cim )
{
	app::console() << fromAssimp( aim->mVertices[0] ) << endl;
	app::console() << aim->mVertices[0].x << ", " << aim->mVertices[0].y << aim->mVertices[0].z << endl;
	// copy vertices
	for ( unsigned i = 0; i < aim->mNumVertices; ++i )
	{
		cim->appendVertex( fromAssimp( aim->mVertices[i] ) );
	}

	if( aim->HasNormals() )
	{
		for ( unsigned i = 0; i < aim->mNumVertices; ++i )
		{
			cim->appendNormal( fromAssimp( aim->mNormals[i] ) );
		}
	}

	// aiVector3D *	mTextureCoords [AI_MAX_NUMBER_OF_TEXTURECOORDS]
	// just one for now
	if ( aim->GetNumUVChannels() > 0 )
	{
		for ( unsigned i = 0; i < aim->mNumVertices; ++i )
		{
			cim->appendTexCoord( Vec2f( aim->mTextureCoords[0][i].x,
										aim->mTextureCoords[0][i].y ) );
		}
	}

	//aiColor4D *mColors [AI_MAX_NUMBER_OF_COLOR_SETS]
	if ( aim->GetNumColorChannels() > 0 )
	{
		for ( unsigned i = 0; i < aim->mNumVertices; ++i )
		{
			cim->appendColorRGBA( fromAssimp( aim->mColors[0][i] ) );
		}
	}

	for ( unsigned i = 0; i < aim->mNumFaces; ++i )
	{
		if ( aim->mFaces[i].mNumIndices > 3 )
		{
			throw AssimpLoaderExc( "non-triangular face found: model " +
					string( aim->mName.data ) + ", face #" +
					toString< unsigned >( i ) );
		}

		cim->appendTriangle( aim->mFaces[ i ].mIndices[ 0 ],
							 aim->mFaces[ i ].mIndices[ 1 ],
							 aim->mFaces[ i ].mIndices[ 2 ] );
	}
}

AssimpLoader::AssimpLoader( fs::path filename ) :
	mUsingMaterials( true ),
	mUsingNormals( true ),
	mUsingTextures( true ),
	mUsingColors( true ),
	mFilePath( filename )
{
	unsigned flags = aiProcessPreset_TargetRealtime_MaxQuality |
					 aiProcess_Triangulate |
					 aiProcess_FlipUVs;
	flags |= aiProcess_ImproveCacheLocality | aiProcess_OptimizeGraph |
			aiProcess_OptimizeMeshes | aiProcess_JoinIdenticalVertices |
			aiProcess_RemoveRedundantMaterials;

	mImporterRef = shared_ptr< Assimp::Importer >( new Assimp::Importer() );
	/*
	mImporterRef->SetPropertyInteger( AI_CONFIG_PP_SBP_REMOVE,
			aiPrimitiveType_LINE | aiPrimitiveType_POINT );
	mImporterRef->SetPropertyInteger( AI_CONFIG_PP_PTV_NORMALIZE, true );
	*/

	mScene = mImporterRef->ReadFile( filename.string(), flags );
	if ( !mScene )
		throw AssimpLoaderExc( mImporterRef->GetErrorString() );

	calculateDimensions();

	loadAllMeshes();
	mRootNode = loadNodes( mScene->mRootNode );
}

AssimpLoader::~AssimpLoader()
{
}

void AssimpLoader::calculateDimensions()
{
	Vec3f aMin, aMax;
	calculateBoundingBox( &aMin, &aMax );
	mBoundingBox = AxisAlignedBox3f( aMin, aMax );
}

void AssimpLoader::calculateBoundingBox( ci::Vec3f *min, ci::Vec3f *max )
{
	aiMatrix4x4 trafo;

	aiVector3D aiMin, aiMax;
	aiMin.x = aiMin.y = aiMin.z =  1e10f;
	aiMax.x = aiMax.y = aiMax.z = -1e10f;

	calculateBoundingBoxForNode( mScene->mRootNode, &aiMin, &aiMax, &trafo );
	*min = fromAssimp( aiMin );
	*max = fromAssimp( aiMax );
}

void AssimpLoader::calculateBoundingBoxForNode( const aiNode *nd, aiVector3D *min, aiVector3D *max, aiMatrix4x4 *trafo )
{
	aiMatrix4x4 prev;

	prev = *trafo;
	//aiMultiplyMatrix4( trafo, &nd->mTransformation );
	*trafo = *trafo * nd->mTransformation;

	for ( unsigned n = 0; n < nd->mNumMeshes; ++n )
	{
		const struct aiMesh *mesh = mScene->mMeshes[ nd->mMeshes[ n ] ];
		for ( unsigned t = 0; t < mesh->mNumVertices; ++t )
		{
			aiVector3D tmp = mesh->mVertices[ t ];
			//aiTransformVecByMatrix4( &tmp, trafo );
			tmp *= (*trafo);

			min->x = math<float>::min( min->x, tmp.x );
			min->y = math<float>::min( min->y, tmp.y );
			min->z = math<float>::min( min->z, tmp.z );
			max->x = math<float>::max( max->x, tmp.x );
			max->y = math<float>::max( max->y, tmp.y );
			max->z = math<float>::max( max->z, tmp.z );
		}
	}

	for ( unsigned n = 0; n < nd->mNumChildren; ++n )
	{
		calculateBoundingBoxForNode( nd->mChildren[n], min, max, trafo );
	}

	*trafo = prev;
}

AssimpNodeRef AssimpLoader::loadNodes( const aiNode *nd, AssimpNodeRef parentRef )
{
	AssimpNodeRef nodeRef = AssimpNodeRef( new AssimpNode() );
	nodeRef->setParent( parentRef );
	nodeRef->setName( fromAssimp( nd->mName ) );

	// store transform
	aiVector3D scaling;
	aiQuaternion rotation;
	aiVector3D position;
	nd->mTransformation.Decompose( scaling, rotation, position );
	nodeRef->setScale( fromAssimp( scaling ) );
	nodeRef->setOrientation( fromAssimp( rotation ) );
	nodeRef->setPosition( fromAssimp( position ) );

	// test
	nodeRef->mTransform = fromAssimp( nd->mTransformation );
	nodeRef->mAiNode = nd;
	nodeRef->mAiTransform = nd->mTransformation;

	// meshes
	for ( unsigned i = 0; i < nd->mNumMeshes; ++i )
	{
		unsigned meshId = nd->mMeshes[ i ];
		if ( meshId >= mModelMeshes.size() )
			throw AssimpLoaderExc( "node " + nodeRef->getName() + " references mesh #" +
					toString< unsigned >( meshId ) + " from " +
					toString< size_t >( mModelMeshes.size() ) + " meshes." );
		nodeRef->mMeshes.push_back( mModelMeshes[ meshId ] );
	}

	// store the node with meshes for rendering
	if ( nd->mNumMeshes > 0 )
	{
		app::console() << "adding mesh node " << nodeRef->getName() << endl;
		mMeshNodes.push_back( nodeRef );
	}

	// process all children
	for ( unsigned n = 0; n < nd->mNumChildren; ++n )
	{
		loadNodes( nd->mChildren[ n ], nodeRef );
	}
	return nodeRef;
}

AssimpMeshHelperRef AssimpLoader::convertAiMesh( const aiMesh *mesh )
{
	// the current meshHelper we will be populating data into.
	AssimpMeshHelperRef meshHelperRef = AssimpMeshHelperRef( new AssimpMeshHelper() );

	meshHelperRef->mName = fromAssimp( mesh->mName );

	// Handle material info
	aiMaterial *mtl = mScene->mMaterials[ mesh->mMaterialIndex ];
	aiColor4D dcolor, scolor, acolor, ecolor;

	if ( AI_SUCCESS == aiGetMaterialColor( mtl, AI_MATKEY_COLOR_DIFFUSE, &dcolor ) )
	{
		meshHelperRef->mMaterial.setDiffuse( fromAssimp( dcolor ) );
	}

	if ( AI_SUCCESS == aiGetMaterialColor( mtl, AI_MATKEY_COLOR_SPECULAR, &scolor ) )
	{
		meshHelperRef->mMaterial.setSpecular( fromAssimp( scolor ) );
	}

	if ( AI_SUCCESS == aiGetMaterialColor( mtl, AI_MATKEY_COLOR_AMBIENT, &acolor ) )
	{
		meshHelperRef->mMaterial.setAmbient( fromAssimp( acolor ) );
	}

	if ( AI_SUCCESS == aiGetMaterialColor( mtl, AI_MATKEY_COLOR_EMISSIVE, &ecolor ) )
	{
		meshHelperRef->mMaterial.setEmission( fromAssimp( ecolor ) );
	}

	float shininess;
	if ( AI_SUCCESS == aiGetMaterialFloat( mtl, AI_MATKEY_SHININESS, &shininess ) )
	{
		meshHelperRef->mMaterial.setShininess( shininess );
	}

#if 0
	int blendMode;
	if(AI_SUCCESS == aiGetMaterialInteger(mtl, AI_MATKEY_BLEND_FUNC, &blendMode)){
		if(blendMode==aiBlendMode_Default){
			meshHelper.blendMode=OF_BLENDMODE_ALPHA;
		}else{
			meshHelper.blendMode=OF_BLENDMODE_ADD;
		}
	}
#endif

	// Culling
	unsigned int max = 1;
	int two_sided;
	if ( ( AI_SUCCESS == aiGetMaterialIntegerArray( mtl, AI_MATKEY_TWOSIDED, &two_sided, &max ) ) && two_sided )
		meshHelperRef->mTwoSided = true;
	else
		meshHelperRef->mTwoSided = false;

	// Load Textures
	int texIndex = 0;
	aiString texPath;

	// TODO: handle other aiTextureTypes
	if ( AI_SUCCESS == mtl->GetTexture( aiTextureType_DIFFUSE, texIndex, &texPath ) )
	{
		app::console() << "loading image from " << texPath.data;
		fs::path texFsPath( texPath.data );
		fs::path modelFolder = mFilePath.parent_path();
		fs::path relTexPath = texFsPath.parent_path();
		fs::path texFile = texFsPath.filename();
		fs::path realPath = modelFolder / relTexPath / texFile;
		app::console() << " [" << realPath.string() << "]" << endl;

		// TODO: cache textures
		meshHelperRef->mTexture = loadImage( realPath );
	}

	meshHelperRef->mAiMesh = mesh;
	fromAssimp( mesh, &meshHelperRef->mCachedTriMesh );
	meshHelperRef->mValidCache = true;
	meshHelperRef->mHasChanged = false;

	meshHelperRef->mAnimatedPos.resize( mesh->mNumVertices );
	if ( mesh->HasNormals() )
	{
		meshHelperRef->mAnimatedNorm.resize( mesh->mNumVertices );
	}

#if 0

	int usage;
	if(getAnimationCount()){
#ifndef TARGET_OPENGLES
		usage = GL_STREAM_DRAW;
#else
		usage = GL_DYNAMIC_DRAW;
#endif
	}else{
		usage = GL_STATIC_DRAW;

	}

	meshHelper.vbo.setVertexData(&mesh->mVertices[0].x,3,mesh->mNumVertices,usage,sizeof(aiVector3D));
	if(mesh->HasVertexColors(0)){
		meshHelper.vbo.setColorData(&mesh->mColors[0][0].r,mesh->mNumVertices,GL_STATIC_DRAW,sizeof(aiColor4D));
	}
	if(mesh->HasNormals()){
		meshHelper.vbo.setNormalData(&mesh->mNormals[0].x,mesh->mNumVertices,usage,sizeof(aiVector3D));
	}
	if (meshHelper.cachedMesh.hasTexCoords()){
		meshHelper.vbo.setTexCoordData(meshHelper.cachedMesh.getTexCoordsPointer()[0].getPtr(),mesh->mNumVertices,GL_STATIC_DRAW,sizeof(ofVec2f));
	}
#endif

	meshHelperRef->mIndices.resize( mesh->mNumFaces * 3 );
	unsigned j = 0;
	for ( unsigned x = 0; x < mesh->mNumFaces; ++x )
	{
		for ( unsigned a = 0; a < mesh->mFaces[x].mNumIndices; ++a)
		{
			meshHelperRef->mIndices[ j++ ] = mesh->mFaces[ x ].mIndices[ a ];
		}
	}

#if 0
	meshHelper.vbo.setIndexData(&meshHelper.indices[0],meshHelper.indices.size(),GL_STATIC_DRAW);
#endif
	return meshHelperRef;
}

void AssimpLoader::loadAllMeshes()
{
	for ( unsigned i = 0; i < mScene->mNumMeshes; ++i )
	{
		app::console() << "loading mesh " << i << endl;
		AssimpMeshHelperRef meshHelperRef = convertAiMesh( mScene->mMeshes[ i ] );

		if ( i == 1 )
		{
				std::vector< Vec3f > &vertices = meshHelperRef->mCachedTriMesh.getVertices();
				app::console() << vertices[0] << " " << vertices.size() << endl;
		}

		mModelMeshes.push_back( meshHelperRef );
	}

#if 0
	animationTime = -1;
	setNormalizedTime(0);
#endif

	app::console() << "finished loading gl resources" << endl;
}

void AssimpLoader::draw()
{
	//ofPushStyle();

	glPushAttrib( GL_ALL_ATTRIB_BITS );
	glPushClientAttrib( GL_CLIENT_ALL_ATTRIB_BITS );
	//glPolygonMode(GL_FRONT_AND_BACK, ofGetGLPolyMode(renderType));
	gl::enable( GL_NORMALIZE );

	/*
	if(getAnimationCount())
	{
		updateGLResources();
	}
	*/
	//----
	if ( mScene->mNumAnimations > 0 )
	{
		unsigned animationIndex = 0;
		float currentTime = math<float>::fmod( app::getElapsedSeconds(), 5.f ) / 5.f;
		const aiAnimation* mAnim = mScene->mAnimations[animationIndex];

		// calculate the transformations for each animation channel
		for( unsigned int a = 0; a < mAnim->mNumChannels; a++)
		{
			const aiNodeAnim* channel = mAnim->mChannels[a];

			aiNode* targetNode = mScene->mRootNode->FindNode(channel->mNodeName);

			// ******** Position *****
			aiVector3D presentPosition( 0, 0, 0);
			if( channel->mNumPositionKeys > 0)
			{
				// Look for present frame number. Search from last position if time is after the last time, else from beginning
				// Should be much quicker than always looking from start for the average use case.
				unsigned int frame = 0;// (currentTime >= lastAnimationTime) ? lastFramePositionIndex : 0;
				while( frame < channel->mNumPositionKeys - 1)
				{
					if( currentTime < channel->mPositionKeys[frame+1].mTime)
						break;
					frame++;
				}

				// interpolate between this frame's value and next frame's value
				unsigned int nextFrame = (frame + 1) % channel->mNumPositionKeys;
				const aiVectorKey& key = channel->mPositionKeys[frame];
				const aiVectorKey& nextKey = channel->mPositionKeys[nextFrame];
				double diffTime = nextKey.mTime - key.mTime;
				if( diffTime < 0.0)
					diffTime += mAnim->mDuration;
				if( diffTime > 0)
				{
					float factor = float( (currentTime - key.mTime) / diffTime);
					presentPosition = key.mValue + (nextKey.mValue - key.mValue) * factor;
				} else
				{
					presentPosition = key.mValue;
				}
			}

			// ******** Rotation *********
			aiQuaternion presentRotation( 1, 0, 0, 0);
			if( channel->mNumRotationKeys > 0)
			{
				unsigned int frame = 0;//(currentTime >= lastAnimationTime) ? lastFrameRotationIndex : 0;
				while( frame < channel->mNumRotationKeys - 1)
				{
					if( currentTime < channel->mRotationKeys[frame+1].mTime)
						break;
					frame++;
				}

				// interpolate between this frame's value and next frame's value
				unsigned int nextFrame = (frame + 1) % channel->mNumRotationKeys;
				const aiQuatKey& key = channel->mRotationKeys[frame];
				const aiQuatKey& nextKey = channel->mRotationKeys[nextFrame];
				double diffTime = nextKey.mTime - key.mTime;
				if( diffTime < 0.0)
					diffTime += mAnim->mDuration;
				if( diffTime > 0)
				{
					float factor = float( (currentTime - key.mTime) / diffTime);
					aiQuaternion::Interpolate( presentRotation, key.mValue, nextKey.mValue, factor);
				} else
				{
					presentRotation = key.mValue;
				}
			}

			// ******** Scaling **********
			aiVector3D presentScaling( 1, 1, 1);
			if( channel->mNumScalingKeys > 0)
			{
				unsigned int frame = 0;//(currentTime >= lastAnimationTime) ? lastFrameScaleIndex : 0;
				while( frame < channel->mNumScalingKeys - 1)
				{
					if( currentTime < channel->mScalingKeys[frame+1].mTime)
						break;
					frame++;
				}

				// TODO: (thom) interpolation maybe? This time maybe even logarithmic, not linear
				presentScaling = channel->mScalingKeys[frame].mValue;
			}

			// build a transformation matrix from it
			//aiMatrix4x4& mat;// = mTransforms[a];
			aiMatrix4x4 mat = aiMatrix4x4( presentRotation.GetMatrix());
			mat.a1 *= presentScaling.x; mat.b1 *= presentScaling.x; mat.c1 *= presentScaling.x;
			mat.a2 *= presentScaling.y; mat.b2 *= presentScaling.y; mat.c2 *= presentScaling.y;
			mat.a3 *= presentScaling.z; mat.b3 *= presentScaling.z; mat.c3 *= presentScaling.z;
			mat.a4 = presentPosition.x; mat.b4 = presentPosition.y; mat.c4 = presentPosition.z;
			//mat.Transpose();

			targetNode->mTransformation = mat;

		}
	}
	//----

	vector< AssimpNodeRef >::const_iterator it = mMeshNodes.begin();
	int mId = 0;
	for ( ; it != mMeshNodes.end(); ++it, mId++ )
	{
		AssimpNodeRef nodeRef = *it;

		gl::pushModelView();

		// TODO: node transform
		//gl::multModelView( nodeRef->getDerivedTransform() );
		/*
		Matrix44f accTransform;
		AssimpNodeRef nRef = nodeRef;
		do
		{
			accTransform = nRef->mTransform * accTransform;
			nRef = dynamic_pointer_cast< AssimpNode, Node >( nRef->getParent() );
		} while ( nRef );
		gl::multModelView( accTransform );
		*/

		/*
		const aiNode *nd = nodeRef->mAiNode;
		aiMatrix4x4 trafo;
		while ( nd != NULL )
		{
			//trafo = trafo * nd->mTransformation;
			trafo = nd->mTransformation * trafo;
			nd = nd->mParent;
		};
		gl::multModelView( fromAssimp( trafo ) );
		//gl::multModelView( fromAssimp( nodeRef->mAiNode->mTransformation ) );
		*/

		vector< AssimpMeshHelperRef >::const_iterator meshIt = nodeRef->mMeshes.begin();
		for ( ; meshIt != nodeRef->mMeshes.end(); ++meshIt )
		{
			AssimpMeshHelperRef meshHelperRef = *meshIt;

			if ( mEnableSkinning )
			{
				// ------------------------------------
				// current mesh we are introspecting
				const aiMesh *mesh = meshHelperRef->mAiMesh;

				// calculate bone matrices
				std::vector< aiMatrix4x4 > boneMatrices( mesh->mNumBones );
				for ( unsigned a = 0; a < mesh->mNumBones; ++a )
				{
					const aiBone *bone = mesh->mBones[ a ];

					// find the corresponding node by again looking recursively through
					// the node hierarchy for the same name
					aiNode *node = mScene->mRootNode->FindNode( bone->mName );

					// start with the mesh-to-bone matrix
					boneMatrices[ a ] = bone->mOffsetMatrix;
					// and now append all node transformations down the parent chain until
					// we're back at mesh coordinates again
					const aiNode *tempNode = node;
					while( tempNode )
					{
						// check your matrix multiplication order here!!!
						boneMatrices[ a ] = tempNode->mTransformation * boneMatrices[ a ];
						tempNode = tempNode->mParent;
					}
				}

				meshHelperRef->mHasChanged = true;
				meshHelperRef->mValidCache = false;

				meshHelperRef->mAnimatedPos.assign( meshHelperRef->mAnimatedPos.size(),
						aiVector3D( 0, 0, 0 ) );
				if ( mesh->HasNormals() )
				{
					meshHelperRef->mAnimatedNorm.assign( meshHelperRef->mAnimatedNorm.size(),
							aiVector3D( 0, 0, 0 ) );
				}

				// loop through all vertex weights of all bones
				for ( unsigned a = 0; a < mesh->mNumBones; ++a )
				{
					const aiBone *bone = mesh->mBones[a];
					const aiMatrix4x4 &posTrafo = boneMatrices[ a ];

					for ( unsigned b = 0; b < bone->mNumWeights; ++b )
					{
						const aiVertexWeight &weight = bone->mWeights[ b ];
						size_t vertexId = weight.mVertexId;
						const aiVector3D& srcPos = mesh->mVertices[vertexId];

						meshHelperRef->mAnimatedPos[vertexId] += weight.mWeight * (posTrafo * srcPos);
					}

					if(mesh->HasNormals())
					{
						// 3x3 matrix, contains the bone matrix without the
						// translation, only with rotation and possibly scaling
						aiMatrix3x3 normTrafo = aiMatrix3x3( posTrafo );
						for ( size_t b = 0; b < bone->mNumWeights; ++b )
						{
							const aiVertexWeight& weight = bone->mWeights[b];
							size_t vertexId = weight.mVertexId;

							const aiVector3D& srcNorm = mesh->mNormals[vertexId];
							meshHelperRef->mAnimatedNorm[vertexId] += weight.mWeight * (normTrafo * srcNorm);
						}
					}
				}

				// update slow
				std::vector< Vec3f > &vertices = meshHelperRef->mCachedTriMesh.getVertices();
				for( size_t v = 0; v < vertices.size(); ++v )
					vertices[v] = fromAssimp( meshHelperRef->mAnimatedPos[ v ] );

				std::vector< Vec3f > &normals = meshHelperRef->mCachedTriMesh.getNormals();
				for( size_t v = 0; v < normals.size(); ++v )
					normals[v] = fromAssimp( meshHelperRef->mAnimatedNorm[ v ] );
			}
			else
			{
				const aiMesh *mesh = meshHelperRef->mAiMesh;

				// update slow
				std::vector< Vec3f > &vertices = meshHelperRef->mCachedTriMesh.getVertices();
				for( size_t v = 0; v < vertices.size(); ++v )
					vertices[v] = fromAssimp( mesh->mVertices[ v ] );

				std::vector< Vec3f > &normals = meshHelperRef->mCachedTriMesh.getNormals();
				for( size_t v = 0; v < normals.size(); ++v )
					normals[v] = fromAssimp( mesh->mNormals[ v ] );
			}

			// ------------------------------------
			if ( mId == 1 )
			{
				std::vector< Vec3f > &vertices = meshHelperRef->mCachedTriMesh.getVertices();
				app::console() << vertices[0] << " " << vertices.size() << endl;
			}

			// Texture Binding
			if ( mUsingTextures && meshHelperRef->mTexture )
			{
				meshHelperRef->mTexture.enableAndBind();
			}

			if ( mUsingMaterials )
			{
				meshHelperRef->mMaterial.apply();
			}

			// Culling
			if ( meshHelperRef->mTwoSided )
				gl::enable( GL_CULL_FACE );
			else
				gl::disable( GL_CULL_FACE );

			gl::draw( meshHelperRef->mCachedTriMesh );

			// Texture Binding
			if ( mUsingTextures && meshHelperRef->mTexture )
			{
				meshHelperRef->mTexture.unbind();
			}
		}
		gl::popModelView();
	}

#if 0
	vector< AssimpMeshHelperRef >::const_iterator it = mModelMeshes.begin();
	for ( ; it != mModelMeshes.end(); ++it )
	{
		AssimpMeshHelperRef meshHelperRef = *it;

		// Texture Binding
		if ( mUsingTextures && meshHelperRef->mTexture )
		{
			meshHelperRef->mTexture.enableAndBind();
		}

		if ( mUsingMaterials )
		{
			meshHelperRef->mMaterial.apply();
		}

		// Culling
		if ( meshHelperRef->mTwoSided )
			gl::enable( GL_CULL_FACE );
		else
			gl::disable( GL_CULL_FACE );

		gl::draw( meshHelperRef->mCachedTriMesh );

		/*
		ofEnableBlendMode(meshHelper.blendMode);
#ifndef TARGET_OPENGLES
		meshHelper.vbo.drawElements(GL_TRIANGLES,meshHelper.indices.size());
#else
		switch(renderType){
			case OF_MESH_FILL:
				meshHelper.vbo.drawElements(GL_TRIANGLES,meshHelper.indices.size());
				break;
			case OF_MESH_WIREFRAME:
				meshHelper.vbo.drawElements(GL_LINES,meshHelper.indices.size());
				break;
			case OF_MESH_POINTS:
				meshHelper.vbo.drawElements(GL_POINTS,meshHelper.indices.size());
				break;
		}
#endif

		*/
		// Texture Binding
		if ( mUsingTextures && meshHelperRef->mTexture )
		{
			meshHelperRef->mTexture.unbind();
		}

		/*
		if(bUsingMaterials){
			meshHelper.material.end();
		}
		*/
	}
#endif

	//ofPopMatrix();

	glPopClientAttrib();
	glPopAttrib();
	//ofPopStyle();
}

} } // namespace mndl::assimp


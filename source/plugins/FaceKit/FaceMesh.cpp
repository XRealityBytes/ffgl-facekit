#include "FaceMesh.h"

#include <cmath>
#include <cstring>

void FaceMesh::ComputeNormals()
{
	for( auto& n : m_normals ) n = glm::vec3( 0.f );

	const std::vector<unsigned int>& indices = m_model.indices;
	for( size_t t = 0; t + 2 < indices.size(); t += 3 )
	{
		const unsigned int i0 = indices[t + 0];
		const unsigned int i1 = indices[t + 1];
		const unsigned int i2 = indices[t + 2];
		glm::vec3 fn = glm::cross( m_final[i1] - m_final[i0], m_final[i2] - m_final[i0] );
		m_normals[i0] += fn;
		m_normals[i1] += fn;
		m_normals[i2] += fn;
	}
	for( auto& n : m_normals )
	{
		float len = glm::length( n );
		if( len > 1e-6f ) n /= len;
	}
}

void FaceMesh::AdoptModel( FaceModelData&& model )
{
	m_model = std::move( model );
	const size_t vertexCount = m_model.basePositions.size();
	m_idBlended = m_model.basePositions;
	m_final     = m_model.basePositions;
	m_normals.assign( vertexCount, glm::vec3( 0.f ) );
	m_dynData.resize( vertexCount );
	m_loaded = m_model.IsValid();
}

void FaceMesh::Clear()
{
	m_model = FaceModelData();
	m_idBlended.clear();
	m_final.clear();
	m_normals.clear();
	m_dynData.clear();
	m_loaded = false;
}

// ── GL resources ──────────────────────────────────────────────────────────────
bool FaceMesh::InitGL()
{
	if( !m_loaded || m_glReady ) return m_glReady;
	const int nv = (int)m_model.basePositions.size();

	glGenBuffers( 1, &m_dynVBO );
	glBindBuffer( GL_ARRAY_BUFFER, m_dynVBO );
	glBufferData( GL_ARRAY_BUFFER, nv * (GLsizeiptr)sizeof( DynVertex ), nullptr, GL_DYNAMIC_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	glGenBuffers( 1, &m_staticVBO );
	glBindBuffer( GL_ARRAY_BUFFER, m_staticVBO );
	glBufferData( GL_ARRAY_BUFFER, nv * (GLsizeiptr)sizeof( float ), m_model.submeshIds.data(), GL_STATIC_DRAW );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	glGenVertexArrays( 1, &m_vao );
	glBindVertexArray( m_vao );

	glBindBuffer( GL_ARRAY_BUFFER, m_dynVBO );
	glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( DynVertex ),
	                       (void*)offsetof( DynVertex, pos ) );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, sizeof( DynVertex ),
	                       (void*)offsetof( DynVertex, normal ) );
	glEnableVertexAttribArray( 1 );

	glBindBuffer( GL_ARRAY_BUFFER, m_staticVBO );
	glVertexAttribPointer( 2, 1, GL_FLOAT, GL_FALSE, sizeof( float ), (void*)0 );
	glEnableVertexAttribArray( 2 );

	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	for( int lv = 0; lv < NUM_LEVELS; ++lv )
	{
		m_componentBuffers[lv].clear();
		m_componentBuffers[lv].resize( m_model.components.size() );
		for( size_t componentIndex = 0; componentIndex < m_model.components.size(); ++componentIndex )
		{
			const std::vector<unsigned int>& componentFaces = m_model.components[componentIndex].levelFaces[lv];
			if( componentFaces.empty() )
				continue;

			ComponentDrawBuffers& drawBuffers = m_componentBuffers[lv][componentIndex];
			drawBuffers.indexCount = (GLsizei)componentFaces.size();
			glGenBuffers( 1, &drawBuffers.ebo );
			glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, drawBuffers.ebo );
			glBufferData( GL_ELEMENT_ARRAY_BUFFER,
			              componentFaces.size() * sizeof( unsigned int ),
			              componentFaces.data(), GL_STATIC_DRAW );
		}
	}
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

	m_glReady = true;
	return true;
}

void FaceMesh::DeInitGL()
{
	if( !m_glReady ) return;
	for( int lv = 0; lv < NUM_LEVELS; ++lv )
	{
		for( size_t componentIndex = 0; componentIndex < m_componentBuffers[lv].size(); ++componentIndex )
		{
			if( m_componentBuffers[lv][componentIndex].ebo )
				glDeleteBuffers( 1, &m_componentBuffers[lv][componentIndex].ebo );
		}
		m_componentBuffers[lv].clear();
	}
	if( m_vao       ) { glDeleteVertexArrays( 1, &m_vao       ); m_vao       = 0; }
	if( m_dynVBO    ) { glDeleteBuffers( 1, &m_dynVBO    ); m_dynVBO    = 0; }
	if( m_staticVBO ) { glDeleteBuffers( 1, &m_staticVBO ); m_staticVBO = 0; }
	m_glReady = false;
}

// ── Per-frame update ──────────────────────────────────────────────────────────
void FaceMesh::Update( const float* exprWeights, const float* idWeights, bool reblendIdentity )
{
	if( !m_loaded ) return;
	const int nv = (int)m_model.basePositions.size();

	// Identity blend (expensive — only when weights changed)
	if( reblendIdentity && m_model.supportsIdentity )
	{
		m_idBlended = m_model.basePositions;
		for( int id = 0; id < NUM_IDENTITIES; id++ )
		{
			if( id >= (int)m_model.identityDeltas.size() )
				break;
			float w = idWeights[id];
			if( fabsf( w ) < 1e-6f ) continue;
			const auto& d = m_model.identityDeltas[id];
			for( int v = 0; v < nv; v++ )
				m_idBlended[v] += w * d[v];
		}
	}

	// Expression blend (every frame)
	m_final = m_idBlended;
	for( int e = 0; e < NUM_EXPRESSIONS; e++ )
	{
		if( e >= (int)m_model.expressionDeltas.size() )
			break;
		float w = exprWeights[e];
		if( fabsf( w ) < 1e-6f ) continue;
		const auto& d = m_model.expressionDeltas[e];
		for( int v = 0; v < nv; v++ )
			m_final[v] += w * d[v];
	}

	ComputeNormals();

	// Pack into staging buffer
	for( int v = 0; v < nv; v++ )
	{
		m_dynData[v].pos    = m_final[v];
		m_dynData[v].normal = m_normals[v];
	}

	// Upload
	if( m_glReady )
	{
		glBindBuffer( GL_ARRAY_BUFFER, m_dynVBO );
		glBufferSubData( GL_ARRAY_BUFFER, 0, nv * (GLsizeiptr)sizeof( DynVertex ), m_dynData.data() );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
	}
}

void FaceMesh::Draw( int renderLevel, const bool* componentEnabled, size_t componentEnabledCount ) const
{
	if( !m_glReady || renderLevel < 0 || renderLevel >= NUM_LEVELS ) return;

	glBindVertexArray( m_vao );
	const std::vector<ComponentDrawBuffers>& levelBuffers = m_componentBuffers[renderLevel];
	for( size_t componentIndex = 0; componentIndex < levelBuffers.size(); ++componentIndex )
	{
		const ComponentDrawBuffers& drawBuffers = levelBuffers[componentIndex];
		if( drawBuffers.ebo == 0 || drawBuffers.indexCount <= 0 )
			continue;

		const bool enabled = componentIndex >= componentEnabledCount || componentEnabled == nullptr
		                   || componentEnabled[componentIndex];
		if( !enabled )
			continue;

		glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, drawBuffers.ebo );
		glDrawElements( GL_TRIANGLES, drawBuffers.indexCount, GL_UNSIGNED_INT, nullptr );
	}
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	glBindVertexArray( 0 );
}

#pragma once
#include <FFGLSDK.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "FaceModel.h"

class FaceMesh
{
public:
	static constexpr int NUM_LEVELS      = 3;  // 0=Face, 1=Face+Eyes, 2=Full

	FaceMesh()  = default;
	~FaceMesh() { DeInitGL(); }

	void AdoptModel( FaceModelData&& model );
	void Clear();

	// Create GPU resources. Call inside a valid GL context.
	bool InitGL();
	void DeInitGL();

	// Blend weights and upload to GPU.
	// reblendIdentity=true triggers a full identity re-blend (expensive but infrequent).
	void Update( const float* exprWeights, const float* idWeights, bool reblendIdentity );

	// Draw sub-set: 0=Face, 1=Face+Eyes, 2=Full
	void Draw( int renderLevel, const bool* componentEnabled, size_t componentEnabledCount ) const;

	bool               IsLoaded()             const { return m_loaded;  }
	bool               IsGLReady()            const { return m_glReady; }
	bool               SupportsIdentity()     const { return m_model.supportsIdentity; }
	bool               HasBounds()            const { return m_model.hasBounds; }
	bool               HasFocusPoint()        const { return m_model.hasFocusPoint; }
	size_t             GetComponentCount()    const { return m_model.components.size(); }
	const std::string& GetStatusText()        const { return m_model.statusText; }
	const std::vector<std::string>& GetWarnings() const { return m_model.warnings; }
	const std::vector<FaceModelComponent>& GetComponents() const { return m_model.components; }
	const glm::vec3&   GetBoundsMin()         const { return m_model.boundsMin; }
	const glm::vec3&   GetBoundsMax()         const { return m_model.boundsMax; }
	const glm::vec3&   GetBoundsCenter()      const { return m_model.boundsCenter; }
	const glm::vec3&   GetFocusPoint()        const { return m_model.focusPoint; }
	float              GetBoundingRadius()    const { return m_model.boundingRadius; }

private:
	struct DynVertex { glm::vec3 pos; glm::vec3 normal; };

	void ComputeNormals();

	bool m_loaded  = false;
	bool m_glReady = false;
	FaceModelData m_model;

	// CPU data
	std::vector<glm::vec3> m_idBlended;
	std::vector<glm::vec3> m_final;
	std::vector<glm::vec3> m_normals;
	std::vector<DynVertex> m_dynData;

	// GL objects
	struct ComponentDrawBuffers
	{
		GLuint  ebo = 0;
		GLsizei indexCount = 0;
	};

	GLuint m_dynVBO    = 0;  // interleaved pos+normal, updated each frame
	GLuint m_staticVBO = 0;  // submesh IDs, static
	GLuint m_vao       = 0;
	std::vector<ComponentDrawBuffers> m_componentBuffers[NUM_LEVELS];
};

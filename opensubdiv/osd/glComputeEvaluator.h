//
//   Copyright 2015 Pixar
//
//   Licensed under the Apache License, Version 2.0 (the "Apache License")
//   with the following modification; you may not use this file except in
//   compliance with the Apache License and the following modification to it:
//   Section 6. Trademarks. is deleted and replaced with:
//
//   6. Trademarks. This License does not grant permission to use the trade
//      names, trademarks, service marks, or product names of the Licensor
//      and its affiliates, except as required to comply with Section 4(c) of
//      the License and to reproduce the content of the NOTICE file.
//
//   You may obtain a copy of the Apache License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the Apache License with the above modification is
//   distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//   KIND, either express or implied. See the Apache License for the specific
//   language governing permissions and limitations under the Apache License.
//

#ifndef OPENSUBDIV3_OSD_GL_COMPUTE_EVALUATOR_H
#define OPENSUBDIV3_OSD_GL_COMPUTE_EVALUATOR_H

#include "../version.h"

#include "../osd/opengl.h"
#include "../osd/types.h"
#include "../osd/vertexDescriptor.h"

namespace OpenSubdiv {
namespace OPENSUBDIV_VERSION {

namespace Far {
    class StencilTable;
}

namespace Osd {

/// \brief GL stencil table (Shader Storage buffer)
///
/// This class is a GLSL SSBO representation of Far::StencilTable.
///
/// GLSLComputeKernel consumes this table to apply stencils
///
class GLStencilTableSSBO {
public:
    static GLStencilTableSSBO *Create(Far::StencilTable const *stencilTable,
                                       void *deviceContext = NULL) {
        (void)deviceContext;  // unused
        return new GLStencilTableSSBO(stencilTable);
    }

    explicit GLStencilTableSSBO(Far::StencilTable const *stencilTable);
    ~GLStencilTableSSBO();

    // interfaces needed for GLSLComputeKernel
    GLuint GetSizesBuffer() const { return _sizes; }
    GLuint GetOffsetsBuffer() const { return _offsets; }
    GLuint GetIndicesBuffer() const { return _indices; }
    GLuint GetWeightsBuffer() const { return _weights; }
    int GetNumStencils() const { return _numStencils; }

private:
    GLuint _sizes;
    GLuint _offsets;
    GLuint _indices;
    GLuint _weights;
    int _numStencils;
};

// ---------------------------------------------------------------------------

class GLComputeEvaluator {
public:
    typedef bool Instantiatable;
    static GLComputeEvaluator * Create(VertexBufferDescriptor const &srcDesc,
                                       VertexBufferDescriptor const &dstDesc,
                                       void * deviceContext = NULL) {
        (void)deviceContext;  // not used
        GLComputeEvaluator *instance = new GLComputeEvaluator();
        if (instance->Compile(srcDesc, dstDesc)) return instance;
        delete instance;
        return NULL;
    }

    /// Constructor.
    GLComputeEvaluator();

    /// Destructor. note that the GL context must be made current.
    ~GLComputeEvaluator();

    /// ----------------------------------------------------------------------
    ///
    ///   Stencil evaluations with StencilTable
    ///
    /// ----------------------------------------------------------------------

    /// \brief Generic static compute function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        transparently from OsdMesh template interface.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindVBO() method returning a
    ///                       GL buffer object of source data
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindVBO() method returning a
    ///                       GL buffer object of destination data
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param stencilTable   stencil table to be applied. The table must have
    ///                       SSBO interfaces.
    ///
    /// @param instance       cached compiled instance. Clients are supposed to
    ///                       pre-compile an instance of this class and provide
    ///                       to this function. If it's null the kernel still
    ///                       compute by instantiating on-demand kernel although
    ///                       it may cause a performance problem.
    ///
    /// @param deviceContext  not used in the GLSL kernel
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER, typename STENCIL_TABLE>
    static bool EvalStencils(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        STENCIL_TABLE const *stencilTable,
        GLComputeEvaluator const *instance,
        void * deviceContext = NULL) {

        if (instance) {
            return instance->EvalStencils(srcBuffer, srcDesc,
                                          dstBuffer, dstDesc,
                                          stencilTable);
        } else {
            // Create a kernel on demand (slow)
            (void)deviceContext;  // unused
            instance = Create(srcDesc, dstDesc);
            if (instance) {
                bool r = instance->EvalStencils(srcBuffer, srcDesc,
                                                dstBuffer, dstDesc,
                                                stencilTable);
                delete instance;
                return r;
            }
            return false;
        }
    }

    /// Dispatch the GLSL compute kernel on GPU asynchronously.
    /// returns false if the kernel hasn't been compiled yet.
    template <typename SRC_BUFFER, typename DST_BUFFER, typename STENCIL_TABLE>
    bool EvalStencils(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        STENCIL_TABLE const *stencilTable) const {
        return EvalStencils(srcBuffer->BindVBO(),
                            srcDesc,
                            dstBuffer->BindVBO(),
                            dstDesc,
                            stencilTable->GetSizesBuffer(),
                            stencilTable->GetOffsetsBuffer(),
                            stencilTable->GetIndicesBuffer(),
                            stencilTable->GetWeightsBuffer(),
                            /* start = */ 0,
                            /* end   = */ stencilTable->GetNumStencils());
    }

    /// Dispatch the GLSL compute kernel on GPU asynchronously.
    /// returns false if the kernel hasn't been compiled yet.
    bool EvalStencils(GLuint srcBuffer, VertexBufferDescriptor const &srcDesc,
                      GLuint dstBuffer, VertexBufferDescriptor const &dstDesc,
                      GLuint sizesBuffer,
                      GLuint offsetsBuffer,
                      GLuint indicesBuffer,
                      GLuint weightsBuffer,
                      int start,
                      int end) const;


    /// ----------------------------------------------------------------------
    ///
    ///   Limit evaluations with PatchTable
    ///
    /// ----------------------------------------------------------------------
    ///
    /// \brief Generic limit eval function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        in the same way.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindVBO() method returning a GL
    ///                       buffer object of source data
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindVBO() method returning a GL
    ///                       buffer object of destination data
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param numPatchCoords number of patchCoords.
    ///
    /// @param patchCoords    array of locations to be evaluated.
    ///                       must have BindVBO() method returning an
    ///                       array of PatchCoord struct in VBO.
    ///
    /// @param patchTable     GLPatchTable or equivalent
    ///
    /// @param instance       cached compiled instance. Clients are supposed to
    ///                       pre-compile an instance of this class and provide
    ///                       to this function. If it's null the kernel still
    ///                       compute by instantiating on-demand kernel although
    ///                       it may cause a performance problem.
    ///
    /// @param deviceContext  not used in the GLXFB evaluator
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE>
    static bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable,
        GLComputeEvaluator const *instance,
        void * deviceContext = NULL) {

        if (instance) {
            return instance->EvalPatches(srcBuffer, srcDesc,
                                         dstBuffer, dstDesc,
                                         numPatchCoords, patchCoords,
                                         patchTable);
        } else {
            // Create an instance on demand (slow)
            (void)deviceContext;  // unused
            instance = Create(srcDesc, dstDesc);
            if (instance) {
                bool r = instance->EvalPatches(srcBuffer, srcDesc,
                                               dstBuffer, dstDesc,
                                               numPatchCoords, patchCoords,
                                               patchTable);
                delete instance;
                return r;
            }
            return false;
        }
    }

    /// \brief Generic limit eval function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        in the same way.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindVBO() method returning a GL
    ///                       buffer object of source data
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindVBO() method returning a GL
    ///                       buffer object of destination data
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param duBuffer
    ///
    /// @param duDesc
    ///
    /// @param dvBuffer
    ///
    /// @param dvDesc
    ///
    /// @param numPatchCoords number of patchCoords.
    ///
    /// @param patchCoords    array of locations to be evaluated.
    ///                       must have BindVBO() method returning an
    ///                       array of PatchCoord struct in VBO.
    ///
    /// @param patchTable     GLPatchTable or equivalent
    ///
    /// @param instance       cached compiled instance. Clients are supposed to
    ///                       pre-compile an instance of this class and provide
    ///                       to this function. If it's null the kernel still
    ///                       compute by instantiating on-demand kernel although
    ///                       it may cause a performance problem.
    ///
    /// @param deviceContext  not used in the GLXFB evaluator
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE>
    static bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        DST_BUFFER *duBuffer,  VertexBufferDescriptor const &duDesc,
        DST_BUFFER *dvBuffer,  VertexBufferDescriptor const &dvDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable,
        GLComputeEvaluator const *instance,
        void * deviceContext = NULL) {

        if (instance) {
            return instance->EvalPatches(srcBuffer, srcDesc,
                                         dstBuffer, dstDesc,
                                         duBuffer, duDesc,
                                         dvBuffer, dvDesc,
                                         numPatchCoords, patchCoords,
                                         patchTable);
        } else {
            // Create an instance on demand (slow)
            (void)deviceContext;  // unused
            instance = Create(srcDesc, dstDesc);
            if (instance) {
                bool r = instance->EvalPatches(srcBuffer, srcDesc,
                                               dstBuffer, dstDesc,
                                               duBuffer, duDesc,
                                               dvBuffer, dvDesc,
                                               numPatchCoords, patchCoords,
                                               patchTable);
                delete instance;
                return r;
            }
            return false;
        }
    }

    /// \brief Generic limit eval function. This function has a same
    ///        signature as other device kernels have so that it can be called
    ///        in the same way.
    ///
    /// @param srcBuffer      Input primvar buffer.
    ///                       must have BindVBO() method returning a
    ///                       const float pointer for read
    ///
    /// @param srcDesc        vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer      Output primvar buffer
    ///                       must have BindVBOBuffer() method returning a
    ///                       float pointer for write
    ///
    /// @param dstDesc        vertex buffer descriptor for the output buffer
    ///
    /// @param numPatchCoords number of patchCoords.
    ///
    /// @param patchCoords    array of locations to be evaluated.
    ///                       must have BindVBO() method returning an
    ///                       array of PatchCoord struct in VBO.
    ///
    /// @param patchTable     GLPatchTable or equivalent
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE>
    bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable) const {

        return EvalPatches(srcBuffer->BindVBO(), srcDesc,
                           dstBuffer->BindVBO(), dstDesc,
                           0, VertexBufferDescriptor(),
                           0, VertexBufferDescriptor(),
                           numPatchCoords,
                           patchCoords->BindVBO(),
                           patchTable->GetPatchArrays(),
                           patchTable->GetPatchIndexBuffer(),
                           patchTable->GetPatchParamBuffer());
    }

    /// \brief Generic limit eval function with derivatives. This function has
    ///        a same signature as other device kernels have so that it can be
    ///        called in the same way.
    ///
    /// @param srcBuffer        Input primvar buffer.
    ///                         must have BindVBO() method returning a
    ///                         const float pointer for read
    ///
    /// @param srcDesc          vertex buffer descriptor for the input buffer
    ///
    /// @param dstBuffer        Output primvar buffer
    ///                         must have BindVBO() method returning a
    ///                         float pointer for write
    ///
    /// @param dstDesc          vertex buffer descriptor for the output buffer
    ///
    /// @param duBuffer         Output U-derivatives buffer
    ///                         must have BindVBO() method returning a
    ///                         float pointer for write
    ///
    /// @param duDesc           vertex buffer descriptor for the duBuffer
    ///
    /// @param dvBuffer         Output V-derivatives buffer
    ///                         must have BindVBO() method returning a
    ///                         float pointer for write
    ///
    /// @param dvDesc           vertex buffer descriptor for the dvBuffer
    ///
    /// @param numPatchCoords   number of patchCoords.
    ///
    /// @param patchCoords      array of locations to be evaluated.
    ///
    /// @param patchTable       GLPatchTable or equivalent
    ///
    template <typename SRC_BUFFER, typename DST_BUFFER,
              typename PATCHCOORD_BUFFER, typename PATCH_TABLE>
    bool EvalPatches(
        SRC_BUFFER *srcBuffer, VertexBufferDescriptor const &srcDesc,
        DST_BUFFER *dstBuffer, VertexBufferDescriptor const &dstDesc,
        DST_BUFFER *duBuffer,  VertexBufferDescriptor const &duDesc,
        DST_BUFFER *dvBuffer,  VertexBufferDescriptor const &dvDesc,
        int numPatchCoords,
        PATCHCOORD_BUFFER *patchCoords,
        PATCH_TABLE *patchTable) const {

        return EvalPatches(srcBuffer->BindVBO(), srcDesc,
                           dstBuffer->BindVBO(), dstDesc,
                           duBuffer->BindVBO(),  duDesc,
                           dvBuffer->BindVBO(),  dvDesc,
                           numPatchCoords,
                           patchCoords->BindVBO(),
                           patchTable->GetPatchArrays(),
                           patchTable->GetPatchIndexBuffer(),
                           patchTable->GetPatchParamBuffer());
    }

    bool EvalPatches(GLuint srcBuffer, VertexBufferDescriptor const &srcDesc,
                     GLuint dstBuffer, VertexBufferDescriptor const &dstDesc,
                     GLuint duBuffer, VertexBufferDescriptor const &duDesc,
                     GLuint dvBuffer, VertexBufferDescriptor const &dvDesc,
                     int numPatchCoords,
                     GLuint patchCoordsBuffer,
                     const PatchArrayVector &patchArrays,
                     GLuint patchIndexBuffer,
                     GLuint patchParamsBuffer) const;

    /// ----------------------------------------------------------------------
    ///
    ///   Other methods
    ///
    /// ----------------------------------------------------------------------

    /// Configure GLSL kernel. A valid GL context must be made current before
    /// calling this function. Returns false if it fails to compile the kernel.
    bool Compile(VertexBufferDescriptor const &srcDesc,
                 VertexBufferDescriptor const &dstDesc);

    /// Wait the dispatched kernel finishes.
    static void Synchronize(void *deviceContext);

private:
    struct _StencilKernel {
        GLuint program;
        GLuint uniformSizes;
        GLuint uniformOffsets;
        GLuint uniformIndices;
        GLuint uniformWeights;
        GLuint uniformStart;
        GLuint uniformEnd;
        GLuint uniformSrcOffset;
        GLuint uniformDstOffset;
    } _stencilKernel;

    struct _PatchKernel {
        GLuint program;
        GLuint uniformSrcOffset;
        GLuint uniformDstOffset;
        GLuint uniformPatchArray;
        GLuint uniformDuDesc;
        GLuint uniformDvDesc;

    } _patchKernel;

    int _workGroupSize;
};

}  // end namespace Osd

}  // end namespace OPENSUBDIV_VERSION
using namespace OPENSUBDIV_VERSION;

}  // end namespace OpenSubdiv


#endif  // OPENSUBDIV3_OSD_GL_COMPUTE_EVALUATOR_H
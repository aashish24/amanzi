/*
  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL.
  Amanzi is released under the three-clause BSD License.
  The terms of use and "as is" disclaimer for this license are
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#ifndef AMANZI_OPERATOR_PDE_DIFFUSION_FRACTURED_MATRIX_HH_
#define AMANZI_OPERATOR_PDE_DIFFUSION_FRACTURED_MATRIX_HH_

#include "Epetra_IntVector.h"
#include "Teuchos_RCP.hpp"

#include "PDE_DiffusionMFD.hh"

namespace Amanzi {
namespace Operators {

class PDE_DiffusionFracturedMatrix : public PDE_DiffusionMFD {
 public:
  PDE_DiffusionFracturedMatrix(Teuchos::ParameterList& plist,
                               const Teuchos::RCP<const AmanziMesh::Mesh>& mesh) :
      PDE_Diffusion(mesh),
      PDE_DiffusionMFD(plist, mesh)
  {
    operator_type_ = OPERATOR_DIFFUSION_FRACTURED_MATRIX;
  }

  // main interface members
  virtual void Init(Teuchos::ParameterList& plist) override;

  virtual void UpdateMatrices(const Teuchos::Ptr<const CompositeVector>& flux,
                              const Teuchos::Ptr<const CompositeVector>& u) override;

  // modify matrix due to boundary conditions 
  //    primary=true indicates that the operator updates both matrix and right-hand
  //      side using BC data. If primary=false, only matrix is changed.
  //    eliminate=true indicates that we eliminate essential BCs for a trial 
  //      function, i.e. zeros go in the corresponding matrix columns and 
  //      right-hand side is modified using BC values. This is the optional 
  //      parameter that enforces symmetry for a symmetric tree operators.
  //    essential_eqn=true indicates that the operator places a positive number on 
  //      the main matrix diagonal for the case of essential BCs. This is the
  //      implementation trick.
  virtual void ApplyBCs(bool primary, bool eliminate, bool essential_eqn) override;

 private:
  int FaceLocalIndex_(int c, int f, const Epetra_BlockMap& cmap);

 private:
  Teuchos::RCP<CompositeVectorSpace> cvs_;
};


// non-member functions
Teuchos::RCP<CompositeVectorSpace> CreateFracturedMatrixCVS(
    const Teuchos::RCP<const AmanziMesh::Mesh>& mesh,
    const Teuchos::RCP<const AmanziMesh::Mesh>& fracture);

}  // namespace Operators
}  // namespace Amanzi


#endif
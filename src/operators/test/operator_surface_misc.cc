/*
  This is the operators component of the Amanzi code. 

  Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "UnitTest++.h"

#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_ParameterXMLFileReader.hpp"

#include "MeshFactory.hh"
#include "Mesh_MSTK.hh"
#include "GMVMesh.hh"

#include "tensor.hh"
#include "mfd3d_diffusion.hh"

#include "LinearOperatorFactory.hh"
#include "OperatorDefs.hh"
#include "OperatorDiffusionSurface.hh"
#include "OperatorAccumulation.hh"
#include "OperatorAdvection.hh"


/* *****************************************************************
* This test replaves tensor and boundary conditions by continuous
* functions. This is a prototype for future solvers.
* **************************************************************** */
TEST(SURFACE_MISC) {
  using namespace Teuchos;
  using namespace Amanzi;
  using namespace Amanzi::AmanziMesh;
  using namespace Amanzi::AmanziGeometry;
  using namespace Amanzi::Operators;

  Epetra_MpiComm comm(MPI_COMM_WORLD);
  int MyPID = comm.MyPID();

  if (MyPID == 0) std::cout << "\nTest: Advection-duffusion on a surface" << std::endl;

  // read parameter list
  std::string xmlFileName = "test/operator_surface_misc.xml";
  ParameterXMLFileReader xmlreader(xmlFileName);
  ParameterList plist = xmlreader.getParameters();

  // create an SIMPLE mesh framework
  ParameterList region_list = plist.get<Teuchos::ParameterList>("Regions");
  GeometricModelPtr gm = new GeometricModel(2, region_list, &comm);

  FrameworkPreference pref;
  pref.clear();
  pref.push_back(MSTK);

  MeshFactory meshfactory(&comm);
  meshfactory.preference(pref);
  RCP<const Mesh> mesh = meshfactory(0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 40, 40, 5, gm);
  RCP<const Mesh_MSTK> mesh_mstk = rcp_static_cast<const Mesh_MSTK>(mesh);

  // extract surface mesh
  std::vector<std::string> setnames;
  setnames.push_back(std::string("Top surface"));

  RCP<Mesh> surfmesh = Teuchos::rcp(new Mesh_MSTK(*mesh_mstk, setnames, AmanziMesh::FACE));

  /* modify diffusion coefficient */
  std::vector<WhetStone::Tensor> K;
  int ncells_owned = surfmesh->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);
  int nfaces_wghost = surfmesh->num_entities(AmanziMesh::FACE, AmanziMesh::USED);

  for (int c = 0; c < ncells_owned; c++) {
    WhetStone::Tensor Kc(2, 1);
    Kc(0, 0) = 1.0;
    K.push_back(Kc);
  }

  // create boundary data
  std::vector<int> bc_model(nfaces_wghost, OPERATOR_BC_NONE);
  std::vector<double> bc_values(nfaces_wghost);

  for (int f = 0; f < nfaces_wghost; f++) {
    const Point& xf = surfmesh->face_centroid(f);
    if (fabs(xf[0]) < 1e-6 || fabs(xf[0] - 1.0) < 1e-6 ||
        fabs(xf[1]) < 1e-6 || fabs(xf[1] - 1.0) < 1e-6) {
      bc_model[f] = OPERATOR_BC_FACE_DIRICHLET;
      bc_values[f] = xf[1] * xf[1];
    }
  }

  // create operator map 
  Teuchos::RCP<CompositeVectorSpace> cvs = Teuchos::rcp(new CompositeVectorSpace());
  cvs->SetMesh(surfmesh);
  cvs->SetGhosted(true);
  cvs->SetComponent("cell", AmanziMesh::CELL, 1);
  cvs->SetOwned(false);
  cvs->AddComponent("face", AmanziMesh::FACE, 1);

  // create accumulation operator
  Teuchos::RCP<OperatorAccumulation> op = Teuchos::rcp(new OperatorAccumulation(cvs, 0));
  op->Init();

  CompositeVector solution(*cvs);
  solution.PutScalar(0.0);  // solution at time T=0

  CompositeVector phi(*cvs);
  phi.PutScalar(0.2);

  double dT = 0.02;
  op->UpdateMatrices(solution, phi, dT);

  // add advection operator
  Teuchos::RCP<OperatorAdvection> op2 = Teuchos::rcp(new OperatorAdvection(*op));

  // initialize velocity
  CompositeVector u(*cvs);
  Epetra_MultiVector& uf = *u.ViewComponent("face");
  int nfaces = surfmesh->num_entities(AmanziMesh::FACE, AmanziMesh::OWNED);
  Point vel(4.0, 4.0, 0.0);
  for (int f = 0; f < nfaces; f++) {
    uf[0][f] = vel * surfmesh->face_normal(f);
  }

  // add advective matrices
  op2->InitOperator(u);
  op2->UpdateMatrices(u);

  // add the diffusion operator. It is the last due to BCs.
  Teuchos::ParameterList olist;
  Teuchos::RCP<OperatorDiffusionSurface> op3 = Teuchos::rcp(new OperatorDiffusionSurface(*op2));
  op3->InitOperator(K, Teuchos::null, olist);
  op3->UpdateMatrices(solution);
  op3->ApplyBCs(bc_model, bc_values);

  // change preconditioner to default
  int schema = Operators::OPERATOR_SCHEMA_DOFS_FACE + 
               Operators::OPERATOR_SCHEMA_DOFS_CELL;
  Teuchos::RCP<Operator> op4 = Teuchos::rcp(new Operator(*op3));

  op4->SymbolicAssembleMatrix(schema);
  op4->AssembleMatrix(schema);

  ParameterList slist = plist.get<Teuchos::ParameterList>("Preconditioners");
  op4->InitPreconditioner("Hypre AMG", slist);

  // solve the problem
  ParameterList lop_list = plist.get<Teuchos::ParameterList>("Solvers");
  AmanziSolvers::LinearOperatorFactory<Operator, CompositeVector, CompositeVectorSpace> factory;
  Teuchos::RCP<AmanziSolvers::LinearOperator<Operator, CompositeVector, CompositeVectorSpace> >
     solver = factory.Create("AztecOO CG", lop_list, op4);

  CompositeVector& rhs = *op4->rhs();
  int ierr = solver->ApplyInverse(rhs, solution);

  if (MyPID == 0) {
    std::cout << "pressure solver (" << solver->name() 
              << "): ||r||=" << solver->residual() << " itr=" << solver->num_itrs()
              << " code=" << ierr << std::endl;

    // visualization
    const Epetra_MultiVector& p = *solution.ViewComponent("cell");
    GMV::open_data_file(*surfmesh, (std::string)"surface.gmv");
    GMV::start_data();
    GMV::write_cell_data(p, 0, "solution");
    GMV::close_data_file();
  }
}
/*
  Operators

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)

  Nonlinear diffusion equation.
*/

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// TPLs
#include "Teuchos_ParameterList.hpp"
#include "UnitTest++.h"

// Amanzi
#include "SolverNewton.hh"
#include "SolverNKA.hh"

// Operators
#include "OperatorDefs.hh"
#include "Mini_Diffusion1D.hh"

using namespace Amanzi;
using namespace Amanzi::Operators;
using namespace Amanzi::WhetStone;

class NonlinearProblem : public AmanziSolvers::SolverFnBase<DenseVector> {
 public:
  // constructor keeps static data
  NonlinearProblem(Teuchos::RCP<Operators::Mini_Diffusion1D> op,
                   const DenseVector& rhs,
                   double bcl, int type_l, double bcr, int type_r) {
    op_ = op;
    bcl_ = bcl;
    type_l_ = type_l;
    bcr_ = bcr;
    type_r_ = type_r;
    rhs0_ = rhs;
  }

  void Residual(const Teuchos::RCP<DenseVector>& u,
                const Teuchos::RCP<DenseVector>& f) {
    op_->rhs() = rhs0_;

    for (int i = 0; i < u->NumRows(); ++i) {
      op_->k()(i) = 1.0 + (*u)(i) * (*u)(i);
    }

    op_->UpdateMatrices();
    op_->ApplyBCs(bcl_, type_l_, bcr_, type_r_);

    op_->Apply(*u, *f);
    f->Update(-1.0, op_->rhs(), 1.0);
  }

  int ApplyPreconditioner(const Teuchos::RCP<const DenseVector>& u,
                          const Teuchos::RCP<DenseVector>& hu) {
    op_->ApplyInverse(*u, *hu);
    return 0;
  }

  double ErrorNorm(const Teuchos::RCP<const DenseVector>& u,
                   const Teuchos::RCP<const DenseVector>& du) {
    double tmp;
    du->NormInf(&tmp);
    return tmp;
  }

  void UpdatePreconditioner(const Teuchos::RCP<const DenseVector>& u) {
    for (int i = 0; i < u->NumRows(); ++i) {
      op_->k()(i) = 1.0 + (*u)(i) * (*u)(i);
      op_->dkdp()(i) = 2.0 * (*u)(i);
    }
    op_->UpdateJacobian(*u, bcl_, type_l_, bcr_, type_r_);
  }
  void ChangedSolution() {};

 private:
  Teuchos::RCP<Amanzi::Operators::Mini_Diffusion1D> op_;
  double bcl_, bcr_;
  int type_l_, type_r_;
  DenseVector rhs0_;
};


/* *****************************************************************
* This test nonlinear diffusion solver in 1D: u(x) = x^2, k(u) = 1 + u^2.
* **************************************************************** */
void MiniDiffusion1D_Nonlinear(double bcl, int type_l, double bcr, int type_r) {
  std::cout << "\nTest: 1D nonlinear elliptic problem: constant absolute K" << std::endl;

  double pl2_err[2], ph1_err[2];
  for (int loop = 0; loop < 2; ++loop) {
    int ncells = (loop + 1) * 30;
    double length(1.0);
    auto mesh = std::make_shared<DenseVector>(DenseVector(ncells + 1));
    // make a non-uniform mesh
    double h = length / ncells;
    for (int i = 0; i < ncells + 1; ++i) (*mesh)(i) = h * i;
    // for (int i = 1; i < ncells; ++i) (*mesh)(i) += h * std::sin(3 * h * i) / 4;

    // create nonlinear diffusion operator with Ka = 1, kr(u) = u^2
    auto op = Teuchos::rcp(new Operators::Mini_Diffusion1D());
    op->Init(mesh, "planar", 1.0, 1.0);

    auto Ka = std::make_shared<DenseVector>(DenseVector(ncells));
    auto kr = std::make_shared<DenseVector>(DenseVector(ncells));
    auto dkdu = std::make_shared<DenseVector>(DenseVector(ncells));
    auto sol = Teuchos::rcp(new DenseVector(ncells));

    for (int i = 0; i < ncells; ++i) {
      double xc = op->mesh_cell_centroid(i);
      (*Ka)(i) = 1.0;
      (*kr)(i) = 2.0;
      (*sol)(i) = 1.0;
      (*dkdu)(i) = 2.0;
    }
    op->Setup(Ka, kr, dkdu);

    // create right-hand side
    DenseVector& rhs = op->rhs();
    for (int i = 0; i < ncells; ++i) { 
      double xc = op->mesh_cell_centroid(i);
      double hc = op->mesh_cell_volume(i);
      rhs(i) = -(10 * std::pow(xc, 4) + 2) * hc;
    }

    // create the Solver
    Teuchos::ParameterList plist;
    plist.set<double>("nonlinear tolerance", 1.0e-7);
    plist.sublist("verbose object").set<std::string>("verbosity level", "high");

    auto fn = Teuchos::rcp(new NonlinearProblem(op, rhs, bcl, type_l, bcr, type_r));
    // Amanzi::AmanziSolvers::SolverNKA<DenseVector, int> newton(plist);
    Amanzi::AmanziSolvers::SolverNewton<DenseVector, int> newton(plist);
    newton.Init(fn, 1);

    // solve the problem
    newton.Solve(sol);  

    // compute error
    double hc, xc, err, pnorm(1.0), hnorm(1.0);
    pl2_err[loop] = 0.0; 
    ph1_err[loop] = 0.0;

    for (int i = 0; i < ncells; ++i) {
      hc = op->mesh_cell_volume(i);
      xc = op->mesh_cell_centroid(i);
      err = xc * xc - (*sol)(i);

      pl2_err[loop] += err * err * hc;
      pnorm += xc * xc * hc;
    }

    pl2_err[loop] = std::pow(pl2_err[loop] / pnorm, 0.5);
    ph1_err[loop] = std::pow(ph1_err[loop] / hnorm, 0.5);
    printf("BCs:%2d%2d  L2(p)=%9.6f H1(p)=%9.6f\n", type_l, type_r, pl2_err[loop], ph1_err[loop]);

    CHECK(pl2_err[loop] < 1e-2);
  }
  CHECK(pl2_err[0] / pl2_err[1] > 3.7);
}


TEST(OPERATOR_MINI_DIFFUSION_NONLINEAR) {
  int dir = Amanzi::Operators::OPERATOR_BC_DIRICHLET;
  int neu = Amanzi::Operators::OPERATOR_BC_NEUMANN;
  MiniDiffusion1D_Nonlinear(0.0, dir, 1.0, dir);
  MiniDiffusion1D_Nonlinear(0.0, dir, -4.0, neu);
}


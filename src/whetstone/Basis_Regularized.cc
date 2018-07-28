/*
  WhetStone, version 2.1
  Release name: naka-to.

  Copyright 2010-201x held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.

  Author: Konstantin Lipnikov (lipnikov@lanl.gov)

  The regularized basis for dG methods: x^k y^l / h^(k+l), where
  h is a measure of cell size.
*/

#include "Basis_Regularized.hh"

namespace Amanzi {
namespace WhetStone {

/* ******************************************************************
* Prepare scaling data for the regularized basis.
****************************************************************** */
void Basis_Regularized::Init(
    const Teuchos::RCP<const AmanziMesh::Mesh>& mesh, int c, int order)
{
  int k0 = monomial_scales_.size();

  if (k0 < order + 1) {
    order_ = order;
    d_ = mesh->space_dimension();
    double volume = mesh->cell_volume(c);

    monomial_scales_.resize(order + 1);
    for (int k = k0; k < order + 1; ++k) {
      monomial_scales_[k] = std::pow(volume, -(double)k / d_);
    }
  }
}


/* ******************************************************************
* Transformation from natural basis to my basis: A_new = R^T A_old R.
****************************************************************** */
void Basis_Regularized::BilinearFormNaturalToMy(DenseMatrix& A) const
{
  int nrows = A.NumRows();
  std::vector<double> a(nrows);

  PolynomialIterator it(d_);
  for (it.begin(); it.MonomialSetOrder() <= order_; ++it) {
    int n = it.PolynomialPosition();
    int m = it.MonomialSetOrder();
    a[n] = monomial_scales_[m];
  }

  // calculate R^T * A * R
  for (int k = 0; k < nrows; ++k) {
    for (int i = 0; i < nrows; ++i) {
      A(i, k) = A(i, k) * a[k] * a[i];
    }
  }
}


/* ******************************************************************
* Transformation from natural basis to my basis: f_new = R^T f_old.
****************************************************************** */
void Basis_Regularized::LinearFormNaturalToMy(DenseVector& f) const
{
  PolynomialIterator it(d_);
  for (it.begin(); it.MonomialSetOrder() <= order_; ++it) {
    int n = it.PolynomialPosition();
    int m = it.MonomialSetOrder();
    f(n) *= monomial_scales_[m];
  }
}


/* ******************************************************************
* Transformation of interface matrix from natural to my bases.
****************************************************************** */
void Basis_Regularized::BilinearFormNaturalToMy(
    std::shared_ptr<Basis> bl, std::shared_ptr<Basis> br, DenseMatrix& A) const
{
  int nrows = A.NumRows();
  int m(nrows / 2);
  std::vector<double> a1(m), a2(m);

  auto bll = std::dynamic_pointer_cast<Basis_Regularized>(bl);
  auto brr = std::dynamic_pointer_cast<Basis_Regularized>(br);

  PolynomialIterator it(d_);
  for (it.begin(); it.MonomialSetOrder() <= order_; ++it) {
    int n = it.PolynomialPosition();
    int m = it.MonomialSetOrder();

    a1[n] = (bll->monomial_scales())[m];
    a2[n] = (brr->monomial_scales())[m];
  }

  // calculate R^T * A * R
  for (int k = 0; k < m; ++k) {
    for (int i = 0; i < m; ++i) {
      A(i, k) = A(i, k) * a1[k] * a1[i];
      A(i, k + m) = A(i, k + m) * a1[i] * a2[k];

      A(i + m, k) = A(i + m, k) * a2[i] * a1[k];
      A(i + m, k + m) = A(i + m, k + m) * a2[i] * a2[k];
    }
  }
}


/* ******************************************************************
* Transformation from my to natural bases: v_old = R * v_new.
****************************************************************** */
void Basis_Regularized::ChangeBasisMyToNatural(DenseVector& v) const
{
  PolynomialIterator it(d_);
  for (it.begin(); it.MonomialSetOrder() <= order_; ++it) {
    int n = it.PolynomialPosition();
    int m = it.MonomialSetOrder();
    v(n) *= monomial_scales_[m];
  }
}


/* ******************************************************************
* Transformation from natural to my bases: v_new = inv(R) * v_old.
****************************************************************** */
void Basis_Regularized::ChangeBasisNaturalToMy(DenseVector& v) const
{
  PolynomialIterator it(d_);
  for (it.begin(); it.MonomialSetOrder() <= order_; ++it) {
    int n = it.PolynomialPosition();
    int m = it.MonomialSetOrder();
    v(n) /= monomial_scales_[m];
  }
}


/* ******************************************************************
* Recover polynomial in the natural basis from vector coefs of 
* coefficients in the regularized basis. 
****************************************************************** */
Polynomial Basis_Regularized::CalculatePolynomial(
    const Teuchos::RCP<const AmanziMesh::Mesh>& mesh,
    int c, int order, DenseVector& coefs) const
{
  int d = mesh->space_dimension();
  Polynomial poly(d, order);

  poly.SetPolynomialCoefficients(coefs);
  poly.set_origin(mesh->cell_centroid(c));

  for (int k = 0; k < order + 1; ++k) {
    auto& mono = poly.MonomialSet(k);
    mono *= monomial_scales_[k];
  }

  return poly;
}

}  // namespace WhetStone
}  // namespace Amanzi

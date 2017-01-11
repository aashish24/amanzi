/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
//! PolynomialFunction: a polynomial

/*
  Copyright 2010-2013 held jointly by LANS/LANL, LBNL, and PNNL. 
  Amanzi is released under the three-clause BSD License. 
  The terms of use and "as is" disclaimer for this license are 
  provided in the top-level COPYRIGHT file.
*/

/*!

A generic polynomial function is given by the following expression:

.. math::
  f(x) = \sum_{{j=0}}^n c_j (x - x_0)^{{p_j}}

where :math:`c_j` are coefficients of monomials,
:math:`p_j` are integer exponents, and :math:`x_0` is the reference point.

Example:

.. code-block:: xml

  <ParameterList name="function-polynomial">
    <Parameter name="coefficients" type="Array(double)" value="{{1.0, 1.0}}"/>
    <Parameter name="exponents" type="Array(int)" value="{{2, 4}}"/>
    <Parameter name="reference point" type="double" value="0.0"/>
  </ParameterList>

*/

#ifndef AMANZI_POLYNOMIAL_FUNCTION_HH_
#define AMANZI_POLYNOMIAL_FUNCTION_HH_

#include <vector>

#include "Function.hh"

namespace Amanzi {

class PolynomialFunction : public Function {
 public:
  PolynomialFunction(const std::vector<double> &c, const std::vector<int> &p, double x0 = 0.0);
  ~PolynomialFunction() {}
  PolynomialFunction* Clone() const { return new PolynomialFunction(*this); }
  double operator()(const std::vector<double>& x) const;

 private:
  int pmin_;
  int pmax_;
  double x0_;
  std::vector<double> c_;
};

} // namespace Amanzi

#endif  // AMANZI_POLYNOMIAL_FUNCTION_HH_

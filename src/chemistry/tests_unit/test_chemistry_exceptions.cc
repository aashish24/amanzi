/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
#include "chemistry_exception.hh"

#include <cstdlib>
#include <cmath>
#include <vector>
#include <iostream>

#include <UnitTest++.h>

#include "exceptions.hh"

SUITE(GeochemistryTests_ChemistryException) {
  TEST(TestChemistryException_default_message) {
    try {
      ChemistryException ce;
      Exceptions::amanzi_throw(ce);
    } catch (ChemistryException& e) {
      CHECK_EQUAL("CHEMISTRY_ERROR: An unknown error has occured.", e.what());
    }
  }  // end TEST()

  TEST(TestChemistryException_error_string) {
    CHECK_EQUAL("CHEMISTRY_ERROR: ", ChemistryException::kChemistryError);
  }  // end TEST()

  TEST(TestChemistryException_message) {
    try {
      ChemistryException ce("Foo bar baz.");
      Exceptions::amanzi_throw(ce);
    } catch (ChemistryException& e) {
      CHECK_EQUAL("CHEMISTRY_ERROR: Foo bar baz.", e.what());
    }
  }  // end TEST()

  TEST(TestChemistryInvalidInput_message) {
    try {
      ChemistryInvalidInput ce("Foo bar baz.");
      Exceptions::amanzi_throw(ce);
    } catch (ChemistryException& e) {
      CHECK_EQUAL("CHEMISTRY_ERROR: Foo bar baz.", e.what());
    }
  }  // end TEST()

  TEST(TestChemistryInvalidSolution_message) {
    try {
      ChemistryInvalidSolution ce("Foo bar baz.");
      Exceptions::amanzi_throw(ce);
    } catch (ChemistryException& e) {
      CHECK_EQUAL("CHEMISTRY_ERROR: Foo bar baz.", e.what());
    }
  }  // end TEST()

  TEST(TestChemistryUnrecoverableError_message) {
    try {
      ChemistryUnrecoverableError ce("Foo bar baz.");
      Exceptions::amanzi_throw(ce);
    } catch (ChemistryException& e) {
      CHECK_EQUAL("CHEMISTRY_ERROR: Foo bar baz.", e.what());
    }
  }  // end TEST()

  TEST(TestChemistryMaxIterationsReached_message) {
    try {
      ChemistryMaxIterationsReached ce("Foo bar baz.");
      Exceptions::amanzi_throw(ce);
    } catch (ChemistryException& e) {
      CHECK_EQUAL("CHEMISTRY_ERROR: Foo bar baz.", e.what());
    }
  }  // end TEST()
}  // end SUITE()

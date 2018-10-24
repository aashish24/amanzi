/* -*-  mode: c++; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------
ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

A field evaluator with no dependencies solved for by a PK.

------------------------------------------------------------------------- */

#include "evaluator/EvaluatorPrimary.hh"

namespace Amanzi {

// registry of method
template <>
Utils::RegisteredFactory<Evaluator, EvaluatorPrimary<double>>
    EvaluatorPrimary<double>::fac_("primary variable double");

template <>
Utils::RegisteredFactory<
    Evaluator, EvaluatorPrimary<CompositeVector, CompositeVectorSpace>>
    EvaluatorPrimary<CompositeVector, CompositeVectorSpace>::fac_(
        "primary variable");

} // namespace Amanzi
#include "evaluator/EvaluatorIndependentFunction.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, EvaluatorIndependentFunction>
    EvaluatorIndependentFunction::fac_("independent variable");

} // namespace Amanzi
#include "evaluator/EvaluatorIndependentTensorFunction.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, EvaluatorIndependentTensorFunction>
    EvaluatorIndependentTensorFunction::fac_("tensor independent variable");

} // namespace Amanzi
#include "evaluator/EvaluatorIndependentFromFile.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, EvaluatorIndependentFromFile>
    EvaluatorIndependentFromFile::fac_("independent variable from file");

} // namespace Amanzi
#include "evaluator/Evaluator_OperatorApply.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, Evaluator_OperatorApply>
    Evaluator_OperatorApply::fac_("operator application");

} // namespace Amanzi
#include "evaluator/Evaluator_BCs.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, Evaluator_BCs>
    Evaluator_BCs::fac_("general boundary conditions");

} // namespace Amanzi
#include "evaluator/Evaluator_BoundaryFunction.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, Evaluator_BoundaryFunction>
    Evaluator_BoundaryFunction::fac_("boundary function");

} // namespace Amanzi
#include "evaluator/EvaluatorSecondaryMonotypeAdditive.hh"
namespace Amanzi {

template<>
Utils::RegisteredFactory<Evaluator, EvaluatorSecondaryMonotypeAdditive<CompositeVector,CompositeVectorSpace>>
EvaluatorSecondaryMonotypeAdditive<CompositeVector,CompositeVectorSpace>::fac_("additive");

} // namespace Amanzi
#include "evaluator/EvaluatorSecondaryMonotypeMultiplicative.hh"
namespace Amanzi {

template<>
Utils::RegisteredFactory<Evaluator, EvaluatorSecondaryMonotypeMultiplicative<CompositeVector,CompositeVectorSpace>>
 EvaluatorSecondaryMonotypeMultiplicative<CompositeVector,CompositeVectorSpace>::fac_("multiplicative");

} // namespace Amanzi
#include "evaluator/EvaluatorCellVolume.hh"
namespace Amanzi {

Utils::RegisteredFactory<Evaluator, EvaluatorCellVolume> EvaluatorCellVolume::fac_("cell volume");

} // namespace Amanzi
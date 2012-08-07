/*
This is the flow component of the Amanzi code. 

Copyright 2010-2012 held jointly by LANS/LANL, LBNL, and PNNL. 
Amanzi is released under the three-clause BSD License. 
The terms of use and "as is" disclaimer for this license are 
provided Reconstruction.cppin the top-level COPYRIGHT file.

Authors: Neil Carlson (nnc@lanl.gov), 
         Konstantin Lipnikov (lipnikov@lanl.gov)

Usage: Richards_PK FPK(parameter_list, flow_state);
       FPK->InitPK();
       FPK->Initialize();  // optional
       FPK->InitSteadyState(T, dT);
       FPK->InitTransient(T, dT);
*/

#include <vector>

#include "Epetra_IntVector.h"
#include "Teuchos_ParameterList.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"

#include "dbc.hh"
#include "errors.hh"
#include "exceptions.hh"

#include "Mesh.hh"
#include "Point.hh"
#include "Matrix_Audit.hpp"

#include "Flow_BC_Factory.hpp"
#include "Flow_State.hpp"
#include "boundary_function.hh"
#include "Richards_PK.hpp"


namespace Amanzi {
namespace AmanziFlow {

/* ******************************************************************
* We set up only default values and call Init() routine to complete
* each variable initialization
****************************************************************** */
Richards_PK::Richards_PK(Teuchos::ParameterList& global_list, Teuchos::RCP<Flow_State> FS_MPC)
{
  Flow_PK::Init(FS_MPC);
  FS = FS_MPC;

  // extract two critical sublists 
  Teuchos::ParameterList flow_list;
  if (global_list.isSublist("Flow")) {
    flow_list = global_list.sublist("Flow");
  } else {
    Errors::Message msg("Richards PK: input parameter list does not have <Flow> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  if (flow_list.isSublist("Richards Problem")) {
    rp_list_ = flow_list.sublist("Richards Problem");
  } else {
    Errors::Message msg("Richards PK: input parameter list does not have <Richards Problem> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  if (global_list.isSublist("Preconditioners")) {
    preconditioner_list_ = global_list.sublist("Preconditioners");
  } else {
    Errors::Message msg("Richards PK: input parameter list does not have <Preconditioners> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  if (global_list.isSublist("Solvers")) {
    solver_list_ = global_list.sublist("Solvers");
  } else {
    Errors::Message msg("Richards PK: input parameter list does not have <Solvers> sublist.");
    Exceptions::amanzi_throw(msg);
  }

  mesh_ = FS->mesh();
  dim = mesh_->space_dimension();

  // Create the combined cell/face DoF map.
  super_map_ = CreateSuperMap();

  // Other fundamental physical quantaties
  rho = *(FS->fluid_density());
  mu = *(FS->fluid_viscosity());
  gravity_.init(dim);
  for (int k = 0; k < dim; k++) gravity_[k] = (*(FS->gravity()))[k];

#ifdef HAVE_MPI
  const Epetra_Comm& comm = mesh_->cell_map(false).Comm();
  MyPID = comm.MyPID();

  const Epetra_Map& source_cmap = mesh_->cell_map(false);
  const Epetra_Map& target_cmap = mesh_->cell_map(true);

  cell_importer_ = Teuchos::rcp(new Epetra_Import(target_cmap, source_cmap));

  const Epetra_Map& source_fmap = mesh_->face_map(false);
  const Epetra_Map& target_fmap = mesh_->face_map(true);

  face_importer_ = Teuchos::rcp(new Epetra_Import(target_fmap, source_fmap));
#endif

  // miscalleneous
  solver = NULL;
  bdf2_dae = NULL;
  bdf1_dae = NULL;
  block_picard = 1;
  error_control_ = FLOW_TI_ERROR_CONTROL_PRESSURE;

  ti_method_sss = FLOW_TIME_INTEGRATION_BDF1;  // time integration (TI) parameters
  ti_method_trs = FLOW_TIME_INTEGRATION_BDF2;

  mfd3d_method_ = FLOW_MFD3D_OPTIMIZED;
  mfd3d_method_preconditioner_ = FLOW_MFD3D_OPTIMIZED;

  Krel_method = FLOW_RELATIVE_PERM_UPWIND_GRAVITY;

  verbosity = FLOW_VERBOSITY_HIGH;
  internal_tests = 0;
}


/* ******************************************************************
* Clean memory.
****************************************************************** */
Richards_PK::~Richards_PK()
{
  delete super_map_;
  if (solver) delete solver;
  delete matrix_;
  delete preconditioner_;

  delete bdf2_dae;
  delete bc_pressure;
  delete bc_flux;
  delete bc_head;
  delete bc_seepage;
}


/* ******************************************************************
* Extract information from Richards Problem parameter list.
****************************************************************** */
void Richards_PK::InitPK()
{
  matrix_ = new Matrix_MFD(FS, *super_map_);
  preconditioner_ = new Matrix_MFD(FS, *super_map_);

  // Create the solution (pressure) vector.
  solution = Teuchos::rcp(new Epetra_Vector(*super_map_));
  solution_cells = Teuchos::rcp(FS->CreateCellView(*solution));
  solution_faces = Teuchos::rcp(FS->CreateFaceView(*solution));
  rhs = Teuchos::rcp(new Epetra_Vector(*super_map_));
  rhs = matrix_->rhs();  // import rhs from the matrix

  // Get solver parameters from the flow parameter list.
  ProcessParameterList();

  // Process boundary data (state may be incomplete at this moment)
  int nfaces = mesh_->num_entities(AmanziMesh::FACE, AmanziMesh::USED);
  bc_markers.resize(nfaces, FLOW_BC_FACE_NULL);
  bc_values.resize(nfaces, 0.0);

  double time = FS->get_time();
  if (time >= 0.0) T_physics = time;

  // Process other fundamental structures
  K.resize(ncells_owned);
  is_matrix_symmetric = (Krel_method == FLOW_RELATIVE_PERM_CENTERED);
  matrix_->SetSymmetryProperty(is_matrix_symmetric);
  matrix_->SymbolicAssembleGlobalMatrices(*super_map_);

  // Allocate data for relative permeability
  const Epetra_Map& cmap = mesh_->cell_map(true);
  const Epetra_Map& fmap = mesh_->face_map(true);

  Krel_cells = Teuchos::rcp(new Epetra_Vector(cmap));
  Krel_faces = Teuchos::rcp(new Epetra_Vector(fmap));

  Krel_cells->PutScalar(1.0);  // we start with fully saturated media
  Krel_faces->PutScalar(1.0);

  if (Krel_method == FLOW_RELATIVE_PERM_UPWIND_GRAVITY) {
    // Kgravity_unit.resize(ncells_wghost);  Resize does not work properly.
    SetAbsolutePermeabilityTensor(K);
    CalculateKVectorUnit(gravity_, Kgravity_unit);
  }

  flow_status_ = FLOW_STATUS_INIT;
}


/* ******************************************************************
* Initialization of auxiliary variables (lambda and two saturations).
* WARNING: Flow_PK may use complex initialization of the remaining 
* state variables.
****************************************************************** */
void Richards_PK::InitializeAuxiliaryData()
{
  // pressures
  Epetra_Vector& pressure = FS->ref_pressure();
  Epetra_Vector& lambda = FS->ref_lambda();
  DeriveFaceValuesFromCellValues(pressure, lambda);

  double time = T_physics;
  UpdateBoundaryConditions(time, lambda);

  // saturations
  Epetra_Vector& ws = FS->ref_water_saturation();
  Epetra_Vector& ws_prev = FS->ref_prev_water_saturation();

  DeriveSaturationFromPressure(pressure, ws);
  ws_prev = ws;
}


/* ******************************************************************
* Initial pressure is set to the pressure for fully saturated rock.
****************************************************************** */
void Richards_PK::InitializeSteadySaturated()
{ 
  double T = FS->get_time();
  SolveFullySaturatedProblem(T, *solution);
}


/* ******************************************************************
* Initial guess is a problem for BDFx. To help launch BDFx, a special
* initialization of an initial guess has been developed based on 
* dynamically relaxed Picard iterations. 
****************************************************************** */
void Richards_PK::InitPicard(double T0)
{
  bool ini_with_darcy = ti_specs_igs_.initialize_with_darcy;
  double clip_value = ti_specs_igs_.clip_saturation;

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_MEDIUM) {
    std::printf("***********************************************************\n");
    std::printf("Richards PK: initializing of initial guess at T(sec)=%9.4e\n", T0);

    if (ini_with_darcy) {
      std::printf("Richards PK: initializing with a clipped Darcy pressure\n");
      std::printf("Richards PK: clipping saturation value =%5.2g\n", clip_value);
    }
    std::printf("***********************************************************\n");
  }

  // set up new preconditioner
  preconditioner_method = ti_specs_igs_.preconditioner_method;
  Teuchos::ParameterList& tmp_list = preconditioner_list_.sublist(preconditioner_name_igs_);
  Teuchos::ParameterList ML_list;
  if (preconditioner_name_igs_ == "Trilinos ML") {
    ML_list = tmp_list.sublist("ML Parameters"); 
  } else if (preconditioner_name_igs_ == "Hypre AMG") {
    ML_list = tmp_list.sublist("BoomerAMG Parameters"); 
  } else if (preconditioner_name_igs_ == "Block ILU") {
    ML_list = tmp_list.sublist("Block ILU Parameters");
  }

  string mfd3d_method_name = tmp_list.get<string>("discretization method", "optimized mfd");
  ProcessStringMFD3D(mfd3d_method_name, &mfd3d_method_preconditioner_); 

  preconditioner_->SetSymmetryProperty(is_matrix_symmetric);
  preconditioner_->SymbolicAssembleGlobalMatrices(*super_map_);
  preconditioner_->InitPreconditioner(preconditioner_method, ML_list);

  // set up new time integration or solver
  solver = new AztecOO;
  solver->SetUserOperator(matrix_);
  solver->SetPrecOperator(preconditioner_);
  solver->SetAztecOption(AZ_solver, AZ_gmres);

  // initialize mass matrices
  SetAbsolutePermeabilityTensor(K);
  for (int c = 0; c < K.size(); c++) K[c] *= rho / mu;
  matrix_->CreateMFDmassMatrices(mfd3d_method_, K);
  preconditioner_->CreateMFDmassMatrices(mfd3d_method_preconditioner_, K);

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_HIGH) {
    int nokay = matrix_->nokay();
    int npassed = matrix_->npassed();
    std::printf("Richards PK: successful and passed matrices: %8d %8d\n", nokay, npassed);   
  }

  // (re)initialize pressure and saturation
  Epetra_Vector& pressure = FS->ref_pressure();
  Epetra_Vector& lambda = FS->ref_lambda();
  Epetra_Vector& ws = FS->ref_water_saturation();

  *solution_cells = pressure;
  *solution_faces = lambda;

  if (ini_with_darcy) {
    SolveFullySaturatedProblem(T0, *solution);
    double pmin = atm_pressure;
    ClipHydrostaticPressure(pmin, clip_value, *solution);
    pressure = *solution_cells;
  }
  DeriveSaturationFromPressure(pressure, ws);

  // control options
  set_time(T0, 0.0);  // overrides data provided in the input file
  ti_method = ti_method_igs;
  num_itrs = 0;
  block_picard = 0;
  error_control_ = FLOW_TI_ERROR_CONTROL_PRESSURE;
  error_control_ |= error_control_igs_;

  // calculate initial guess: claening is required (lipnikov@lanl.gov)
  T_physics = ti_specs_igs_.T0;
  dT = ti_specs_igs_.dT0;
  AdvanceToSteadyState_Picard(ti_specs_igs_);
 
  Epetra_Vector& ws_prev = FS->ref_prev_water_saturation();
  DeriveSaturationFromPressure(*solution_cells, ws);
  ws_prev = ws;
}


/* ******************************************************************
* Separate initialization of solver may be required for steady state
* and transient runs. BDF2 and BDF1 will eventually merge but are 
* separated strictly (no code optimization) for the moment.
****************************************************************** */
void Richards_PK::InitSteadyState(double T0, double dT0)
{
  bool ini_with_darcy = ti_specs_sss_.initialize_with_darcy;
  double clip_value = ti_specs_sss_.clip_saturation;

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_MEDIUM) {
    std::printf("***********************************************************\n");
    std::printf("Richards PK: initializing steady-state at T(sec)=%9.4e dT(sec)=%9.4e \n", T0, dT0);

    if (ini_with_darcy) {
      std::printf("Richards PK: initializing with a clipped Darcy pressure\n");
      std::printf("Richards PK: clipping saturation value =%5.2g\n", clip_value);
    }
    std::printf("***********************************************************\n");
  }

  // set up new preconditioner
  preconditioner_method = ti_specs_sss_.preconditioner_method;
  Teuchos::ParameterList& tmp_list = preconditioner_list_.sublist(preconditioner_name_sss_);
  Teuchos::ParameterList ML_list;
  if (preconditioner_name_sss_ == "Trilinos ML") {
    ML_list = tmp_list.sublist("ML Parameters");
  } else if (preconditioner_name_sss_ == "Hypre AMG") {
    ML_list = tmp_list.sublist("BoomerAMG Parameters");
  } else if (preconditioner_name_sss_ == "Block ILU") {
    ML_list = tmp_list.sublist("Block ILU Parameters");
  }
  string mfd3d_method_name = tmp_list.get<string>("discretization method", "optimized mfd");
  ProcessStringMFD3D(mfd3d_method_name, &mfd3d_method_preconditioner_); 

  preconditioner_->SetSymmetryProperty(is_matrix_symmetric);
  preconditioner_->SymbolicAssembleGlobalMatrices(*super_map_);
  preconditioner_->InitPreconditioner(preconditioner_method, ML_list);

  // set up new time integration or solver
  if (ti_method_sss == FLOW_TIME_INTEGRATION_BDF2) {
    Teuchos::ParameterList tmp_list = rp_list_.sublist("steady state time integrator").sublist("BDF2").sublist("BDF2 parameters");
    if (! tmp_list.isSublist("VerboseObject"))
        tmp_list.sublist("VerboseObject") = rp_list_.sublist("VerboseObject");

    Teuchos::RCP<Teuchos::ParameterList> bdf2_list(new Teuchos::ParameterList(tmp_list));
    if (bdf2_dae == NULL) bdf2_dae = new BDF2::Dae(*this, *super_map_);
    bdf2_dae->setParameterList(bdf2_list);

  } else if (ti_method_sss == FLOW_TIME_INTEGRATION_BDF1) {
    Teuchos::ParameterList tmp_list = rp_list_.sublist("steady state time integrator").sublist("BDF1").sublist("BDF1 parameters");
    if (! tmp_list.isSublist("VerboseObject"))
        tmp_list.sublist("VerboseObject") = rp_list_.sublist("VerboseObject");

    Teuchos::RCP<Teuchos::ParameterList> bdf1_list(new Teuchos::ParameterList(tmp_list));
    if (bdf1_dae == NULL) bdf1_dae = new BDF1Dae(*this, *super_map_);
    bdf1_dae->setParameterList(bdf1_list);

  } else if (solver == NULL) {
    solver = new AztecOO;
    solver->SetUserOperator(matrix_);
    solver->SetPrecOperator(preconditioner_);
    solver->SetAztecOption(AZ_solver, AZ_cg);  // symmetry is required
  }

  // initialize mass matrices
  SetAbsolutePermeabilityTensor(K);
  for (int c = 0; c < K.size(); c++) K[c] *= rho / mu;
  matrix_->CreateMFDmassMatrices(mfd3d_method_, K);
  preconditioner_->CreateMFDmassMatrices(mfd3d_method_preconditioner_, K);

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_HIGH) {
    int nokay = matrix_->nokay();
    int npassed = matrix_->npassed();
    std::printf("Richards PK: successful and passed matrices: %8d %8d\n", nokay, npassed);   
  }

  // (re)initialize pressure and saturation
  Epetra_Vector& pressure = FS->ref_pressure();
  Epetra_Vector& lambda = FS->ref_lambda();
  Epetra_Vector& water_saturation = FS->ref_water_saturation();

  *solution_cells = pressure;
  *solution_faces = lambda;

  if (ini_with_darcy) {
    SolveFullySaturatedProblem(T0, *solution);
    double pmin = atm_pressure;
    ClipHydrostaticPressure(pmin, clip_value, *solution);
    pressure = *solution_cells;
  }
  DeriveSaturationFromPressure(pressure, water_saturation);

  // control options
  set_time(T0, dT0);  // overrides data provided in the input file
  ti_method = ti_method_sss;
  num_itrs = 0;
  block_picard = 0;
  error_control_ = FLOW_TI_ERROR_CONTROL_PRESSURE +  // usually 1 [Pa]
                   FLOW_TI_ERROR_CONTROL_SATURATION;  // usually 1e-4
  error_control_ |= error_control_sss_;

  flow_status_ = FLOW_STATUS_STEADY_STATE_INIT;

  // DEBUG
  // AdvanceToSteadyState();
  // CommitState(FS); WriteGMVfile(FS); exit(0);
}


/* ******************************************************************
* Initialization analyzes status of matrix/preconditioner pair. 
* BDF2 and BDF1 will eventually merge but are separated strictly 
* (no code optimization) for the moment.  
* WARNING: Initialization of lambda is done in MPC and may be 
* erroneous in pure transient mode.
****************************************************************** */
void Richards_PK::InitTransient(double T0, double dT0)
{
  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_MEDIUM) {
    std::printf("***********************************************************\n");
    std::printf("Richards PK: initializing transient flow: T(sec)=%10.5e dT(sec)=%9.4e\n", T0, dT0);
    std::printf("***********************************************************\n");
  }
  set_time(T0, dT0);

  // set up new preconditioner
  preconditioner_method = ti_specs_trs_.preconditioner_method;
  Teuchos::ParameterList& tmp_list = preconditioner_list_.sublist(preconditioner_name_trs_);
  Teuchos::ParameterList ML_list;
  if (preconditioner_name_trs_ == "Trilinos ML") {
    ML_list = tmp_list.sublist("ML Parameters");
  } else if (preconditioner_name_trs_ == "Hypre AMG") {
    ML_list = tmp_list.sublist("BoomerAMG Parameters");
  } else if (preconditioner_name_trs_ == "Block ILU") {
    ML_list = tmp_list.sublist("Block ILU Parameters");
  }
  string mfd3d_method_name = tmp_list.get<string>("discretization method", "optimized mfd");
  ProcessStringMFD3D(mfd3d_method_name, &mfd3d_method_preconditioner_); 

  preconditioner_->SetSymmetryProperty(is_matrix_symmetric);
  preconditioner_->SymbolicAssembleGlobalMatrices(*super_map_);
  preconditioner_->InitPreconditioner(preconditioner_method, ML_list);

  if (ti_method_trs == FLOW_TIME_INTEGRATION_BDF2) {
    if (bdf2_dae != NULL) delete bdf2_dae;  // The only way to reset BDF2 is to delete it.

    Teuchos::ParameterList tmp_list = rp_list_.sublist("transient time integrator").sublist("BDF2").sublist("BDF2 parameters");
    if (! tmp_list.isSublist("VerboseObject"))
        tmp_list.sublist("VerboseObject") = rp_list_.sublist("VerboseObject");

    Teuchos::RCP<Teuchos::ParameterList> bdf2_list(new Teuchos::ParameterList(tmp_list));
    bdf2_dae = new BDF2::Dae(*this, *super_map_);
    bdf2_dae->setParameterList(bdf2_list);

  } else if (ti_method_trs == FLOW_TIME_INTEGRATION_BDF1) {
    if (bdf1_dae != NULL) delete bdf1_dae;  // the only way to reset BDF1 is to delete it

    Teuchos::ParameterList tmp_list = rp_list_.sublist("transient time integrator").sublist("BDF1").sublist("BDF1 parameters");
    if (! tmp_list.isSublist("VerboseObject"))
        tmp_list.sublist("VerboseObject") = rp_list_.sublist("VerboseObject");

    Teuchos::RCP<Teuchos::ParameterList> bdf1_list(new Teuchos::ParameterList(tmp_list));
    bdf1_dae = new BDF1Dae(*this, *super_map_);
    bdf1_dae->setParameterList(bdf1_list);

  } else if (solver == NULL) {
    solver = new AztecOO;
    solver->SetUserOperator(matrix_);
    solver->SetPrecOperator(preconditioner_);
    solver->SetAztecOption(AZ_solver, AZ_cg);  // symmetry is required
  }

  // initialize mass matrices
  SetAbsolutePermeabilityTensor(K);
  for (int c = 0; c < K.size(); c++) K[c] *= rho / mu;
  matrix_->CreateMFDmassMatrices(mfd3d_method_, K);
  preconditioner_->CreateMFDmassMatrices(mfd3d_method_preconditioner_, K);

  // initialize pressure
  Epetra_Vector& pressure = FS->ref_pressure();
  Epetra_Vector& lambda = FS->ref_lambda();
  *solution_cells = pressure;
  *solution_faces = lambda;

  // initialize saturations
  Epetra_Vector& water_saturation = FS->ref_water_saturation();
  DeriveSaturationFromPressure(pressure, water_saturation);

  // calculate total water mass
  Epetra_Vector& phi = FS->ref_porosity();
  mass_bc = 0.0;
  for (int c = 0; c < ncells_owned; c++) {
    mass_bc += water_saturation[c] * rho * phi[c] * mesh_->cell_volume(c); 
  }

  // control options
  ti_method = ti_method_trs;
  num_itrs = 0;
  block_picard = 0;
  error_control_ = FLOW_TI_ERROR_CONTROL_PRESSURE +  // usually 1 [Pa]
                   FLOW_TI_ERROR_CONTROL_SATURATION;  // usually 1e-4
  error_control_ |= error_control_trs_;

  flow_status_ = FLOW_STATUS_TRANSIENT_STATE_INIT;
}


/* ******************************************************************
* this routine avoid limitations of MPC by bumping the time step.                                          
****************************************************************** */
double Richards_PK::CalculateFlowDt()
{
  if (ti_method == FLOW_TIME_INTEGRATION_PICARD && block_picard == 1) dT *= 1e+4;
  return dT;
}


/* ******************************************************************* 
* Performs one time step of size dT_MPC either for steady-state or 
* transient calculations.
* Warning: BDF2 and BDF1 will merge eventually.
******************************************************************* */
int Richards_PK::Advance(double dT_MPC)
{
  dT = dT_MPC;
  double time = FS->get_time();
  if (time >= 0.0) T_physics = time;

  // predict water mass change during time step
  time = T_physics;
  if (num_itrs == 0) {  // initialization
    Epetra_Vector udot(*super_map_);
    ComputeUDot(time, *solution, udot);
    if (ti_method == FLOW_TIME_INTEGRATION_BDF2) {
      bdf2_dae->set_initial_state(time, *solution, udot);

    } else if (ti_method == FLOW_TIME_INTEGRATION_BDF1) {
      bdf1_dae->set_initial_state(time, *solution, udot);

    } else if (ti_method == FLOW_TIME_INTEGRATION_PICARD) {
      if (flow_status_ == FLOW_STATUS_STEADY_STATE_INIT) {
        AdvanceToSteadyState();
        block_picard = 1;  // We will wait for transient initialization.
      }
    }

    int ierr;
    update_precon(time, *solution, dT, ierr);
  }

  if (ti_method == FLOW_TIME_INTEGRATION_BDF2) {
    bdf2_dae->bdf2_step(dT, 0.0, *solution, dTnext);
    bdf2_dae->commit_solution(dT, *solution);
    bdf2_dae->write_bdf2_stepping_statistics();

    T_physics = bdf2_dae->most_recent_time();

  } else if (ti_method == FLOW_TIME_INTEGRATION_BDF1) {
    bdf1_dae->bdf1_step(dT, *solution, dTnext);
    bdf1_dae->commit_solution(dT, *solution);
    bdf1_dae->write_bdf1_stepping_statistics();

    T_physics = bdf1_dae->most_recent_time();

  } else if (ti_method == FLOW_TIME_INTEGRATION_PICARD) {
    if (block_picard == 0) {
      PicardTimeStep(time, dT, dTnext);  // Updates solution vector.
      //AndersonAccelerationTimeStep(time, dT, dTnext);
    } else {
      dTnext = dT;
    }
  }
  num_itrs++;

  flow_status_ = FLOW_STATUS_TRANSIENT_STATE_COMPLETE;
  return 0;
}


/* ******************************************************************
* Transfer part of the internal data needed by transport to the 
* flow state FS_MPC. MPC may request to populate the original FS.
* The consistency condition is improved by adjusting saturation while
* preserving its LED property.
****************************************************************** */
void Richards_PK::CommitState(Teuchos::RCP<Flow_State> FS_MPC)
{
  // save cell-based and face-based pressures 
  Epetra_Vector& pressure = FS_MPC->ref_pressure();
  pressure = *solution_cells;
  Epetra_Vector& lambda = FS_MPC->ref_lambda();
  lambda = *solution_faces;

  // update saturations
  Epetra_Vector& ws = FS_MPC->ref_water_saturation();
  Epetra_Vector& ws_prev = FS_MPC->ref_prev_water_saturation();

  ws_prev = ws;
  DeriveSaturationFromPressure(pressure, ws);

  // calculate Darcy flux as diffusive part + advective part.
  Epetra_Vector& flux = FS_MPC->ref_darcy_flux();
  matrix_->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces);  // We remove dT from mass matrices.
  matrix_->DeriveDarcyMassFlux(*solution, *face_importer_, flux);
  AddGravityFluxes_DarcyFlux(K, *Krel_cells, *Krel_faces, flux);
  for (int c = 0; c < nfaces_owned; c++) flux[c] /= rho;

  // update mass balance
  // ImproveAlgebraicConsistency(flux, ws_prev, ws);

  if (MyPID == 0 && verbosity >= FLOW_VERBOSITY_HIGH) {
    Epetra_Vector& phi = FS_MPC->ref_porosity();
    mass_bc += WaterVolumeChangePerSecond(bc_markers, flux) * rho * dT;

    mass_amanzi = 0.0;
    for (int c = 0; c < ncells_owned; c++) {
      mass_amanzi += ws[c] * rho * phi[c] * mesh_->cell_volume(c);
    }
    double mass_loss = mass_bc - mass_amanzi; 
    std::printf("Richards PK: water mass = %9.4e, lost = %9.4e\n", mass_amanzi, mass_loss);
  }

  dT = dTnext;

  // DEBUG
  // WriteGMVfile(FS_MPC);
}


/* ******************************************************************
* Estimate du/dt from the pressure equations, du/dt = g - A*u.
****************************************************************** */
double Richards_PK::ComputeUDot(double T, const Epetra_Vector& u, Epetra_Vector& udot)
{
  ComputePreconditionerMFD(u, matrix_, T, 0.0, false);  // Calculate only stiffness matrix.
  double norm_udot = matrix_->ComputeNegativeResidual(u, udot);

  Epetra_Vector* udot_faces = FS->CreateFaceView(udot);
  udot_faces->PutScalar(0.0);

  return norm_udot;
}


/* ******************************************************************
* Gathers together routines to compute MFD matrices Axx(u) and 
* preconditioner Sff(u) using internal time step dT.                             
****************************************************************** */
void Richards_PK::ComputePreconditionerMFD(
    const Epetra_Vector& u, Matrix_MFD* matrix_operator,
    double Tp, double dTp, bool flag_update_ML)
{
  Epetra_Vector* u_cells = FS->CreateCellView(u);
  Epetra_Vector* u_faces = FS->CreateFaceView(u);

  // call bundeled code
  CalculateRelativePermeability(u);
  UpdateBoundaryConditions(Tp, *u_faces);

  // setup a new algebraic problem
  matrix_operator->CreateMFDstiffnessMatrices(*Krel_cells, *Krel_faces);
  matrix_operator->CreateMFDrhsVectors();
  AddGravityFluxes_MFD(K, *Krel_cells, *Krel_faces, matrix_operator);
  if (flag_update_ML) AddTimeDerivative_MFD(*u_cells, dTp, matrix_operator);
  matrix_operator->ApplyBoundaryConditions(bc_markers, bc_values);
  matrix_operator->AssembleGlobalMatrices();
  if (flag_update_ML) {
    matrix_operator->ComputeSchurComplement(bc_markers, bc_values);
    matrix_operator->UpdatePreconditioner();
  }

  // DEBUG
  // Matrix_Audit audit(mesh_, matrix_opeartor);
  // audit.InitAudit();
  // audit.CheckSpectralBounds();
}


/* ******************************************************************
* BDF methods need a good initial guess.
* This method gives a less smoother solution than in Flow 1.0.
* WARNING: Each owned face must have at least one owned cell. 
* Probability that this assumption is violated is close to zero. 
* Even when it happens, the code will not crash.
****************************************************************** */
void Richards_PK::DeriveFaceValuesFromCellValues(const Epetra_Vector& ucells, Epetra_Vector& ufaces)
{
  AmanziMesh::Entity_ID_List cells;

  for (int f = 0; f < nfaces_owned; f++) {
    cells.clear();
    mesh_->face_get_cells(f, AmanziMesh::OWNED, &cells);
    int ncells = cells.size();

    if (ncells > 0) {
      double face_value = 0.0;
      for (int n = 0; n < ncells; n++) face_value += ucells[cells[n]];
      ufaces[f] = face_value / ncells;
    } else {
      ufaces[f] = atm_pressure;
    }
  }
}


/* ******************************************************************
* Temporary convertion from double to tensor.                                               
****************************************************************** */
void Richards_PK::SetAbsolutePermeabilityTensor(std::vector<WhetStone::Tensor>& K)
{
  const Epetra_Vector& vertical_permeability = FS->ref_vertical_permeability();
  const Epetra_Vector& horizontal_permeability = FS->ref_horizontal_permeability();

  for (int c = 0; c < K.size(); c++) {
    if (vertical_permeability[c] == horizontal_permeability[c]) {
      K[c].init(dim, 1);
      K[c](0, 0) = vertical_permeability[c];
    } else {
      K[c].init(dim, 2);
      for (int i = 0; i < dim-1; i++) K[c](i, i) = horizontal_permeability[c];
      K[c](dim-1, dim-1) = vertical_permeability[c];
    }
  }
}


/* ******************************************************************
* Adds time derivative to the cell-based part of MFD algebraic system.                                               
****************************************************************** */
void Richards_PK::AddTimeDerivative_MFD(
    Epetra_Vector& pressure_cells, double dT_prec, Matrix_MFD* matrix_operator)
{
  Epetra_Vector dSdP(mesh_->cell_map(false));
  DerivedSdP(pressure_cells, dSdP);

  const Epetra_Vector& phi = FS->ref_porosity();
  std::vector<double>& Acc_cells = matrix_operator->Acc_cells();
  std::vector<double>& Fc_cells = matrix_operator->Fc_cells();

  int ncells = mesh_->num_entities(AmanziMesh::CELL, AmanziMesh::OWNED);

  for (int c = 0; c < ncells; c++) {
    double volume = mesh_->cell_volume(c);
    double factor = rho * phi[c] * dSdP[c] * volume / dT_prec;
    Acc_cells[c] += factor;
    Fc_cells[c] += factor * pressure_cells[c];
  }
}


/* ******************************************************************
* Saturation should be in exact balance with Darcy fluxes in order to
* have local extrema dimishing (LED) property for concentrations. 
* WARNING: we can enforce it strictly only in some cells.
****************************************************************** */
void Richards_PK::ImproveAlgebraicConsistency(const Epetra_Vector& flux, 
                                              const Epetra_Vector& ws_prev, Epetra_Vector& ws)
{
  // create a disctributed flux vector
  Epetra_Vector flux_d(mesh_->face_map(true));
  for (int f = 0; f < nfaces_owned; f++) flux_d[f] = flux[f];
  FS->CopyMasterFace2GhostFace(flux_d);

  // create a distributed saturation vector
  Epetra_Vector ws_d(mesh_->cell_map(true));
  for (int c = 0; c < ncells_owned; c++) ws_d[c] = ws[c];
  FS->CopyMasterCell2GhostCell(ws_d);

  WhetStone::MFD3D mfd(mesh_);
  const Epetra_Vector& phi = FS->ref_porosity();
  AmanziMesh::Entity_ID_List faces;
  std::vector<int> dirs;

  for (int c = 0; c < ncells_owned; c++) {
    mesh_->cell_get_faces_and_dirs(c, &faces, &dirs);
    int nfaces = faces.size();

    // calculate min/max values
    double wsmin, wsmax;
    wsmin = wsmax = ws[c];
    for (int n = 0; n < nfaces; n++) {
      int f = faces[n];
      int c2 = mfd.cell_get_face_adj_cell(c, f);
      wsmin = std::min<double>(wsmin, ws[c2]);
      wsmax = std::max<double>(wsmax, ws[c2]);
    }

    // predict new saturation
    ws[c] = ws_prev[c];
    double factor = dT / (phi[c] * mesh_->cell_volume(c));
    for (int n = 0; n < nfaces; n++) {
      int f = faces[n];
      ws[c] -= factor * flux_d[f] * dirs[n]; 
    }

    // limit new saturation
    ws[c] = std::max<double>(ws[c], wsmin);
    ws[c] = std::min<double>(ws[c], wsmax);
  }
}


}  // namespace AmanziFlow
}  // namespace Amanzi


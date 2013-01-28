/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
A base two-phase, thermal Richard's equation with water vapor.

Authors: Ethan Coon (ATS version) (ecoon@lanl.gov)
*/

#include "Epetra_FECrsMatrix.h"
#include "EpetraExt_RowMatrixOut.h"
#include "boost/math/special_functions/fpclassify.hpp"

#include "richards.hh"

namespace Amanzi {
namespace Flow {

#define DEBUG_FLAG 1
#define DEBUG_RES_FLAG 0

// Richards is a BDFFnBase
// -----------------------------------------------------------------------------
// computes the non-linear functional g = g(t,u,udot)
// -----------------------------------------------------------------------------
void Richards::fun(double t_old,
                   double t_new,
                   Teuchos::RCP<TreeVector> u_old,
                   Teuchos::RCP<TreeVector> u_new,
                   Teuchos::RCP<TreeVector> g) {
  niter_++;

  // VerboseObject stuff.
  Teuchos::OSTab tab = getOSTab();

  ASSERT(S_inter_->time() == t_old);
  ASSERT(S_next_->time() == t_new);
  double h = t_new - t_old;

  Teuchos::RCP<CompositeVector> u = u_new->data();

#if DEBUG_FLAG
  int nc = u->size("cell") - 1;
  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_EXTREME, true)) {
    *out_ << "----------------------------------------------------------------" << std::endl;
    *out_ << "Richards Residual calculation: T0 = " << t_old
          << " T1 = " << t_new << " H = " << h << std::endl;
    *out_ << "  p0: " << (*u)("cell",0,0) << " " << (*u)("face",0,3)
          << std::endl;
    *out_ << "  p1: " << (*u)("cell",0,nc) << " " << (*u)("face",0,500)
          << std::endl;
  }
#endif

  // pointer-copy temperature into state and update any auxilary data
  solution_to_state(u_new, S_next_);

  // update boundary conditions
  bc_pressure_->Compute(t_new);
  bc_flux_->Compute(t_new);
  UpdateBoundaryConditions_();

  // zero out residual
  Teuchos::RCP<CompositeVector> res = g->data();
  res->PutScalar(0.0);

  // diffusion term, treated implicitly
  ApplyDiffusion_(S_next_.ptr(), res.ptr());

  // accumulation term
  AddAccumulation_(res.ptr());

#if DEBUG_FLAG
  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_EXTREME, true)) {
    Teuchos::RCP<const CompositeVector> satl1 = S_next_->GetFieldData("saturation_liquid");
    Teuchos::RCP<const CompositeVector> satl0 = S_inter_->GetFieldData("saturation_liquid");
    Teuchos::RCP<const CompositeVector> sati1 = S_next_->GetFieldData("saturation_ice");
    Teuchos::RCP<const CompositeVector> sati0 = S_inter_->GetFieldData("saturation_ice");
    *out_ << "  sat_old_0: " << (*satl0)("cell",0) << ", " << (*sati0)("cell",0) << std::endl;
    *out_ << "  sat_new_0: " << (*satl1)("cell",0) << ", " << (*sati1)("cell",0) << std::endl;
    *out_ << "  sat_old_1: " << (*satl0)("cell",nc) << ", " << (*sati0)("cell",nc) << std::endl;
    *out_ << "  sat_new_1: " << (*satl1)("cell",nc) << ", " << (*sati1)("cell",nc) << std::endl;

    *out_ << "  res0 (after accumulation): " << (*res)("cell",0,0)
          << " " << (*res)("face",0,3) << std::endl;
    *out_ << "  res1 (after accumulation): " << (*res)("cell",0,nc)
          << " " << (*res)("face",0,500) << std::endl;
  }
#endif

#if DEBUG_RES_FLAG
  if (niter_ < 23) {
    std::stringstream namestream;
    namestream << "flow_residual_" << niter_;
    *S_next_->GetFieldData(namestream.str(),name_) = *res;

    std::stringstream solnstream;
    solnstream << "flow_solution_" << niter_;
    *S_next_->GetFieldData(solnstream.str(),name_) = *u;
  }
#endif
};

// -----------------------------------------------------------------------------
// Apply the preconditioner to u and return the result in Pu.
// -----------------------------------------------------------------------------
void Richards::precon(Teuchos::RCP<const TreeVector> u, Teuchos::RCP<TreeVector> Pu) {
  // VerboseObject stuff.
  Teuchos::OSTab tab = getOSTab();

#if DEBUG_FLAG
  // Dump residual
  int nc = u->data()->size("cell") - 1;
  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_EXTREME, true)) {
    *out_ << "Precon application:" << std::endl;
    *out_ << "  p0: " << (*u->data())("cell",0,0) << " "
          << (*u->data())("face",0,3) << std::endl;
    *out_ << "  p1: " << (*u->data())("cell",0,nc) << " "
          << (*u->data())("face",0,500) << std::endl;
  }
#endif

  // Apply the preconditioner
  preconditioner_->ApplyInverse(*u->data(), Pu->data().ptr());

#if DEBUG_FLAG
  // Dump correction
  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_EXTREME, true)) {

  *out_ << "  PC*p0: " << (*Pu->data())("cell",0,0) << " "
        << (*Pu->data())("face",0,3) << std::endl;
  *out_ << "  PC*p1: " << (*Pu->data())("cell",0,nc) << " "
        << (*Pu->data())("face",0,500) << std::endl;
  }
#endif
};


// -----------------------------------------------------------------------------
// Update the preconditioner at time t and u = up
// -----------------------------------------------------------------------------
void Richards::update_precon(double t, Teuchos::RCP<const TreeVector> up, double h) {
  // VerboseObject stuff.
  Teuchos::OSTab tab = getOSTab();

#if DEBUG_FLAG
  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_EXTREME, true)) {
    *out_ << "Precon update at t = " << t << std::endl;
  }
#endif

  // update state with the solution up.
  ASSERT(S_next_->time() == t);
  PKDefaultBase::solution_to_state(up, S_next_);

  // update the rel perm according to the scheme of choice
  UpdatePermeabilityData_(S_next_.ptr());

  // update boundary conditions
  bc_pressure_->Compute(S_next_->time());
  bc_flux_->Compute(S_next_->time());
  UpdateBoundaryConditions_();

  Teuchos::RCP<const CompositeVector> rel_perm =
      S_next_->GetFieldData("numerical_rel_perm");
  Teuchos::RCP<const CompositeVector> rho =
      S_next_->GetFieldData("mass_density_liquid");
  Teuchos::RCP<const Epetra_Vector> gvec =
      S_next_->GetConstantVectorData("gravity");

  // Update the preconditioner with darcy and gravity fluxes
  preconditioner_->CreateMFDstiffnessMatrices(rel_perm.ptr());
  preconditioner_->CreateMFDrhsVectors();
  AddGravityFluxes_(gvec.ptr(), rel_perm.ptr(), rho.ptr(), preconditioner_.ptr());

  // Update the preconditioner with accumulation terms.
  // -- update the accumulation derivatives
  S_next_->GetFieldEvaluator("water_content")
      ->HasFieldDerivativeChanged(S_next_.ptr(), name_, key_);

  // -- get the accumulation deriv
  Teuchos::RCP<const CompositeVector> dwc_dp =
      S_next_->GetFieldData("dwater_content_d"+key_);
  Teuchos::RCP<const CompositeVector> pres =
      S_next_->GetFieldData(key_);

  // -- update the cell-cell block
  std::vector<double>& Acc_cells = preconditioner_->Acc_cells();
  std::vector<double>& Fc_cells = preconditioner_->Fc_cells();
  for (int c=0; c!=dwc_dp->size("cell"); ++c) {
    Acc_cells[c] += (*dwc_dp)("cell",c) / h;
    Fc_cells[c] += (*pres)("cell",c) * (*dwc_dp)("cell",c) / h;

  }

  // Assemble and precompute the Schur complement for inversion.
  preconditioner_->ApplyBoundaryConditions(bc_markers_, bc_values_);

  if (assemble_preconditioner_) {
    preconditioner_->AssembleGlobalMatrices();
    preconditioner_->ComputeSchurComplement(bc_markers_, bc_values_);
    preconditioner_->UpdatePreconditioner();
  }

  /*
  // dump the schur complement
  Teuchos::RCP<Epetra_FECrsMatrix> sc = preconditioner_->Schur();
  std::stringstream filename_s;
  filename_s << "schur_" << S_next_->cycle() << ".txt";
  EpetraExt::RowMatrixToMatlabFile(filename_s.str().c_str(), *sc);
  *out_ << "updated precon " << S_next_->cycle() << std::endl;

  // print the rel perm
  Teuchos::RCP<const CompositeVector> cell_rel_perm =
      S_next_->GetFieldData("relative_permeability");
  *out_ << "REL PERM: " << std::endl;
  cell_rel_perm->Print(*out_);
  *out_ << std::endl;
  *out_ << "UPWINDED REL PERM: " << std::endl;
  rel_perm->Print(*out_);
  */
};


double Richards::enorm(Teuchos::RCP<const TreeVector> u,
                       Teuchos::RCP<const TreeVector> du) {
  // update the tolerances if we are continuing from an crappy IC
  if (continuation_to_ss_) {
    atol_ = atol0_ + 1.e5*atol0_/(1.0 + S_next_->time());
    rtol_ = rtol0_ + 1.e5*rtol0_/(1.0 + S_next_->time());
  }

  // cell error given by tolerances on water content
  S_next_->GetFieldEvaluator("water_content")->HasFieldChanged(S_next_.ptr(), name_);
  const CompositeVector& wc = *S_next_->GetFieldData("water_content");

  const CompositeVector& res = *du->data();
  double h = S_next_->time() - S_inter_->time();

  double enorm_cell(0.);
  int ncells = res.size("cell");
  for (int c=0; c!=ncells; ++c) {
    double tmp = abs(h*res("cell",c)) / (atol_+rtol_*abs(wc("cell",c)));
    enorm_cell = std::max<double>(enorm_cell, tmp);
  }

  // cell error given by tolerances on pressure
  double enorm_face(0.);
  /*
  int nfaces = res.size("face");
  for (int f=0; f!=nfaces; ++f) {
    double tmp = abs(res("face",f)) / (atol_+rtol_*101325.0);
    enorm_face = std::max<double>(enorm_face, tmp);
  }
  */

  if (out_.get() && includesVerbLevel(verbosity_, Teuchos::VERB_HIGH, true)) {
    double infnorm_c(0.), infnorm_f(0.);
    res.ViewComponent("cell",false)->NormInf(&infnorm_c);
    res.ViewComponent("face",false)->NormInf(&infnorm_f);

    double buf_c(0.), buf_f(0.);
    MPI_Allreduce(&enorm_cell, &buf_c, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
    //MPI_Allreduce(&enorm_face, &buf_f, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

    Teuchos::OSTab tab = getOSTab();
    *out_ << "ENorm (Infnorm) of: " << name_ << ": "
          << "cell = " << buf_c << " (" << infnorm_c << ")  "
          << "face = " << buf_f << " (" << infnorm_f << ")  " << std::endl;

  }

  double enorm_val(std::max<double>(enorm_face, enorm_cell));
#ifdef HAVE_MPI
  double buf = enorm_val;
  MPI_Allreduce(&buf, &enorm_val, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
#endif
  return enorm_val;
};


}  // namespace Flow
}  // namespace Amanzi




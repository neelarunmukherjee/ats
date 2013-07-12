/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------
ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

EWCModel evaluates the full chain of models to determine energy and water
content as a function of temperature, pressure (and optionally porosity).

This format is not a typical Model format, and doesn't have an evaluator, but
is instead used by the MPCDelegateEWC, which is delegated much of the
calculations by a standard MPC.

------------------------------------------------------------------------- */

#ifndef AMANZI_EWC_MODEL_HH_
#define AMANZI_EWC_MODEL_HH_

#include "State.hh"

namespace Amanzi {

class State;

class EWCModel {

 public:
  virtual void InitializeModel(const Teuchos::Ptr<State>& S) = 0;
  virtual void UpdateModel(const Teuchos::Ptr<State>& S) = 0;

  virtual int Evaluate(double T, double p, double base_poro, double& energy, double& wc) = 0;
  virtual int InverseEvaluate(double energy, double wc, double base_poro, double& T, double& p) = 0;
  virtual int InverseEvaluateEnergy(double energy, double p, double base_poro, double& T) = 0;

  virtual int EvaluateSaturations(double T, double p, double base_poro, double& s_gas, double& s_liq, double& s_ice) = 0;
};


} // namespace


#endif
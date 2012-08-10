/* -*-  mode: c++; c-default-style: "google"; indent-tabs-mode: nil -*- */

/*
  ATS

  Simple EOS for constant density and viscosity.
  Defaults to reasonable values for water.

  http://software.lanl.gov/ats/trac

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

#ifndef FLOWRELATIONS_EOS_CONSTANT_HH_
#define FLOWRELATIONS_EOS_CONSTANT_HH_

#include "Teuchos_ParameterList.hpp"

#include "factory.hh"
#include "eos.hh"

namespace Amanzi {
namespace Flow {
namespace FlowRelations {

// Equation of State model
class EOSConstant : public EOS {

public:
  explicit
  EOSConstant(Teuchos::ParameterList& eos_plist);

  EOSConstant(const EOSConstant& other);

  virtual Teuchos::RCP<FieldModel> Clone() const;

  virtual double Density(double T, double p) { return rho_/M_; }
  virtual double DDensityDT(double T, double p) { return 0.0; }
  virtual double DDensityDp(double T, double p) { return 0.0; }

  double molar_mass() const { return M_; }
  virtual bool is_molar_basis() const { return true; }

private:
  virtual void InitializeFromPlist_();

  double M_;
  double rho_;

  static Utils::RegisteredFactory<FieldModel,EOSConstant> factory_;

};

} // namespace
} // namespace
} // namespace

#endif

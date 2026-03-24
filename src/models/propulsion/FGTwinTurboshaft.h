/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

 Header:       FGTwinTurboshaft.h
 Purpose:      Twin turboshaft engines driving one rotor thruster (combined shaft).

 ------------- Copyright (C) 2025 JSBSim contributors -------------

 This program is free software; you can redistribute it and/or modify it under
 the terms of the GNU Lesser General Public License as published by the Free Software
 Foundation; either version 2 of the License, or (at your option) any later
 version.

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
SENTRY
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#ifndef FGTWINTURBOSHAFT_H
#define FGTWINTURBOSHAFT_H

#include <array>
#include <memory>
#include "FGEngine.h"
#include "math/FGTable.h"

namespace JSBSim {

/** Two identical turboshaft powerplants sharing one FGRotor thruster.
    Thermodynamics follow FGTurboProp per side; propeller / beta / reverse paths
    are omitted. MaxPower is per side (each turbine), total shaft cap is 2×
    MaxPower when both run. */
class FGTwinTurboshaft : public FGEngine
{
public:
  static constexpr int NSides = 2;

  FGTwinTurboshaft(FGFDMExec* Executive, Element* el, int engine_number, struct Inputs& input);

  enum phaseType { ttOff, ttRun, ttSpinUp, ttStart, ttTrim };

  void Calculate(void) override;
  double CalcFuelNeed(void) override;

  double GetPowerAvailable(void) const override { return (HP * hptoftlbssec); }
  double GetRPM(void) const { return RPM; }
  double GetIeluThrottle(void) const { return ThrottlePos; }
  bool GetIeluIntervent(void) const { return Ielu_intervent; }

  double Seek(double* var, double target, double accel, double decel);
  double ExpSeek(double* var, double target, double accel_tau, double decel_tau);

  phaseType GetPhase(int side) const { return phase[side]; }

  bool GetCutoff(void) const { return Cutoff[0] && Cutoff[1]; }
  bool GetCutoff(int side) const { return Cutoff[side]; }

  double GetN1(int side) const { return N1[side]; }
  double GetITT(int side) const { return Eng_ITT_degC[side]; }
  double GetEngStarting(void) const { return EngStarting[0] || EngStarting[1] ? 1.0 : 0.0; }

  double getOilPressure_psi(void) const { return OilPressure_psi; }
  double getOilTemp_degF(void) { return KelvinToFahrenheit(OilTemp_degK); }

  bool GetGeneratorPower(void) const { return GeneratorPower; }
  int GetCondition(int side) const { return Condition[side]; }

  void SetCutoff(bool cutoff);
  void SetCutoff(int side, bool cutoff);
  void SetGeneratorPower(bool gp) { GeneratorPower = gp; }
  void SetCondition(int c);
  void SetCondition(int side, int c);

  int InitRunning(void) override;
  std::string GetEngineLabels(const std::string& delimiter) override;
  std::string GetEngineValues(const std::string& delimiter) override;

private:
  std::array<phaseType, NSides> phase;
  double IdleN1;
  std::array<double, NSides> N1;
  double MaxN1;
  double N1_factor;
  double ThrottlePos;
  std::array<bool, NSides> Cutoff;

  double OilPressure_psi;
  double OilTemp_degK;

  double Ielu_max_torque;
  bool Ielu_intervent;
  double OldThrottle;

  double Idle_Max_Delay;
  double MaxPower;
  double StarterN1;
  double MaxStartingTime;
  double RPM;
  double PSFC;

  double HP;
  std::array<double, NSides> HP_side;

  std::array<double, NSides> StartTime;

  double ITT_Delay;
  std::array<double, NSides> Eng_ITT_degC;
  std::array<double, NSides> Eng_Temperature;

  std::array<bool, NSides> EngStarting;
  bool GeneratorPower;
  std::array<int, NSides> Condition;

  std::array<double, NSides> FuelFlowSide;
  std::array<double, NSides> CombustionEfficiency;

  bool independent_throttles;
  std::array<double, NSides> ThrottleNormSide;

  double SideOff(int side);
  double SideRun(int side, double throttlePos);
  double SideSpinUp(int side);
  double SideStart(int side);

  void UpdatePhasesBeforePower(void);
  void ApplyIelu(void);
  void UpdateOil(double n1_avg);

  void SetDefaults(void);
  bool Load(FGFDMExec* exec, Element* el);
  void bindmodel(FGPropertyManager* pm, bool independent_throttle_bind);
  void Debug(int from);

  std::unique_ptr<FGTable> ITT_N1;
  std::unique_ptr<FGTable> EnginePowerRPM_N1;
  std::shared_ptr<FGParameter> EnginePowerVC;
  std::unique_ptr<FGTable> CombustionEfficiency_N1;
};

} // namespace JSBSim

#endif

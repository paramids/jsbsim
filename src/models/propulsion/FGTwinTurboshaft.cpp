/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

 Module:       FGTwinTurboshaft.cpp
 Purpose:      Twin turboshaft engines on one rotor thruster.

 ------------- Copyright (C) 2025 JSBSim contributors -------------

FUNCTIONAL DESCRIPTION
--------------------------------------------------------------------------------

Two thermodynamic sides (same model as FGTurboProp: N1, EnginePowerRPM_N1, ITT,
PSFC, etc.) share one FGRotor thruster.  maxpower in XML is per side; shaft
power is HP_side[0] + HP_side[1], then Thruster->Calculate(total * hptoftlbssec).

Phases (ttOff / ttRun / ttSpinUp / ttStart / ttTrim) are tracked per side.
Propeller beta/reverse paths are omitted; IELU uses main-rotor torque only.

Optional <independent_throttles> ties throttle-norm-side-0/1; otherwise both
sides follow in.ThrottlePos[EngineNumber].

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
INCLUDES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "FGTwinTurboshaft.h"
#include "FGRotor.h"
#include "math/FGFunction.h"
#include "input_output/FGXMLElement.h"

using namespace std;

namespace JSBSim {

/*%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
CLASS IMPLEMENTATION
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%*/

FGTwinTurboshaft::FGTwinTurboshaft(FGFDMExec* exec, Element* el, int engine_number,
                                  struct Inputs& input)
  : FGEngine(engine_number, input)
{
  SetDefaults();
  Load(exec, el);
  Debug(0);
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

bool FGTwinTurboshaft::Load(FGFDMExec* exec, Element* el)
{
  MaxStartingTime = 999999; // default: no spin-up timeout
  Ielu_max_torque = -1;     // negative: IELU disabled until XML sets ielumaxtorque

  Element* function_element = el->FindElement("function");
  while (function_element) {
    string name = function_element->GetAttributeValue("name");
    if (name == "EnginePowerVC")
      function_element->SetAttributeValue("name", string("propulsion/engine[#]/") + name);
    function_element = el->FindNextElement("function");
  }

  FGEngine::Load(exec, el); // loads thruster from parent <engine>; must be rotor

  if (Thruster->GetType() != FGThruster::ttRotor) {
    cerr << el->ReadFrom()
         << " twin_turboshaft_engine requires a rotor thruster (single main rotor)." << endl;
    throw("FGTwinTurboshaft: invalid thruster type");
  }

  string property_prefix = CreateIndexedPropertyName("propulsion/engine", EngineNumber);
  EnginePowerVC = GetPreFunction(property_prefix + "/EnginePowerVC");

  if (el->FindElement("idlen1"))
    IdleN1 = el->FindElementValueAsNumber("idlen1");
  if (el->FindElement("maxn1"))
    MaxN1 = el->FindElementValueAsNumber("maxn1");
  if (el->FindElement("maxpower"))
    MaxPower = el->FindElementValueAsNumber("maxpower");
  if (el->FindElement("psfc"))
    PSFC = el->FindElementValueAsNumber("psfc");
  if (el->FindElement("n1idle_max_delay"))
    Idle_Max_Delay = el->FindElementValueAsNumber("n1idle_max_delay");
  if (el->FindElement("maxstartingtime"))
    MaxStartingTime = el->FindElementValueAsNumber("maxstartingtime");
  if (el->FindElement("startern1"))
    StarterN1 = el->FindElementValueAsNumber("startern1");
  if (el->FindElement("ielumaxtorque"))
    Ielu_max_torque = el->FindElementValueAsNumber("ielumaxtorque");
  if (el->FindElement("itt_delay"))
    ITT_Delay = el->FindElementValueAsNumber("itt_delay");
  if (el->FindElement("independent_throttles"))
    independent_throttles = (el->FindElementValueAsNumber("independent_throttles") != 0.0);

  Element* table_element = el->FindElement("table");
  auto PropertyManager = exec->GetPropertyManager();

  while (table_element) {
    string name = table_element->GetAttributeValue("name");
    if (!EnginePowerVC && name == "EnginePowerVC") {
      table_element->SetAttributeValue("name", string("propulsion/engine[#]/") + name);
      EnginePowerVC = std::make_shared<FGTable>(PropertyManager, table_element,
                                                to_string((int)EngineNumber));
      table_element->SetAttributeValue("name", name);
      cerr << table_element->ReadFrom()
           << "Note: Using the EnginePowerVC without enclosed <function> tag is deprecated"
           << endl;
    } else if (name == "EnginePowerRPM_N1") {
      EnginePowerRPM_N1 = std::make_unique<FGTable>(PropertyManager, table_element);
    } else if (name == "ITT_N1") {
      ITT_N1 = std::make_unique<FGTable>(PropertyManager, table_element);
    } else if (name == "CombustionEfficiency_N1") {
      CombustionEfficiency_N1 = std::make_unique<FGTable>(PropertyManager, table_element);
    } else {
      cerr << el->ReadFrom() << "Unknown table type: " << name
           << " in twin_turboshaft_engine definition." << endl;
    }
    table_element = el->FindNextElement("table");
  }

  N1_factor = MaxN1 - IdleN1;
  OilTemp_degK = in.TAT_c + 273.0;

  if (!CombustionEfficiency_N1) {
    CombustionEfficiency_N1 = std::make_unique<FGTable>(6);
    *CombustionEfficiency_N1 << 60.0 << 12.0 / 52.0;
    *CombustionEfficiency_N1 << 82.0 << 12.0 / 30.0;
    *CombustionEfficiency_N1 << 96.0 << 12.0 / 16.0;
    *CombustionEfficiency_N1 << 100.0 << 1.0;
    *CombustionEfficiency_N1 << 104.0 << 1.5;
    *CombustionEfficiency_N1 << 110.0 << 6.0;
  }

  bindmodel(PropertyManager.get(), independent_throttles);
  return true;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Each frame: resolve throttles, update phases, run Side*() for both turbines,
// sum HP and fuel, drive the single rotor with combined shaft power.

void FGTwinTurboshaft::Calculate(void)
{
  RunPreFunctions();
  Cranking = false;

  // IELU uses ThrottlePos; independent mode also tracks per-side norms for thermo.
  if (independent_throttles) {
    ThrottlePos = 0.5 * (ThrottleNormSide[0] + ThrottleNormSide[1]);
  } else {
    double t = in.ThrottlePos[EngineNumber];
    ThrottleNormSide[0] = ThrottleNormSide[1] = t;
    ThrottlePos = t;
  }

  RPM = Thruster->GetEngineRPM(); // same shaft for both sides

  UpdatePhasesBeforePower();
  ApplyIelu();

  HP_side[0] = HP_side[1] = 0.0;
  for (int s = 0; s < NSides; ++s) {
    double thr = independent_throttles ? ThrottleNormSide[s] : ThrottlePos;
    switch (phase[s]) {
    case ttOff:
      HP_side[s] = SideOff(s);
      break;
    case ttRun:
      HP_side[s] = SideRun(s, thr);
      break;
    case ttSpinUp:
      HP_side[s] = SideSpinUp(s);
      break;
    case ttStart:
      HP_side[s] = SideStart(s);
      break;
    default:
      HP_side[s] = 0.0;
      break;
    }
  }

  HP = HP_side[0] + HP_side[1];
  FuelFlow_pph = FuelFlowSide[0] + FuelFlowSide[1];

  double n1_avg = 0.5 * (N1[0] + N1[1]);
  UpdateOil(n1_avg);

  Running = (phase[0] == ttRun) || (phase[1] == ttRun);

  LoadThrusterInputs();
  double power = HP * hptoftlbssec;
  if (RPM <= 0.1) power = max(power, 0.0); // mirror FGTurboProp: no negative power at standstill
  Thruster->Calculate(power);

  RunPostFunctions();
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Per-side "off": windmilling N1, ITT toward ambient, no combustor fuel.

double FGTwinTurboshaft::SideOff(int s)
{
  EngStarting[s] = false;

  FuelFlowSide[s] = Seek(&FuelFlowSide[s], 0, 800.0, 800.0);

  N1[s] = ExpSeek(&N1[s], in.qbar / 15.0, Idle_Max_Delay * 2.5, Idle_Max_Delay * 5);

  Eng_Temperature[s] = ExpSeek(&Eng_Temperature[s], in.TAT_c, 300, 400);
  double ITT_goal = ITT_N1->GetValue(N1[s], 0.1)
                    + ((N1[s] > 20) ? 0.0 : (20 - N1[s]) / 20.0 * Eng_Temperature[s]);
  Eng_ITT_degC[s] = ExpSeek(&Eng_ITT_degC[s], ITT_goal, ITT_Delay, ITT_Delay * 1.2);

  if (RPM > 5) return -0.012; // small negative HP: engine friction when rotor windmills
  return 0.0;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Per-side "running": same power/fuel/ITT path as FGTurboProp::Run.

double FGTwinTurboshaft::SideRun(int s, double throttlePos)
{
  double EngPower_HP;

  EngStarting[s] = false;
  Starter = false;

  double old_N1 = N1[s];
  N1[s] = ExpSeek(&N1[s], IdleN1 + throttlePos * N1_factor, Idle_Max_Delay, Idle_Max_Delay * 2.4);

  EngPower_HP = EnginePowerRPM_N1->GetValue(RPM, N1[s]);
  EngPower_HP *= EnginePowerVC->GetValue();
  if (EngPower_HP > MaxPower) EngPower_HP = MaxPower;

  CombustionEfficiency[s] = CombustionEfficiency_N1->GetValue(N1[s]);
  FuelFlowSide[s] = PSFC / CombustionEfficiency[s] * EngPower_HP;

  Eng_Temperature[s] = ExpSeek(&Eng_Temperature[s], Eng_ITT_degC[s], 300, 400);
  double ITT_goal = ITT_N1->GetValue((N1[s] - old_N1) * 300 + N1[s], 1);
  Eng_ITT_degC[s] = ExpSeek(&Eng_ITT_degC[s], ITT_goal, ITT_Delay, ITT_Delay * 1.2);

  if (Cutoff[s]) phase[s] = ttOff;
  if (Starved) phase[s] = ttOff;

  return EngPower_HP;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Per-side starter spin-up (GeneratorPower required, same as turboprop).

double FGTwinTurboshaft::SideSpinUp(int s)
{
  double EngPower_HP;
  EngStarting[s] = true;

  if (!GeneratorPower) {
    EngStarting[s] = false;
    phase[s] = ttOff;
    StartTime[s] = -1;
    return 0.0;
  }

  N1[s] = ExpSeek(&N1[s], StarterN1, Idle_Max_Delay * 6, Idle_Max_Delay * 2.4);

  Eng_Temperature[s] = ExpSeek(&Eng_Temperature[s], in.TAT_c, 300, 400);
  double ITT_goal = ITT_N1->GetValue(N1[s], 0.1)
                    + ((N1[s] > 20) ? 0.0 : (20 - N1[s]) / 20.0 * Eng_Temperature[s]);
  Eng_ITT_degC[s] = ExpSeek(&Eng_ITT_degC[s], ITT_goal, ITT_Delay, ITT_Delay * 1.2);

  EngPower_HP = EnginePowerRPM_N1->GetValue(RPM, N1[s]);
  EngPower_HP *= EnginePowerVC->GetValue();
  if (EngPower_HP > MaxPower) EngPower_HP = MaxPower;

  FuelFlowSide[s] = 0.0;

  if (StartTime[s] >= 0) StartTime[s] += in.TotalDeltaT;
  if (StartTime[s] > MaxStartingTime && MaxStartingTime > 0) {
    phase[s] = ttOff;
    StartTime[s] = -1;
  }

  return EngPower_HP;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Light-off once N1 > 15%; below IdleN1 burns fuel until spool-up completes.

double FGTwinTurboshaft::SideStart(int s)
{
  double EngPower_HP = 0.0;

  EngStarting[s] = false;
  if ((N1[s] > 15.0) && !Starved) {
    double old_N1 = N1[s];
    Cranking = true;
    if (N1[s] < IdleN1) {
      EngPower_HP = EnginePowerRPM_N1->GetValue(RPM, N1[s]);
      EngPower_HP *= EnginePowerVC->GetValue();
      if (EngPower_HP > MaxPower) EngPower_HP = MaxPower;
      N1[s] = ExpSeek(&N1[s], IdleN1 * 1.1, Idle_Max_Delay * 4, Idle_Max_Delay * 2.4);
      CombustionEfficiency[s] = CombustionEfficiency_N1->GetValue(N1[s]);
      FuelFlowSide[s] = PSFC / CombustionEfficiency[s] * EngPower_HP;
      Eng_Temperature[s] = ExpSeek(&Eng_Temperature[s], Eng_ITT_degC[s], 300, 400);
      double ITT_goal = ITT_N1->GetValue((N1[s] - old_N1) * 300 + N1[s], 1);
      Eng_ITT_degC[s] = ExpSeek(&Eng_ITT_degC[s], ITT_goal, ITT_Delay, ITT_Delay * 1.2);

    } else {
      phase[s] = ttRun;
      Cranking = false;
      FuelFlowSide[s] = 0;
    }
  } else {
    phase[s] = ttOff;
  }

  return EngPower_HP;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Phase transitions (mirrors FGTurboProp logic, applied independently per side).

void FGTwinTurboshaft::UpdatePhasesBeforePower(void)
{
  for (int s = 0; s < NSides; ++s) {
    if ((phase[s] == ttTrim) && (in.TotalDeltaT > 0)) {
      if (Running && !Starved) {
        phase[s] = ttRun;
        N1[s] = IdleN1;
        OilTemp_degK = 366.0;
        Cutoff[s] = false;
      } else {
        phase[s] = ttOff;
        Cutoff[s] = true;
        Eng_ITT_degC[s] = in.TAT_c;
        Eng_Temperature[s] = in.TAT_c;
        OilTemp_degK = in.TAT_c + 273.15;
      }
    }
  }

  if (!Running && Starter) {
    for (int s = 0; s < NSides; ++s) {
      if (phase[s] == ttOff) {
        phase[s] = ttSpinUp;
        if (StartTime[s] < 0) StartTime[s] = 0;
      }
    }
  }

  for (int s = 0; s < NSides; ++s) {
    if (!Running && !Cutoff[s] && (N1[s] > 15.0)) {
      phase[s] = ttStart;
      StartTime[s] = -1;
    }
    if (Cutoff[s] && (phase[s] != ttSpinUp)) phase[s] = ttOff;
  }

  if (in.TotalDeltaT == 0) {
    for (int s = 0; s < NSides; ++s) phase[s] = ttTrim; // trim pass: hold until integration resumes
  }

  if (Starved) {
    for (int s = 0; s < NSides; ++s) phase[s] = ttOff;
  }

  for (int s = 0; s < NSides; ++s) {
    if (Condition[s] >= 10) {
      phase[s] = ttOff;
      StartTime[s] = -1;
    }
  }
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Torque limiter: reduces commanded throttle(s) when main rotor torque exceeds
// ielumaxtorque (disabled if ielumaxtorque <= 0).

void FGTwinTurboshaft::ApplyIelu(void)
{
  if (Ielu_max_torque <= 0.0) {
    Ielu_intervent = false;
    OldThrottle = ThrottlePos;
    return;
  }

  double torque = static_cast<FGRotor*>(Thruster)->GetTorque();
  bool anyCondition = (Condition[0] >= 1) || (Condition[1] >= 1);

  if (!anyCondition) {
    if (fabs(torque) > Ielu_max_torque && ThrottlePos >= OldThrottle) {
      ThrottlePos = OldThrottle - 0.1 * in.TotalDeltaT;
      if (independent_throttles) {
        ThrottleNormSide[0] = max(0.0, ThrottleNormSide[0] - 0.1 * in.TotalDeltaT);
        ThrottleNormSide[1] = max(0.0, ThrottleNormSide[1] - 0.1 * in.TotalDeltaT);
      }
      Ielu_intervent = true;
    } else if (Ielu_intervent && ThrottlePos >= OldThrottle) {
      ThrottlePos = OldThrottle + 0.05 * in.TotalDeltaT;
      if (independent_throttles) {
        ThrottleNormSide[0] = min(1.0, ThrottleNormSide[0] + 0.05 * in.TotalDeltaT);
        ThrottleNormSide[1] = min(1.0, ThrottleNormSide[1] + 0.05 * in.TotalDeltaT);
      }
      Ielu_intervent = true;
    } else {
      Ielu_intervent = false;
    }
  } else {
    Ielu_intervent = false;
  }

  OldThrottle = ThrottlePos;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// Single oil model from average N1 (avoids double-applying oil dynamics per side).

void FGTwinTurboshaft::UpdateOil(double n1_avg)
{
  if (n1_avg > 25.0)
    OilTemp_degK = Seek(&OilTemp_degK, 353.15, 0.4 - n1_avg * 0.001, 0.04);
  else
    OilTemp_degK = ExpSeek(&OilTemp_degK, 273.15 + in.TAT_c, 400, 400);

  OilPressure_psi =
    (n1_avg / 100.0 * 0.25 + (0.1 - (OilTemp_degK - 273.15) * 0.1 / 80.0) * n1_avg / 100.0)
    / 7692.0e-6;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

double FGTwinTurboshaft::CalcFuelNeed(void)
{
  FuelFlowRate = FuelFlow_pph / 3600.0;
  FuelExpended = FuelFlowRate * in.TotalDeltaT;
  if (!Starved) FuelUsedLbs += FuelExpended;
  return FuelExpended;
}

// Same ramp helpers as FGTurboProp (linear / first-order lag).

double FGTwinTurboshaft::Seek(double* var, double target, double accel, double decel)
{
  double v = *var;
  if (v > target) {
    v -= in.TotalDeltaT * decel;
    if (v < target) v = target;
  } else if (v < target) {
    v += in.TotalDeltaT * accel;
    if (v > target) v = target;
  }
  return v;
}

double FGTwinTurboshaft::ExpSeek(double* var, double target, double accel_tau, double decel_tau)
{
  double v = *var;
  if (v > target) {
    v = (v - target) * exp(-in.TotalDeltaT / decel_tau) + target;
  } else if (v < target) {
    v = (target - v) * (1 - exp(-in.TotalDeltaT / accel_tau)) + v;
  }
  return v;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGTwinTurboshaft::SetDefaults(void)
{
  for (int s = 0; s < NSides; ++s) {
    N1[s] = 0.0;
    phase[s] = ttOff;
    Eng_ITT_degC[s] = 0.0;
    Cutoff[s] = true;
    EngStarting[s] = false;
    StartTime[s] = -1;
    HP_side[s] = 0.0;
    FuelFlowSide[s] = 0.0;
    CombustionEfficiency[s] = 1.0;
    Eng_Temperature[s] = 0.0;
    Condition[s] = 0;
    ThrottleNormSide[s] = 0.0;
  }
  HP = 0.0;
  Type = etTwinTurboshaft;
  IdleN1 = 30.0;
  MaxN1 = 100.0;
  GeneratorPower = true;
  Ielu_intervent = false;
  Idle_Max_Delay = 1.0;
  ThrottlePos = OldThrottle = 0.0;
  ITT_Delay = 0.05;
  independent_throttles = false;
}

void FGTwinTurboshaft::SetCutoff(bool cutoff)
{
  Cutoff[0] = Cutoff[1] = cutoff;
}

void FGTwinTurboshaft::SetCutoff(int side, bool cutoff)
{
  if (side >= 0 && side < NSides) Cutoff[side] = cutoff;
}

void FGTwinTurboshaft::SetCondition(int c)
{
  Condition[0] = Condition[1] = c;
}

void FGTwinTurboshaft::SetCondition(int side, int c)
{
  if (side >= 0 && side < NSides) Condition[side] = c;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

string FGTwinTurboshaft::GetEngineLabels(const string& delimiter)
{
  ostringstream buf;
  buf << Name << "_N1_0[" << EngineNumber << "]" << delimiter << Name << "_N1_1[" << EngineNumber
      << "]" << delimiter << Name << "_PwrAvail[" << EngineNumber << "]" << delimiter
      << Thruster->GetThrusterLabels(EngineNumber, delimiter);
  return buf.str();
}

string FGTwinTurboshaft::GetEngineValues(const string& delimiter)
{
  ostringstream buf;
  buf << N1[0] << delimiter << N1[1] << delimiter << HP << delimiter
      << Thruster->GetThrusterValues(EngineNumber, delimiter);
  return buf.str();
}

int FGTwinTurboshaft::InitRunning(void)
{
  double dt = in.TotalDeltaT;
  in.TotalDeltaT = 0.0;
  SetCutoff(false);
  Running = true;
  for (int s = 0; s < NSides; ++s) {
    phase[s] = ttRun;
    Cutoff[s] = false;
  }
  Calculate();
  in.TotalDeltaT = dt;
  return (phase[0] == ttRun) && (phase[1] == ttRun);
}

// throttle-norm-side-* are only tied when XML requests independent_throttles;
// otherwise both sides track FCS throttle via in.ThrottlePos in Calculate().

void FGTwinTurboshaft::bindmodel(FGPropertyManager* PropertyManager, bool independent_throttle_bind)
{
  string base = CreateIndexedPropertyName("propulsion/engine", EngineNumber);
  PropertyManager->Tie((base + "/n1-side-0").c_str(), &N1[0]);
  PropertyManager->Tie((base + "/n1-side-1").c_str(), &N1[1]);
  PropertyManager->Tie((base + "/itt-c-side-0").c_str(), &Eng_ITT_degC[0]);
  PropertyManager->Tie((base + "/itt-c-side-1").c_str(), &Eng_ITT_degC[1]);
  PropertyManager->Tie((base + "/power-hp").c_str(), &HP);
  PropertyManager->Tie((base + "/power-hp-side-0").c_str(), &HP_side[0]);
  PropertyManager->Tie((base + "/power-hp-side-1").c_str(), &HP_side[1]);
  PropertyManager->Tie((base + "/ielu_intervent").c_str(), &Ielu_intervent);
  PropertyManager->Tie((base + "/combustion_efficiency-side-0").c_str(), &CombustionEfficiency[0]);
  PropertyManager->Tie((base + "/combustion_efficiency-side-1").c_str(), &CombustionEfficiency[1]);
  PropertyManager->Tie((base + "/engtemp-c-side-0").c_str(), &Eng_Temperature[0]);
  PropertyManager->Tie((base + "/engtemp-c-side-1").c_str(), &Eng_Temperature[1]);
  PropertyManager->Tie((base + "/cutoff-side-0").c_str(), &Cutoff[0]);
  PropertyManager->Tie((base + "/cutoff-side-1").c_str(), &Cutoff[1]);
  if (independent_throttle_bind) {
    PropertyManager->Tie((base + "/throttle-norm-side-0").c_str(), &ThrottleNormSide[0]);
    PropertyManager->Tie((base + "/throttle-norm-side-1").c_str(), &ThrottleNormSide[1]);
  }
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

void FGTwinTurboshaft::Debug(int from)
{
  if (debug_lvl <= 0) return;
  if (debug_lvl & 1) {
    if (from == 2) {
      cout << "\n **** Twin turboshaft engine ****\n";
      cout << "    Engine Name: " << Name << endl;
      cout << "      IdleN1:      " << IdleN1 << endl;
      cout << "      MaxN1:       " << MaxN1 << endl;
    }
  }
  if (debug_lvl & 2) {
    if (from == 0) cout << "Instantiated: FGTwinTurboshaft" << endl;
    if (from == 1) cout << "Destroyed:    FGTwinTurboshaft" << endl;
  }
}

} // namespace JSBSim

#include "HumanoidWorld.hpp"

#include <functional>
#include <vector>
#include <algorithm>
#include "ode/ode.h"

#include "bib/Utils.hpp"
#include "ODEFactory.hpp"


// void dRFrom3Points(dMatrix3 R, double X1x, double X1y, double X1z,
//                    double Y1x, double Y1y, double Y1z,
//                    double Z1x, double Z1y, double Z1z) {
//   double pre;
//   double nut;
//   double rot;
//
//   double Z1xy = sqrt (Z1x*Z1x + Z1y*Z1y);
//   LOG_DEBUG("" << Z1xy);
//   if (Z1xy > DBL_EPSILON) {
//     pre = atan2 (Y1x*Z1y - Y1y*Z1x, X1x*Z1y - X1y*Z1x);
//     nut = atan2 (Z1xy, Z1z);
//     rot = -atan2 (-Z1x, Z1y);
//   } else {
//     pre = 0.f;
//     nut = (Z1z > 0.) ? 0. : M_PI;
//     rot = -atan2 (X1y, X1x);
//   }
//
//   LOG_DEBUG(pre << " " << nut << " " << rot);
//   dRFromEulerAngles(R, pre, nut, rot);
// }

// void EulerToAxis(dVector3 R, const dReal* E) {
//   double heading = E[0];
//   double attitude = E[1];
//   double bank = E[2];
//   // Assuming the angles are in radians.
//   double c1 = std::cos(heading/2);
//   double s1 = std::sin(heading/2);
//   double c2 = std::cos(attitude/2);
//   double s2 = std::sin(attitude/2);
//   double c3 = std::cos(bank/2);
//   double s3 = std::sin(bank/2);
//   double c1c2 = c1*c2;
//   double s1s2 = s1*s2;
//   double w =c1c2*c3 - s1s2*s3;
//   double x =c1c2*s3 + s1s2*c3;
//   double y =s1*c2*c3 + c1*s2*s3;
//   double z =c1*s2*c3 - s1*c2*s3;
//   double angle = 2 * std::acos(w);
//   double norm = x*x+y*y+z*z;
//   if (norm < 0.001) { // when all euler angles are zero angle =0 so
//     // we can set axis to anything to avoid divide by zero
//     x=1;
//     y=z=0;
//   } else {
//     norm = std::sqrt(norm);
//     x /= norm;
//     y /= norm;
//     z /= norm;
//   }
//   
//   R[0] = x;
//   R[1] = y;
//   R[2] = z;
// }

double softstepf(double x) {
  if (x<=0)
    return 0;
  else if(x <= 0.5)
    return 2*x*x;
  else if(x <= 1)
    return 1 - 2*(x-1)*(x-1);
  else
    return 1;
}

HumanoidWorld::HumanoidWorld(humanoid_physics _phy) : odeworld(ODEFactory::getInstance()->createWorld(false)),
  phy(_phy) {

  dWorldSetGravity(odeworld.world_id, 0, 0.0, GRAVITY);

//   dContact contact[2];          // up to 3 contacts
  contact[0].surface.mode = dContactApprox0;
  contact[1].surface.mode = dContactApprox0;

  if (phy.approx == 1) {
    contact[0].surface.mode = contact[0].surface.mode | dContactApprox1;
    contact[1].surface.mode = contact[1].surface.mode | dContactApprox1;
  } else if (phy.approx == 2) {
    contact[0].surface.mode = contact[0].surface.mode | dContactApprox1_1;
    contact[1].surface.mode = contact[1].surface.mode | dContactApprox1_1;
  } else if (phy.approx == 3) {
    contact[0].surface.mode = contact[0].surface.mode | dContactApprox1_N;
    contact[1].surface.mode = contact[1].surface.mode | dContactApprox1_N;
  }

  if (phy.mu2 >= 0.0000f) {
    contact[0].surface.mode = contact[0].surface.mode | dContactMu2;
    contact[1].surface.mode = contact[1].surface.mode | dContactMu2;
  }

  if (phy.soft_cfm >= 0.0000f) {
    contact[0].surface.mode = contact[0].surface.mode | dContactSoftCFM;
    contact[1].surface.mode = contact[1].surface.mode | dContactSoftCFM;
  }

  if (phy.slip1 >= 0.0000f) {
    contact[0].surface.mode = contact[0].surface.mode | dContactSlip1;
    contact[1].surface.mode = contact[1].surface.mode | dContactSlip1;
  }

  if (phy.slip2 >= 0.0000f) {
    contact[0].surface.mode = contact[0].surface.mode | dContactSlip2;
    contact[1].surface.mode = contact[1].surface.mode | dContactSlip2;
  }

  if (phy.soft_erp >= 0.0000f) {
    contact[0].surface.mode = contact[0].surface.mode | dContactSoftERP;
    contact[1].surface.mode = contact[1].surface.mode | dContactSoftERP;
  }

  if (phy.bounce >= 0.0000f) {
    contact[0].surface.mode = contact[0].surface.mode | dContactBounce;
    contact[1].surface.mode = contact[1].surface.mode | dContactBounce;
  }

  contact[0].surface.mu = phy.mu;
  contact[0].surface.mu2 = phy.mu2;
  contact[0].surface.soft_cfm = phy.soft_cfm;
  contact[0].surface.slip1 = phy.slip1;
  contact[0].surface.slip2 = phy.slip2;
  contact[0].surface.soft_erp = phy.soft_erp;
  contact[0].surface.bounce = phy.bounce;

  contact[1].surface.mu = phy.mu;
  contact[1].surface.mu2 = phy.mu2;
  contact[1].surface.soft_cfm = phy.soft_cfm;
  contact[1].surface.slip1 = phy.slip1;
  contact[1].surface.slip2 = phy.slip2;
  contact[1].surface.soft_erp = phy.soft_erp;
  contact[1].surface.bounce = phy.bounce;

  createWorld();

  head_touch = false;
  fknee_touch = false;
  bknee_touch = false;
  penalty = 0;

  if(phy.predev == 0)
    internal_state.resize(18);
  else if(phy.predev == 1 || phy.predev == 10)
    internal_state.resize(18 - 4);
  else if(phy.predev == 2 || phy.predev == 11)
    internal_state.resize(18);
  else if(phy.predev == 3 || phy.predev == 12)
    internal_state.resize(18);

  update_state();
}

HumanoidWorld::~HumanoidWorld() {
  for (ODEObject * o : bones) {
    dGeomDestroy(o->getGeom());
    if(o->getID() != nullptr)
      dBodyDestroy(o->getID());
    delete o;
  }

  dGeomDestroy(ground);

  ODEFactory::getInstance()->destroyWorld(odeworld);
}

void HumanoidWorld::apply_armature(dMass* m, double k) {
  if(!phy.apply_armature)
    return;

  m->I[0] = m->I[0] + k;
  m->I[3] = m->I[3] + k;
  m->I[6] = m->I[6] + k;
}

void HumanoidWorld::apply_damping(dBodyID body, double v) {
  if(phy.damping == 1)
    dBodySetLinearDampingThreshold(body, v);
  else if(phy.damping == 2)
    dBodySetAngularDampingThreshold(body, v);
  else if(phy.damping == 3)
    dBodySetLinearDamping(body, v);
  else if(phy.damping == 4)
    dBodySetAngularDamping(body, v);
}

void HumanoidWorld::createWorld() {
  ground = ODEFactory::getInstance()->createGround(odeworld);

//   dWorldSetCFM(odeworld.world_id, 1.);
//   dWorldSetERP(odeworld.world_id, 1.);

//   <compiler angle="degree" inertiafromgeom="true"/>
  double density = 1062;  // Average human body density;

//   <joint limited='true' damping='.01' armature='.1' stiffness='8' solreflimit='.02 1' solimplimit='0 .8 .03' />
//   <joint armature="1" damping="1" limited="true"/>
//   dWorldSetLinearDamping(odeworld.world_id, .01);
  if(phy.damping == 1)
    dWorldSetLinearDampingThreshold(odeworld.world_id, 1);
  else if(phy.damping == 2)
    dWorldSetAngularDampingThreshold(odeworld.world_id, 1);
  else if(phy.damping == 3)
    dWorldSetLinearDamping(odeworld.world_id, 1);
  else if(phy.damping == 4)
    dWorldSetAngularDamping(odeworld.world_id, 1);

//   armature
//     Armature inertia (or rotor inertia) of all degrees of freedom created by this joint. These are constants added to the diagonal of the inertia matrix in generalized coordinates. They make the simulation more stable, and often increase physical realism. This is because when a motor is attached to the system with a transmission that amplifies the motor force by c, the inertia of the rotor (i.e. the moving part of the motor) is amplified by c*c. The same holds for gears in the early stages of planetary gear boxes. These extra inertias often dominate the inertias of the robot parts that are represented explicitly in the model, and the armature attribute is the way to model them.
//   stiffness
//   A positive value generates a spring force (linear in position) acting along the tendon. The equilibrium length of the spring corresponds to the tendon length when the model is in its initial configuration.
//   solreflimit, solimplimit
//   Constraint solver parameters for simulating tendon limits. See Solver parameters.

//   <geom condim="3" friction="1 .1 .1" material="MatPlane" name="floor" pos="0 0 0" rgba="0.8 0.9 0.8 1" size="20 20 0.125" type="plane"/>
//   <geom conaffinity="1" condim="1" contype="1" margin="0.001" material="geom" rgba="0.8 0.6 .4 1"/>
//   done in collision

  double old_body_pos[3] = {0, 0, 0};
//   <body name="torso" pos="0 0 1.4">
  dBodyID torso = dBodyCreate(odeworld.world_id);
  old_body_pos[2] += 1.4;
  dBodySetPosition(torso, old_body_pos[0], old_body_pos[1], old_body_pos[2]);
  double torso_body_pos[3] = {old_body_pos[0], old_body_pos[1], old_body_pos[2]};

//   <joint name='rootx' type='slide' pos='0 0 0' axis='1 0 0' limited='false' damping='0' armature='0' stiffness='0' />
//   <joint name='rootz' type='slide' pos='0 0 0' axis='0 0 1' limited='false' damping='0' armature='0' stiffness='0' />
//   <joint name='rooty' type='hinge' pos='0 0 0' axis='0 1 0' limited='false' damping='0' armature='0' stiffness='0' />

//   <joint armature="0" damping="0" limited="false" name="root" pos="0 0 0" stiffness="0" type="free"/>


//   <geom fromto="0 -.07 0 0 .07 0" name="torso1" size="0.07" type="capsule"/>
  dGeomID g_torso1 = dCreateCapsule(odeworld.space_id, 0.07, 0.14f);
  dGeomSetBody(g_torso1, torso);
  int rot_direction = 1;
  dMatrix3 Rot_y;
  dRFromAxisAndAngle(Rot_y, 1, 0, 0, M_PI/2.f);
  dGeomSetOffsetRotation(g_torso1, Rot_y);

  dMass m_torso;
  dMassSetCapsule(&m_torso, density, rot_direction, 0.07, 0.14f);

//   <geom name="head" pos="0 0 .19" size=".09" type="sphere" user="258"/>
  dGeomID g_head = dCreateSphere(odeworld.space_id, 0.09f);
  dGeomSetBody(g_head, torso);
  dGeomSetOffsetPosition(g_head, 0, 0, 0.19);

  dMass m_head;
  dMassSetSphere(&m_head, density, 0.09f);

//   <geom fromto="-.01 -.06 -.12 -.01 .06 -.12" name="uwaist" size="0.06" type="capsule"/>
  dGeomID g_uwaist = dCreateCapsule(odeworld.space_id, 0.06f, 0.12f);
  dGeomSetBody(g_uwaist, torso);
  dGeomSetOffsetRotation(g_uwaist, Rot_y);
  dGeomSetOffsetPosition(g_uwaist, -.01, 0, -0.12);

  dMass m_uwaist;
  dMassSetCapsule(&m_uwaist, density, rot_direction, 0.06, 0.12f);

  dMassAdd(&m_torso, &m_head);
  dMassAdd(&m_torso, &m_uwaist);
  dBodySetMass(torso, &m_torso);

//   <body name="lwaist" pos="-.01 0 -0.260" quat="1.000 0 -0.002 0">
  dBodyID lwaist = dBodyCreate(odeworld.world_id);
  old_body_pos[0] += -0.01;
  old_body_pos[2] += -0.260;
  dBodySetPosition(lwaist, old_body_pos[0], old_body_pos[1], old_body_pos[2]);
  dQuaternion q_lwaist = {1., 0, -0.002, 0};
  dBodySetQuaternion(lwaist, q_lwaist);

//   <geom fromto="0 -.06 0 0 .06 0" name="lwaist" size="0.06" type="capsule"/>
  dGeomID g_lwaist = dCreateCapsule(odeworld.space_id, 0.06f, 0.12f);
  dGeomSetBody(g_lwaist, lwaist);
  dGeomSetOffsetRotation(g_lwaist, Rot_y);

  dMass m_lwaist;
  dMassSetCapsule(&m_lwaist, density, rot_direction, 0.06, 0.12f);
  dBodySetMass(lwaist, &m_lwaist);

//   <joint armature="0.02" axis="0 0 1" damping="5" name="abdomen_z" pos="0 0 0.065" range="-45 45" stiffness="20" type="hinge"/>
//   <joint armature="0.02" axis="0 1 0" damping="5" name="abdomen_y" pos="0 0 0.065" range="-75 30" stiffness="10" type="hinge"/>
  dJointID j_abdomen_zy = dJointCreateUniversal(odeworld.world_id, nullptr);
  dJointAttach(j_abdomen_zy, lwaist, torso);
  dJointSetUniversalAxis1(j_abdomen_zy, 0, 0, 1);//no body attached no effect
  dJointSetUniversalAxis2(j_abdomen_zy, 0, 1, 0);//no body attached no effect
  dJointSetUniversalParam(j_abdomen_zy, dParamLoStop, -45*M_PI/180.f);
  dJointSetUniversalParam(j_abdomen_zy, dParamHiStop, 45*M_PI/180.f);
  dJointSetUniversalParam(j_abdomen_zy, dParamLoStop2, -75*M_PI/180.f);
  dJointSetUniversalParam(j_abdomen_zy, dParamHiStop2, 30*M_PI/180.f);
  dJointSetUniversalAnchor(j_abdomen_zy, old_body_pos[0], old_body_pos[1], old_body_pos[2] + 0.065);
  apply_damping(lwaist, 5);

//   <body name="pelvis" pos="0 0 -0.165" quat="1.000 0 -0.002 0">
  dBodyID pelvis = dBodyCreate(odeworld.world_id);
  old_body_pos[2] += -0.165;
  dBodySetPosition(pelvis, old_body_pos[0], old_body_pos[1], old_body_pos[2]);
  dBodySetQuaternion(pelvis, q_lwaist);
  double pelvis_body_pos[3] = {old_body_pos[0], old_body_pos[1], old_body_pos[2]};

//   <geom fromto="-.02 -.07 0 -.02 .07 0" name="butt" size="0.09" type="capsule"/>
  dGeomID g_butt = dCreateCapsule(odeworld.space_id, 0.09f, 0.14f);
  dGeomSetBody(g_butt, pelvis);
  dGeomSetOffsetRotation(g_butt, Rot_y);
  dGeomSetOffsetPosition(g_butt, -0.02, 0, 0);

  dMass m_pelvis;
  dMassSetCapsule(&m_pelvis, density, rot_direction, 0.09, 0.14);
  dBodySetMass(pelvis, &m_pelvis);

//   <joint armature="0.02" axis="1 0 0" damping="5" name="abdomen_x" pos="0 0 0.1" range="-35 35" stiffness="10" type="hinge"/>
  dJointID j_abdomen_x = dJointCreateHinge(odeworld.world_id, nullptr);
  dJointAttach(j_abdomen_x, pelvis, lwaist);
  dJointSetHingeAxis(j_abdomen_x, 1, 0, 0);//no body attached no effect
  dJointSetHingeParam(j_abdomen_x, dParamLoStop, -35*M_PI/180.f);
  dJointSetHingeParam(j_abdomen_x, dParamHiStop, 35*M_PI/180.f);
  dJointSetHingeAnchor(j_abdomen_x, old_body_pos[0], old_body_pos[1], old_body_pos[2] + 0.1f);
  apply_damping(pelvis, 6);
  
//   <body name="right_thigh" pos="0 -0.1 -0.04">
  dBodyID right_thigh = dBodyCreate(odeworld.world_id);
  old_body_pos[1] += -0.1;
  old_body_pos[2] += -0.04;
  dBodySetPosition(right_thigh, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   <geom fromto="0 0 0 0 0.01 -.34" name="right_thigh1" size="0.06" type="capsule"/>
  dGeomID g_right_thigh1 = dCreateCapsule(odeworld.space_id, 0.06f, sqrt(0.01*0.01 + 0.34*0.34));
  dGeomSetBody(g_right_thigh1, right_thigh);

  dMatrix3 R_right_thigh1;
//   dRFrom3Points(R_right_thigh1,
//                 0., 0., -0.34,
//                 0, 0., 0,
//                 0., 0.01, -0.34
//                 );
  dRFromAxisAndAngle(R_right_thigh1, -1, 0, 0, 3.11219f);

  dGeomSetOffsetRotation(g_right_thigh1, R_right_thigh1);
  dGeomSetOffsetPosition(g_right_thigh1, 0., 0.01/2.f, -0.34/2.f);

  dMass m_right_thigh;
  dMassSetCapsule(&m_right_thigh, density, 3, 0.06, sqrt(0.01*0.01 + 0.34*0.34));
  dBodySetMass(right_thigh, &m_right_thigh);

//   <joint armature="0.01" axis="1 0 0" damping="5" name="right_hip_x" pos="0 0 0" range="-25 5" stiffness="10" type="hinge"/>
//   <joint armature="0.01" axis="0 0 1" damping="5" name="right_hip_z" pos="0 0 0" range="-60 35" stiffness="10" type="hinge"/>
//   <joint armature="0.0080" axis="0 1 0" damping="5" name="right_hip_y" pos="0 0 0" range="-110 20" stiffness="20" type="hinge"/>
  dJointID j_right_hip_xyz2 = dJointCreateBall(odeworld.world_id, nullptr);
  dJointAttach(j_right_hip_xyz2, pelvis, right_thigh);
  dJointSetBallAnchor(j_right_hip_xyz2, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   ODEObject* debug2= ODEFactory::getInstance()->createSphere(odeworld, old_body_pos[0],
//                                                              old_body_pos[1], old_body_pos[2], 0.01, 10, true);
  
  dJointID j_right_hip_xyz = dJointCreateAMotor(odeworld.world_id, nullptr);
  dJointAttach(j_right_hip_xyz, pelvis, right_thigh);
  dJointSetAMotorNumAxes(j_right_hip_xyz, 3);
  dJointSetAMotorMode(j_right_hip_xyz, dAMotorEuler);
  dJointSetAMotorAxis(j_right_hip_xyz, 0,1,0,1,0);
  dJointSetAMotorAxis(j_right_hip_xyz, 2,2,0,0,1);
  //[-25 5] axis x PI/2 ok
  //[-110 20] axis y out of PI/2
  //[-60 35] axis z in PI/2
  // axis z anchor to body 2
  // axis x or y anchored to body 1
  // axis y out of PI/2 so it must be controled
  dJointSetAMotorParam(j_right_hip_xyz, dParamLoStop3, -25*M_PI/180.f);
  dJointSetAMotorParam(j_right_hip_xyz, dParamHiStop3, 5*M_PI/180.f);
  dJointSetAMotorParam(j_right_hip_xyz, dParamLoStop, -110*M_PI/180.f);
  dJointSetAMotorParam(j_right_hip_xyz, dParamHiStop, 20*M_PI/180.f);
  dJointSetAMotorParam(j_right_hip_xyz, dParamLoStop2, -60*M_PI/180.f);
  dJointSetAMotorParam(j_right_hip_xyz, dParamHiStop2, 35*M_PI/180.f);

//   <body name="right_shin" pos="0 0.01 -0.403">
  dBodyID right_shin = dBodyCreate(odeworld.world_id);
  old_body_pos[1] += 0.01;
  old_body_pos[2] += -0.403;
  dBodySetPosition(right_shin, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   <geom fromto="0 0 0 0 0 -.3" name="right_shin1" size="0.049" type="capsule"/>
  dGeomID g_right_shin1 = dCreateCapsule(odeworld.space_id, 0.049f, .3);
  dGeomSetBody(g_right_shin1, right_shin);
  dGeomSetOffsetPosition(g_right_shin1, 0,0, -.3/2.f);

  dMass m_right_shin;
  dMassSetCapsule(&m_right_shin, density, 3, 0.049, .3);

//   <joint armature="0.0060" axis="0 -1 0" name="right_knee" pos="0 0 .02" range="-160 -2" type="hinge"/>
  dJointID j_right_knee = dJointCreateHinge(odeworld.world_id, nullptr);
  dJointAttach(j_right_knee, right_thigh, right_shin);
  dJointSetHingeAxis(j_right_knee, 0, -1, 0);
  dJointSetHingeParam(j_right_knee, dParamHiStop, -2*M_PI/180.f);
  dJointSetHingeParam(j_right_knee, dParamLoStop, -150*M_PI/180.f);//160 seems to bug (knee does one loop)
  dJointSetHingeAnchor(j_right_knee, old_body_pos[0], old_body_pos[1], old_body_pos[2]+ .02);

//   <body name="right_foot" pos="0 0 -0.45">
//   <geom name="right_foot" pos="0 0 0.1" size="0.075" type="sphere" user="0"/>
  dGeomID g_right_foot = dCreateSphere(odeworld.space_id, 0.075f);
  dGeomSetBody(g_right_foot, right_shin);
  dGeomSetOffsetPosition(g_right_foot, 0, 0, -0.45f + 0.1f);
  
  dMass m_right_foot;
  dMassSetSphere(&m_right_foot, density, 0.075f);
  dMassAdd(&m_right_shin, &m_right_foot);
  dBodySetMass(right_shin, &m_right_shin);

//   <body name="left_thigh" pos="0 0.1 -0.04">
  dBodyID left_thigh = dBodyCreate(odeworld.world_id);
  old_body_pos[0] = pelvis_body_pos[0];
  old_body_pos[1] = pelvis_body_pos[1];
  old_body_pos[2] = pelvis_body_pos[2];
  old_body_pos[1] += 0.1;
  old_body_pos[2] += -0.04;
  dBodySetPosition(left_thigh, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   <geom fromto="0 0 0 0 -0.01 -.34" name="left_thigh1" size="0.06" type="capsule"/>
  dGeomID g_left_thigh1 = dCreateCapsule(odeworld.space_id, 0.06f, sqrt(0.01*0.01 + 0.34*0.34));
  dGeomSetBody(g_left_thigh1, left_thigh);
  dMatrix3 R_left_thigh1;
  dRFromAxisAndAngle(R_left_thigh1, 1, 0, 0, 3.11219f);
  dGeomSetOffsetRotation(g_left_thigh1, R_left_thigh1);
  dGeomSetOffsetPosition(g_left_thigh1, 0., 0.01/2.f, -0.34/2.f);
  
  dMass m_left_thigh;
  dMassSetCapsule(&m_left_thigh, density, 3, 0.06, sqrt(0.01*0.01 + 0.34*0.34));
  dBodySetMass(left_thigh, &m_left_thigh);

//   <joint armature="0.01" axis="-1 0 0" damping="5" name="left_hip_x" pos="0 0 0" range="-25 5" stiffness="10" type="hinge"/>
//   <joint armature="0.01" axis="0 1 0" damping="5" name="left_hip_y" pos="0 0 0" range="-120 20" stiffness="20" type="hinge"/>
//   <joint armature="0.01" axis="0 0 -1" damping="5" name="left_hip_z" pos="0 0 0" range="-60 35" stiffness="10" type="hinge"/>
  dJointID j_left_hip_xyz2 = dJointCreateBall(odeworld.world_id, nullptr);
  dJointAttach(j_left_hip_xyz2, pelvis, left_thigh);
  dJointSetBallAnchor(j_left_hip_xyz2, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

  dJointID j_left_hip_xyz = dJointCreateAMotor(odeworld.world_id, nullptr);
  dJointAttach(j_left_hip_xyz, pelvis, left_thigh);
  dJointSetAMotorMode(j_left_hip_xyz, dAMotorEuler);
  dJointSetAMotorAxis(j_left_hip_xyz, 0,1,0,1,0);
  dJointSetAMotorAxis(j_left_hip_xyz, 2,2,0,0,-1);
  dJointSetAMotorParam(j_left_hip_xyz, dParamLoStop3, -25*M_PI/180.f);
  dJointSetAMotorParam(j_left_hip_xyz, dParamHiStop3, 5*M_PI/180.f);
  dJointSetAMotorParam(j_left_hip_xyz, dParamLoStop, -110*M_PI/180.f);//right leg is set to 110
  dJointSetAMotorParam(j_left_hip_xyz, dParamHiStop, 20*M_PI/180.f);
  dJointSetAMotorParam(j_left_hip_xyz, dParamLoStop2, -60*M_PI/180.f);
  dJointSetAMotorParam(j_left_hip_xyz, dParamHiStop2, 35*M_PI/180.f);
  
//   dJointSetAMotorParam(j_left_hip_xyz, dParamLoStop2, 60*M_PI/180.f);
//   dJointSetAMotorParam(j_left_hip_xyz, dParamHiStop2, -35*M_PI/180.f);

//   <body name="left_shin" pos="0 -0.01 -0.403">
  dBodyID left_shin = dBodyCreate(odeworld.world_id);
  old_body_pos[1] += -0.01;
  old_body_pos[2] += -0.403;
  dBodySetPosition(left_shin, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   <geom fromto="0 0 0 0 0 -.3" name="left_shin1" size="0.049" type="capsule"/>
  dGeomID g_left_shin1 = dCreateCapsule(odeworld.space_id, 0.049f, .3);
  dGeomSetBody(g_left_shin1, left_shin);
  dGeomSetOffsetPosition(g_left_shin1, 0,0, -.3/2.f);
  
  dMass m_left_shin;
  dMassSetCapsule(&m_left_shin, density, 3, 0.049, .3);

//  <joint armature="0.0060" axis="0 -1 0" name="left_knee" pos="0 0 .02" range="-160 -2" stiffness="1" type="hinge"/>
  dJointID j_left_knee = dJointCreateHinge(odeworld.world_id, nullptr);
  dJointAttach(j_left_knee, left_thigh, left_shin);
  dJointSetHingeAxis(j_left_knee, 0, -1, 0);
  dJointSetHingeParam(j_left_knee, dParamHiStop, -2*M_PI/180.f);
  dJointSetHingeParam(j_left_knee, dParamLoStop, -150*M_PI/180.f);//160 seems to bug (knee does one loop)
  dJointSetHingeAnchor(j_left_knee, old_body_pos[0], old_body_pos[1], old_body_pos[2]+ .02);

//   <body name="left_foot" pos="0 0 -0.45">
//   <geom name="left_foot" type="sphere" size="0.075" pos="0 0 0.1" user="0" />
  dGeomID g_left_foot = dCreateSphere(odeworld.space_id, 0.075f);
  dGeomSetBody(g_left_foot, left_shin);
  dGeomSetOffsetPosition(g_left_foot, 0, 0, -0.45f + 0.1f);
  
  dMass m_left_foot;
  dMassSetSphere(&m_left_foot, density, 0.075f);
  dMassAdd(&m_left_shin, &m_left_foot);
  dBodySetMass(left_shin, &m_left_shin);

//   <body name="right_upper_arm" pos="0 -0.17 0.06">
  dBodyID right_upper_arm = dBodyCreate(odeworld.world_id);
  old_body_pos[0] = torso_body_pos[0];
  old_body_pos[1] = torso_body_pos[1];
  old_body_pos[2] = torso_body_pos[2];
  old_body_pos[1] += -0.17;
  old_body_pos[2] += 0.06;
  dBodySetPosition(right_upper_arm, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   <geom fromto="0 0 0 .16 -.16 -.16" name="right_uarm1" size="0.04 0.16" type="capsule"/>
//   dGeomID g_right_uarm1 = dCreateCapsule(odeworld.space_id, 0.04f, .16*2.);
  dGeomID g_right_uarm1 = dCreateCapsule(odeworld.space_id, 0.04f, 1.1*sqrt(3*0.16*0.16));//rm me *1.5
  dGeomSetBody(g_right_uarm1, right_upper_arm);
  dMatrix3 R_right_uarm1;
  dRFromAxisAndAngle(R_right_uarm1, -1, -1, 0, M_PI/4.f);
//   dRFrom3Points(R_right_uarm1,
//                 0, 0, -0.16,
//                 0, 0, 0,
//                 0.16, -0.16, -0.16
//                );
//   dRFromEulerAngles(R_right_thigh1, M_PI/)
  dGeomSetOffsetRotation(g_right_uarm1, R_right_uarm1);
  dGeomSetOffsetPosition(g_right_uarm1, 0.16/2.f, -0.16/2.f, -0.16/2.f);
  
  dMass m_right_uarm;
  dMassSetCapsule(&m_right_uarm, density, 3, 0.04f, sqrt(3*0.16*0.16));
  dBodySetMass(right_upper_arm, &m_right_uarm);

//   <joint armature="0.0068" axis="2 1 1" name="right_shoulder1" pos="0 0 0" range="-85 60" stiffness="1" type="hinge"/>
//   <joint armature="0.0051" axis="0 -1 1" name="right_shoulder2" pos="0 0 0" range="-85 60" stiffness="1" type="hinge"/>
  dJointID j_right_shoulder = dJointCreateUniversal(odeworld.world_id, nullptr);
  dJointAttach(j_right_shoulder, right_upper_arm, torso);
  dJointSetUniversalAxis1(j_right_shoulder, 2, 1, 1);//no body attached no effect
  dJointSetUniversalAxis2(j_right_shoulder, 0, -1, 1);//no body attached no effect
  dJointSetUniversalParam(j_right_shoulder, dParamLoStop, -85*M_PI/180.f);
  dJointSetUniversalParam(j_right_shoulder, dParamHiStop, 60*M_PI/180.f);
  dJointSetUniversalParam(j_right_shoulder, dParamLoStop2, -85*M_PI/180.f);
  dJointSetUniversalParam(j_right_shoulder, dParamHiStop2, 60*M_PI/180.f);
  dJointSetUniversalAnchor(j_right_shoulder, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

// //   <body name="right_lower_arm" pos=".18 -.18 -.18">
//   dBodyID right_lower_arm = dBodyCreate(odeworld.world_id);
//   old_body_pos[0] += 0.18;
//   old_body_pos[1] += -0.18;
//   old_body_pos[2] += -0.18;
//   dBodySetPosition(right_lower_arm, old_body_pos[0], old_body_pos[1], old_body_pos[2]);
// 
// //   <geom fromto="0.01 0.01 0.01 .17 .17 .17" name="right_larm" size="0.031" type="capsule"/>
//   dGeomID g_right_larm = dCreateCapsule(odeworld.space_id, 0.031f, sqrt(.16*.16*3.));
//   dGeomSetBody(g_right_larm, right_lower_arm);
//   dMatrix3 R_right_larm;
//   dRFromAxisAndAngle(R_right_larm, 1, 1, 0, M_PI/4.f);
//   dGeomSetOffsetRotation(g_right_larm, R_right_larm);
//   dGeomSetOffsetPosition(g_right_larm, -0.18/2.f, 0.18/2.f, -0.18/2.f);//why?
//   
//   dMass m_right_larm;
//   dMassSetCapsule(&m_right_larm, density, 3, 0.031, sqrt(.16*.16*3.));
// 
// //   <joint armature="0.0028" axis="0 -1 1" name="right_elbow" pos="0 0 0" range="-90 50" stiffness="0" type="hinge"/>
//   dJointID j_right_elbow = dJointCreateHinge(odeworld.world_id, nullptr);
//   dJointAttach(j_right_elbow, right_lower_arm, right_upper_arm);
//   dJointSetHingeAxis(j_right_elbow, 0, -1, 1);
//   dJointSetHingeParam(j_right_elbow, dParamHiStop, 50*M_PI/180.);
//   dJointSetHingeParam(j_right_elbow, dParamLoStop, -90*M_PI/180.);
//   dJointSetHingeAnchor(j_right_elbow, old_body_pos[0], old_body_pos[1], old_body_pos[2]);
// 
// //   <geom name="right_hand" pos=".18 .18 .18" size="0.04" type="sphere"/>
//   dGeomID g_right_hand = dCreateSphere(odeworld.space_id, 0.04);
//   dGeomSetBody(g_right_hand, right_lower_arm);
//   dGeomSetOffsetPosition(g_right_hand, -.18, .18, -0.18);//why?
//   
//   dMass m_right_hand;
//   dMassSetSphere(&m_right_hand, density, 0.04f);
//   dMassAdd(&m_right_larm, &m_right_hand);
//   dBodySetMass(right_lower_arm, &m_right_larm);

  //   <body name="left_upper_arm" pos="0 0.17 0.06">
  dBodyID left_upper_arm = dBodyCreate(odeworld.world_id);
  old_body_pos[0] = torso_body_pos[0];
  old_body_pos[1] = torso_body_pos[1];
  old_body_pos[2] = torso_body_pos[2];
  old_body_pos[1] += 0.17;
  old_body_pos[2] += 0.06;
  dBodySetPosition(left_upper_arm, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

  //   <geom fromto="0 0 0 .16 .16 -.16" name="left_uarm1" size="0.04 0.16" type="capsule"/>
  dGeomID g_left_uarm1 = dCreateCapsule(odeworld.space_id, 0.04f, sqrt(3*0.16*0.16));
  dGeomSetBody(g_left_uarm1, left_upper_arm);
  dMatrix3 R_left_uarm1;
  dRFromAxisAndAngle(R_left_uarm1, 1, -1, 0, M_PI/4.f);
  //   dRFrom3Points(R_left_uarm1,
  //                 0, 0, -0.16,
  //                 0, 0, 0,
  //                 0.16, -0.16, -0.16
  //                );
  //   dRFromEulerAngles(R_left_thigh1, M_PI/)
  dGeomSetOffsetRotation(g_left_uarm1, R_left_uarm1);
  dGeomSetOffsetPosition(g_left_uarm1, 0.16/2.f, 0.16/2.f, -0.16/2.f);
  
  dMass m_left_uarm;
  dMassSetCapsule(&m_left_uarm, density, 3, 0.04f, sqrt(3*0.16*0.16));
  dBodySetMass(left_upper_arm, &m_left_uarm);

//   <joint armature="0.0068" axis="2 -1 1" name="left_shoulder1" pos="0 0 0" range="-60 85" stiffness="1" type="hinge"/>
//   <joint armature="0.0051" axis="0 1 1" name="left_shoulder2" pos="0 0 0" range="-60 85" stiffness="1" type="hinge"/>
  dJointID j_left_shoulder = dJointCreateUniversal(odeworld.world_id, nullptr);
  dJointAttach(j_left_shoulder, left_upper_arm, torso);
  dJointSetUniversalAxis1(j_left_shoulder, 2, -1, 1);//no body attached no effect
  dJointSetUniversalAxis2(j_left_shoulder, 0, 1, 1);//no body attached no effect
  dJointSetUniversalParam(j_left_shoulder, dParamLoStop, -60*M_PI/180.f);
  dJointSetUniversalParam(j_left_shoulder, dParamHiStop, 85*M_PI/180.f);
  dJointSetUniversalParam(j_left_shoulder, dParamLoStop2, -60*M_PI/180.f);
  dJointSetUniversalParam(j_left_shoulder, dParamHiStop2, 85*M_PI/180.f);
  dJointSetUniversalAnchor(j_left_shoulder, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

  //   <body name="left_lower_arm" pos=".18 .18 -.18">
  dBodyID left_lower_arm = dBodyCreate(odeworld.world_id);
  old_body_pos[0] += 0.18;
  old_body_pos[1] += 0.18;
  old_body_pos[2] += -0.18;
  dBodySetPosition(left_lower_arm, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

//   <geom fromto="0.01 -0.01 0.01 .17 -.17 .17" name="left_larm" size="0.031" type="capsule"/>
  dGeomID g_left_larm = dCreateCapsule(odeworld.space_id, 0.031f, sqrt(.16*.16*3.));
  dGeomSetBody(g_left_larm, left_lower_arm);
  dMatrix3 R_left_larm;
  dRFromAxisAndAngle(R_left_larm, -1, 1, 0, M_PI/4.f);
  dGeomSetOffsetRotation(g_left_larm, R_left_larm);
  dGeomSetOffsetPosition(g_left_larm, -0.18/2.f, -0.18/2.f, -0.18/2.f);

  dMass m_left_larm;
  dMassSetCapsule(&m_left_larm, density, 3, 0.031, sqrt(.16*.16*3.));
  
  //   <joint armature="0.0028" axis="0 -1 -1" name="left_elbow" pos="0 0 0" range="-90 50" stiffness="0" type="hinge"/>
  dJointID j_left_elbow = dJointCreateHinge(odeworld.world_id, nullptr);
  dJointAttach(j_left_elbow, left_lower_arm, left_upper_arm);
  dJointSetHingeAxis(j_left_elbow, 0, -1, -1);
  dJointSetHingeParam(j_left_elbow, dParamHiStop, 50*M_PI/180.);
  dJointSetHingeParam(j_left_elbow, dParamLoStop, -90*M_PI/180.);
  dJointSetHingeAnchor(j_left_elbow, old_body_pos[0], old_body_pos[1], old_body_pos[2]);

  //   <geom name="left_hand" pos=".18 -.18 .18" size="0.04" type="sphere"/>
  dGeomID g_left_hand = dCreateSphere(odeworld.space_id, 0.04);
  dGeomSetBody(g_left_hand, left_lower_arm);
  dGeomSetOffsetPosition(g_left_hand, -.18, -.18, -0.18);
  
  dMass m_left_hand;
  dMassSetSphere(&m_left_hand, density, 0.04f);
  dMassAdd(&m_left_larm, &m_left_hand);
  dBodySetMass(left_lower_arm, &m_left_larm);

  joints.push_back(j_abdomen_zy);
  joints.push_back(j_abdomen_x);
  joints.push_back(j_right_hip_xyz);
  joints.push_back(j_right_hip_xyz2);
  joints.push_back(j_right_knee);
  joints.push_back(j_left_hip_xyz);
  joints.push_back(j_left_hip_xyz2);
  joints.push_back(j_left_knee);
  joints.push_back(j_right_shoulder);
//   joints.push_back(j_right_elbow);
  joints.push_back(j_left_shoulder);
  joints.push_back(j_left_elbow);

  bones.push_back(new ODEObject(torso, m_torso, g_torso1, 0,0,0.,density, m_torso.mass));
  bones.push_back(new ODEObject(nullptr, m_torso, g_head, 0,0,0.,density, m_head.mass));
  bones.push_back(new ODEObject(nullptr, m_torso, g_uwaist, 0,0,0.,density, m_uwaist.mass));

  bones.push_back(new ODEObject(lwaist, m_torso, g_lwaist, 0,0,0.,density, m_lwaist.mass));
  bones.push_back(new ODEObject(pelvis, m_torso, g_butt, 0,0,0.,density, m_pelvis.mass));//4
  bones.push_back(new ODEObject(right_thigh, m_torso, g_right_thigh1, 0,0,0.,density, m_right_thigh.mass));
  bones.push_back(new ODEObject(right_shin, m_torso, g_right_shin1, 0,0,0.,density, m_right_shin.mass));
  bones.push_back(new ODEObject(nullptr, m_torso, g_right_foot, 0,0,0.,density, m_right_foot.mass));
  bones.push_back(new ODEObject(left_thigh, m_torso, g_left_thigh1, 0,0,0.,density, m_left_thigh.mass));
  bones.push_back(new ODEObject(left_shin, m_torso, g_left_shin1, 0,0,0.,density, m_left_shin.mass));
  bones.push_back(new ODEObject(nullptr, m_torso, g_left_foot, 0,0,0.,density, m_left_foot.mass));

  bones.push_back(new ODEObject(right_upper_arm, m_torso, g_right_uarm1, 0,0,0.,density, m_right_uarm.mass));
//   bones.push_back(new ODEObject(right_lower_arm, m_torso, g_right_larm, 0,0,0.,density, m_right_larm.mass));//12
//   bones.push_back(new ODEObject(nullptr, m_torso, g_right_hand, 0,0,0.,density, m_right_hand.mass));//13

  bones.push_back(new ODEObject(left_upper_arm, m_torso, g_left_uarm1, 0,0,0.,density, m_left_uarm.mass));
  bones.push_back(new ODEObject(left_lower_arm, m_torso, g_left_larm, 0,0,0.,density, m_left_larm.mass));//15
  bones.push_back(new ODEObject(nullptr, m_torso, g_left_hand, 0,0,0.,density, m_left_hand.mass));//16

//   bones.push_back(debug2);
#ifndef NDEBUG
  double mass_sum = 0;
  for(auto a : bones) {
//     LOG_DEBUG(a->getMassValue());
    mass_sum += a->getMassValue();
  }
  LOG_DEBUG("total mass : " << mass_sum);
//   ASSERT(mass_sum >= 14.f - 0.001f && mass_sum <= 14.f + 0.001f, "sum mass : " << mass_sum);
#endif

//   bib::Logger::PRINT_ELEMENTS(second_bone->getMass().I, 9, "pole inertia : ");


  dJointID fixed_head = dJointCreateSlider(odeworld.world_id, nullptr);
  dJointAttach(fixed_head, torso, nullptr);
//   dJointSetSliderAxis(fixed_head, 1, 1, 0);
  joints.push_back(fixed_head);

//   dJointID fixed_hand1 = dJointCreateSlider(odeworld.world_id, nullptr);
//   dJointAttach(fixed_hand1, right_lower_arm, nullptr);
// //   dJointSetSliderAxis(fixed_hand1, 1, 1, 0);
//   joints.push_back(fixed_hand1);

  dJointID fixed_hand2 = dJointCreateSlider(odeworld.world_id, nullptr);
  dJointAttach(fixed_hand2, left_lower_arm, nullptr);
//   dJointSetSliderAxis(fixed_hand2, 1, 1, 0);
  joints.push_back(fixed_hand2);
  
  dJointID fixed_leg1 = dJointCreateSlider(odeworld.world_id, nullptr);
  dJointAttach(fixed_leg1, right_shin, nullptr);
  dJointSetSliderAxis(fixed_leg1, 0, 1, 1);
  joints.push_back(fixed_leg1);
  
  dJointID fixed_leg2 = dJointCreateSlider(odeworld.world_id, nullptr);
  dJointAttach(fixed_leg2, left_shin, nullptr);
  dJointSetSliderAxis(fixed_leg2, 0, 1, 1);
  joints.push_back(fixed_leg2);
  
    dJointID fixed_pelvis = dJointCreateSlider(odeworld.world_id, nullptr);
    dJointAttach(fixed_pelvis, pelvis, nullptr);
//     dJointSetSliderAxis(fixed_pelvis, 0, 1, 1);
    joints.push_back(fixed_pelvis);
}

void nearCallbackHumanoid(void* data, dGeomID o1, dGeomID o2) {
  nearCallbackDataHumanoid* d = reinterpret_cast<nearCallbackDataHumanoid*>(data);
  HumanoidWorld* inst = d->inst;

  // only collide things with the ground | only to debug with humanoid
//   if(o1 != inst->ground && o2 != inst->ground)
//     return;

//   <geom contype='1' conaffinity='0' condim='3' friction='.4 .1 .1' rgba='0.8 0.6 .4 1' solimp='0.0 0.8 0.01' solref='0.02 1' />
//   if(o1 == inst->bones[5]->getGeom() || o2 == inst->bones[5]->getGeom())
//     inst->fknee_touch = true;
//   if(o1 == inst->bones[2]->getGeom() || o2 == inst->bones[2]->getGeom())
//     inst->bknee_touch = true;
//   if(o1 == inst->bones[0]->getGeom() || o2 == inst->bones[0]->getGeom())
//     inst->head_touch = true;
  
  dBodyID b1 = dGeomGetBody(o1);
  dBodyID b2 = dGeomGetBody(o2);
  if (b1 && b2 && dAreConnected(b1, b2)){
//     LOG_DEBUG("purge collide");
    return;
  }
  
//   LOG_DEBUG("collide " << " " << dGeomGetClass(o1) << " " << dGeomGetClass(o2));
//   uint b=0;
//   for(ODEObject* o : inst->bones){
//     if(o->getGeom() == o1)
//       LOG_DEBUG("first " << b);
//     if(o->getGeom() == o2)
//       LOG_DEBUG("second " << b);
//     b++;
//   }

  if (int numc = dCollide (o1,o2,2,&inst->contact[0].geom,sizeof(dContact))) {
    for (int i=0; i<numc; i++) {
      dJointID c = dJointCreateContact (inst->odeworld.world_id,inst->odeworld.contactgroup,&inst->contact[i]);
      dJointAttach (c, dGeomGetBody(o1), dGeomGetBody(o2));
    }
//     LOG_DEBUG(numc);
  }

}

void HumanoidWorld::step(const vector<double>& _motors) {
  std::vector<double> motors(_motors);
//   if(phy.predev == 1 || phy.predev == 2 || phy.predev == 3) {
//     motors.resize(6);
//     motors[2] = 0;
//     motors[3] = _motors[2];
//     motors[4] = _motors[3];
//     motors[5] = 0;
//   } else if (phy.predev == 10 || phy.predev == 11 || phy.predev == 12) {
//     motors.resize(6);
//     motors[1] = 0;
//     motors[2] = _motors[1];
//     motors[3] = _motors[2];
//     motors[4] = 0;
//     motors[5] = _motors[3];
//   }
//
//   if(phy.from_predev == 1 || phy.from_predev == 2 || phy.from_predev == 3){
//     motors[2] = _motors[4];
//     motors[3] = _motors[2];
//     motors[4] = _motors[3];
// //     motors[5] = _motors[5];
//   } else if(phy.from_predev == 10 || phy.from_predev == 11 || phy.from_predev == 12){
//     motors[1] = _motors[4];
//     motors[2] = _motors[1];
//     motors[3] = _motors[2];
//     motors[4] = _motors[5];
//     motors[5] = _motors[3];
//   }
//
//   head_touch = false;
//   fknee_touch = false;
//   bknee_touch = false;
//   penalty = 0;
//


//   to debug
//   double force_multiplier = 35;
//   for(dJointID j : joints)
//     if(dJointGetType(j) == dJointTypeHinge)
//       dJointAddHingeTorque(j, (bib::Utils::rand01() * 2.f - 1.f) * force_multiplier);
//     else if(dJointGetType(j) == dJointTypeUniversal)
//       dJointAddUniversalTorques(j, (bib::Utils::rand01() * 2.f - 1.f) * force_multiplier,
//                              (bib::Utils::rand01() * 2.f - 1.f) * force_multiplier);
//     else if(dJointGetType(j) == dJointTypeAMotor )
//       dJointAddAMotorTorques(j, (bib::Utils::rand01() * 2.f - 1.f) * force_multiplier,
//                              (bib::Utils::rand01() * 2.f - 1.f) * force_multiplier,
//                              (bib::Utils::rand01() * 2.f - 1.f) * force_multiplier);


  const double gear_abdomen_y = 100;
  const double gear_abdomen_z = 100;
  const double gear_abdomen_x = 100;
  const double gear_right_hip_x = 100;
  const double gear_right_hip_z = 100;
  const double gear_right_hip_y = 300;
  const double gear_right_knee = 200;
  const double gear_left_hip_x = 100;
  const double gear_left_hip_z = 100;
  const double gear_left_hip_y = 300;
  const double gear_left_knee = 200;
  const double gear_right_shoulder1 = 25;
  const double gear_right_shoulder2 = 25;
  const double gear_right_elbow = 25;
  const double gear_left_shoulder1 = 25;
  const double gear_left_shoulder2 = 25;
  const double gear_left_elbow = 25;

  unsigned int begin_index = 0;
  std::vector<double> gears(17);
  gears[begin_index++] = gear_abdomen_y;
  gears[begin_index++] = gear_abdomen_z;
  gears[begin_index++] = gear_abdomen_x;
  gears[begin_index++] = gear_right_hip_x;
  gears[begin_index++] = gear_right_hip_z;
  gears[begin_index++] = gear_right_hip_y;
  gears[begin_index++] = gear_right_knee;
  gears[begin_index++] = gear_left_hip_x;
  gears[begin_index++] = gear_left_hip_z;
  gears[begin_index++] = gear_left_hip_y;
  gears[begin_index++] = gear_left_knee;
  gears[begin_index++] = gear_right_shoulder1;
  gears[begin_index++] = gear_right_shoulder2;
  gears[begin_index++] = gear_right_elbow;
  gears[begin_index++] = gear_left_shoulder1;
  gears[begin_index++] = gear_left_shoulder2;
  gears[begin_index++] = gear_left_elbow;

//   std::vector<double> hinge_angles(17);
//   double ha_abdomen_y = dJointGetUniversalAngle2(joints[0]);
//   double ha_abdomen_z = dJointGetUniversalAngle1(joints[0]);
//   double ha_abdomen_x = dJointGetHingeAngle(joints[1]);
//   double ha_right_hip_x = dJointGetAMotorAngle(joints[2], 0);
//   double ha_right_hip_z = dJointGetAMotorAngle(joints[2], 1);//todo check
//   double ha_right_hip_y = dJointGetAMotorAngle(joints[2], 2);
//   double ha_right_knee = dJointGetHingeAngle(joints[4]);
//   double ha_left_hip_x = dJointGetAMotorAngle(joints[5], 0);
//   double ha_left_hip_z = dJointGetAMotorAngle(joints[5], 1);
//   double ha_left_hip_y = dJointGetAMotorAngle(joints[5], 2);
//   double ha_left_knee = dJointGetHingeAngle(joints[7]);
//   double ha_right_shoulder1 = dJointGetUniversalAngle1(joints[8]);
//   double ha_right_shoulder2 = dJointGetUniversalAngle2(joints[8]);
//   double ha_right_elbow = dJointGetHingeAngle(joints[9]);
//   double ha_left_shoulder1 = dJointGetUniversalAngle1(joints[10]);
//   double ha_left_shoulder2 = dJointGetUniversalAngle1(joints[10]);
//   double ha_left_elbow = dJointGetHingeAngle(joints[11]);
// 
//   begin_index = 0;
//   hinge_angles[begin_index++] = ha_abdomen_y;
//   hinge_angles[begin_index++] = ha_abdomen_z;
//   hinge_angles[begin_index++] = ha_abdomen_x;
//   hinge_angles[begin_index++] = ha_right_hip_x;
//   hinge_angles[begin_index++] = ha_right_hip_z;
//   hinge_angles[begin_index++] = ha_right_hip_y;
//   hinge_angles[begin_index++] = ha_right_knee;
//   hinge_angles[begin_index++] = ha_left_hip_x;
//   hinge_angles[begin_index++] = ha_left_hip_z;
//   hinge_angles[begin_index++] = ha_left_hip_y;
//   hinge_angles[begin_index++] = ha_left_knee;
//   hinge_angles[begin_index++] = ha_right_shoulder1;
//   hinge_angles[begin_index++] = ha_right_shoulder2;
//   hinge_angles[begin_index++] = ha_right_elbow;
//   hinge_angles[begin_index++] = ha_left_shoulder1;
//   hinge_angles[begin_index++] = ha_left_shoulder2;
//   hinge_angles[begin_index++] = ha_left_elbow;
// 
//   std::vector<double> hinge_angles_rate(17);
//   begin_index = 0;
//   double har_abdomen_y = dJointGetUniversalAngle2Rate(joints[0]);
//   double har_abdomen_z = dJointGetUniversalAngle1Rate(joints[0]);
//   double har_abdomen_x = dJointGetHingeAngleRate(joints[1]);
//   double har_right_hip_x = dJointGetAMotorAngleRate(joints[2], 0);
//   double har_right_hip_z = dJointGetAMotorAngleRate(joints[2], 1);//todo check
//   double har_right_hip_y = dJointGetAMotorAngleRate(joints[2], 2);
//   double har_right_knee = dJointGetHingeAngleRate(joints[4]);
//   double har_left_hip_x = dJointGetAMotorAngleRate(joints[5], 0);
//   double har_left_hip_z = dJointGetAMotorAngleRate(joints[5], 1);
//   double har_left_hip_y = dJointGetAMotorAngleRate(joints[5], 2);
//   double har_left_knee = dJointGetHingeAngleRate(joints[7]);
//   double har_right_shoulder1 = dJointGetUniversalAngle1Rate(joints[8]);
//   double har_right_shoulder2 = dJointGetUniversalAngle2Rate(joints[8]);
//   double har_right_elbow = dJointGetHingeAngleRate(joints[9]);
//   double har_left_shoulder1 = dJointGetUniversalAngle1Rate(joints[10]);
//   double har_left_shoulder2 = dJointGetUniversalAngle1Rate(joints[10]);
//   double har_left_elbow = dJointGetHingeAngleRate(joints[11]);
// 
// //   hinge_angles_rate[begin_index++] = har_abdomen_y;
// //   hinge_angles_rate[begin_index++] = har_abdomen_z;
//   hinge_angles_rate[begin_index++] = har_abdomen_x;
//   hinge_angles_rate[begin_index++] = har_right_hip_x;
//   hinge_angles_rate[begin_index++] = har_right_hip_z;
//   hinge_angles_rate[begin_index++] = har_right_hip_y;
//   hinge_angles_rate[begin_index++] = har_right_knee;
//   hinge_angles_rate[begin_index++] = har_left_hip_x;
//   hinge_angles_rate[begin_index++] = har_left_hip_z;
//   hinge_angles_rate[begin_index++] = har_left_hip_y;
//   hinge_angles_rate[begin_index++] = har_left_knee;
//   hinge_angles_rate[begin_index++] = har_right_shoulder1;
//   hinge_angles_rate[begin_index++] = har_right_shoulder2;
//   hinge_angles_rate[begin_index++] = har_right_elbow;
//   hinge_angles_rate[begin_index++] = har_left_shoulder1;
//   hinge_angles_rate[begin_index++] = har_left_shoulder2;
//   hinge_angles_rate[begin_index++] = har_left_elbow;
// 
//   std::vector<double> f_motors(17);
//   //double
//   if(phy.control == 1) {
//     for(uint i=0; i < 17 ; i++)
//       f_motors[i] = bib::Utils::transform(f_motors[i], -1, 1, -gears[i], gears[i]);
//   } else if(phy.control==2 || phy.control==3) {
//     std::vector<double> p_motor(17);
//     for(uint i=0; i < 17 ; i++)
//       p_motor[i] = 2.0f/M_PI * atan(-2.0f*hinge_angles[i] - 0.05 * hinge_angles_rate[i]);
// 
//     if(phy.control==3) {
//       for(uint i=0; i < 17; i++)
//         motors[i] = bib::Utils::transform(motors[i], -1.f, 1.f, -2.f, 2.f);
//     }
// 
//     for(uint i=0; i < 17; i++)
//       f_motors[i] = gears[i] * std::min(std::max((double)-1., p_motor[i]+motors[i]), (double)1.);
// 
//     if(phy.reward == 2) {
//       begin_index = 0;
//       double sub_pelnalty = 0.f;
//       for(uint i=0; i < 17; i++)
//         sub_pelnalty += std::max(fabs(p_motor[i]+motors[i]) - 1.f, (double) 0.f);
// 
//       penalty += -0.05 * sub_pelnalty;
//     }
//   }

//   dJointAddUniversalTorques(joints[0], f_motors[0], f_motors[1]);
//   dJointAddHingeTorque(joints[1], f_motors[2]);
//   dJointAddAMotorTorques(joints[2], f_motors[3], f_motors[4], f_motors[5]);
//   dJointAddHingeTorque(joints[4], f_motors[6]);
//   dJointAddAMotorTorques(joints[5], f_motors[7], f_motors[8], f_motors[9]);
//   dJointAddHingeTorque(joints[7], f_motors[10]);
//   dJointAddUniversalTorques(joints[8], f_motors[11], f_motors[12]);
//   dJointAddHingeTorque(joints[9], f_motors[13]);
//   dJointAddUniversalTorques(joints[10], f_motors[14], f_motors[15]);
//   dJointAddHingeTorque(joints[11], f_motors[16]);


//   DEBUG
#ifndef NDEBUG

  if(debug_step % 2000 == 0) {
    debug_step = 0;
    
    if(factors.size() == 0)
      factors.resize(17, 0);
    for(uint i=0;i<gears.size();i++)
      if(i != target_motor/2)
        factors[i] = 0;
    for(uint i=0;i<bones.size();i++)
      bones[i]->setColorMode(0);
    
    
    if(target_motor > 17){
      target_motor=0;
      target_joint=0;
    }
    uint i = target_motor/2;
    if(factors[i] <= 0.f)
      factors[i] = 1.f;
    else
      factors[i] = -1.f;
    
    const dBodyID body1 = dJointGetBody(joints[target_joint], 0);
    for (ODEObject * o : bones) {
      if(o->getID() != nullptr && o->getID() == body1)
        o->setColorMode(1);
    }
    LOG_DEBUG("changed "<< target_motor << " " << (uint)(target_motor/2) << " " << target_joint);
    target_motor++;
    
    if(target_motor % 2 == 0){
      if(dJointGetType(joints[target_joint]) == dJointTypeHinge)
        target_joint++;
      else if(dJointGetType(joints[target_joint]) == dJointTypeUniversal)
        if(target_motor % 4 == 0)
          target_joint++;
    }
    
//     bib::Logger::PRINT_ELEMENTS(factors);
  }
  debug_step++;

  
//   for(uint i=0; i<gears.size(); i++)
//     gears[i] = gears[i] * factors[i];
#endif

  dJointAddUniversalTorques(joints[8], -gears[11], -gears[12]);
//   dJointAddHingeTorque(joints[9], gears[13]);

//   dJointAddUniversalTorques(joints[0], gears[0], gears[1]);
//   dJointAddHingeTorque(joints[1], gears[2]);
//   dJointAddAMotorTorques(joints[2], gears[3], gears[4], gears[5]);
//   dJointAddHingeTorque(joints[4], gears[6]);
//   dJointAddAMotorTorques(joints[5], gears[7], gears[8], gears[9]);
//   dJointAddHingeTorque(joints[7], gears[10]);
//   dJointAddUniversalTorques(joints[8], gears[11], gears[12]);
//   dJointAddHingeTorque(joints[9], gears[13]);
//   dJointAddUniversalTorques(joints[10], gears[14], gears[15]);
//   dJointAddHingeTorque(joints[11], gears[16]);

  if(phy.reward == 1 || phy.reward == 3) {
    for (auto a : motors)
      penalty += a*a;
    penalty = - 1e-1 * 0.5 * penalty;
  }

  Mutex::scoped_lock lock(ODEFactory::getInstance()->wannaStep());

  nearCallbackDataHumanoid d = {this};
  dSpaceCollide(odeworld.space_id, &d, &nearCallbackHumanoid);
  dWorldStep(odeworld.world_id, WORLD_STEP);

  lock.release();

  dJointGroupEmpty(odeworld.contactgroup);

  update_state();
}

void HumanoidWorld::update_state() {
//   uint begin_index = 0;
//
//   dBodyID torso = bones[1]->getID();
//   const dReal* root_pos = dBodyGetPosition(torso);
//   const dReal* root_vel = dBodyGetLinearVel(torso);
//   const dReal* root_angvel = dBodyGetAngularVel(torso);
//   const dReal* root_rot = dBodyGetQuaternion(torso);
//   ASSERT(root_rot[3] <= 1 , "not normalized");
//   double s = dSqrt(1.0f-root_rot[3]*root_rot[3]);
//
//   std::list<double> substate;
//
//   substate.push_back(root_pos[0]);//- rootx     slider      position (m)
//   substate.push_back(root_pos[2]);//- rootz     slider      position (m)
//   substate.push_back(s <= 0.0000001f ? root_rot[2] : root_rot[2]/s) ;// - rooty     hinge       angle (rad)
//   substate.push_back(dJointGetHingeAngle(joints[0])); //- bthigh    hinge       angle (rad)
//   substate.push_back(dJointGetHingeAngle(joints[1]));
//   substate.push_back(dJointGetHingeAngle(joints[2]));
//   substate.push_back(dJointGetHingeAngle(joints[3]));
//   substate.push_back(dJointGetHingeAngle(joints[4]));
//   substate.push_back(dJointGetHingeAngle(joints[5]));
//
//   substate.push_back(root_vel[0]);//- rootx     slider      velocity (m/s)
//   substate.push_back(root_vel[2]);//- rootz     slider      velocity (m/s)
//   substate.push_back(root_angvel[1]);//- rooty     hinge       angular velocity (rad/s)
//   substate.push_back(dJointGetHingeAngleRate(joints[0])); //- bthigh    hinge       angular velocity (rad/s)
//   substate.push_back(dJointGetHingeAngleRate(joints[1]));
//   substate.push_back(dJointGetHingeAngleRate(joints[2]));
//   substate.push_back(dJointGetHingeAngleRate(joints[3]));
//   substate.push_back(dJointGetHingeAngleRate(joints[4]));
//   substate.push_back(dJointGetHingeAngleRate(joints[5]));
//
//   ASSERT(substate.size() == 18, "wrong indices");
//
//   if((phy.from_predev == 0 && (phy.predev == 0 || phy.predev == 2 || phy.predev == 11 || phy.predev == 3 || phy.predev == 12)) ||
//     phy.from_predev == 2 || phy.from_predev == 11 || phy.from_predev == 3 || phy.from_predev == 12) {
//     std::copy(substate.begin(), substate.end(), internal_state.begin());
//
//     if(phy.predev == 3){
//       internal_state[17] = 0.0f;
//       internal_state[14] = 0.0f;
//       internal_state[8]  = 0.0f;
//       internal_state[5]  = 0.0f;
//     } else if(phy.predev == 12){
//       internal_state[16] = 0.0f;
//       internal_state[13] = 0.0f;
//       internal_state[7]  = 0.0f;
//       internal_state[4]  = 0.0f;
//     }
//   } else {
//     std::list<uint> later;
//
//     if(phy.predev == 1 || phy.from_predev == 1) {
//       auto it = substate.begin();
//       std::advance(it, 17);
//       later.push_back(*it);
//       substate.erase(it);
//
//       it = substate.begin();
//       std::advance(it, 14);
//       later.push_back(*it);
//       substate.erase(it);
//
//       it = substate.begin();
//       std::advance(it, 8);
//       later.push_back(*it);
//       substate.erase(it);
//
//       it = substate.begin();
//       std::advance(it, 5);
//       substate.erase(it);
//
//       std::copy(substate.begin(), substate.end(), internal_state.begin());
//     } else if(phy.predev == 10 || phy.from_predev == 10) {
//       auto it = substate.begin();
//       std::advance(it, 16);
//       later.push_back(*it);
//       substate.erase(it);
//
//       it = substate.begin();
//       std::advance(it, 13);
//       later.push_back(*it);
//       substate.erase(it);
//
//       it = substate.begin();
//       std::advance(it, 7);
//       later.push_back(*it);
//       substate.erase(it);
//
//       it = substate.begin();
//       std::advance(it, 4);
//       later.push_back(*it);
//       substate.erase(it);
//       std::copy(substate.begin(), substate.end(), internal_state.begin());
//     }
//
//     if(phy.from_predev != 0)
//       std::copy(later.begin(), later.end(), internal_state.begin() + substate.size());
//   }
//
// //   bib::Logger::PRINT_ELEMENTS(internal_state);
//
// //   if(fknee_touch){
// //     LOG_DEBUG("front touched");
// //   }
// //   if(bknee_touch){
// //     LOG_DEBUG("back touched");
// //   }
//
//   if(phy.reward == 2 || phy.reward == 3) {
//     if(head_touch)
//       penalty += -1;
//     if(fknee_touch)
//       penalty += -1;
//     if(bknee_touch)
//       penalty += -1;
//
//     reward = penalty + root_vel[0];
//   } else if(phy.reward == 1) {
//     reward = penalty + root_vel[0];
//   }
}

const std::vector<double>& HumanoidWorld::state() const {
  return internal_state;
}

unsigned int HumanoidWorld::activated_motors() const {
  if(phy.predev != 0)
    return 4;
  return 17;
}

bool HumanoidWorld::final_state() const {
  return false;
}

double HumanoidWorld::performance() const {
  if(final_state())
    return -1000;
  return reward;
}

void HumanoidWorld::resetPositions(std::vector<double>&, const std::vector<double>&) {
  LOG_DEBUG("resetPositions");

  for (ODEObject * o : bones) {
    dGeomDestroy(o->getGeom());
    if(o->getID() != nullptr)
      dBodyDestroy(o->getID());
    delete o;
  }
  bones.clear();

  for(auto j : joints)
    dJointDestroy(j);
  joints.clear();

  dGeomDestroy(ground);

  createWorld();

  head_touch = false;
  fknee_touch = false;
  bknee_touch = false;
  penalty = 0;

  update_state();

  LOG_DEBUG("endResetPositions");
}
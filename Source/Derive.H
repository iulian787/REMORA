#ifndef _Derive_H_
#define _Derive_H_

#include <AMReX_FArrayBox.H>
#include <AMReX_Geometry.H>

namespace derived {

void remora_dernull(
  const amrex::Box& bx,
  amrex::FArrayBox& dremoraab,
  int dcomp,
  int ncomp,
  const amrex::FArrayBox& datfab,
  const amrex::Array4<const amrex::Real>& pm,
  const amrex::Array4<const amrex::Real>& pn,
  const amrex::Geometry& geomdata,
  amrex::Real time,
  const int* bcrec,
  const int level);

void remora_dervort(
  const amrex::Box& bx,
  amrex::FArrayBox& dremoraab,
  int dcomp,
  int ncomp,
  const amrex::FArrayBox& datfab,
  const amrex::Array4<const amrex::Real>& pm,
  const amrex::Array4<const amrex::Real>& pn,
  const amrex::Geometry& geomdata,
  amrex::Real time,
  const int* bcrec,
  const int level);
}
#endif


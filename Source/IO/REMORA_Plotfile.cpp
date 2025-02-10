#include <REMORA.H>
#include "AMReX_Interp_3D_C.H"
#include "AMReX_PlotFileUtil.H"

using namespace amrex;

PhysBCFunctNoOp null_bc_for_fill;

template<typename V, typename T>
bool containerHasElement(const V& iterable, const T& query) {
    return std::find(iterable.begin(), iterable.end(), query) != iterable.end();
}

void
REMORA::setPlotVariables (const std::string& pp_plot_var_names)
{
    ParmParse pp(pp_prefix);

    if (pp.contains(pp_plot_var_names.c_str()))
    {
        std::string nm;

        int nPltVars = pp.countval(pp_plot_var_names.c_str());

        for (int i = 0; i < nPltVars; i++)
        {
            pp.get(pp_plot_var_names.c_str(), nm, i);

            // Add the named variable to our list of plot variables
            // if it is not already in the list
            if (!containerHasElement(plot_var_names, nm)) {
                plot_var_names.push_back(nm);
            }
        }
    } else {
        //
        // The default is to add none of the variables to the list
        //
        plot_var_names.clear();
    }

    // Get state variables in the same order as we define them,
    // since they may be in any order in the input list
    Vector<std::string> tmp_plot_names;

    for (int i = 0; i < NCONS; ++i) {
        if ( containerHasElement(plot_var_names, cons_names[i]) ) {
            tmp_plot_names.push_back(cons_names[i]);
        }
    }
    // Check for velocity since it's not in cons_names
    // If we are asked for any velocity component, we will need them all
    if (containerHasElement(plot_var_names, "x_velocity") ||
        containerHasElement(plot_var_names, "y_velocity") ||
        containerHasElement(plot_var_names, "z_velocity")) {
        tmp_plot_names.push_back("x_velocity");
        tmp_plot_names.push_back("y_velocity");
        tmp_plot_names.push_back("z_velocity");
    }

    // If we are asked for any location component, we will provide them all
    if (containerHasElement(plot_var_names, "x_cc") ||
        containerHasElement(plot_var_names, "y_cc") ||
        containerHasElement(plot_var_names, "z_cc")) {
        tmp_plot_names.push_back("x_cc");
        tmp_plot_names.push_back("y_cc");
        tmp_plot_names.push_back("z_cc");
    }

    for (int i = 0; i < derived_names.size(); ++i) {
        if ( containerHasElement(plot_var_names, derived_names[i]) ) {
               tmp_plot_names.push_back(derived_names[i]);
        } // if
    } // i

#ifdef REMORA_USE_PARTICLES
    const auto& particles_namelist( particleData.getNamesUnalloc() );
    for (auto it = particles_namelist.cbegin(); it != particles_namelist.cend(); ++it) {
        std::string tmp( (*it)+"_count" );
        if (containerHasElement(plot_var_names, tmp) ) {
            tmp_plot_names.push_back(tmp);
        }
    }
#endif

    // Check to see if we found all the requested variables
    for (auto plot_name : plot_var_names) {
      if (!containerHasElement(tmp_plot_names, plot_name)) {
           Warning("\nWARNING: Requested to plot variable '" + plot_name + "' but it is not available");
      }
    }
    plot_var_names = tmp_plot_names;
}

void
REMORA::appendPlotVariables (const std::string& pp_plot_var_names)
{
    ParmParse pp(pp_prefix);

    if (pp.contains(pp_plot_var_names.c_str())) {
        std::string nm;
        int nPltVars = pp.countval(pp_plot_var_names.c_str());
        for (int i = 0; i < nPltVars; i++) {
            pp.get(pp_plot_var_names.c_str(), nm, i);
            // Add the named variable to our list of plot variables
            // if it is not already in the list
            if (!containerHasElement(plot_var_names, nm)) {
                plot_var_names.push_back(nm);
            }
        }
    }

    Vector<std::string> tmp_plot_names(0);
#ifdef REMORA_USE_PARTICLES
    Vector<std::string> particle_mesh_plot_names;
    particleData.GetMeshPlotVarNames( particle_mesh_plot_names );
    for (int i = 0; i < particle_mesh_plot_names.size(); i++) {
        std::string tmp(particle_mesh_plot_names[i]);
        if (containerHasElement(plot_var_names, tmp) ) {
            tmp_plot_names.push_back(tmp);
        }
    }
#endif

    for (int i = 0; i < tmp_plot_names.size(); i++) {
        plot_var_names.push_back( tmp_plot_names[i] );
    }

    // Finally, check to see if we found all the requested variables
    for (const auto& plot_name : plot_var_names) {
        if (!containerHasElement(plot_var_names, plot_name)) {
             if (amrex::ParallelDescriptor::IOProcessor()) {
                 Warning("\nWARNING: Requested to plot variable '" + plot_name + "' but it is not available");
             }
        }
    }
}

// Write plotfile to disk
void
REMORA::WritePlotFile ()
{
    Vector<std::string> varnames;
    varnames.insert(varnames.end(), plot_var_names.begin(), plot_var_names.end());

    const int ncomp_mf = varnames.size();
    const auto ngrow_vars = IntVect(NGROW-1,NGROW-1,0);

    if (ncomp_mf == 0) {
        return;
    }

    // We fillpatch here because some of the derived quantities require derivatives
    //     which require ghost cells to be filled. Don't fill the boundary, though.
    for (int lev = 0; lev <= finest_level; ++lev) {
        FillPatchNoBC(lev, t_new[lev], *cons_new[lev], cons_new, BdyVars::t,0,true,false);
        FillPatchNoBC(lev, t_new[lev], *xvel_new[lev], xvel_new, BdyVars::u,0,true,false);
        FillPatchNoBC(lev, t_new[lev], *yvel_new[lev], yvel_new, BdyVars::v,0,true,false);
        FillPatchNoBC(lev, t_new[lev], *zvel_new[lev], zvel_new, BdyVars::null,0,true,false);
    }

    // Array of MultiFabs to hold the plotfile data
    Vector<MultiFab> mf(finest_level+1);
    for (int lev = 0; lev <= finest_level; ++lev) {
        mf[lev].define(grids[lev], dmap[lev], ncomp_mf, ngrow_vars);
    }

    // Array of MultiFabs for nodal data
    Vector<MultiFab> mf_nd(finest_level+1);
    for (int lev = 0; lev <= finest_level; ++lev) {
        BoxArray nodal_grids(grids[lev]); nodal_grids.surroundingNodes();
        mf_nd[lev].define(nodal_grids, dmap[lev], AMREX_SPACEDIM, 0);
        mf_nd[lev].setVal(0.);
    }

    // Array of MultiFabs for cell-centered velocity
    Vector<MultiFab> mf_cc_vel(finest_level+1);

    if (containerHasElement(plot_var_names, "x_velocity") ||
        containerHasElement(plot_var_names, "y_velocity") ||
        containerHasElement(plot_var_names, "z_velocity") ||
        containerHasElement(plot_var_names, "vorticity") ) {

        for (int lev = 0; lev <= finest_level; ++lev) {
            mf_cc_vel[lev].define(grids[lev], dmap[lev], AMREX_SPACEDIM, IntVect(1,1,0));
            mf_cc_vel[lev].setVal(0.0_rt); // zero out velocity in case we have any wall boundaries
            average_face_to_cellcenter(mf_cc_vel[lev],0,
                                       Array<const MultiFab*,3>{xvel_new[lev],yvel_new[lev],zvel_new[lev]});
            mf_cc_vel[lev].FillBoundary(geom[lev].periodicity());
        } // lev

        // We need ghost cells if computing vorticity
        amrex::Interpolater* mapper = &cell_cons_interp;
        if ( containerHasElement(plot_var_names, "vorticity") ) {
            for (int lev = 1; lev <= finest_level; ++lev) {
                Vector<MultiFab*> fmf = {&(mf_cc_vel[lev]), &(mf_cc_vel[lev])};
                Vector<Real> ftime    = {t_new[lev], t_new[lev]};
                Vector<MultiFab*> cmf = {&mf_cc_vel[lev-1], &mf_cc_vel[lev-1]};
                Vector<Real> ctime    = {t_new[lev], t_new[lev]};

                MultiFab mf_to_fill;
                amrex::FillPatchTwoLevels(mf_cc_vel[lev], t_new[lev], cmf, ctime, fmf, ftime,
                                          0, 0, AMREX_SPACEDIM, geom[lev-1], geom[lev],
                                          null_bc_for_fill, 0, null_bc_for_fill, 0, refRatio(lev-1),
                                          mapper, domain_bcs_type, 0);
            } // lev
        } // if
    } // if

    for (int lev = 0; lev <= finest_level; ++lev)
    {
        int mf_comp = 0;

        // First, copy any of the conserved state variables into the output plotfile
        AMREX_ALWAYS_ASSERT(cons_names.size() == NCONS);
        for (int i = 0; i < NCONS; ++i) {
            if (containerHasElement(plot_var_names, cons_names[i])) {
              MultiFab::Copy(mf[lev],*cons_new[lev],i,mf_comp,1,ngrow_vars);
                mf_comp++;
            }
        } // NCONS

        // Next, check for velocities
        if (containerHasElement(plot_var_names, "x_velocity")) {
            MultiFab::Copy(mf[lev], mf_cc_vel[lev], 0, mf_comp, 1, 0);
            mf_comp += 1;
        }
        if (containerHasElement(plot_var_names, "y_velocity")) {
            MultiFab::Copy(mf[lev], mf_cc_vel[lev], 1, mf_comp, 1, 0);
            mf_comp += 1;
        }
        if (containerHasElement(plot_var_names, "z_velocity")) {
            MultiFab::Copy(mf[lev], mf_cc_vel[lev], 2, mf_comp, 1, 0);
            mf_comp += 1;
        }

        // Define standard process for calling the functions in Derive.cpp
        auto calculate_derived = [&](const std::string& der_name,
                                     decltype(derived::remora_dernull)& der_function)
        {
            if (containerHasElement(plot_var_names, der_name)) {
                MultiFab dmf(mf[lev], make_alias, mf_comp, 1);
#ifdef _OPENMP
#pragma omp parallel if (amrex::Gpu::notInLaunchRegion())
#endif
                for (MFIter mfi(dmf, TilingIfNotGPU()); mfi.isValid(); ++mfi)
                {
                    const Box& bx = mfi.tilebox();
                    auto& dfab = dmf[mfi];

                    if (der_name == "vorticity") {
                        auto const& sfab = mf_cc_vel[lev][mfi];
                        der_function(bx, dfab, 0, 1, sfab, vec_pm[lev]->const_array(mfi), vec_pn[lev]->const_array(mfi), Geom(lev), t_new[0], nullptr, lev);
                    } else {
                        auto const& sfab = (*cons_new[lev])[mfi];
                        der_function(bx, dfab, 0, 1, sfab, vec_pm[lev]->const_array(mfi), vec_pn[lev]->const_array(mfi), Geom(lev), t_new[0], nullptr, lev);
                    }
                }

                mf_comp++;
            }
        };

        // Note: All derived variables must be computed in order of "derived_names" defined in REMORA.H
        calculate_derived("vorticity",  derived::remora_dervort);

        // Fill cell-centered location
        Real dx = Geom()[lev].CellSizeArray()[0];
        Real dy = Geom()[lev].CellSizeArray()[1];

        // Next, check for location names -- if we write one we write all
        if (containerHasElement(plot_var_names, "x_cc") ||
            containerHasElement(plot_var_names, "y_cc") ||
            containerHasElement(plot_var_names, "z_cc"))
        {
            MultiFab dmf(mf[lev], make_alias, mf_comp, AMREX_SPACEDIM);
#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
            for (MFIter mfi(dmf, TilingIfNotGPU()); mfi.isValid(); ++mfi) {
                const Box& bx = mfi.tilebox();
                const Array4<Real> loc_arr = dmf.array(mfi);
                const Array4<Real const> zp_arr = vec_z_phys_nd[lev]->const_array(mfi);

                ParallelFor(bx, [=] AMREX_GPU_DEVICE (int i, int j, int k) {
                    loc_arr(i,j,k,0) = (i+0.5_rt) * dx;
                    loc_arr(i,j,k,1) = (j+0.5_rt) * dy;
                    loc_arr(i,j,k,2) = 0.125_rt * (zp_arr(i,j  ,k  ) + zp_arr(i+1,j  ,k  ) +
                                                   zp_arr(i,j+1,k  ) + zp_arr(i+1,j+1,k  ) +
                                                   zp_arr(i,j  ,k+1) + zp_arr(i+1,j  ,k+1) +
                                                   zp_arr(i,j+1,k+1) + zp_arr(i+1,j+1,k+1) );
                });
            } // mfi
            mf_comp += AMREX_SPACEDIM;
        } // if containerHasElement

#ifdef REMORA_USE_PARTICLES
        const auto& particles_namelist( particleData.getNames() );
        for (ParticlesNamesVector::size_type i = 0; i < particles_namelist.size(); i++) {
            if (containerHasElement(plot_var_names, std::string(particles_namelist[i]+"_count"))) {
                MultiFab temp_dat(mf[lev].boxArray(), mf[lev].DistributionMap(), 1, 0);
                temp_dat.setVal(0);
                particleData[particles_namelist[i]]->Increment(temp_dat, lev);
                MultiFab::Copy(mf[lev], temp_dat, 0, mf_comp, 1, 0);
                mf_comp += 1;
            }
        }

        Vector<std::string> particle_mesh_plot_names(0);
        particleData.GetMeshPlotVarNames( particle_mesh_plot_names );
        for (int i = 0; i < particle_mesh_plot_names.size(); i++) {
            std::string plot_var_name(particle_mesh_plot_names[i]);
            if (containerHasElement(plot_var_names, plot_var_name) ) {
                MultiFab temp_dat(mf[lev].boxArray(), mf[lev].DistributionMap(), 1, 1);
                temp_dat.setVal(0);
                particleData.GetMeshPlotVar(plot_var_name, temp_dat, lev);
                MultiFab::Copy(mf[lev], temp_dat, 0, mf_comp, 1, 0);
                mf_comp += 1;
            }
        }
#endif

        MultiFab::Copy(mf_nd[lev],*vec_z_phys_nd[lev],0,2,1,0);
        Real dz = Geom()[lev].CellSizeArray()[2];
        int N = Geom()[lev].Domain().size()[2];

#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for (MFIter mfi(mf_nd[lev], TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            const Box& bx = mfi.tilebox();
            Array4<Real> mf_arr = mf_nd[lev].array(mfi);
            ParallelFor(bx, [=] AMREX_GPU_DEVICE (int i, int j, int k) {
                mf_arr(i,j,k,2) = mf_arr(i,j,k,2) + (N-k) * dz;
            });
        } // mfi

    } // lev

    std::string plotfilename = Concatenate(plot_file_name, istep[0], file_min_digits);

    if (finest_level == 0)
    {
        if (plotfile_type == PlotfileType::amrex) {
            amrex::Print() << "Writing plotfile " << plotfilename << "\n";
            WriteMultiLevelPlotfileWithBathymetry(plotfilename, finest_level+1,
                                                  GetVecOfConstPtrs(mf),
                                                  GetVecOfConstPtrs(mf_nd),
                                                  varnames,
                                                  t_new[0], istep);
            writeJobInfo(plotfilename);

#ifdef REMORA_USE_PARTICLES
            particleData.Checkpoint(plotfilename);
#endif

#ifdef REMORA_USE_HDF5
        } else if (plotfile_type == PlotfileType::hdf5) {
            amrex::Print() << "Writing plotfile " << plotfilename+"d01.h5" << "\n";
            WriteMultiLevelPlotfileHDF5(plotfilename, finest_level+1,
                                        GetVecOfConstPtrs(mf),
                                        varnames,
                                        Geom(), t_new[0], istep, refRatio());
#endif
        } else if (!(plotfile_type == PlotfileType::netcdf)) {
            amrex::Abort("User specified unknown plot_filetype");
        }

    } else { // multilevel

        Vector<IntVect>   r2(finest_level);
        Vector<Geometry>  g2(finest_level+1);
        Vector<MultiFab> mf2(finest_level+1);

        mf2[0].define(grids[0], dmap[0], ncomp_mf, 0);

        // Copy level 0 as is
        MultiFab::Copy(mf2[0],mf[0],0,0,mf[0].nComp(),0);

        // Define a new multi-level array of Geometry's so that we pass the new "domain" at lev > 0
        Array<int,AMREX_SPACEDIM> periodicity =
                     {Geom()[0].isPeriodic(0),Geom()[0].isPeriodic(1),Geom()[0].isPeriodic(2)};
        g2[0].define(Geom()[0].Domain(),&(Geom()[0].ProbDomain()),0,periodicity.data());

        if (plotfile_type == PlotfileType::amrex) {
            r2[0] = IntVect(1,1,ref_ratio[0][0]);
            for (int lev = 1; lev <= finest_level; ++lev) {
                if (lev > 1) {
                    r2[lev-1][0] = 1;
                    r2[lev-1][1] = 1;
                    r2[lev-1][2] = r2[lev-2][2] * ref_ratio[lev-1][0];
                }

                mf2[lev].define(refine(grids[lev],r2[lev-1]), dmap[lev], ncomp_mf, 0);

                // Set the new problem domain
                Box d2(Geom()[lev].Domain());
                d2.refine(r2[lev-1]);

                g2[lev].define(d2,&(Geom()[lev].ProbDomain()),0,periodicity.data());
            }

            // Make a vector of BCRec with default values so we can use it here -- note the values
            //      aren't actually used because we do PCInterp
            amrex::Vector<amrex::BCRec> null_dom_bcs;
            null_dom_bcs.resize(mf2[0].nComp());
            for (int n = 0; n < mf2[0].nComp(); n++) {
                for (int dir = 0; dir < AMREX_SPACEDIM; dir++) {
                    null_dom_bcs[n].setLo(dir, REMORABCType::int_dir);
                    null_dom_bcs[n].setHi(dir, REMORABCType::int_dir);
                }
            }

            // Do piecewise interpolation of mf into mf2
            for (int lev = 1; lev <= finest_level; ++lev) {
                Interpolater* mapper_c = &pc_interp;
                InterpFromCoarseLevel(mf2[lev], t_new[lev], mf[lev],
                                      0, 0, mf2[lev].nComp(),
                                      geom[lev], g2[lev],
                                      null_bc_for_fill, 0, null_bc_for_fill, 0,
                                      r2[lev-1], mapper_c, null_dom_bcs, 0);
            }

            // Define an effective ref_ratio which is isotropic to be passed into WriteMultiLevelPlotfile
            Vector<IntVect> rr(finest_level);
            for (int lev = 0; lev < finest_level; ++lev) {
                rr[lev] = IntVect(ref_ratio[lev][0],ref_ratio[lev][1],ref_ratio[lev][0]);
            }

            WriteMultiLevelPlotfile(plotfilename, finest_level+1, GetVecOfConstPtrs(mf2), varnames,
                                    g2, t_new[0], istep, rr);
            writeJobInfo(plotfilename);

#ifdef REMORA_USE_PARTICLES
            particleData.Checkpoint(plotfilename);
#endif
        }
    } // end multi-level
}

void
REMORA::WriteMultiLevelPlotfileWithBathymetry (const std::string& plotfilename, int nlevels,
                                              const Vector<const MultiFab*>& mf,
                                              const Vector<const MultiFab*>& mf_nd,
                                              const Vector<std::string>& varnames,
                                              Real time,
                                              const Vector<int>& level_steps,
                                              const std::string &versionName,
                                              const std::string &levelPrefix,
                                              const std::string &mfPrefix,
                                              const Vector<std::string>& extra_dirs) const
{
    BL_PROFILE("WriteMultiLevelPlotfileWithBathymetry()");

    BL_ASSERT(nlevels <= mf.size());
    BL_ASSERT(nlevels <= ref_ratio.size()+1);
    BL_ASSERT(nlevels <= level_steps.size());
    BL_ASSERT(mf[0]->nComp() == varnames.size());

    bool callBarrier(false);
    PreBuildDirectorHierarchy(plotfilename, levelPrefix, nlevels, callBarrier);
    if (!extra_dirs.empty()) {
        for (const auto& d : extra_dirs) {
            const std::string ed = plotfilename+"/"+d;
            PreBuildDirectorHierarchy(ed, levelPrefix, nlevels, callBarrier);
        }
    }
    ParallelDescriptor::Barrier();

    if (ParallelDescriptor::MyProc() == ParallelDescriptor::NProcs()-1) {
        Vector<BoxArray> boxArrays(nlevels);
        for(int level(0); level < boxArrays.size(); ++level) {
            boxArrays[level] = mf[level]->boxArray();
        }

        auto f = [=]() {
            VisMF::IO_Buffer io_buffer(VisMF::IO_Buffer_Size);
            std::string HeaderFileName(plotfilename + "/Header");
            std::ofstream HeaderFile;
            HeaderFile.rdbuf()->pubsetbuf(io_buffer.dataPtr(), io_buffer.size());
            HeaderFile.open(HeaderFileName.c_str(), std::ofstream::out   |
                                                    std::ofstream::trunc |
                                                    std::ofstream::binary);
            if( ! HeaderFile.good()) FileOpenFailed(HeaderFileName);
            WriteGenericPlotfileHeaderWithBathymetry(HeaderFile, nlevels, boxArrays, varnames,
                                                     time, level_steps, versionName,
                                                     levelPrefix, mfPrefix);
        };

        if (AsyncOut::UseAsyncOut()) {
            AsyncOut::Submit(std::move(f));
        } else {
            f();
        }
    }

    std::string mf_nodal_prefix = "Nu_nd";
    for (int level = 0; level <= finest_level; ++level)
    {
        if (AsyncOut::UseAsyncOut()) {
            VisMF::AsyncWrite(*mf[level],
                              MultiFabFileFullPrefix(level, plotfilename, levelPrefix, mfPrefix),
                              true);
            VisMF::AsyncWrite(*mf_nd[level],
                              MultiFabFileFullPrefix(level, plotfilename, levelPrefix, mf_nodal_prefix),
                              true);
        } else {
            const MultiFab* data;
            std::unique_ptr<MultiFab> mf_tmp;
            if (mf[level]->nGrowVect() != 0) {
                mf_tmp = std::make_unique<MultiFab>(mf[level]->boxArray(),
                                                    mf[level]->DistributionMap(),
                                                    mf[level]->nComp(), 0, MFInfo(),
                                                    mf[level]->Factory());
                MultiFab::Copy(*mf_tmp, *mf[level], 0, 0, mf[level]->nComp(), 0);
                data = mf_tmp.get();
            } else {
                data = mf[level];
            }
            VisMF::Write(*data       , MultiFabFileFullPrefix(level, plotfilename, levelPrefix, mfPrefix));
            VisMF::Write(*mf_nd[level], MultiFabFileFullPrefix(level, plotfilename, levelPrefix, mf_nodal_prefix));
        }
    }
}

void
REMORA::WriteGenericPlotfileHeaderWithBathymetry (std::ostream &HeaderFile,
                                                 int nlevels,
                                                 const Vector<BoxArray> &bArray,
                                                 const Vector<std::string> &varnames,
                                                 Real time,
                                                 const Vector<int> &level_steps,
                                                 const std::string &versionName,
                                                 const std::string &levelPrefix,
                                                 const std::string &mfPrefix) const
{
        BL_ASSERT(nlevels <= bArray.size());
        BL_ASSERT(nlevels <= ref_ratio.size()+1);
        BL_ASSERT(nlevels <= level_steps.size());

        HeaderFile.precision(17);

        // ---- this is the generic plot file type name
        HeaderFile << versionName << '\n';

        HeaderFile << varnames.size() << '\n';

        for (int ivar = 0; ivar < varnames.size(); ++ivar) {
            HeaderFile << varnames[ivar] << "\n";
        }
        HeaderFile << AMREX_SPACEDIM << '\n';
        HeaderFile << time << '\n';
        HeaderFile << finest_level << '\n';
        for (int i = 0; i < AMREX_SPACEDIM; ++i) {
            HeaderFile << geom[0].ProbLo(i) << ' ';
        }
        HeaderFile << '\n';
        for (int i = 0; i < AMREX_SPACEDIM; ++i) {
            HeaderFile << geom[0].ProbHi(i) << ' ';
        }
        HeaderFile << '\n';
        for (int i = 0; i < finest_level; ++i) {
            HeaderFile << ref_ratio[i][0] << ' ';
        }
        HeaderFile << '\n';
        for (int i = 0; i <= finest_level; ++i) {
            HeaderFile << geom[i].Domain() << ' ';
        }
        HeaderFile << '\n';
        for (int i = 0; i <= finest_level; ++i) {
            HeaderFile << level_steps[i] << ' ';
        }
        HeaderFile << '\n';
        for (int i = 0; i <= finest_level; ++i) {
            for (int k = 0; k < AMREX_SPACEDIM; ++k) {
                HeaderFile << geom[i].CellSize()[k] << ' ';
            }
            HeaderFile << '\n';
        }
        HeaderFile << (int) geom[0].Coord() << '\n';
        HeaderFile << "0\n";

        for (int level = 0; level <= finest_level; ++level) {
            HeaderFile << level << ' ' << bArray[level].size() << ' ' << time << '\n';
            HeaderFile << level_steps[level] << '\n';

            const IntVect& domain_lo = geom[level].Domain().smallEnd();
            for (int i = 0; i < bArray[level].size(); ++i)
            {
                // Need to shift because the RealBox ctor we call takes the
                // physical location of index (0,0,0).  This does not affect
                // the usual cases where the domain index starts with 0.
                const Box& b = shift(bArray[level][i], -domain_lo);
                RealBox loc = RealBox(b, geom[level].CellSize(), geom[level].ProbLo());
                for (int n = 0; n < AMREX_SPACEDIM; ++n) {
                    HeaderFile << loc.lo(n) << ' ' << loc.hi(n) << '\n';
                }
            }

            HeaderFile << MultiFabHeaderPath(level, levelPrefix, mfPrefix) << '\n';
        }
        HeaderFile << "1" << "\n";
        HeaderFile << "3" << "\n";
        HeaderFile << "amrexvec_nu_x" << "\n";
        HeaderFile << "amrexvec_nu_y" << "\n";
        HeaderFile << "amrexvec_nu_z" << "\n";
        std::string mf_nodal_prefix = "Nu_nd";
        for (int level = 0; level <= finest_level; ++level) {
            HeaderFile << MultiFabHeaderPath(level, levelPrefix, mf_nodal_prefix) << '\n';
        }
}

void
REMORA::mask_arrays_for_write(int lev, Real fill_value) {
    for (MFIter mfi(*cons_new[lev],false); mfi.isValid(); ++mfi) {
        Box bx = mfi.tilebox();
        Box gbx1 = mfi.growntilebox(IntVect(NGROW+1,NGROW+1,0));
        Box ubx = mfi.grownnodaltilebox(0,IntVect(NGROW,NGROW,0));
        Box vbx = mfi.grownnodaltilebox(1,IntVect(NGROW,NGROW,0));

        Array4<Real> const& Zt_avg1 = vec_Zt_avg1[lev]->array(mfi);
        Array4<Real> const& ubar = vec_ubar[lev]->array(mfi);
        Array4<Real> const& vbar = vec_vbar[lev]->array(mfi);
        Array4<Real> const& xvel = xvel_new[lev]->array(mfi);
        Array4<Real> const& yvel = yvel_new[lev]->array(mfi);
        Array4<Real> const& temp = cons_new[lev]->array(mfi,Temp_comp);
        Array4<Real> const& salt = cons_new[lev]->array(mfi,Salt_comp);

        Array4<Real const> const& mskr = vec_mskr[lev]->array(mfi);
        Array4<Real const> const& msku = vec_msku[lev]->array(mfi);
        Array4<Real const> const& mskv = vec_mskv[lev]->array(mfi);

        ParallelFor(makeSlab(gbx1,2,0), [=] AMREX_GPU_DEVICE (int i, int j, int )
        {
            if (!mskr(i,j,0)) {
                Zt_avg1(i,j,0) = 0.0_rt; //fill_value;
            }
        });
        ParallelFor(gbx1, [=] AMREX_GPU_DEVICE (int i, int j, int k)
        {
            if (!mskr(i,j,0)) {
                temp(i,j,k) = fill_value;
                salt(i,j,k) = fill_value;
            }
        });
        ParallelFor(makeSlab(ubx,2,0), 3, [=] AMREX_GPU_DEVICE (int i, int j, int , int n)
        {
            if (!msku(i,j,0)) {
                ubar(i,j,0,n) = fill_value;
            }
        });
        ParallelFor(makeSlab(vbx,2,0), 3, [=] AMREX_GPU_DEVICE (int i, int j, int , int n)
        {
            if (!mskv(i,j,0)) {
                vbar(i,j,0,n) = fill_value;
            }
        });
        ParallelFor(ubx, [=] AMREX_GPU_DEVICE (int i, int j, int k)
        {
            if (!msku(i,j,0)) {
                xvel(i,j,k) = fill_value;
            }
        });
        ParallelFor(vbx, [=] AMREX_GPU_DEVICE (int i, int j, int k)
        {
            if (!mskv(i,j,0)) {
                yvel(i,j,k) = fill_value;
            }
        });
    }
    Gpu::streamSynchronize();
}

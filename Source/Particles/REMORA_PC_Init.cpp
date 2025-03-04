#ifdef REMORA_USE_PARTICLES

#include <REMORA_PC.H>
#include <AMReX_ParmParse.H>

using namespace amrex;

/*! Read inputs from file */
void REMORAPC::readInputs ()
{
    BL_PROFILE("REMORAPC::readInputs");

    ParmParse pp(m_name);

    m_initialization_type = REMORAParticleInitializations::init_box_uniform;
    pp.query("initial_distribution_type", m_initialization_type);

    if (m_initialization_type == REMORAParticleInitializations::init_box_uniform)
    {
        Vector<Real> particle_box_lo(AMREX_SPACEDIM);
        Vector<Real> particle_box_hi(AMREX_SPACEDIM);

        // Defaults
        for (int i = 0; i < AMREX_SPACEDIM; i++) { particle_box_lo[i] = Geom(0).ProbLo(i); }
        for (int i = 0; i < AMREX_SPACEDIM; i++) { particle_box_hi[i] = Geom(0).ProbHi(i); }

        pp.queryAdd("particle_box_lo", particle_box_lo, AMREX_SPACEDIM);
        AMREX_ASSERT(particle_box_lo.size() == AMREX_SPACEDIM);

        pp.queryAdd("particle_box_hi", particle_box_hi, AMREX_SPACEDIM);
        AMREX_ASSERT(particle_box_hi.size() == AMREX_SPACEDIM);

        m_particle_box.setLo(particle_box_lo);
        m_particle_box.setHi(particle_box_hi);

        // We default to placing the particles randomly within each cell,
        // but can override this for regression testing
        place_randomly_in_cells = true;
        pp.query("place_randomly_in_cells", place_randomly_in_cells);
    }

    m_ppc_init = 1;
    pp.query("initial_particles_per_cell", m_ppc_init);

    m_advect_w_flow = (m_name == REMORAParticleNames::tracers ? true : false);
    pp.query("advect_with_flow", m_advect_w_flow);

    return;
}

/*! Initialize particles in domain */
void REMORAPC::InitializeParticles (const std::unique_ptr<MultiFab>& a_height_ptr)
{
    BL_PROFILE("REMORAPC::initializeParticles");

    if (m_initialization_type == REMORAParticleInitializations::init_box_uniform) {
        initializeParticlesUniformDistributionInBox( a_height_ptr , m_particle_box );
    } else {
        Print() << "Error: " << m_initialization_type
                << " is not a valid initialization for "
                << m_name << " particle species.\n";
        Error("See error message!");
    }
    return;
}

/*! Uniform distribution: the number of particles per grid cell is specified
 *  by "initial_particles_per_cell", and they are randomly distributed. */
void REMORAPC::initializeParticlesUniformDistributionInBox (const std::unique_ptr<MultiFab>& a_height_ptr,
                                                         const RealBox& particle_init_domain)
{
    BL_PROFILE("REMORAPC::initializeParticlesUniformDistributionInBox");

    const int lev = 0;
    const auto dx = Geom(lev).CellSizeArray();
    const auto plo = Geom(lev).ProbLoArray();

    int particles_per_cell = m_ppc_init;

    iMultiFab num_particles( ParticleBoxArray(lev),
                             ParticleDistributionMap(lev),
                             1, 0 );
    num_particles.setVal(0);
    for(MFIter mfi = MakeMFIter(lev); mfi.isValid(); ++mfi) {
        const Box& tile_box  = mfi.tilebox();
        auto num_particles_arr = num_particles[mfi].array();
        if (a_height_ptr) {
            const auto height_arr = (*a_height_ptr)[mfi].array();
            ParallelFor(tile_box, [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
            {
                Real x = plo[0] + (i + 0.5)*dx[0];
                Real y = plo[1] + (j + 0.5)*dx[1];
                Real z = 0.125 * (height_arr(i,j  ,k  ) + height_arr(i+1,j  ,k  ) +
                                  height_arr(i,j+1,k  ) + height_arr(i+1,j+1,k  ) +
                                  height_arr(i,j  ,k+1) + height_arr(i+1,j  ,k+1) +
                                  height_arr(i,j+1,k+1) + height_arr(i+1,j+1,k  ) );
                if (particle_init_domain.contains(RealVect(x,y,z))) {
                    num_particles_arr(i,j,k) = particles_per_cell;
                }
            });

        } else {
            ParallelFor(tile_box, [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
            {
                Real x = plo[0] + (i + 0.5)*dx[0];
                Real y = plo[1] + (j + 0.5)*dx[1];
                Real z = plo[2] + (k + 0.5)*dx[2];
                if (particle_init_domain.contains(RealVect(x,y,z))) {
                    num_particles_arr(i,j,k) = particles_per_cell;
                }
            });
        }
    }

    iMultiFab offsets( ParticleBoxArray(lev),
                       ParticleDistributionMap(lev),
                       1, 0 );
    offsets.setVal(0);

    for(MFIter mfi = MakeMFIter(lev); mfi.isValid(); ++mfi) {
        const Box& tile_box  = mfi.tilebox();

        int np = 0;
        {
            int ncell = num_particles[mfi].numPts();
            const int* in = num_particles[mfi].dataPtr();
            int* out = offsets[mfi].dataPtr();
            np = Scan::PrefixSum<int>( ncell,
                                       [=] AMREX_GPU_DEVICE (int i) -> int { return in[i]; },
                                       [=] AMREX_GPU_DEVICE (int i, int const &x) { out[i] = x; },
                                       Scan::Type::exclusive,
                                       Scan::retSum );
        }
        auto offset_arr = offsets[mfi].array();

        auto& particle_tile = DefineAndReturnParticleTile(lev, mfi);
        particle_tile.resize(np);
        auto aos = &particle_tile.GetArrayOfStructs()[0];
        auto& soa = particle_tile.GetStructOfArrays();
        auto* vx_ptr = soa.GetRealData(REMORAParticlesRealIdxSoA::vx).data();
        auto* vy_ptr = soa.GetRealData(REMORAParticlesRealIdxSoA::vy).data();
        auto* vz_ptr = soa.GetRealData(REMORAParticlesRealIdxSoA::vz).data();
        auto* mass_ptr = soa.GetRealData(REMORAParticlesRealIdxSoA::mass).data();

        const auto num_particles_arr = num_particles[mfi].array();

        auto my_proc = ParallelDescriptor::MyProc();
        Long pid;
        {
            pid = ParticleType::NextID();
            ParticleType::NextID(pid+np);
        }
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE( static_cast<Long>(pid + np) < LastParticleID,
                                          "Error: overflow on particle id numbers!" );

        if (a_height_ptr && place_randomly_in_cells) {

            const auto height_arr        = (*a_height_ptr)[mfi].array();

            ParallelForRNG(tile_box, [=] AMREX_GPU_DEVICE (int i, int j, int k,
                                                           const RandomEngine& rnd_engine) noexcept
            {
                int start = offset_arr(i,j,k);
                for (int n = start; n < start+num_particles_arr(i,j,k); n++) {
                    Real r[3] = {Random(rnd_engine), Random(rnd_engine), Random(rnd_engine)};
                    Real v[3] = {0.0, 0.0, 0.0};

                    Real x = plo[0] + (i + r[0])*dx[0];
                    Real y = plo[1] + (j + r[1])*dx[1];

                    Real sx[] = { amrex::Real(1.) - r[0], r[0]};
                    Real sy[] = { amrex::Real(1.) - r[1], r[1]};

                    Real height_at_pxy_lo = 0.;
                    for (int ii = 0; ii < 2; ++ii) {
                        for (int jj = 0; jj < 2; ++jj) {
                            height_at_pxy_lo += sx[ii] * sy[jj] * height_arr(i+ii,j+jj,k);
                        }
                    }
                    Real height_at_pxy_hi = 0.;
                    for (int ii = 0; ii < 2; ++ii) {
                        for (int jj = 0; jj < 2; ++jj) {
                            height_at_pxy_hi += sx[ii] * sy[jj] * height_arr(i+ii,j+jj,k+1);
                        }
                    }

                    Real z = height_at_pxy_lo  + r[2] * (height_at_pxy_hi - height_at_pxy_lo);

                    auto& p = aos[n];
                    p.id()  = pid + n;
                    p.cpu() = my_proc;

                    p.pos(0) = x; p.pos(1) = y; p.pos(2) = z;

                    p.idata(REMORAParticlesIntIdxAoS::k) = k;

                    vx_ptr[n] = v[0]; vy_ptr[n] = v[1]; vz_ptr[n] = v[2];

                    mass_ptr[n] = 1.0e-6;
               }
            });

        } else if (a_height_ptr && !place_randomly_in_cells) {

            const auto height_arr        = (*a_height_ptr)[mfi].array();

            ParallelFor(tile_box, [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
            {
                int start = offset_arr(i,j,k);
                for (int n = start; n < start+num_particles_arr(i,j,k); n++) {
                    Real r[3] = {0.3, 0.7, 0.25};
                    Real v[3] = {0.0, 0.0, 0.0};

                    Real x = plo[0] + (i + r[0])*dx[0];
                    Real y = plo[1] + (j + r[1])*dx[1];

                    Real sx[] = { amrex::Real(1.) - r[0], r[0]};
                    Real sy[] = { amrex::Real(1.) - r[1], r[1]};

                    Real height_at_pxy_lo = 0.;
                    for (int ii = 0; ii < 2; ++ii) {
                        for (int jj = 0; jj < 2; ++jj) {
                            height_at_pxy_lo += sx[ii] * sy[jj] * height_arr(i+ii,j+jj,k);
                        }
                    }
                    Real height_at_pxy_hi = 0.;
                    for (int ii = 0; ii < 2; ++ii) {
                        for (int jj = 0; jj < 2; ++jj) {
                            height_at_pxy_hi += sx[ii] * sy[jj] * height_arr(i+ii,j+jj,k+1);
                        }
                    }

                    Real z = height_at_pxy_lo  + r[2] * (height_at_pxy_hi - height_at_pxy_lo);

                    auto& p = aos[n];
                    p.id()  = pid + n;
                    p.cpu() = my_proc;

                    p.pos(0) = x; p.pos(1) = y; p.pos(2) = z;

                    p.idata(REMORAParticlesIntIdxAoS::k) = k;

                    vx_ptr[n] = v[0]; vy_ptr[n] = v[1]; vz_ptr[n] = v[2];

                    mass_ptr[n] = 1.0e-6;
               }
            });

        } else if (!a_height_ptr && place_randomly_in_cells) {

            ParallelForRNG(tile_box, [=] AMREX_GPU_DEVICE (int i, int j, int k,
                                                           const RandomEngine& rnd_engine) noexcept
            {
                int start = offset_arr(i,j,k);
                for (int n = start; n < start+num_particles_arr(i,j,k); n++) {
                    Real r[3] = {Random(rnd_engine), Random(rnd_engine), Random(rnd_engine)};
                    Real v[3] = {0.0, 0.0, 0.0};

                    Real x = plo[0] + (i + r[0])*dx[0];
                    Real y = plo[1] + (j + r[1])*dx[1];
                    Real z = plo[2] + (k + r[2])*dx[2];

                    auto& p = aos[n];
                    p.id()  = pid + n;
                    p.cpu() = my_proc;

                    p.pos(0) = x; p.pos(1) = y; p.pos(2) = z;

                    p.idata(REMORAParticlesIntIdxAoS::k) = k;

                    vx_ptr[n] = v[0]; vy_ptr[n] = v[1]; vz_ptr[n] = v[2];

                    mass_ptr[n] = 1.0e-6;
               }
            });

        } else { // if (!a_height_ptr && !place_randomly_in_cells) {

            ParallelFor(tile_box, [=] AMREX_GPU_DEVICE (int i, int j, int k) noexcept
            {
                int start = offset_arr(i,j,k);
                for (int n = start; n < start+num_particles_arr(i,j,k); n++) {
                    Real r[3] = {0.3, 0.7, 0.25};
                    Real v[3] = {0.0, 0.0, 0.0};

                    Real x = plo[0] + (i + r[0])*dx[0];
                    Real y = plo[1] + (j + r[1])*dx[1];
                    Real z = plo[2] + (k + r[2])*dx[2];

                    auto& p = aos[n];
                    p.id()  = pid + n;
                    p.cpu() = my_proc;

                    p.pos(0) = x; p.pos(1) = y; p.pos(2) = z;

                    p.idata(REMORAParticlesIntIdxAoS::k) = k;

                    vx_ptr[n] = v[0]; vy_ptr[n] = v[1]; vz_ptr[n] = v[2];

                    mass_ptr[n] = 1.0e-6;
               }
            });
        }
    }

    return;
}

#endif

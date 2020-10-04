//========================================================================================
// AthenaPK - a performance portable block structured AMR astrophysical MHD code.
// Copyright (c) 2020, Athena-Parthenon Collaboration. All rights reserved.
// Licensed under the BSD 3-Clause License (the "LICENSE").
//========================================================================================

#include <memory>
#include <string>
#include <vector>

// Athena headers
#include "bvals/cc/bvals_cc_in_one.hpp"
#include "hydro.hpp"
#include "hydro_driver.hpp"

using namespace parthenon::driver::prelude;

namespace Hydro {

HydroDriver::HydroDriver(ParameterInput *pin, ApplicationInput *app_in, Mesh *pm)
    : MultiStageBlockTaskDriver(pin, app_in, pm) {
  // fail if these are not specified in the input file
  pin->CheckRequired("hydro", "eos");

  // warn if these fields aren't specified in the input file
  pin->CheckDesired("hydro", "cfl");
}

// first some helper tasks
auto UpdateContainer(BlockList_t &blocks, const int stage,
                     const std::vector<std::string> &stage_name, Integrator *integrator)
    -> TaskStatus {
  // TODO(pgrete): this update is currently hardcoded to work for rk1 and vl2
  const Real beta = integrator->beta[stage - 1];
  const Real dt = integrator->dt;
  parthenon::Update::UpdateContainer(blocks, stage_name[stage - 1], "dUdt", beta * dt,
                                     stage_name[stage]);

  return TaskStatus::complete;
}

// See the advection.hpp declaration for a description of how this function gets called.
auto HydroDriver::MakeTaskCollection(BlockList_t &blocks, int stage) -> TaskCollection {
  TaskCollection tc;

  TaskID none(0);
  // Number of task lists that can be executed indepenently and thus *may*
  // be executed in parallel and asynchronous.
  // Being extra verbose here in this example to highlight that this is not
  // required to be 1 or blocks.size() but could also only apply to a subset of blocks.
  auto num_task_lists_executed_independently = blocks.size();
  TaskRegion &async_region1 = tc.AddRegion(num_task_lists_executed_independently);

  for (int i = 0; i < blocks.size(); i++) {
    auto &pmb = blocks[i];
    auto &tl = async_region1[i];
    // first make other useful containers
    if (stage == 1) {
      auto &base = pmb->real_containers.Get();
      pmb->real_containers.Add("dUdt", base);
      for (int i = 1; i < integrator->nstages; i++)
        pmb->real_containers.Add(stage_name[i], base);
    }

    // pull out the container we'll use to get fluxes and/or compute RHSs
    auto &sc0 = pmb->real_containers.Get(stage_name[stage - 1]);
    // pull out a container we'll use to store dU/dt.
    // This is just -flux_divergence in this example
    auto &dudt = pmb->real_containers.Get("dUdt");
    // pull out the container that will hold the updated state
    // effectively, sc1 = sc0 + dudt*dt
    auto &sc1 = pmb->real_containers.Get(stage_name[stage]);

    auto start_recv = tl.AddTask(none, &Container<Real>::StartReceiving, sc1.get(),
                                 BoundaryCommSubset::all);

    TaskID advect_flux;
    auto pkg = pmb->packages["Hydro"];
    if (pkg->Param<bool>("use_scratch")) {
      advect_flux = tl.AddTask(none, Hydro::CalculateFluxesWScratch, sc0, stage);
    } else {
      advect_flux = tl.AddTask(none, Hydro::CalculateFluxes, sc0, stage);
    }
  }
  // note that task within this region that contains only a single task list
  // could still be executed in parallel
  TaskRegion &single_tasklist_region = tc.AddRegion(1);
  {
    auto &tl = single_tasklist_region[0];

    // compute the divergence of fluxes of conserved variables
    auto flux_div = tl.AddTask(none, parthenon::Update::FluxDivergenceMesh, blocks,
                               stage_name[stage - 1], "dUdt");

    // apply du/dt to all independent fields in the container
    auto update_container =
        tl.AddTask(flux_div, UpdateContainer, blocks, stage, stage_name, integrator);

    // update ghost cells
    auto send =
        tl.AddTask(update_container, parthenon::cell_centered_bvars::SendBoundaryBuffers,
                   blocks, stage_name[stage]);

    auto recv = tl.AddTask(send, parthenon::cell_centered_bvars::ReceiveBoundaryBuffers,
                           blocks, stage_name[stage]);
    auto fill_from_bufs = tl.AddTask(recv, parthenon::cell_centered_bvars::SetBoundaries,
                                     blocks, stage_name[stage]);
  }
  TaskRegion &async_region2 = tc.AddRegion(num_task_lists_executed_independently);

  for (int i = 0; i < blocks.size(); i++) {
    auto &pmb = blocks[i];
    auto &tl = async_region2[i];
    auto &sc1 = pmb->real_containers.Get(stage_name[stage]);

    auto clear_comm_flags = tl.AddTask(none, &Container<Real>::ClearBoundary, sc1.get(),
                                       BoundaryCommSubset::all);

    // set physical boundaries
    auto set_bc = tl.AddTask(fill_from_bufs, parthenon::ApplyBoundaryConditions, sc1);

    // fill in derived fields
    auto fill_derived =
        tl.AddTask(set_bc, parthenon::FillDerivedVariables::FillDerived, sc1);

    // estimate next time step
    if (stage == integrator->nstages) {
      auto new_dt = tl.AddTask(
          fill_derived,
          [](std::shared_ptr<Container<Real>> &rc) {
            auto pmb = rc->GetBlockPointer();
            pmb->SetBlockTimestep(parthenon::Update::EstimateTimestep(rc));
            return TaskStatus::complete;
          },
          sc1);
    }

    // removed purging of stages
    // removed check for refinement conditions here
  }
  return tc;
}
} // namespace Hydro
